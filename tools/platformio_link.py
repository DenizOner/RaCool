"""Preserve ESP-IDF whole-archive scopes in PlatformIO 6.9's SCons link model."""
import json
from pathlib import Path
from SCons.Subst import Literal

Import("env")

# PlatformIO 6.9 does not track sdkconfig.h for these preprocessed scripts.
# A stale MMU alignment can map code and rodata onto incompatible flash pages.
build_directory = Path(env.subst("$BUILD_DIR"))
configuration_header = str(build_directory / "config" / "sdkconfig.h")
for script in (build_directory / "esp-idf" / "esp_system" / "ld" / "sections.ld.in",
               build_directory / "memory.ld", build_directory / "sections.ld"):
    env.Depends(str(script), configuration_header)

# PlatformIO 6.9 sorts these two positional flags, leaving whole-archive enabled
# for libc and libnosys. Recover the per-library scope from CMake's code model.
reply = Path(env.subst("$BUILD_DIR")) / ".cmake/api/v1/reply"
models = list(reply.glob("target-RaCool.elf-*.json"))
if len(models) != 1:
    raise RuntimeError("Expected one RaCool CMake link model")
fragments = json.loads(models[0].read_text(encoding="utf-8"))["link"]["commandFragments"]
whole = False
archives = set()
for item in fragments:
    fragment = item["fragment"].strip()
    if fragment == "-Wl,--whole-archive":
        whole = True
    elif fragment == "-Wl,--no-whole-archive":
        whole = False
    elif whole and fragment.endswith(".a"):
        archives.add(fragment.replace("\\", "/").rsplit("/", 1)[-1])
if whole:
    raise RuntimeError("Unbalanced whole-archive scope in CMake model")

flags = {"-Wl,--whole-archive", "-Wl,--no-whole-archive"}
env.Replace(LINKFLAGS=[flag for flag in env["LINKFLAGS"] if str(flag) not in flags])
original_library_flags = env["_LIBFLAGS"]


def scoped_library_flags(target, source, env, for_signature):
    # Keep LIBS as real dependencies. Positional flags belong only in the command;
    # putting them in LIBS makes SCons search for files named '-Wl,...'. Resolve
    # lazily so libraries added by PlatformIO's dependency finder are included.
    rendered = env.subst_list(original_library_flags, target=target, source=source)[0]
    result = []
    matched = set()
    for argument in rendered:
        name = str(argument).replace("\\", "/").rsplit("/", 1)[-1]
        if name in archives:
            result.extend([Literal("-Wl,--whole-archive"), argument,
                           Literal("-Wl,--no-whole-archive")])
            matched.add(name)
        else:
            result.append(argument)
    if matched != archives:
        raise RuntimeError("CMake whole-archive libraries missing from SCons link command")
    return result


env.Replace(_LIBFLAGS=scoped_library_flags)
