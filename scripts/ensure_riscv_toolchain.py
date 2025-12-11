from pathlib import Path

from SCons.Script import Import

Import("env")

# Ensure the RISC-V toolchain directory is explicitly on PATH.
package_dir = env.PioPlatform().get_package_dir("toolchain-riscv32-esp")

# PlatformIO 6.1+ for ESP32-C3 ships the toolchain in a nested folder
# (<package>/riscv32-esp-elf/bin). Probe both the legacy and nested layout.
if package_dir:
    candidate_bins = [
        Path(package_dir) / "bin",
        Path(package_dir) / "riscv32-esp-elf" / "bin",
    ]
    found = False
    for toolchain_bin in candidate_bins:
        if toolchain_bin.exists():
            env.PrependENVPath("PATH", str(toolchain_bin))
            print(f"Added RISC-V toolchain to PATH: {toolchain_bin}")
            found = True
            break
    if not found:
        print("Warning: RISC-V toolchain bin directory missing in expected locations:")
        for toolchain_bin in candidate_bins:
            print(f" - {toolchain_bin}")
else:
    print("Warning: toolchain-riscv32-esp package not found; install with `pio pkg install -g toolchain-riscv32-esp`.")
