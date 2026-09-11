#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_PATH="${SCRIPT_DIR}/Cocoons.xcodeproj"
DERIVED_DATA="${SCRIPT_DIR}/.derivedData"

if ! command -v xcodegen >/dev/null 2>&1; then
  echo "缺少 XcodeGen。请先执行: brew install xcodegen" >&2
  exit 1
fi

xcodegen generate --spec "${SCRIPT_DIR}/project.yml" --project "${SCRIPT_DIR}"

xcodebuild \
  -project "${PROJECT_PATH}" \
  -scheme Cocoons \
  -configuration Release \
  -destination "platform=macOS" \
  -derivedDataPath "${DERIVED_DATA}" \
  build CODE_SIGNING_ALLOWED=NO

echo
echo "Cocoons.app:"
echo "${DERIVED_DATA}/Build/Products/Release/Cocoons.app"
