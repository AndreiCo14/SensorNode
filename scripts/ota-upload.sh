#!/usr/bin/env bash
# Build firmware and upload to OTA server, then optionally trigger OTA on device.
#
# Usage:
#   ./scripts/ota-upload.sh [ENV] [CHIP_ID]
#
#   ENV      PlatformIO environment (default: esp8266)
#   CHIP_ID  Decimal chip ID — if provided, publishes MQTT OTA trigger automatically
#
# Examples:
#   ./scripts/ota-upload.sh                        # build + upload only
#   ./scripts/ota-upload.sh esp32c3                # build esp32c3 variant + upload
#   ./scripts/ota-upload.sh esp8266 1234567890     # build + upload + trigger device
#
# ─── Load deployment env ──────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -f "$SCRIPT_DIR/../.env" ]; then
    source "$SCRIPT_DIR/../.env"
fi

# ─── Server config (values from .env; fallbacks used if .env is absent) ───────
OTA_SSH_USER="${OTA_SSH_USER:-antonr}"
OTA_SSH_ADDR="${OTA_SSH_ADDR:-192.168.110.112}"
OTA_SSH_PORT="${OTA_SSH_PORT:-2222}"
OTA_SERVER_PATH="${OTA_SERVER_PATH:-/volumes/ota/files/OTA/dev/SensorNode}"
OTA_BASE_URL="${OTA_BASE_URL:-http://ota.tartak.by/files/OTA/dev/SensorNode}"

# ─── MQTT config (used only if CHIP_ID is provided) ──────────────────────────
MQTT_HOST="${MQTT_HOST:-mq.airmq.cc}"
MQTT_PORT="${MQTT_PORT:-18883}"
# ─────────────────────────────────────────────────────────────────────────────

set -euo pipefail
set -x

ENV=${1:-esp8266}
CHIP_ID=${2:-""}

# ── Build ─────────────────────────────────────────────────────────────────────
echo "=== Building env: $ENV ==="
pio run -e "$ENV"

BIN=".pio/build/$ENV/firmware.bin"
if [ ! -f "$BIN" ]; then
    echo "ERROR: binary not found at $BIN"
    exit 1
fi

# ── Extract build number from platformio.ini ──────────────────────────────────
BUILD=$(python3 -c "
import sys, re
env = sys.argv[1]
with open('platformio.ini') as f: txt = f.read()
sec = re.search(r'\[env:' + re.escape(env) + r'\](.+?)(?=^\[|\Z)', txt, re.M|re.S)
if sec:
    m = re.search(r'FW_BUILD=\"([^\"]+)\"', sec.group(1))
    if m: print(m.group(1)); sys.exit(0)
sys.exit(1)
" "$ENV")
if [ -z "$BUILD" ]; then
    echo "ERROR: could not extract FW_BUILD for env '$ENV' from platformio.ini"
    exit 1
fi

REMOTE_BIN="firmware-${ENV}-${BUILD}.bin"
LATEST_BIN="firmware-${ENV}-latest.bin"
OTA_URL="${OTA_BASE_URL}/${LATEST_BIN}"

# Shorthand helpers for ssh/scp with the configured port
SSH="ssh -p ${OTA_SSH_PORT} ${OTA_SSH_USER}@${OTA_SSH_ADDR}"
SCP="scp -v -P ${OTA_SSH_PORT}"

# ── Upload ────────────────────────────────────────────────────────────────────
echo "=== Uploading $BIN → ${OTA_SSH_ADDR}:${OTA_SERVER_PATH}/${REMOTE_BIN} ==="
$SCP "$BIN" "${OTA_SSH_USER}@${OTA_SSH_ADDR}:${OTA_SERVER_PATH}/${REMOTE_BIN}"

echo "=== Symlinking to $LATEST_BIN ==="
$SSH "cd '${OTA_SERVER_PATH}' && ln -sf '${REMOTE_BIN}' '${LATEST_BIN}'"

# ── Determine per-environment version JSON filename ───────────────────────────
case "$ENV" in
    esp8266)  VERSION_FILE="version-esp8266.json" ;;
    esp32c3)  VERSION_FILE="version-esp32c3.json" ;;
    *)        VERSION_FILE="version-${ENV}.json" ;;
esac

# ── Update version JSON ───────────────────────────────────────────────────────
VERSION_JSON=$(printf '{"build":"%s","env":"%s","url":"%s"}' "$BUILD" "$ENV" "$OTA_URL")
echo "=== Writing version JSON ($VERSION_FILE): $VERSION_JSON ==="
echo "$VERSION_JSON" | $SSH "cat > '${OTA_SERVER_PATH}/${VERSION_FILE}'"

# ── Summary ───────────────────────────────────────────────────────────────────
echo ""
echo "=== Upload complete ==="
echo "  Build:   $BUILD"
echo "  Env:     $ENV"
echo "  OTA URL: $OTA_URL"
echo ""

# ── MQTT trigger (optional) ───────────────────────────────────────────────────
if [ -n "$CHIP_ID" ]; then
    TOPIC="Sensors/${CHIP_ID}/cmd"
    PAYLOAD="{\"ota\":\"${OTA_URL}\"}"

    if command -v mosquitto_pub &>/dev/null; then
        echo "=== Triggering OTA on device $CHIP_ID ==="
        mosquitto_pub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "$TOPIC" -m "$PAYLOAD"
        echo "OTA trigger sent."
    else
        echo "mosquitto_pub not found — trigger manually:"
        echo "  mosquitto_pub -h $MQTT_HOST -p $MQTT_PORT \\"
        echo "    -t '$TOPIC' -m '$PAYLOAD'"
    fi
else
    TOPIC="Sensors/<chipId>/cmd"
    PAYLOAD="{\"ota\":\"${OTA_URL}\"}"
    echo "To trigger OTA, run:"
    echo "  mosquitto_pub -h $MQTT_HOST -p $MQTT_PORT \\"
    echo "    -t '$TOPIC' -m '$PAYLOAD'"
    echo ""
    echo "Or pass CHIP_ID as second argument:"
    echo "  $0 $ENV <chipId>"
fi
