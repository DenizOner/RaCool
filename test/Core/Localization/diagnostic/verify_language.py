"""Exercise real language persistence and sleeping-screen behavior; restore locale."""
import argparse
import codecs
import json
import re
import sys
import time
from pathlib import Path
import serial

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))
from Infrastructure.diagnostic.serial_reconnect import read_available
from Communication.DebugConsole.diagnostic.developer_session import DeveloperSession,has_mode_response


def parse_locale(text):
    matches = re.findall(r"^RACOOL_CLI v=1 type=language locale=([a-z]{2,3}(?:-[A-Z]{2})?)\r*\n", text, re.M)
    return matches[-1] if matches else None


def parse_config(text):
    matches = re.findall(r"^RACOOL_CLI v=1 type=language_config saved=([a-z]{2,3}(?:-[A-Z]{2})?) active=([a-z]{2,3}(?:-[A-Z]{2})?) restorable=([01])\r*\n", text, re.M)
    return {"saved": matches[-1][0], "active": matches[-1][1], "restorable": matches[-1][2] == "1"} if matches else None


def parse_set(text):
    matches = re.findall(r"^RACOOL_CLI v=1 type=language_set result=(applied|invalid_locale|save_failed)(?: locale=([a-z]{2,3}(?:-[A-Z]{2})?)| error=(-?\d+))?\r*\n", text, re.M)
    if not matches: return None
    result, locale, error = matches[-1]
    if result == "applied" and not locale: return None
    if result == "save_failed" and not error: return None
    if result == "invalid_locale" and (locale or error): return None
    return {"result": result, "locale": locale or None, "error": int(error) if error else None}


def parse_catalogs(text):
    entries = re.findall(r"^RACOOL_CLI v=1 type=language_catalog locale=([a-z]{2,3}(?:-[A-Z]{2})?)\r*\n", text, re.M)
    ending = re.search(r"^RACOOL_CLI v=1 type=language_catalog_end count=(\d+)\r*\n", text, re.M)
    if not ending or len(entries) != int(ending[1]) or not entries or len(set(entries)) != len(entries): return None
    return entries


