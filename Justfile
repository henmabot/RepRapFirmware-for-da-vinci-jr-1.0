set shell := ["bash", "-eu", "-o", "pipefail", "-c"]

default:
    @just --list

# Build the Duet 2 WiFi RepRapFirmware image with the command-line toolchain.
build:
    python3 tools/native_build.py build

# Remove all native build outputs.
clean:
    python3 tools/native_build.py clean
