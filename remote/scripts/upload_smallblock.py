#!/usr/bin/env python3
"""
esptool wrapper with reduced block sizes for ESP32-S3 native USB CDC.

Root cause: this ESP32-S3 (QFN56, AP_3v3 Octal PSRAM) reports UARTDEV_BUF_NO=0
(ROM thinks it's on UART0) even though the port is USB-CDC. esptool therefore
uses the default ROM block sizes (ESP_RAM_BLOCK=0x1800, FLASH_WRITE_SIZE=0x400)
which overflow the ROM's USB-CDC receive buffer and cause every write to timeout
or return "Requested resource not found" (ROM error 0x05).

Fix: patch all block-size constants to 0x80 (128 bytes) — small enough to fit
in a single USB full-speed bulk packet — before esptool connects.
"""
import sys
import os

# Point at the PlatformIO-bundled esptool
ESPTOOL_PKG = os.path.join(
    os.path.expanduser("~"),
    ".platformio", "packages",
    "tool-esptoolpy@src-e9520c52db7d0ecbb98379d0d58b38a9",
)
sys.path.insert(0, ESPTOOL_PKG)

SMALL_BLOCK = 0x80  # 128 bytes

import esptool.loader as _loader
_loader.ESPLoader.FLASH_WRITE_SIZE = SMALL_BLOCK
_loader.ESPLoader.ESP_RAM_BLOCK   = SMALL_BLOCK

from esptool.targets import esp32s3 as _s3
_s3.ESP32S3ROM.USB_RAM_BLOCK       = SMALL_BLOCK
_s3.ESP32S3ROM.FLASH_WRITE_SIZE    = SMALL_BLOCK
_s3.ESP32S3ROM.ESP_RAM_BLOCK       = SMALL_BLOCK
_s3.ESP32S3StubLoader.USB_RAM_BLOCK    = SMALL_BLOCK
_s3.ESP32S3StubLoader.FLASH_WRITE_SIZE = SMALL_BLOCK
_s3.ESP32S3StubLoader.ESP_RAM_BLOCK    = SMALL_BLOCK

from esptool import main
sys.exit(main())
