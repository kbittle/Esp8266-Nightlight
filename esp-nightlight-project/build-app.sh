#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<EOF
Usage: $0 [IMAGE] [MAKE_ARGS] [GIT_HASH] [--clean] [--erase-flash] [--flash PORT]
  
  IMAGE         Docker image to use (default: esp8266-build-env:latest)
  MAKE_ARGS     Make arguments (default: -j\$(nproc) CONFIG_SDK_PYTHON=python3)
  GIT_HASH      Git commit hash (default: auto-detect from host git)
  --clean       Run 'make clean' before building (erase cached artifacts)
  --erase-flash Erase flash on device before flashing
  --flash PORT  Flash after build (e.g., --flash /dev/ttyUSB0)

Examples:
  $0                                    # Build only
  $0 --clean                            # Clean build
  $0 esp8266-build-env:latest           # Build with specific image
  $0 "" "-j4"                           # Build with 4 jobs
  $0 "" "" abc1234                      # Build with specific git hash
  $0 --clean --flash /dev/ttyUSB0       # Clean, build, and flash
  $0 --erase-flash --flash /dev/ttyUSB0 # Erase device flash, build, and flash
EOF
}

IMAGE_NAME="esp8266-build-env:latest"
MAKE_ARGS="-j$(nproc) CONFIG_SDK_PYTHON=python3"
GIT_HASH=""
CLEAN=false
ERASE_FLASH=false
FLASH_PORT=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --clean)
            CLEAN=true
            shift
            ;;
        --erase-flash)
            ERASE_FLASH=true
            shift
            ;;
        --flash)
            FLASH_PORT="${2:-}"
            if [[ -z "$FLASH_PORT" ]]; then
                echo "Error: --flash requires a port argument"
                exit 1
            fi
            shift 2
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            if [[ -z "$IMAGE_NAME" || "$IMAGE_NAME" == "esp8266-build-env:latest" ]]; then
                IMAGE_NAME="$1"
            elif [[ -z "$MAKE_ARGS" || "$MAKE_ARGS" == "-j$(nproc) CONFIG_SDK_PYTHON=python3" ]]; then
                MAKE_ARGS="$1"
            elif [[ -z "$GIT_HASH" ]]; then
                GIT_HASH="$1"
            else
                echo "Error: Unknown argument: $1"
                usage
                exit 1
            fi
            shift
            ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "${SCRIPT_DIR}")"
PROJECT_DIR="${SCRIPT_DIR}"

# Get git hash from host (or fallback to container's env)
GIT_HASH="${GIT_HASH:-$(git -C "${ROOT_DIR}" rev-parse --short HEAD 2>/dev/null || echo "unknown")}"

if [[ ! -d "${PROJECT_DIR}" ]]; then
    echo "Error: Project directory not found at ${PROJECT_DIR}"
    exit 1
fi

echo "Building app in container using image: ${IMAGE_NAME}"
echo "Project directory: ${PROJECT_DIR}"
echo "Git hash: ${GIT_HASH}"
echo "Make arguments: ${MAKE_ARGS}"
[[ "$CLEAN" == true ]] && echo "Clean build: yes"
[[ "$ERASE_FLASH" == true ]] && echo "Erase flash: yes"
[[ -n "$FLASH_PORT" ]] && echo "Flash port: ${FLASH_PORT}"

if [[ "$CLEAN" == true ]]; then
    echo "Cleaning build artifacts..."
    docker run --rm \
        -e IDF_PATH=/opt/esp/ESP8266_RTOS_SDK \
        -e GIT_COMMIT_HASH="${GIT_HASH}" \
        -v "${ROOT_DIR}:/workspace" \
        -w "/workspace/esp-nightlight-project" \
        "${IMAGE_NAME}" \
        make clean
fi

docker run --rm \
    -e IDF_PATH=/opt/esp/ESP8266_RTOS_SDK \
    -e GIT_COMMIT_HASH="${GIT_HASH}" \
    -v "${ROOT_DIR}:/workspace" \
    -w "/workspace/esp-nightlight-project" \
    "${IMAGE_NAME}" \
    make ${MAKE_ARGS} GIT_HASH="${GIT_HASH}" PROJECT_VER="${GIT_HASH}"

if [[ "$ERASE_FLASH" == true ]]; then
    echo "Erasing flash on ${FLASH_PORT:-/dev/ttyUSB0}..."
    docker run --rm \
        -e IDF_PATH=/opt/esp/ESP8266_RTOS_SDK \
        -v "${ROOT_DIR}:/workspace" \
        -w "/workspace/esp-nightlight-project" \
        --device="${FLASH_PORT:-/dev/ttyUSB0}" \
        "${IMAGE_NAME}" \
        make erase_flash PORT="${FLASH_PORT:-/dev/ttyUSB0}"
fi

if [[ -n "$FLASH_PORT" ]]; then
    echo "Flashing to ${FLASH_PORT}..."
    docker run --rm \
        -e IDF_PATH=/opt/esp/ESP8266_RTOS_SDK \
        -v "${ROOT_DIR}:/workspace" \
        -w "/workspace/esp-nightlight-project" \
        --device="${FLASH_PORT}" \
        "${IMAGE_NAME}" \
        make flash PORT="${FLASH_PORT}"
fi

echo "Build complete. Output in ${PROJECT_DIR}/build/"