def parse_screen(text):
    matches = re.findall(r"^RACOOL_CLI v=1 type=ui screen=(active|suspended) service=\d+ boot=(?:running|idle)\r*\n", text, re.M)
    return matches[-1].upper() if matches else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    prefix = Path(args.output)
    evidence = {"completed": False, "restored": False, "checks": [], "original": None}
    def save():
        prefix.with_suffix(".json").write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding="utf-8")
    port = serial.Serial(port=None, baudrate=115200, timeout=0.1)
    port.dtr = port.rts = False
    port.port = args.port
    with port, prefix.with_suffix(".log").open("w", encoding="utf-8") as log:
        def receive(deadline, decoder):
            data = read_available(port, deadline, log.write)
            text = decoder.decode(data)
            log.write(text); log.flush()
            return text

        def command(text, accept, timeout=8):
            port.reset_input_buffer()
            log.write(f"\nCOMMAND: {text}\n")
            port.write((text + "\r\n").encode())
            decoder = codecs.getincrementaldecoder("utf-8")("replace")
            response = ""
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                response += receive(deadline, decoder)
                if accept(response): return response
            raise RuntimeError("Missing fresh command result: " + text)

        def locale():
            return parse_locale(command("LANGUAGE --machine", lambda text: parse_locale(text) is not None))

        def apply(code):
            outcome = parse_set(command("LANGUAGE SET " + code + " --machine", lambda text: parse_set(text) is not None))
            if outcome["result"] != "applied" or outcome["locale"] != code: raise RuntimeError("Language write rejected")
            actual = locale()
            if actual != code: raise RuntimeError("Applied locale readback mismatch")

        def asleep():
            deadline = time.monotonic() + 180
            while time.monotonic() < deadline:
                state = parse_screen(command("UI STATUS --machine", lambda text: parse_screen(text) is not None))
                if state == "SUSPENDED": return
                time.sleep(1)
            raise RuntimeError("OLED did not reach its configured sleep state")

        def restart():
            command("power restart 500 --machine", lambda text: "RACOOL_CLI v=1 type=power_restart result=queued delay_ms=500 error=0" in text)
            decoder = codecs.getincrementaldecoder("utf-8")("replace")
            boot = ""; deadline = time.monotonic() + 40
            while time.monotonic() < deadline:
                boot += receive(deadline, decoder)
                marker = boot.rfind("ESP-ROM:")
                if marker >= 0 and re.search(r"rst:[^\n]*\(SW_CPU\)", boot[marker:]) and "racool>" in boot[marker:] and "SYSTEM INITIALIZATION COMPLETE" in boot[marker:]: break
            else: raise RuntimeError("No fresh software boot/console after restart")
            session.after_boot()
            for attempt in range(5):
                try: return locale(), boot
                except RuntimeError:
                    if attempt == 4: raise
                    time.sleep(0.5)

        session=DeveloperSession(lambda text:command(text,has_mode_response))
        try:
            session.open()
            baseline = parse_config(command("LANGUAGE CONFIG --machine", lambda text: parse_config(text) is not None))
            evidence["configuration_baseline"] = baseline; save()
            if not baseline["restorable"] or baseline["saved"] != baseline["active"]:
                raise RuntimeError("Refusing to overwrite an unavailable or fallback locale")
            original = evidence["original"] = baseline["saved"]; save()
            listing = parse_catalogs(command("LANGUAGE LIST --machine", lambda text: parse_catalogs(text) is not None))
            if not {"tr", "en", original}.issubset(listing): raise RuntimeError("Required catalogs unavailable")
            evidence["checks"].append({"kind": "catalog_list", "accepted": bool(listing)})
            asleep()
            for code in ("en", "tr"):
                apply(code)
                state = parse_screen(command("UI STATUS --machine", lambda text: parse_screen(text) is not None))
                if state != "SUSPENDED": raise RuntimeError("Language setting woke sleeping OLED")
                evidence["checks"].append({"kind": "immediate_no_wake", "locale": code, "screen": state, "timestamp": time.time()}); save()
            rejected = parse_set(command("LANGUAGE SET missing --machine", lambda text: parse_set(text) is not None))
            if rejected["result"] != "invalid_locale": raise RuntimeError("Invalid locale was not rejected")
            if locale() != "tr": raise RuntimeError("Rejected locale altered selection")
            evidence["checks"].append({"kind": "unknown_locale_rejected"})
            apply("en")
            actual, boot = restart()
            if actual != "en": raise RuntimeError("Locale did not survive real software restart")
            if "Guru Meditation" in boot or "Queued attribute failed" in boot:
                raise RuntimeError("New software boot contains firmware faults")
            evidence["checks"].append({"kind": "fresh_software_boot_persistence", "locale": actual}); save()
            evidence["completed"] = True
        except Exception as error:
            evidence["error"] = str(error)
            raise
        finally:
            try:
                if evidence["original"] is not None:
                    if locale() != evidence["original"]: apply(evidence["original"])
                    if locale() != evidence["original"]: raise RuntimeError("Original locale not restored")
                    restored = parse_config(command("LANGUAGE CONFIG --machine", lambda text: parse_config(text) is not None))
                    if restored != evidence["configuration_baseline"]: raise RuntimeError("Saved/active locale baseline not restored")
                    evidence["restored"] = True
            except Exception as error:
                evidence["restoration_error"] = str(error)
                evidence["completed"] = False
                raise
            finally:
                try: session.close()
                except Exception as error:
                    evidence['completed']=False;evidence['developer_mode_restore_error']=str(error);raise
                finally: save()
    print("Language live persistence/no-wake/restoration accepted")


if __name__ == "__main__": main()
