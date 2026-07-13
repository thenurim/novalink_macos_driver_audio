#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
CONFIG="${1:-Debug}"
OUT_DIR="${ROOT}/build/dist"

echo "==> Building NovaLINK Audio Device (${CONFIG})"
xcodebuild -project "${ROOT}/NovaLinkAudioDriver/NovaLINKDriver.xcodeproj" \
  -scheme "NovaLINK Audio Device" \
  -configuration "${CONFIG}" \
  -derivedDataPath "${ROOT}/build/DerivedData-Driver" \
  CODE_SIGN_IDENTITY="-" \
  CODE_SIGNING_REQUIRED=NO \
  CODE_SIGNING_ALLOWED=NO \
  ENABLE_ADDRESS_SANITIZER=NO \
  ENABLE_UNDEFINED_BEHAVIOR_SANITIZER=NO \
  build

echo "==> Building NovaLINK passthrough app (${CONFIG})"
xcodebuild -project "${ROOT}/NovaLINKApp/NovaLINKApp.xcodeproj" \
  -scheme "NovaLINK" \
  -configuration "${CONFIG}" \
  -derivedDataPath "${ROOT}/build/DerivedData-App" \
  CODE_SIGN_IDENTITY="-" \
  CODE_SIGNING_REQUIRED=NO \
  CODE_SIGNING_ALLOWED=NO \
  ENABLE_ADDRESS_SANITIZER=NO \
  ENABLE_UNDEFINED_BEHAVIOR_SANITIZER=NO \
  build

echo "==> Building NovaLINKXPCHelper (${CONFIG})"
xcodebuild -project "${ROOT}/NovaLINKApp/NovaLINKApp.xcodeproj" \
  -scheme NovaLINKXPCHelper \
  -configuration "${CONFIG}" \
  -derivedDataPath "${ROOT}/build/DerivedData-App" \
  CODE_SIGN_IDENTITY="-" \
  CODE_SIGNING_REQUIRED=NO \
  CODE_SIGNING_ALLOWED=NO \
  ENABLE_ADDRESS_SANITIZER=NO \
  ENABLE_UNDEFINED_BEHAVIOR_SANITIZER=NO \
  build

mkdir -p "${OUT_DIR}"
rm -rf "${OUT_DIR}/NovaLINK Audio Device.driver" \
       "${OUT_DIR}/NovaLINK Audio Passthrough.app" \
       "${OUT_DIR}/NovaLINKXPCHelper.xpc"

cp -R "${ROOT}/build/DerivedData-Driver/Build/Products/${CONFIG}/NovaLINK Audio Device.driver" \
  "${OUT_DIR}/"
cp -R "${ROOT}/build/DerivedData-App/Build/Products/${CONFIG}/NovaLINK Audio Passthrough.app" \
  "${OUT_DIR}/"
cp -R "${ROOT}/build/DerivedData-App/Build/Products/${CONFIG}/NovaLINKXPCHelper.xpc" \
  "${OUT_DIR}/"

# Bind Info.plist + entitlements so TCC microphone grants stick (linker-signed
# binaries leave Info.plist unbound and re-prompt on every launch / relaunch).
PASSTHROUGH_APP="${OUT_DIR}/NovaLINK Audio Passthrough.app"
ENTITLEMENTS="${ROOT}/NovaLINKApp/NovaLINKApp/NovaLINKApp.entitlements"
echo "==> Ad-hoc codesign passthrough app (stable identifier + entitlements)"
codesign --force --deep --sign - \
  --identifier "life.thenurim.novalink.App" \
  --entitlements "${ENTITLEMENTS}" \
  --options runtime \
  "${PASSTHROUGH_APP}"
codesign --verify --verbose=2 "${PASSTHROUGH_APP}" || true

echo "==> Done. Artifacts in ${OUT_DIR}"
ls -la "${OUT_DIR}"
ls -la "${PASSTHROUGH_APP}/Contents/MacOS/"
