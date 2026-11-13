"""Checked temporary Developer Mode ownership for serial diagnostic helpers.

The caller supplies an exchange function that sends once and returns the full
response after its normal bounded deadline. No command is retried here.
"""
import re

_MODE = re.compile(r"(?m)^RACOOL_CLI v=1 type=developer_mode enabled=([01])\r?$")


def parse_mode(text):
    values = _MODE.findall(text)
    if len(values) != 1:
        raise RuntimeError("Developer Mode response missing or ambiguous")
    return values[0] == "1"


def has_mode_response(text):
    try:
        parse_mode(text)
        return True
    except RuntimeError:
        return False


class DeveloperSession:
    def __init__(self, exchange):
        self.exchange = exchange
        self.original = None
        self.changed = False

    def _read(self):
        return parse_mode(self.exchange("DEVELOPER STATUS --machine"))

    def _set(self, enabled):
        value = "ON" if enabled else "OFF"
        result = parse_mode(self.exchange(f"DEVELOPER {value} --machine"))
        if result != enabled or self._read() != enabled:
            raise RuntimeError("Developer Mode was not applied")

    def open(self):
        if self.original is not None:
            raise RuntimeError("Developer Mode session already open")
        self.original = self._read()
        if not self.original:
            # Record responsibility before transmission: a timeout is ambiguous.
            self.changed = True
            self._set(True)

    def after_boot(self):
        if self.original is None:
            raise RuntimeError("Developer Mode session is not open")
        if not self._read():
            raise RuntimeError("Developer Mode preference changed after reboot")

    def close(self):
        if self.original is None:
            return
        current = self._read()
        if self.changed:
            if current:
                self._set(False)
        elif current != self.original:
            raise RuntimeError("Developer Mode changed outside this session")
        self.original = None
        self.changed = False
