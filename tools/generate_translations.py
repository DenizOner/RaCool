"""Validate all product catalogs and generate immutable, build-local C tables."""
import argparse
import json
import re
import unicodedata
from pathlib import Path

MAX_MESSAGE_BYTES = 2048
MAX_OLED_MESSAGE_BYTES = 96
OLED_DOMAINS = {"menu", "display", "monitor", "common", "climate", "power", "radio",
                "temperature", "animation", "color", "pairing", "editor", "boot", "brand"}
MAX_CATALOGS = 64
FORMAT = re.compile(r"%[-+ #0]*\d*(?:\.\d+)?(?:hh|ll|[hljztL])?[diuoxXfFeEgGaAcsp%]")
C_TOKEN = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*[\s\S]*?\*/|//[^\n]*')
C_REFERENCE = re.compile(r'LOC_PRINTF\s*\(\s*[^,]+,\s*(LOC_[A-Z0-9_]+)\s*,\s*((?:"(?:\\.|[^"\\])*"\s*)+)')


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate JSON key: {key}")
        result[key] = value
    return result


def format_signature(text):
    result = []
    index = 0
    while index < len(text):
        if text[index] != "%":
            index += 1
            continue
        match = FORMAT.match(text, index)
        if not match:
            raise ValueError("Unsupported printf conversion (including %n or dynamic widths)")
        conversion = match.group()
        if conversion != "%%":
            # Width/precision may be localized; ABI length and conversion must match.
            result.append(re.sub(r"^%[-+ #0]*\d*(?:\.\d+)?", "", conversion))
            if conversion.endswith("%"):
                raise ValueError("Only literal %% is supported")
        index = match.end()
    return result


def load_catalogs(directory):
    paths = sorted(Path(directory).glob("*.json"))
    if not paths or len(paths) > MAX_CATALOGS:
        raise ValueError("Catalog count must be between 1 and 64")
    catalogs = []
    locales = set()
    for path in paths:
        catalog = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_object)
        if not isinstance(catalog, dict) or set(catalog) != {"locale", "name", "messages"}:
            raise ValueError(f"Invalid catalog structure: {path.name}")
        locale, name, messages = catalog["locale"], catalog["name"], catalog["messages"]
        if not isinstance(locale, str) or not re.fullmatch(r"[a-z]{2,3}(?:-[A-Z]{2})?", locale):
            raise ValueError("Invalid locale code")
        if locale != path.stem or locale in locales:
            raise ValueError("Locale must be unique and match its filename")
        if not isinstance(name, str) or not name or len(name.encode("utf-8")) > 63 or "\0" in name:
            raise ValueError("Invalid language name")
        catalog["name"] = unicodedata.normalize("NFC", name)
        if not isinstance(messages, dict) or not messages:
            raise ValueError("Empty or invalid messages")
        identifiers = set()
        for key, text in messages.items():
            if not re.fullmatch(r"[a-z][a-z0-9_]*(?:\.[a-z][a-z0-9_]*)+", key):
                raise ValueError(f"Invalid semantic message key: {key}")
            identifier = key.upper().replace(".", "_")
            if identifier in identifiers:
                raise ValueError(f"Generated identifier collision: {key}")
            identifiers.add(identifier)
            if not isinstance(text, str) or "\0" in text or len(text.encode("utf-8")) > MAX_MESSAGE_BYTES:
                raise ValueError(f"Invalid message text: {key}")
            messages[key] = unicodedata.normalize("NFC", text)
            if key.split(".", 1)[0] in OLED_DOMAINS and len(messages[key].encode("utf-8")) > MAX_OLED_MESSAGE_BYTES:
                raise ValueError(f"OLED message exceeds UTF-8 buffer limit: {key}")
            format_signature(text)
        locales.add(locale)
        catalogs.append(catalog)
    if not {"en", "tr"}.issubset(locales):
        raise ValueError("English and Turkish catalogs are required")
    reference = next(c for c in catalogs if c["locale"] == "en")["messages"]
    for catalog in catalogs:
        if set(catalog["messages"]) != set(reference):
            raise ValueError(f"Message keys differ: {catalog['locale']}")
        for key, text in catalog["messages"].items():
            if format_signature(text) != format_signature(reference[key]):
                raise ValueError(f"Format contract differs: {catalog['locale']}/{key}")
    return catalogs


