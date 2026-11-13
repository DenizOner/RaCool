"""Live serial entry point; requires --port and --output, separate from host tests."""
from pathlib import Path
import subprocess
import sys

if __name__ == "__main__":
    helper = Path(__file__).resolve().parents[2] / "diagnostic" / "verify_readonly_health.py"
    raise SystemExit(subprocess.run([sys.executable, str(helper), "--kind", "diagnostics", *sys.argv[1:]], check=False).returncode)
