"""
Pre-build script for esp32c3-bridge env.
Compiles partitions_esp32.csv → binary → src_bridge/target_partitions.h
"""
import subprocess, os, sys

Import("env")

project_dir = env.subst("$PROJECT_DIR")
csv_path    = os.path.join(project_dir, "partitions_esp32.csv")
out_dir     = os.path.join(project_dir, "src", "bridge")
bin_path    = os.path.join(out_dir, "target_partitions.bin")
hdr_path    = os.path.join(out_dir, "target_partitions.h")

os.makedirs(out_dir, exist_ok=True)

# gen_esp32part.py ships with the Arduino ESP32 framework
gen_part = os.path.join(
    env.subst("$PACKAGES_DIR"),
    "framework-arduinoespressif32",
    "tools", "gen_esp32part.py"
)
if not os.path.exists(gen_part):
    print("gen_partition_header.py: gen_esp32part.py not found at", gen_part)
    exit(1)

result = subprocess.run(
    [sys.executable, gen_part, "--flash-size", "4MB", csv_path, bin_path],
    capture_output=True, text=True
)
if result.returncode != 0:
    print("gen_partition_header.py: failed to generate partition binary")
    print(result.stderr)
    exit(1)

with open(bin_path, "rb") as f:
    data = f.read()

with open(hdr_path, "w") as f:
    f.write("// Auto-generated from partitions_esp32.csv — do not edit\n")
    f.write("#pragma once\n")
    f.write("#include <stdint.h>\n\n")
    f.write(f"static const uint32_t TARGET_PT_OFFSET = 0x8000;\n")
    f.write(f"static const uint32_t TARGET_OTD_OFFSET = 0xe000;\n")
    f.write(f"static const uint32_t TARGET_OTD_SIZE   = 0x2000;\n\n")
    f.write(f"static const uint8_t target_partitions_bin[] = {{\n    ")
    for i, b in enumerate(data):
        f.write(f"0x{b:02X},")
        if (i + 1) % 16 == 0:
            f.write("\n    ")
    f.write("\n};\n")
    f.write(f"static const size_t target_partitions_bin_len = {len(data)};\n")

print(f"gen_partition_header.py: wrote {len(data)} bytes → {os.path.relpath(hdr_path, project_dir)}")
