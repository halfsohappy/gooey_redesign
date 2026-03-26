# PlatformIO extra script — replaces the default espressif32 upload action
# with a call to upload_smallblock.py so `pio run -t upload` works on this
# ESP32-S3 AP_3v3 device without a BOOT button.
Import("env")  # noqa: F821  (SCons injection)

import os
import sys
import subprocess

def small_block_upload(source, target, env):
    """Upload via the small-block esptool shim."""
    python    = sys.executable
    shim      = os.path.join(env.subst("$PROJECT_DIR"), "scripts", "upload_smallblock.py")
    build_dir = env.subst("$BUILD_DIR")
    port      = env.subst("$UPLOAD_PORT")

    # PIOHOME_DIR isn't always expanded via subst; use the env var or default
    piohome = (
        os.environ.get("PLATFORMIO_CORE_DIR")
        or os.environ.get("PIOHOME_DIR")
        or os.path.expanduser("~/.platformio")
    )
    boot_app0 = os.path.join(
        piohome, "packages",
        "framework-arduinoespressif32",
        "tools", "partitions", "boot_app0.bin",
    )

    cmd = [
        python, shim,
        "--chip", "esp32s3",
        "--port", port,
        "--baud", "115200",
        "--before", "default_reset",
        "--after", "hard_reset",
        "write_flash", "-u",
        "--flash_mode", "dio",
        "--flash_freq", "80m",
        "--flash_size", "16MB",
        "0x0000", os.path.join(build_dir, "bootloader.bin"),
        "0x8000", os.path.join(build_dir, "partitions.bin"),
        "0xe000", boot_app0,
        "0x10000", os.path.join(build_dir, "firmware.bin"),
    ]

    print("Uploading via small-block shim (128-byte blocks for AP_3v3 USB-CDC)...")
    ret = subprocess.call(cmd)
    if ret != 0:
        env.Exit(ret)

env.Replace(UPLOADCMD=small_block_upload)
