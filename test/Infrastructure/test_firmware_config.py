"""Check selected ESP-IDF flash settings against PlatformIO board settings."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))


import configparser
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


class FirmwareConfigurationTests(unittest.TestCase):
    def test_selected_target_and_production_flash_match_board(self):
        config = configparser.ConfigParser(interpolation=None)
        config.read(ROOT / "platformio.ini", encoding="utf-8")
        for name in ("esp32-c6-devkitc-1", "esp32-c6-devkitc-1_test"):
            with self.subTest(environment=name):
                section = config["env:" + name]
                selected = section.get("board_build.esp-idf.sdkconfig_path", "sdkconfig." + name)
                text = (ROOT / selected).read_text(encoding="utf-8")
                size = section["board_upload.flash_size"]
                self.assertRegex(text, r'(?m)^CONFIG_ESPTOOLPY_FLASHSIZE="' + re.escape(size) + r'"$')
                self.assertRegex(text, r'(?m)^CONFIG_ESPTOOLPY_FLASHSIZE_' + re.escape(size) + r'=y$')
                choices = re.findall(r"(?m)^CONFIG_ESPTOOLPY_FLASHSIZE_([0-9]+MB)=y$", text)
                self.assertEqual(choices, [size])
                self.assertRegex(text, r"(?m)^CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192$")
                self.assertRegex(text, r"(?m)^CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y$")
                self.assertRegex(text, r"(?m)^CONFIG_FREERTOS_HZ=100$")
                self.assertRegex(text, r"(?m)^CONFIG_PM_ENABLE=y$")
                self.assertRegex(text, r"(?m)^CONFIG_FREERTOS_USE_TICKLESS_IDLE=y$")
                partitions = section["board_build.partitions"]
                self.assertRegex(text, r"(?m)^CONFIG_PARTITION_TABLE_CUSTOM=y$")
                self.assertRegex(text, r'(?m)^CONFIG_PARTITION_TABLE_FILENAME="' + re.escape(partitions) + r'"$')
                self.assertRegex(text, r'(?m)^CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="' + re.escape(partitions) + r'"$')

    def test_zigbee_pin_matches_lock_and_managed_headers(self):
        manifest = (ROOT / "src/idf_component.yml").read_text()
        match = re.search(r'espressif/esp-zigbee-lib:\s*"(\d+\.\d+\.\d+)"', manifest)
        self.assertIsNotNone(match)
        version = match[1]
        lock = (ROOT / "dependencies.lock").read_text()
        component = re.search(r'(?ms)^  espressif/esp-zigbee-lib:\n(.*?)(?=^  [^ ]|\Z)', lock)
        self.assertIsNotNone(component)
        self.assertRegex(component[1], r'(?m)^    version: ' + re.escape(version) + r'$')
        header = (ROOT / "managed_components/espressif__esp-zigbee-lib/include/esp_zigbee_version.h").read_text()
        selected = [int(re.search(r'(?m)^#define ESP_ZIGBEE_VER_' + part + r' (\d+)$', header)[1])
                    for part in ("MAJOR", "MINOR", "PATCH")]
        self.assertEqual(version, ".".join(map(str, selected)))
        api = (ROOT / "managed_components/espressif__esp-zigbee-lib/include/esp_zigbee.h").read_text()
        self.assertIn("esp_err_t esp_zigbee_stop(void);", api)

    def test_all_target_environments_track_project_manifests(self):
        config = configparser.ConfigParser(interpolation=None)
        config.read(ROOT / "platformio.ini", encoding="utf-8")
        for name in ("esp32-c6-devkitc-1", "esp32-c6-devkitc-1_test"):
            with self.subTest(environment=name):
                self.assertIn("pre:tools/platformio_manifest.py", config["env:" + name]["extra_scripts"])


if __name__ == "__main__":
    unittest.main()
