from pathlib import Path

from SCons.Script import Import

Import("env")

# Ensure the RISC-V toolchain directory is explicitly on PATH.
package_dir = env.PioPlatform().get_package_dir("toolchain-riscv32-esp")
if package_dir:
    toolchain_bin = Path(package_dir) / "bin"
    if toolchain_bin.exists():
        env.PrependENVPath("PATH", str(toolchain_bin))
        print(f"Added RISC-V toolchain to PATH: {toolchain_bin}")
    else:
        print(f"Warning: RISC-V toolchain bin directory missing: {toolchain_bin}")
else:
    print("Warning: toolchain-riscv32-esp package not found; install with `pio pkg install -g toolchain-riscv32-esp`.")
