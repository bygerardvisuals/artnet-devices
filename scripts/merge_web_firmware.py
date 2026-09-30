"""Create the single image consumed by the Chrome installer after each build."""

import subprocess
import shutil
from pathlib import Path

from SCons.Script import Import

Import("env")


def merge_for_web_installer(source, target, env):
    project = Path(env.subst("$PROJECT_DIR"))
    build = Path(env.subst("$BUILD_DIR"))
    environment = env.subst("$PIOENV")
    destination = project / "web-installer" / "firmware" / f"artnet-devices-{environment}.bin"
    ota_destination = project / "web-installer" / "firmware" / f"artnet-devices-{environment}-ota.bin"
    destination.parent.mkdir(parents=True, exist_ok=True)
    board = env.BoardConfig()
    mcu = board.get("build.mcu", "esp32")
    # AddPostAction supplies SCons nodes in an order that differs by platform.
    # Resolve the application image by its known build path so factory and OTA
    # images never accidentally embed the ELF executable.
    app_binary = build / f"{env.subst('$PROGNAME')}.bin"
    bootloader_offset = "0x1000" if mcu in ("esp32", "esp32s2") else "0x0000"

    # ESP8266 already emits a self-contained image at flash offset 0.
    if mcu == "esp8266":
        shutil.copyfile(app_binary, destination)
        shutil.copyfile(app_binary, ota_destination)
        return

    framework = Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32"))

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
        "0x10000", str(app_binary),
    ]
    print("Creating Chrome installer image:", destination)
    subprocess.check_call(command)
    shutil.copyfile(app_binary, ota_destination)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_for_web_installer)
