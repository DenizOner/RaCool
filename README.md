# RaCool ❄️🔥

[![RaCool CI](https://github.com/DenizOner/RaCool/actions/workflows/ci.yml/badge.svg)](https://github.com/DenizOner/RaCool/actions/workflows/ci.yml)
[![ESP32-C6](https://img.shields.io/badge/SoC-ESP32--C6%20(8MB%20Flash)-red.svg)](https://www.espressif.com/en/products/socs/esp32-c6)
[![Framework](https://img.shields.io/badge/Framework-ESP--IDF%20%2F%20FreeRTOS-blue.svg)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/)
[![Zigbee 3.0](https://img.shields.io/badge/Protocol-Native%20Zigbee%203.0-orange.svg)](https://github.com/Koenkk/zigbee2mqtt)
[![UI](https://img.shields.io/badge/GUI-LVGL%209%20OLED%20(128x64)-brightgreen.svg)](https://lvgl.io)

**RaCool** is a mains-powered, high-precision smart radiator cooler, active ventilation, and thermostat controller built on the **ESP32-C6** microcontroller (8 MB flash).

It features native **Zigbee 3.0** integration into **Home Assistant** via **Zigbee2MQTT**, an on-device **128×64 SSD1306 OLED** user interface powered by **LVGL 9**, a **KY-040 rotary encoder** for intuitive tactile control, dual non-contact infrared sensing (**MLX90614** for ambient and radiator surface temperature), fan relay actuation, and dual RGB status indicators.

---

## 🌟 Key Features

- **Native Zigbee 3.0**: Directly connects to Zigbee2MQTT with custom external converter ([`tools/racool.mjs`](tools/racool.mjs)), exposing climate entities, radiator/ambient temperatures, fan speed modes, child lock, and full diagnostics.
- **Dual Infrared Temperature Sensing (MLX90614)**: Measures both radiator surface temperature and ambient room temperature simultaneously without thermal lag.
- **LVGL 9 OLED UI**: Multi-language (Turkish, English, Russian, Greek), high-contrast 128×64 monochrome OLED interface with smooth transitions, setting editors, and screensaver modes.
- **Rotary Encoder (KY-040)**: Rotary knob navigation with click/long-press actions (10s long-press triggers hardware factory reset).
- **Intelligent Fan Control**: Multiple cooling profiles (Eco, Comfort, Boost, Manual, Automatic differential cooling).
- **Dual RGB Status LEDs & Built-in LED**: Visual indication of heating/cooling states, Zigbee pairing, alarms, and lockouts.
- **Enterprise-grade Architecture**: Built on FreeRTOS with strict separation of concerns, Dependency Injection, ServiceLocator, EventBus, memory management, and hardware watchdogs.

---

## 🛠️ Hardware Specifications

| Component | Specification | Description |
|---|---|---|
| **MCU** | ESP32-C6 (rev 0.1), RISC-V, 8 MB Flash | Wi-Fi 6, Bluetooth 5 (LE), Zigbee 3.0 / Thread |
| **Display** | 128×64 Monochrome OLED (SSD1306, I2C) | LVGL 9 display engine with custom typography |
| **Input** | KY-040 Rotary Encoder + Push Button | Hardware PCNT counter, debounced input |
| **Sensors** | MLX90614 Dual IR (I2C) + On-chip Sensor | Radiator surface temp, ambient room temp, chip temp |
| **Actuator** | Active Fan Relay (GPIO) | Controlled ventilation & convection cooling |
| **Indicators** | 2× WS2812B RGB LEDs + 1× Builtin LED | Multi-state lighting, animations, and alarms |
| **Power** | 220V Mains Powered | Always-on, non-battery design |

---

## 📂 Project Structure

```
RaCool/
├── .agent/               # Agent rules, workflows, skills and scripts
├── .github/              # GitHub Actions CI/CD workflows and issue templates
├── include/              # Public C/C++ header files organized by module
│   ├── Communication/    # Zigbee, DebugConsole, SerialMonitor
│   ├── Controllers/      # UIController, ClimateController, SystemController
│   ├── Core/             # DI, EventBus, Logger, ServiceLocator, SystemInitializer
│   ├── Hardware/         # OLED, RotaryEncoder, RGBLED, BuiltinLED, MLX90614, Relay
│   └── Management/       # ApplicationManager, ConfigurationManager, Watchdog
├── src/                  # Implementation sources mirroring include/
├── test/                 # Test suites and diagnostic harnesses
│   ├── Communication/    # Zigbee & Serial integration tests
│   ├── Controllers/      # Controller unit tests
│   ├── Core/             # Core infrastructure tests
│   ├── Hardware/         # Hardware mock and unit tests
│   ├── Infrastructure/   # Shared mocks, support libraries, and diagnostic scripts
│   ├── Management/       # Application & Configuration tests
│   ├── native_suites.json# Host native unit test manifest
│   └── target_suites.json# On-target hardware test manifest
├── tools/                # Build scripts, font converters, Zigbee converter, HA cards
│   ├── racool.mjs        # Authoritative Zigbee2MQTT device converter
│   └── home-assistant/   # Custom Lovelace UI cards and dashboards
├── platformio.ini        # PlatformIO build configuration
└── sdkconfig.defaults    # Baseline ESP-IDF configuration
```

---

## 🚀 Getting Started

### Prerequisites

- **Python 3.10+**
- **Node.js 20+**
- **PlatformIO Core 6.1.16+** (`pip install -U platformio`)
- **GCC / Clang** (for running host native unit tests)

### Installation

```bash
# Clone the repository
git clone https://github.com/DenizOner/RaCool.git
cd RaCool

# Install Node.js dependencies
npm ci

# Install Python development dependencies
pip install -r requirements-dev.txt
```

---

## 🧪 Testing & Verification

RaCool enforces a zero-warning, zero-error test and verification policy:

```bash
# 1. Verify test directory layout and inventory
node test/check_test_layout.mjs
node test/check_test_inventory.mjs

# 2. Run all helper test suites (82 suites, 406 cases)
node test/run_helper_tests.mjs

# 3. Run host native unit tests (GCC-based Unity)
node test/run_native_tests.mjs
# (Or on Windows PowerShell):
powershell -ExecutionPolicy Bypass -File test/run_native_tests.ps1

# 4. Build firmware with PlatformIO (0 warnings, 0 errors required)
pio run -e esp32-c6-devkitc-1
```

---

## 🤖 GitHub Actions CI/CD

Every pull request and push to `main` triggers our automated [GitHub Actions CI](https://github.com/DenizOner/RaCool/actions):

1. **Layout & Inventory Verification**: Validates module counterparts and suite manifests.
2. **Helper Test Suites**: Executes all 82 node/python verification suites.
3. **Host Native Unit Tests**: Compiles and runs all C unit tests using Unity on the host runner.
4. **Firmware Compilation**: Builds production firmware for `esp32-c6-devkitc-1` and publishes build artifacts (`firmware.bin`, `bootloader.bin`, `partitions.bin`).

---

## 🤝 Contribution & Development Workflow

1. Tasks, bug reports, and features are tracked via [GitHub Issues](https://github.com/DenizOner/RaCool/issues).
2. Work on a dedicated feature or bug branch (`feature/xyz` or `fix/xyz`).
3. Ensure all local tests and inventory checks pass before pushing.
4. Open a Pull Request referencing the issue (`Closes #<id>`).
5. Ensure GitHub Actions CI passes completely before merging.

---

## 📄 Directives & Architectural Rules

See [AGENTS.md](AGENTS.md) for engineering directives, memory management, lock hierarchies, and hardware constraints.  
See [CODEBASE.md](CODEBASE.md) for detailed module architecture and supported CLI commands.

