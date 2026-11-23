#!/usr/bin/env bash
# Builds an example and copies the firmware into docs/firmware/<name>/ for the web installer.
#
#   tools/build-web-firmware.sh examples/04_Apps/VoiceBoard voiceboard "TinkerPad VoiceBoard" 1.0.0
#
# Needs arduino-cli with the esp32 core and FastLED installed.
set -euo pipefail

SKETCH=${1:?sketch folder}
NAME=${2:?short name, e.g. voiceboard}
TITLE=${3:?title shown in the installer}
VERSION=${4:?version, e.g. 1.0.0}

FQBN="esp32:esp32:esp32s3:PSRAM=opi,CDCOnBoot=default,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB"
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$ROOT/docs/firmware/$NAME"
BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT

arduino-cli compile --fqbn "$FQBN" --output-dir "$BUILD" "$ROOT/$SKETCH"

BASE=$(basename "$SKETCH")
# The esp32 core lives in a different place on macOS and Linux; take the newest one found.
CORE=$( { ls -d "$HOME"/Library/Arduino15/packages/esp32/hardware/esp32/* \
             "$HOME"/.arduino15/packages/esp32/hardware/esp32/* 2>/dev/null || true; } | sort -V | tail -1)
[ -n "$CORE" ] || { echo "esp32 core not found" >&2; exit 1; }

mkdir -p "$OUT"
cp "$BUILD/$BASE.ino.bootloader.bin" "$OUT/bootloader.bin"
cp "$BUILD/$BASE.ino.partitions.bin" "$OUT/partitions.bin"
cp "$CORE/tools/partitions/boot_app0.bin" "$OUT/boot_app0.bin"
cp "$BUILD/$BASE.ino.bin" "$OUT/firmware.bin"

cat > "$OUT/manifest.json" <<JSON
{
  "name": "$TITLE",
  "version": "$VERSION",
  "new_install_prompt_erase": true,
  "builds": [
    {
      "chipFamily": "ESP32-S3",
      "parts": [
        { "path": "bootloader.bin", "offset": 0 },
        { "path": "partitions.bin", "offset": 32768 },
        { "path": "boot_app0.bin", "offset": 57344 },
        { "path": "firmware.bin", "offset": 65536 }
      ]
    }
  ]
}
JSON

echo "Wrote $OUT"
ls -la "$OUT"
