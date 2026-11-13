"""Exercise temporary-image restoration without touching hardware or MQTT."""
import os
from pathlib import Path
import subprocess
import unittest
import uuid


class HealthReportingWrapperTests(unittest.TestCase):
    def test_each_failure_restores_production_and_warnings_fail_acceptance(self):
        root = Path(__file__).resolve().parents[3]
        wrapper = root / 'test/Communication/SerialMonitor/diagnostic/verify_health_reporting.ps1'
        for scenario in ('success', 'upload_error', 'upload_warning', 'live_error', 'restore_error'):
            with self.subTest(scenario=scenario):
                work = root / 'test/build' / ('health-wrapper-' + uuid.uuid4().hex)
                (work / 'test/build').mkdir(parents=True)
                (work / 'platformio').mkdir()
                (work / 'platformio/__init__.py').write_text('', encoding='utf-8')
                (work / 'platformio/__main__.py').write_text('''
import os,sys
mode=os.environ['HEALTH_WRAPPER_CASE']
diagnostic='esp32-c6-health-report' in sys.argv
print('DIAGNOSTIC_UPLOAD' if diagnostic else 'PRODUCTION_RESTORE')
if diagnostic and mode=='upload_warning':print('Warning: injected upload diagnostic')
sys.exit(1 if (diagnostic and mode=='upload_error') or (not diagnostic and mode=='restore_error') else 0)
''', encoding='utf-8')
                live = work / 'test/Communication/Zigbee/diagnostic'
                live.mkdir(parents=True)
                (live / 'live_zigbee.mjs').write_text('''
import fs from 'node:fs';
fs.writeFileSync('test/build/live-ran.json','{}');
fs.writeFileSync('test/build/zigbee-live-verify.json','{}');
process.exit(process.env.HEALTH_WRAPPER_CASE === 'live_error' ? 1 : 0);
''', encoding='utf-8')
                env = {**os.environ, 'HEALTH_WRAPPER_CASE': scenario}
                result = subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                                         '-File', str(wrapper), '-Port', 'FAKE'], cwd=work,
                                        env=env, capture_output=True, timeout=45)
                self.assertEqual(result.returncode == 0, scenario == 'success',
                                 result.stderr.decode('utf-8', errors='replace'))
                restore = work / 'test/build/health-report-production-restore.log'
                raw = restore.read_bytes()
                text = raw.decode('utf-16' if raw[:2] == b'\xff\xfe' else 'utf-8-sig')
                self.assertIn('PRODUCTION_RESTORE', text)
                self.assertEqual((work / 'test/build/live-ran.json').exists(),
                                 scenario not in ('upload_error', 'upload_warning'))


if __name__ == '__main__':
    unittest.main()
