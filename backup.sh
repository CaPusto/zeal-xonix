#!/usr/bin/env bash
#
# Create a timestamped backup of the project in ~/backups.
# Build artifacts are excluded; sources, assets and configs are included.
#
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_NAME="$(basename "$PROJECT_DIR")"
BACKUP_DIR="${HOME}/backups"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
ARCHIVE="${BACKUP_DIR}/${PROJECT_NAME}-${TIMESTAMP}.tar.gz"

if [[ ! -f "${PROJECT_DIR}/CMakeLists.txt" ]]; then
    echo "error: ${PROJECT_DIR} does not look like the project root" >&2
    exit 1
fi

mkdir -p "${BACKUP_DIR}"

tar \
    --exclude='./build' \
    --exclude='./tmp' \
    --exclude='./.git' \
    -czf "${ARCHIVE}" \
    -C "${PROJECT_DIR}" .

echo "Backup created: ${ARCHIVE}"
echo "Size: $(du -h "${ARCHIVE}" | cut -f1)"
