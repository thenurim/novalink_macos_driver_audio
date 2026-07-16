#!/usr/bin/env bash
# vim: tw=0:
#
# install_driver.sh
# Installs (or uninstalls) the NovaLINK HAL audio driver and restarts coreaudiod.
#
# Usage:
#   ./install_driver.sh              # Release build (default)
#   ./install_driver.sh -d           # Debug build
#   ./install_driver.sh -p /path/to/NovaLINK\ Audio\ Device.driver
#   ./install_driver.sh -u           # Uninstall only
#   ./install_driver.sh --no-restart # Skip coreaudiod restart
#

set -euo pipefail
IFS=$'\n\t'

ROOT="$(cd "$(dirname "$0")" && pwd)"
DRIVER_NAME="NovaLINK Audio Device.driver"
HAL_PLUGINS_DIR="/Library/Audio/Plug-Ins/HAL"
INSTALLED_DRIVER_PATH="${HAL_PLUGINS_DIR}/${DRIVER_NAME}"

CONFIG="Release"
DRIVER_PATH=""
UNINSTALL_ONLY=false
RESTART_COREAUDIOD=true

bold() {
  if [[ -t 1 ]] && command -v tput >/dev/null 2>&1; then
    printf '%s%s%s' "$(tput bold)" "$*" "$(tput sgr0)"
  else
    printf '%s' "$*"
  fi
}

usage() {
  cat <<EOF
Usage: $(basename "$0") [options]

Installs ${DRIVER_NAME} into ${HAL_PLUGINS_DIR} and restarts coreaudiod.

Options:
  -d, --debug         Install Debug build (default: Release)
  -r, --release       Install Release build (default)
  -p, --path PATH     Install from an explicit .driver bundle path
  -u, --uninstall     Remove the installed driver only
      --no-restart    Do not restart coreaudiod after install/uninstall
  -h, --help          Show this help

Search order when -p is not given:
  1) ${ROOT}/build/dist/${DRIVER_NAME}
  2) ${ROOT}/build/DerivedData-Driver/Build/Products/<Config>/${DRIVER_NAME}
  3) Newest match under ${ROOT}/build/DerivedData-*/Build/Products/<Config>/
EOF
  exit "${1:-0}"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -d|--debug)
      CONFIG="Debug"
      shift
      ;;
    -r|--release)
      CONFIG="Release"
      shift
      ;;
    -p|--path)
      DRIVER_PATH="${2:-}"
      if [[ -z "${DRIVER_PATH}" ]]; then
        echo "error: -p requires a path" >&2
        usage 1
      fi
      shift 2
      ;;
    -u|--uninstall)
      UNINSTALL_ONLY=true
      shift
      ;;
    --no-restart)
      RESTART_COREAUDIOD=false
      shift
      ;;
    -h|--help)
      usage 0
      ;;
    *)
      echo "error: unknown option: $1" >&2
      usage 1
      ;;
  esac
done

resolve_driver_path() {
  if [[ -n "${DRIVER_PATH}" ]]; then
    return
  fi

  local candidates=(
    "${ROOT}/build/dist/${DRIVER_NAME}"
    "${ROOT}/build/DerivedData-Driver/Build/Products/${CONFIG}/${DRIVER_NAME}"
  )

  local found=""
  # Prefer newest DerivedData-* product matching the requested configuration.
  if [[ -d "${ROOT}/build" ]]; then
    while IFS= read -r -d '' path; do
      found="${path}"
      break
    done < <(find "${ROOT}/build" -type d -path "*/Build/Products/${CONFIG}/${DRIVER_NAME}" -print0 2>/dev/null \
              | xargs -0 ls -td 2>/dev/null || true)
  fi

  if [[ -n "${found}" ]]; then
    candidates+=("${found}")
  fi

  local candidate
  for candidate in "${candidates[@]}"; do
    if [[ -d "${candidate}/Contents/MacOS" ]]; then
      DRIVER_PATH="${candidate}"
      return
    fi
  done

  echo "error: could not find ${DRIVER_NAME} (${CONFIG})." >&2
  echo "Build first with ./build_all.sh ${CONFIG}, or pass -p /path/to/${DRIVER_NAME}" >&2
  exit 1
}

remove_installed_driver() {
  if [[ ! -d "${INSTALLED_DRIVER_PATH}" ]]; then
    echo "No installed driver at ${INSTALLED_DRIVER_PATH}"
    return
  fi

  local size_kb
  size_kb="$(du -sk "${INSTALLED_DRIVER_PATH}" | cut -f1)"
  if [[ ! "${size_kb}" =~ ^[0-9]+$ ]] || [[ "${size_kb}" -gt 2048 ]]; then
    echo "error: refusing to remove ${INSTALLED_DRIVER_PATH} (unexpected size: ${size_kb} KB)" >&2
    exit 1
  fi

  echo "$(bold "Removing") ${INSTALLED_DRIVER_PATH}"
  sudo rm -rf "${INSTALLED_DRIVER_PATH}"
}

install_driver() {
  resolve_driver_path

  if [[ ! -d "${DRIVER_PATH}/Contents/MacOS" ]]; then
    echo "error: not a valid driver bundle: ${DRIVER_PATH}" >&2
    exit 1
  fi

  echo "$(bold "Installing") ${CONFIG} driver"
  echo "  from: ${DRIVER_PATH}"
  echo "  to:   ${INSTALLED_DRIVER_PATH}"

  remove_installed_driver
  sudo mkdir -p "${HAL_PLUGINS_DIR}"
  sudo cp -R "${DRIVER_PATH}" "${HAL_PLUGINS_DIR}/"
  sudo chown -R root:wheel "${INSTALLED_DRIVER_PATH}"
  sudo chmod -R 755 "${INSTALLED_DRIVER_PATH}"

  echo "$(bold "Installed.")"
}

restart_coreaudiod() {
  echo "$(bold "Restarting coreaudiod")"
  # Prefer launchctl; fall back to killall.
  if ! sudo launchctl kill SIGTERM system/com.apple.audio.coreaudiod 2>/dev/null; then
    sudo killall coreaudiod 2>/dev/null || true
  fi
  # Give HAL plugins a moment to reload.
  sleep 1
  echo "If a running app loses audio, toggle the default output device or relaunch the app."
}

# ---------------------------------------------------------------------------

if [[ "${EUID}" -eq 0 ]]; then
  echo "error: run as a normal user; the script will prompt for sudo when needed." >&2
  exit 1
fi

sudo -v

if [[ "${UNINSTALL_ONLY}" == true ]]; then
  remove_installed_driver
else
  install_driver
fi

if [[ "${RESTART_COREAUDIOD}" == true ]]; then
  restart_coreaudiod
fi

echo "Done."
