#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
CONFIG="${1:-Release}"
OUT_DIR="${ROOT}/build/dist"
PASSTHROUGH_APP_NAME="NovaLINK Audio Passthrough.app"
PASSTHROUGH_IDENTIFIER="life.thenurim.novalink.App"
ENTITLEMENTS="${ROOT}/NovaLINKApp/NovaLINKApp/NovaLINKApp.entitlements"

if [[ "${CONFIG}" != "Debug" && "${CONFIG}" != "Release" && "${CONFIG}" != "DebugOpt" ]]; then
  echo "Usage: $0 [Debug|Release|DebugOpt]" >&2
  exit 1
fi

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
       "${OUT_DIR}/${PASSTHROUGH_APP_NAME}" \
       "${OUT_DIR}/NovaLINKXPCHelper.xpc"

cp -R "${ROOT}/build/DerivedData-Driver/Build/Products/${CONFIG}/NovaLINK Audio Device.driver" \
  "${OUT_DIR}/"
cp -R "${ROOT}/build/DerivedData-App/Build/Products/${CONFIG}/${PASSTHROUGH_APP_NAME}" \
  "${OUT_DIR}/"
cp -R "${ROOT}/build/DerivedData-App/Build/Products/${CONFIG}/NovaLINKXPCHelper.xpc" \
  "${OUT_DIR}/"

# Drop debug symbols from distributable companion (package size + cleaner install).
rm -rf "${OUT_DIR}/${PASSTHROUGH_APP_NAME}/Contents/MacOS/"*.dSYM

PASSTHROUGH_APP="${OUT_DIR}/${PASSTHROUGH_APP_NAME}"

# Bind Info.plist + entitlements so TCC microphone grants stick (linker-signed
# binaries leave Info.plist unbound and re-prompt on every launch / relaunch).
sign_passthrough_app() {
  local app="$1"
  local bin="${app}/Contents/MacOS/NovaLINK Audio Passthrough"
  if [[ ! -f "${ENTITLEMENTS}" ]]; then
    echo "error: missing entitlements: ${ENTITLEMENTS}" >&2
    exit 1
  fi
  if [[ ! -x "${bin}" ]]; then
    echo "error: missing executable: ${bin}" >&2
    exit 1
  fi
  # Sign the Mach-O first, then the bundle, so Info.plist is sealed into the CodeDirectory.
  codesign --force --sign - \
    --identifier "${PASSTHROUGH_IDENTIFIER}" \
    --entitlements "${ENTITLEMENTS}" \
    --options runtime \
    "${bin}"
  codesign --force --deep --sign - \
    --identifier "${PASSTHROUGH_IDENTIFIER}" \
    --entitlements "${ENTITLEMENTS}" \
    --options runtime \
    "${app}"
}

verify_passthrough_codesign() {
  local app="$1"
  local info
  info="$(codesign -dv --verbose=4 "${app}" 2>&1 || true)"
  if ! grep -q "Identifier=${PASSTHROUGH_IDENTIFIER}" <<<"${info}"; then
    echo "error: expected Identifier=${PASSTHROUGH_IDENTIFIER}" >&2
    echo "${info}" >&2
    exit 1
  fi
  if grep -q 'Info.plist=not bound' <<<"${info}"; then
    echo "error: Info.plist not bound — TCC mic grants will not stick" >&2
    echo "${info}" >&2
    exit 1
  fi
  if grep -q 'linker-signed' <<<"${info}"; then
    echo "error: still linker-signed after codesign" >&2
    echo "${info}" >&2
    exit 1
  fi
  if ! grep -q 'Info.plist entries=' <<<"${info}"; then
    echo "error: Info.plist binding missing from codesign output" >&2
    echo "${info}" >&2
    exit 1
  fi
  codesign --verify --verbose=2 "${app}"
}

echo "==> Ad-hoc codesign passthrough app (stable identifier + entitlements)"
sign_passthrough_app "${PASSTHROUGH_APP}"
verify_passthrough_codesign "${PASSTHROUGH_APP}"

echo "==> Done. Artifacts in ${OUT_DIR}"
ls -la "${OUT_DIR}"
ls -la "${PASSTHROUGH_APP}/Contents/MacOS/"
codesign -dv --verbose=2 "${PASSTHROUGH_APP}" 2>&1 | egrep 'Identifier|Info.plist|flags|Signature|Sealed' || true
