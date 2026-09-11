# activate.fish — Adds ./toolchain/bin to PATH for the current fish shell session.
#
# IMPORTANT: must be sourced, not executed:
#   source ./activate.fish

set -l _activate_dir (cd (dirname (status --current-filename)); and pwd)
set -l _toolchain_bin "$_activate_dir/toolchain/bin"

if not test -d "$_toolchain_bin"
    echo "Error: $_toolchain_bin not found. Run ./install.sh first." >&2
    exit 1
end

# Stash the old PATH so deactivate can restore it
set -gx _PRE_ARM_TOOLCHAIN_PATH $PATH

set -gx PATH "$_toolchain_bin" $PATH

echo "==> ARM GNU Toolchain activated:"
arm-none-eabi-gcc --version | head -n 1
echo "==> Run 'deactivate_arm_toolchain' to restore your previous PATH."

function deactivate_arm_toolchain --on-event deactivate_arm_toolchain
    if set -q _PRE_ARM_TOOLCHAIN_PATH
        set -gx PATH $_PRE_ARM_TOOLCHAIN_PATH
        set -e _PRE_ARM_TOOLCHAIN_PATH
        echo "==> Restored previous PATH."
    end
    functions -e deactivate_arm_toolchain
end

set -e _activate_dir
set -e _toolchain_bin
