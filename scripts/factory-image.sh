#!/usr/bin/env bash
# Build a combined factory flash image for SensorNode ESP32-C3.
# Merges bootloader + partition table + firmware + LittleFS into a single
# binary that can be written to a blank board at offset 0x0.
#
# Usage:
#   ./scripts/factory-image.sh [ENV]
#
#   ENV   PlatformIO environment (default: esp32c3)
#
# Output:
#   factory/factory-<ENV>-<BUILD>.bin
#
# Flash with:
#   esptool.py --chip esp32c3 --port /dev/ttyUSB0 write_flash 0x0 factory/factory-esp32c3-<BUILD>.bin
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# Prefer PlatformIO-bundled esptool; fall back to system PATH
_ESPTOOL_PY="${HOME}/.platformio/packages/tool-esptoolpy/esptool.py"
if [ -f "$_ESPTOOL_PY" ]; then
    ESPTOOL=(python3 "$_ESPTOOL_PY")
else
    ESPTOOL=(esptool.py)
fi
cd "$PROJECT_DIR"

ENV=${1:-esp32c3}
BUILD_DIR=".pio/build/$ENV"
OUT_DIR="factory"

# ── Validate environment ──────────────────────────────────────────────────────
case "$ENV" in
    esp32c3)
        CHIP=esp32c3
        PARTITIONS_CSV=partitions_esp32.csv
        ;;
    *)
        echo "ERROR: Unknown environment '$ENV'"; exit 1 ;;
esac

# ── Build firmware ────────────────────────────────────────────────────────────
echo "=== Building firmware: $ENV ==="
pio run -e "$ENV"

# ── Build filesystem ──────────────────────────────────────────────────────────
echo "=== Building LittleFS: $ENV ==="
pio run -t buildfs -e "$ENV"

# ── Locate binaries ───────────────────────────────────────────────────────────
BOOTLOADER="$BUILD_DIR/bootloader.bin"
PARTITIONS="$BUILD_DIR/partitions.bin"
FIRMWARE="$BUILD_DIR/firmware.bin"
LITTLEFS="$BUILD_DIR/littlefs.bin"

for f in "$BOOTLOADER" "$PARTITIONS" "$FIRMWARE" "$LITTLEFS"; do
    [ -f "$f" ] || { echo "ERROR: missing $f"; exit 1; }
done

# ── Read partition offsets from partitions CSV ────────────────────────────────
APP_OFFSET=$(awk -F',' '/ota_0/ {gsub(/ /,"",$4); print $4}' "$PARTITIONS_CSV")
FS_OFFSET=$(awk -F',' '/storage/ {gsub(/ /,"",$4); print $4}' "$PARTITIONS_CSV")

echo "  bootloader  @ 0x0"
echo "  partitions  @ 0x8000"
echo "  firmware    @ $APP_OFFSET"
echo "  littlefs    @ $FS_OFFSET"

# ── Extract build tag from .build_counter (same logic as version.py) ─────────
TODAY=$(date +%Y%m%d)
if [ -f ".build_counter" ]; then
    COUNTER_DATE=$(cut -d: -f1 .build_counter)
    COUNTER_N=$(cut -d: -f2 .build_counter)
    if [ "$COUNTER_DATE" = "$TODAY" ]; then
        BUILD="${TODAY}$(printf '%02d' "$COUNTER_N")"
    else
        BUILD="${TODAY}00"
    fi
else
    BUILD="${TODAY}00"
fi

# ── Merge ─────────────────────────────────────────────────────────────────────
mkdir -p "$OUT_DIR"
OUTPUT="$OUT_DIR/factory-${ENV}-${BUILD}.bin"

echo "=== Merging into $OUTPUT ==="
"${ESPTOOL[@]}" --chip "$CHIP" merge_bin \
    --flash_mode dio \
    --flash_freq 80m \
    --flash_size 4MB \
    --output "$OUTPUT" \
    0x0      "$BOOTLOADER" \
    0x8000   "$PARTITIONS" \
    "$APP_OFFSET" "$FIRMWARE" \
    "$FS_OFFSET"  "$LITTLEFS"

SIZE=$(du -h "$OUTPUT" | cut -f1)
echo ""
echo "=== Factory image ready ==="
echo "  File:  $OUTPUT  ($SIZE)"
echo ""
echo "Flash command:"
echo "  esptool.py --chip $CHIP --port /dev/ttyUSB0 --baud 921600 \\"
echo "    write_flash 0x0 $OUTPUT"
