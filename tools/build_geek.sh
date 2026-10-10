#!/usr/bin/env bash
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# Separate cache and sdkconfig: never let the GEEK's Quad PSRAM settings
# replace C480's Octal settings or accidentally reuse its display binary.
exec bash "$ROOT/tools/build_receiver.sh" -B "$ROOT/receiver/build-geek" \
  -D "SDKCONFIG=$ROOT/receiver/sdkconfig.geek" \
  -D "SDKCONFIG_DEFAULTS=$ROOT/receiver/sdkconfig.geek.defaults" \
  -D VIBE_RECEIVER_BOARD=geek "$@"
