#!/usr/bin/env bash
# Build firmware and upload to OTA server, then optionally trigger OTA on device.
#
# Usage:
#   ./scripts/ota-upload.sh [ENV] [CHIP_ID]
#
#   ENV      PlatformIO environment (default: esp8266)
#            Append -release suffix for the release channel: esp8266-release
#   CHIP_ID  Decimal chip ID — if provided, publishes MQTT OTA trigger automatically
#
# Examples:
#   ./scripts/ota-upload.sh                            # build + upload to dev
#   ./scripts/ota-upload.sh esp32c3                    # esp32c3 dev channel
#   ./scripts/ota-upload.sh esp8266-release            # esp8266 release channel
#   ./scripts/ota-upload.sh esp8266-release 1234567890 # release + trigger device
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
OTA_SERVER_BASE="${OTA_SERVER_BASE:-/volumes/ota/files/OTA/SensorNode}"
OTA_URL_BASE="${OTA_URL_BASE:-https://ota.tartak.by/files/OTA/SensorNode}"

# ─── MQTT config (used only if CHIP_ID is provided) ──────────────────────────
MQTT_HOST="${MQTT_HOST:-mq.airmq.cc}"
MQTT_PORT="${MQTT_PORT:-18883}"
# ─────────────────────────────────────────────────────────────────────────────

set -euo pipefail
set -x

ENV=${1:-esp8266}
CHIP_ID=${2:-""}

# ── Derive channel and base env name from ENV ─────────────────────────────────
if [[ "$ENV" == *"-release" ]]; then
    CHANNEL="release"
    BASE_ENV="${ENV%-release}"
else
    CHANNEL="dev"
    BASE_ENV="$ENV"
fi

OTA_SERVER_PATH="${OTA_SERVER_BASE}/${CHANNEL}"
OTA_BASE_URL="${OTA_URL_BASE}/${CHANNEL}"

# ── Build ─────────────────────────────────────────────────────────────────────
echo "=== Building env: $ENV (channel: $CHANNEL) ==="
OTA_RELEASE=1 pio run -e "$ENV"

BIN=".pio/build/$ENV/firmware.bin"
if [ ! -f "$BIN" ]; then
    echo "ERROR: binary not found at $BIN"
    exit 1
fi

# ── Extract build number from .build_counter (written by version.py) ─────────
if [ ! -f ".build_counter" ]; then
    echo "ERROR: .build_counter not found — did 'pio run' succeed?"
    exit 1
fi
_date=$(cut -d: -f1 .build_counter)
_num=$(cut -d: -f2 .build_counter)
BUILD="${_date}$(printf '%02d' "$_num")"
if [ -z "$BUILD" ]; then
    echo "ERROR: could not parse .build_counter"
    exit 1
fi

REMOTE_BIN="firmware-${BASE_ENV}-${BUILD}.bin"
LATEST_BIN="firmware-${BASE_ENV}-latest.bin"
# Binary download URL always uses HTTP — ESP8266 can't handle HTTPS for large downloads
BIN_BASE_URL="${OTA_BASE_URL/https:\/\//http:\/\/}"
OTA_URL="${BIN_BASE_URL}/${LATEST_BIN}"

# Shorthand helpers for ssh/scp with the configured port
SSH="ssh -p ${OTA_SSH_PORT} ${OTA_SSH_USER}@${OTA_SSH_ADDR}"
SCP="scp -v -P ${OTA_SSH_PORT}"

# ── Upload ────────────────────────────────────────────────────────────────────
echo "=== Uploading $BIN → ${OTA_SSH_ADDR}:${OTA_SERVER_PATH}/${REMOTE_BIN} ==="
$SCP "$BIN" "${OTA_SSH_USER}@${OTA_SSH_ADDR}:${OTA_SERVER_PATH}/${REMOTE_BIN}"

echo "=== Symlinking to $LATEST_BIN ==="
$SSH "cd '${OTA_SERVER_PATH}' && ln -sf '${REMOTE_BIN}' '${LATEST_BIN}'"

# ── Determine per-environment version JSON filename ───────────────────────────
case "$BASE_ENV" in
    esp8266)  VERSION_FILE="version-esp8266.json" ;;
    esp32c3)  VERSION_FILE="version-esp32c3.json" ;;
    *)        VERSION_FILE="version-${BASE_ENV}.json" ;;
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
