# ⚡ ESP8266 Flashing & Memory Architecture Guide

This guide explains the ESP8266 RTOS SDK flash memory layout and provides step-by-step instructions for flashing your **ESP8266-LoRa-Gateway** firmware.

---

## ❓ Why Are There 3 Binary Files?

Unlike simpler Arduino sketches that lump everything into a single file, Espressif's RTOS SDK splits firmware into three modular components:

| Memory Offset | File | Description |
| :--- | :--- | :--- |
| `0x00000` | **`bootloader.bin`** | **First-stage Bootloader**: Initializes hardware, verifies image checksums, and handles OTA switching. |
| `0x08000` | **`partition-table.bin`** | **Memory Map**: Defines the location and size of app partitions, NVS storage, and file systems. |
| `0x10000` | **`esp-gateway-project-<hash>.bin`** | **Main Application**: Your compiled C code (FreeRTOS, Wi-Fi, LoRa, SK9822 LED drivers). |

---

## 🗺️ Flash Memory Map

```text
+-------------------+---------------------------------------------------+
| Flash Address     | Component                                         |
+-------------------+---------------------------------------------------+
| 0x00000 - 0x07FFF | 🚀 Bootloader                                     |
| 0x08000 - 0x08FFF | 🗺️ Partition Table                                |
| 0x09000 - 0x0FFFF | 💾 NVS (Non-Volatile Storage)                     |
| 0x10000 +         | ⚙️ Main Application Firmware                       |
+-------------------+---------------------------------------------------+
```

---

## 🏗️ Build & Automatic Flashing Commands

If you are developing locally with the **ESP8266 RTOS SDK** environment active, you can use the SDK's built-in `make` commands. 

When you run make flash, Espressif's build system automatically checks all three components (bootloader.bin, partition-table.bin, and esp-gateway-project-<hash>.bin) and flashes whichever ones are missing or out of date to their respective offsets (0x0000, 0x8000, and 0x10000).

```bash
# 1. Navigate to the project directory
cd esp-gateway-project

# 2. Build the firmware binaries
make -j$(nproc) CONFIG_SDK_PYTHON=python3

# 3. Flash all components automatically to the board
make flash PORT=/dev/ttyUSB0 CONFIG_SDK_PYTHON=python3

# 4. Open the serial console monitor
make monitor PORT=/dev/ttyUSB0

# 5. Build, flash, and open monitor in a single step
make flash monitor PORT=/dev/ttyUSB0 CONFIG_SDK_PYTHON=python3
```

Behind the scenes, make flash expands to an esptool.py command that passes all three binary targets simultaneously:

```bash
esptool.py -p /dev/ttyUSB0 -b 115200 write_flash \
  0x0000 bootloader.bin \
  0x8000 partition-table.bin \
  0x10000 esp-gateway-project-<hash>.bin
```

## ⚙️ WSL Share USB Port

If you are trying to flash the esp from within WSL. You have to share the COM port.

Open powershell as admin run: 

```bash
usbipd list
Connected:
BUSID  VID:PID    DEVICE                                                        STATE
9-4    1a86:7523  USB-SERIAL CH340 (COM23)                                      Not shared
```

Now attach the device to WSL.

`usbipd bind --busid 9-4`

`usbipd attach --wsl --busid 9-4`

To detach:

`usbipd detach --busid <busid>`