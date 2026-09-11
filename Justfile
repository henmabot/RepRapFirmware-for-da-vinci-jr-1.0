set shell := ["bash", "-eu", "-o", "pipefail", "-c"]

default:
    @just --list

# Build both Da Vinci Jr 1.0 MCU firmware images with the command-line toolchain.
build:
    python3 tools/native_build.py build
    python3 tools/lpc_build.py build

# Remove all native build outputs.
clean:
    python3 tools/native_build.py clean
    python3 tools/lpc_build.py clean
