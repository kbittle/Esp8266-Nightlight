#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "${SCRIPT_DIR}")"
IMAGE_NAME="esp8266-build-env"
TAG="${1:-latest}"
GIT_HASH="${2:-$(git -C "${ROOT_DIR}" rev-parse --short HEAD 2>/dev/null || echo "unknown")}"

echo "Building Docker image: ${IMAGE_NAME}:${TAG}"
echo "Git commit hash: ${GIT_HASH}"
docker build -t "${IMAGE_NAME}:${TAG}" \
    --build-arg GIT_COMMIT_HASH="${GIT_HASH}" \
    -f docker/Dockerfile "${ROOT_DIR}"

echo "Done. Image tagged as ${IMAGE_NAME}:${TAG}"