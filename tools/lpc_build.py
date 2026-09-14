#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / "build" / "LpcFirmware"
FLASH_SIZE = 64 * 1024
RAM_TOP = 0x10002000
CPU_FLAGS = ("-mcpu=cortex-m0", "-mthumb")
COMMON_FLAGS = (
    "-Os",
    *CPU_FLAGS,
    "-ffunction-sections",
    "-fdata-sections",
    "-fno-unwind-tables",
    "-fno-asynchronous-unwind-tables",
    "-Wall",
    "-Wextra",
    "-Werror=return-type",
)
INCLUDES = (
    "-I",
    str(ROOT / "LpcFirmware" / "src"),
    "-I",
    str(ROOT / "Shared" / "src"),
)
SOURCES = (
    ROOT / "LpcFirmware" / "src" / "startup.S",
    ROOT / "LpcFirmware" / "src" / "Uart.cpp",
    ROOT / "LpcFirmware" / "src" / "Gpio.cpp",
    ROOT / "LpcFirmware" / "src" / "Pwm.cpp",
    ROOT / "LpcFirmware" / "src" / "Thermal.cpp",
    ROOT / "LpcFirmware" / "src" / "main.cpp",
    ROOT / "Shared" / "src" / "LpcProtocol.cpp",
)


def tool(name: str) -> str:
    executable = os.environ.get("CROSS_COMPILE", "arm-none-eabi-") + name
    if shutil.which(executable) is None:
        raise RuntimeError(f"{executable} was not found in PATH")
    return executable


def run(command: list[str]) -> None:
    subprocess.run(command, check=True)


def compile_source(source: Path) -> Path:
    relative = source.relative_to(ROOT)
    obj = BUILD_DIR / "obj" / relative.with_suffix(relative.suffix + ".o")
    obj.parent.mkdir(parents=True, exist_ok=True)
    if source.suffix == ".S":
        command = [tool("gcc"), *COMMON_FLAGS, *INCLUDES, "-c", str(source), "-o", str(obj)]
    else:
        command = [
            tool("g++"),
            *COMMON_FLAGS,
            "-std=gnu++17",
            "-fno-exceptions",
            "-fno-rtti",
            "-fno-threadsafe-statics",
            "-fno-use-cxa-atexit",
            "-nostdlib",
            *INCLUDES,
            "-c",
            str(source),
            "-o",
            str(obj),
        ]
    run(command)
    return obj


def validate_image(binary: Path) -> None:
    data = binary.read_bytes()
    if len(data) < 32:
        raise RuntimeError("LPC1115 image is too small to contain a vector table")
    if len(data) > FLASH_SIZE:
        raise RuntimeError(f"LPC1115 image exceeds {FLASH_SIZE} bytes of flash")

    words = struct.unpack_from("<8I", data)
    if words[0] != RAM_TOP:
        raise RuntimeError(f"LPC1115 initial stack pointer is 0x{words[0]:08X}, expected 0x{RAM_TOP:08X}")
    reset_vector = words[1]
    if (reset_vector & 1) == 0 or (reset_vector & ~1) >= len(data):
        raise RuntimeError(f"LPC1115 reset vector 0x{reset_vector:08X} is not a valid Thumb target in the image")
    if sum(words) & 0xFFFFFFFF:
        raise RuntimeError("LPC1115 vector checksum is invalid")


def build() -> None:
    clean()
    objects = [compile_source(source) for source in SOURCES]
    elf = BUILD_DIR / "Lpc1115Firmware.elf"
    binary = BUILD_DIR / "Lpc1115Firmware.bin"
    map_file = BUILD_DIR / "Lpc1115Firmware.map"
    linker_script = ROOT / "LpcFirmware" / "lpc1115.ld"
    run(
        [
            tool("g++"),
            *CPU_FLAGS,
            "-nostdlib",
            "-Wl,--gc-sections",
            "-Wl,--fatal-warnings",
            f"-Wl,-Map,{map_file}",
            f"-T{linker_script}",
            *map(str, objects),
            "-lm",
            "-lc",
            "-lgcc",
            "-o",
            str(elf),
        ]
    )
    run([tool("objcopy"), "-O", "binary", str(elf), str(binary)])
    validate_image(binary)
    run([tool("size"), str(elf)])
    print(f"LPC firmware: {binary.relative_to(ROOT)}", flush=True)


def clean() -> None:
    shutil.rmtree(BUILD_DIR, ignore_errors=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="Build the Da Vinci Jr LPC1115 firmware")
    parser.add_argument("command", choices=("build", "clean"))
    args = parser.parse_args()
    try:
        clean() if args.command == "clean" else build()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
