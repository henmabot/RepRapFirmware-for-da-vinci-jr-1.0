#!/usr/bin/env bash
#
# install.sh — Installs ARM GNU Toolchain 13.2.Rel1 into ./toolchain
#
# Usage: ./install.sh
#
set -euo pipefail

VERSION="13.2.Rel1"
VERSION_URL_PART="13.2.rel1"   # Arm's download URLs use lowercase
INSTALL_DIR="$(pwd)/toolchain"

# --- Detect macOS architecture ---
ARCH="$(uname -m)"
case "$ARCH" in
  arm64)
    PLATFORM="darwin-arm64"
    ;;
  x86_64)
    PLATFORM="darwin-x86_64"
    ;;
  *)
    echo "Unsupported architecture: $ARCH" >&2
    exit 1
    ;;
esac

FILENAME="arm-gnu-toolchain-${VERSION_URL_PART}-${PLATFORM}-arm-none-eabi"
URL="https://developer.arm.com/-/media/Files/downloads/gnu/${VERSION_URL_PART}/binrel/${FILENAME}.tar.xz"

echo "==> Installing ARM GNU Toolchain ${VERSION} (${PLATFORM})"
echo "==> Target directory: ${INSTALL_DIR}"

if [ -d "${INSTALL_DIR}" ]; then
  echo "==> ${INSTALL_DIR} already exists. Removing it first."
  rm -rf "${INSTALL_DIR}"
fi

mkdir -p "${INSTALL_DIR}"

TMP_TAR="$(mktemp -t arm-toolchain.XXXXXX.tar.xz)"
trap 'rm -f "${TMP_TAR}"' EXIT

echo "==> Downloading from:"
echo "    ${URL}"
curl -fL --progress-bar -o "${TMP_TAR}" "${URL}"

echo "==> Extracting..."
tar -xf "${TMP_TAR}" -C "${INSTALL_DIR}" --strip-components=1

echo "==> Verifying..."
"${INSTALL_DIR}/bin/arm-none-eabi-gcc" --version | head -n 1

echo ""
echo "==> Done. Installed to: ${INSTALL_DIR}"
echo "==> Run 'source ./activate.sh' to add it to your PATH for this shell session."
