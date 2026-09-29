"""Create the single image consumed by the Chrome installer after each build."""

import subprocess
from pathlib import Path

from SCons.Script import Import

Import("env")


def merge_for_web_installer(source, target, env):
    project = Path(env.subst("$PROJECT_DIR"))
    build = Path(env.subst("$BUILD_DIR"))
    framework = Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32"))
    destination = project / "web-installer" / "firmware" / "merged-firmware.bin"
    destination.parent.mkdir(parents=True, exist_ok=True)
    board = env.BoardConfig()
    mcu = board.get("build.mcu", "esp32")
    bootloader_offset = "0x1000" if mcu in ("esp32", "esp32s2") else "0x0000"

    command = [
        env.subst("$PYTHONEXE"),
        env.subst("$UPLOADER"),
        "--chip", mcu, "merge_bin",
        "--flash_mode", board.get("build.flash_mode", "dio"),
        "--flash_freq", board.get("build.f_flash", "40000000L").replace("000000L", "m"),
        "--flash_size", board.get("upload.flash_size", "4MB"),
        "-o", str(destination),
        bootloader_offset, str(build / "bootloader.bin"),
        "0x8000", str(build / "partitions.bin"),
        "0xe000", str(framework / "tools" / "partitions" / "boot_app0.bin"),
        "0x10000", str(source[0]),
    ]
    print("Creating Chrome installer image:", destination)
    subprocess.check_call(command)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_for_web_installer)
