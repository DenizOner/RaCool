"""Bounded recovery of an existing serial connection; never replays writes."""
import time
import serial


def read_available(port, deadline, record, *, clock=None, sleep=None):
    """Read or reconnect the same selected port until the caller's deadline.

    A reconnection is transport evidence only, not proof of a fresh boot.
    Commands remain the caller's responsibility and are never retried here.
    """
    clock = clock or time.monotonic
    sleep = sleep or time.sleep
    try:
        return port.read(port.in_waiting or 1)
    except (serial.SerialException, OSError) as error:
        record("\nSERIAL_DISCONNECTED: " + type(error).__name__ + "\n")
        port.close()
    while clock() < deadline:
        try:
            port.open()
            record("\nSERIAL_RECONNECTED\n")
            return b""
        except (serial.SerialException, OSError):
            sleep(min(0.25, max(0, deadline - clock())))
    raise TimeoutError("Serial connection did not recover before capture deadline")
