#!/usr/bin/env bash
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BASE="$(dirname "$ROOT")/.tools"
if [ -z "${IDF_PATH:-}" ]; then
 export IDF_PATH="$BASE/platformio/packages/framework-espidf"
 export IDF_PYTHON_ENV_PATH="$BASE/esp32-test"
 export IDF_PYTHON_CHECK_CONSTRAINTS=no
 export PATH="$BASE/platformio/packages/toolchain-xtensa-esp-elf/bin:$BASE/platformio/packages/tool-cmake/bin:$BASE/platformio/packages/tool-ninja:$PATH"
fi
export IDF_COMPONENT_MANAGER=0
cd "$ROOT/receiver"
exec "${IDF_PYTHON_ENV_PATH:+$IDF_PYTHON_ENV_PATH/bin/}python" "$IDF_PATH/tools/idf.py" "$@"
