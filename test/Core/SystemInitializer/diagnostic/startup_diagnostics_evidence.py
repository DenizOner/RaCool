"""Validate local bootstrap and initial health ordering in a fresh serial boot."""
import re


def validate(text):
    clean = re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", text)
    boot = clean.find("ESP-ROM:")
    if boot < 0 or clean.find("ESP-ROM:", boot + 1) >= 0:
        raise ValueError("Require exactly one fresh ROM boot")
    complete = clean.find("System Initialization COMPLETE", boot)
    hardware = clean.find("SUCCESS: InternalTemperatureSensor started", boot)
    infrared = clean.find("SUCCESS: InfraredTemperatureSensor started", boot)
    ready = clean.find("app_main: System ready.", boot)
    joined = clean.find("Device joined Zigbee network successfully", boot)
    scan = clean.find("Asynchronous self-test started", boot)
    if min(complete, hardware, infrared, ready, joined, scan) < 0:
        raise ValueError("Incomplete startup evidence")
    if not hardware < complete < ready or not infrared < complete:
        raise ValueError("Bootstrap claimed readiness before hardware completion")
    if scan < joined:
        raise ValueError("Initial self-test preceded actual Zigbee readiness")
    if re.search(r"FAILED (?:Initialize|Register|Start)|Service FAILED:|Custom check FAILED:|Guru Meditation|Task watchdog got triggered", clean):
        raise ValueError("Startup or health failure")
    finished = re.search(r"Self-test finished\. Health Score: (\d+)", clean[scan:])
    if not finished or int(finished[1]) != 100:
        raise ValueError("Initial scan did not complete at full service health")
    return {"bootstrap_precedes_ready": True, "initial_scan_after_join": True,
            "initial_score": int(finished[1]), "scope": "Fresh serial service ordering, not physical or remote delivery"}
