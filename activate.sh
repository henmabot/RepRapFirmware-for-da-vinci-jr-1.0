# activate.sh — Adds ./toolchain/bin to PATH for the current shell session.
#
# IMPORTANT: must be sourced, not executed:
#   source ./activate.sh
#   . ./activate.sh
#
# Running it directly (./activate.sh) will NOT work — the PATH change
# would only apply to a subshell and be lost immediately.

# Detect whether this file is being sourced or executed
(return 0 2>/dev/null) || {
  echo "Error: activate.sh must be sourced, not executed." >&2
  echo "Run:   source ./activate.sh" >&2
  exit 1
}

# Resolve the directory this script lives in, so it works regardless of cwd
_ACTIVATE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-${(%):-%x}}")" && pwd)"
_TOOLCHAIN_BIN="${_ACTIVATE_DIR}/toolchain/bin"

if [ ! -d "${_TOOLCHAIN_BIN}" ]; then
  echo "Error: ${_TOOLCHAIN_BIN} not found. Run ./install.sh first." >&2
  return 1
fi

# Stash the old PATH so deactivate can restore it
export _PRE_ARM_TOOLCHAIN_PATH="${PATH}"

export PATH="${_TOOLCHAIN_BIN}:${PATH}"

echo "==> ARM GNU Toolchain activated:"
arm-none-eabi-gcc --version | head -n 1
echo "==> Run 'deactivate_arm_toolchain' to restore your previous PATH."

deactivate_arm_toolchain() {
  if [ -n "${_PRE_ARM_TOOLCHAIN_PATH:-}" ]; then
    export PATH="${_PRE_ARM_TOOLCHAIN_PATH}"
    unset _PRE_ARM_TOOLCHAIN_PATH
    echo "==> Restored previous PATH."
  fi
  unset -f deactivate_arm_toolchain
}

unset _ACTIVATE_DIR _TOOLCHAIN_BIN