def c_string(text):
    return '"' + ''.join(chr(byte) if 32 <= byte < 127 and byte not in (34, 92)
                         else f"\\{byte:03o}" for byte in text.encode("utf-8")) + '"'


def validate_source_contracts(catalogs, source_directory):
    reference = next(c for c in catalogs if c["locale"] == "en")["messages"]
    messages = {"LOC_" + key.upper().replace(".", "_"): value for key, value in reference.items()}
    for path in Path(source_directory).rglob("*.c"):
        source = path.read_text(encoding="utf-8")
        if "LOC_PRINTF" not in source:
            continue
        clean = C_TOKEN.sub(lambda match: " " * len(match[0]) if match[0].startswith(("//", "/*")) else match[0], source)
        masked = C_TOKEN.sub(lambda match: " " * len(match[0]), clean)
        for call in re.finditer(r"\bLOC_PRINTF\s*\(", masked):
            match = C_REFERENCE.match(clean, call.start())
            if not match:
                raise ValueError(f"LOC_PRINTF requires a constant message and literal format reference: {path.name}")
            message, literals = match.groups()
            if message not in messages:
                raise ValueError(f"Unknown source message: {message}")
            expected = "".join(json.loads(token[0]) for token in re.finditer(r'"(?:\\.|[^"\\])*"', literals))
            if format_signature(expected) != format_signature(messages[message]):
                raise ValueError(f"Source format contract differs: {path.name}/{message}")


def write_changed(path, text):
    if path.is_file() and path.read_text(encoding="utf-8") == text:
        return
    import os
    temporary = path.with_name(f"{path.name}.{os.getpid()}.tmp")
    try:
        temporary.write_text(text, encoding="utf-8", newline="\n")
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def generate(directory, output, source_directory=None):
    catalogs = load_catalogs(directory)
    if source_directory is not None:
        validate_source_contracts(catalogs, source_directory)
    keys = sorted(catalogs[0]["messages"])
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    header = ["/* Generated; edit JSON catalogs in resources/translations. */", "#pragma once", "#include <stddef.h>",
              "typedef enum {"]
    header += [f"    LOC_{key.upper().replace('.', '_')}," for key in keys]
    header += ["    LOC_MESSAGE_COUNT", "} LocalizationMessage;",
               "typedef struct LocalizationCatalog { const char *code; const char *name; const char *messages[LOC_MESSAGE_COUNT]; } LocalizationCatalog;",
               "extern const char * const localization_message_keys[LOC_MESSAGE_COUNT];",
               "extern const LocalizationCatalog localization_catalogs[];",
               "extern const size_t localization_catalog_count;", ""]
    source = ['#include "LocalizationCatalog.generated.h"', "const char * const localization_message_keys[LOC_MESSAGE_COUNT] = {"]
    source += [f"    {c_string(key)}," for key in keys]
    source += ["};", "const LocalizationCatalog localization_catalogs[] = {"]
    for catalog in catalogs:
        source.append(f"    {{ {c_string(catalog['locale'])}, {c_string(catalog['name'])}, {{")
        source += [f"        {c_string(catalog['messages'][key])}," for key in keys]
        source.append("    } },")
    source += ["};", "const size_t localization_catalog_count = sizeof(localization_catalogs) / sizeof(localization_catalogs[0]);", ""]
    write_changed(output / "LocalizationCatalog.generated.h", "\n".join(header))
    write_changed(output / "LocalizationCatalog.generated.c", "\n".join(source))
    write_changed(output / "catalog-characters.txt", "".join(c["name"] + "".join(c["messages"].values()) for c in catalogs))
    return len(catalogs), len(keys)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("catalogs")
    parser.add_argument("output")
    arguments = parser.parse_args()
    count, keys = generate(arguments.catalogs, arguments.output, Path(__file__).resolve().parents[1] / "src")
    print(f"Validated {count} catalogs with {keys} messages each")
