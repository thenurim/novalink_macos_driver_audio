#!/usr/bin/env bash
# vim: tw=0:
#
# install_driver.sh
# Installs the NovaLINK HAL driver, XPCHelper, passthrough app, and a background
# LaunchAgent that hosts playthrough without requiring the user to open the companion UI.
#
# Usage:
#   ./build_all.sh Release
#   ./install_driver.sh
#

set -euo pipefail
IFS=$'\n\t'

ROOT="$(cd "$(dirname "$0")" && pwd)"
DRIVER_NAME="NovaLINK Audio Device.driver"
HELPER_NAME="NovaLINKXPCHelper.xpc"
APP_NAME="NovaLINK Audio Passthrough.app"
HAL_PLUGINS_DIR="/Library/Audio/Plug-Ins/HAL"
INSTALLED_DRIVER_PATH="${HAL_PLUGINS_DIR}/${DRIVER_NAME}"
DEFAULT_HELPER_INSTALL_DIR="/Library/Application Support/NovaLINK"
LAUNCHD_HELPER_PLIST="/Library/LaunchDaemons/life.thenurim.novalink.XPCHelper.plist"
HELPER_LABEL="life.thenurim.novalink.XPCHelper"
AGENT_LABEL="life.thenurim.novalink.PassthroughAgent"
AGENT_PLIST_NAME="${AGENT_LABEL}.plist"
AGENT_TEMPLATE="${ROOT}/NovaLINKApp/NovaLINKXPCHelper/${AGENT_PLIST_NAME}.template"
DEFAULT_APP_INSTALL="/Applications/${APP_NAME}"

CONFIG="Release"
UNINSTALL_ONLY=false
RESTART_SERVICES=true

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

Installs driver + XPCHelper + passthrough app + background LaunchAgent.
Artifacts are taken only from ${ROOT}/build/dist/

Options:
  -d, --debug         Install Debug build from build/dist
  -r, --release       Install Release build (default)
  -u, --uninstall     Remove driver + LaunchAgent
      --no-restart    Do not restart services after install
  -h, --help
EOF
  exit "${1:-0}"
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -d|--debug) CONFIG="Debug"; shift ;;
    -r|--release) CONFIG="Release"; shift ;;
    -u|--uninstall) UNINSTALL_ONLY=true; shift ;;
    --no-restart) RESTART_SERVICES=false; shift ;;
    -h|--help) usage 0 ;;
    *) echo "error: unknown option: $1" >&2; usage 1 ;;
  esac
done

require_dist_bundle() {
  local name="$1"
  local path="${ROOT}/build/dist/${name}"
  if [[ ! -d "${path}/Contents/MacOS" ]]; then
    echo "error: missing ${path}" >&2
    echo "Build first: ./build_all.sh ${CONFIG}" >&2
    exit 1
  fi
  printf '%s' "${path}"
}

sha256_file() {
  shasum -a 256 "$1" | awk '{print $1}'
}

require_same_sha() {
  local src="$1"
  local dst="$2"
  local s d
  s="$(sha256_file "${src}")"
  d="$(sha256_file "${dst}")"
  if [[ "${s}" != "${d}" ]]; then
    echo "error: install mismatch" >&2
    echo "  src: ${src}" >&2
    echo "       sha=${s}" >&2
    echo "  dst: ${dst}" >&2
    echo "       sha=${d}" >&2
    exit 1
  fi
}

verify_driver_has_fallback() {
  local bin="$1"
  local syms
  syms="$(nm -gU "${bin}" 2>/dev/null || true)"
  if ! grep -q 'StartFallbackPlayThroughSync' <<<"${syms}"; then
    echo "error: ${bin} is missing StartFallbackPlayThroughSync" >&2
    exit 1
  fi
}

verify_helper_has_fallback() {
  local bin="$1"
  local hay
  hay="$(strings "${bin}" 2>/dev/null || true)"
  if ! grep -q 'NovaLINKFallbackPlayThrough' <<<"${hay}"; then
    echo "error: ${bin} is missing NovaLINKFallbackPlayThrough" >&2
    exit 1
  fi
}

install_bundle_ditto() {
  local src="$1"
  local dst="$2"
  sudo rm -rf "${dst}"
  sudo mkdir -p "$(dirname "${dst}")"
  # ditto preserves bundle layout more reliably than cp -R.
  sudo ditto "${src}" "${dst}"
  sudo chown -R root:wheel "${dst}"
  sudo chmod -R 755 "${dst}"
}

resolve_helper_install_dirs() {
  # Always update Application Support. Also refresh /usr/local/libexec if present.
  local dirs=("${DEFAULT_HELPER_INSTALL_DIR}")
  if [[ -d "/usr/local/libexec/${HELPER_NAME}" ]]; then
    dirs+=("/usr/local/libexec")
  fi
  # Prefer launchd path first so verify matches the running binary.
  if [[ -f "${LAUNCHD_HELPER_PLIST}" ]]; then
    local program
    program="$(/usr/libexec/PlistBuddy -c 'Print :ProgramArguments:0' "${LAUNCHD_HELPER_PLIST}" 2>/dev/null || true)"
    if [[ -n "${program}" && "${program}" == *"/NovaLINKXPCHelper.xpc/"* ]]; then
      local xpc_dir="${program%%/Contents/MacOS/*}"
      dirs=("$(dirname "${xpc_dir}")" "${dirs[@]}")
    fi
  fi
  # Unique, preserve order
  printf '%s\n' "${dirs[@]}" | awk 'NF && !seen[$0]++'
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

unload_agent_for_current_user() {
  local uid
  uid="$(id -u)"
  launchctl bootout "gui/${uid}/${AGENT_LABEL}" 2>/dev/null || true
  rm -f "${HOME}/Library/LaunchAgents/${AGENT_PLIST_NAME}"
  if [[ -f "/Library/LaunchAgents/${AGENT_PLIST_NAME}" ]]; then
    sudo rm -f "/Library/LaunchAgents/${AGENT_PLIST_NAME}"
  fi
}

install_driver() {
  local src driver_bin
  src="$(require_dist_bundle "${DRIVER_NAME}")"
  driver_bin="${src}/Contents/MacOS/NovaLINK Audio Device"
  verify_driver_has_fallback "${driver_bin}"

  echo "$(bold "Installing") ${CONFIG} driver"
  echo "  from: ${src}"
  echo "  to:   ${INSTALLED_DRIVER_PATH}"
  remove_installed_driver
  install_bundle_ditto "${src}" "${INSTALLED_DRIVER_PATH}"

  local installed_bin="${INSTALLED_DRIVER_PATH}/Contents/MacOS/NovaLINK Audio Device"
  verify_driver_has_fallback "${installed_bin}"
  require_same_sha "${driver_bin}" "${installed_bin}"
  echo "$(bold "Installed driver.") sha=$(sha256_file "${installed_bin}")"
}

install_helper() {
  local src helper_bin
  src="$(require_dist_bundle "${HELPER_NAME}")"
  helper_bin="${src}/Contents/MacOS/NovaLINKXPCHelper"
  verify_helper_has_fallback "${helper_bin}"

  local install_dir dest
  while IFS= read -r install_dir; do
    [[ -z "${install_dir}" ]] && continue
    dest="${install_dir}/${HELPER_NAME}"
    echo "$(bold "Installing") ${CONFIG} XPCHelper"
    echo "  from: ${src}"
    echo "  to:   ${dest}"
    install_bundle_ditto "${src}" "${dest}"
    verify_helper_has_fallback "${dest}/Contents/MacOS/NovaLINKXPCHelper"
    require_same_sha "${helper_bin}" "${dest}/Contents/MacOS/NovaLINKXPCHelper"
    echo "$(bold "Installed XPCHelper.") sha=$(sha256_file "${dest}/Contents/MacOS/NovaLINKXPCHelper")"
  done < <(resolve_helper_install_dirs)
}

install_app() {
  local src
  src="$(require_dist_bundle "${APP_NAME}")"
  echo "$(bold "Installing") ${CONFIG} passthrough app"
  echo "  from: ${src}"
  echo "  to:   ${DEFAULT_APP_INSTALL}"
  install_bundle_ditto "${src}" "${DEFAULT_APP_INSTALL}"
  require_same_sha \
    "${src}/Contents/MacOS/NovaLINK Audio Passthrough" \
    "${DEFAULT_APP_INSTALL}/Contents/MacOS/NovaLINK Audio Passthrough"
  echo "$(bold "Installed app.")"
}

install_agent() {
  if [[ ! -f "${AGENT_TEMPLATE}" ]]; then
    echo "error: missing LaunchAgent template: ${AGENT_TEMPLATE}" >&2
    exit 1
  fi

  local app_exec="${DEFAULT_APP_INSTALL}/Contents/MacOS/NovaLINK Audio Passthrough"
  if [[ ! -x "${app_exec}" ]]; then
    echo "error: missing executable: ${app_exec}" >&2
    exit 1
  fi

  local tmp_plist user_plist uid
  tmp_plist="$(mktemp)"
  user_plist="${HOME}/Library/LaunchAgents/${AGENT_PLIST_NAME}"
  uid="$(id -u)"
  mkdir -p "${HOME}/Library/LaunchAgents"

  python3 - "${AGENT_TEMPLATE}" "${tmp_plist}" "${app_exec}" <<'PY'
import sys
src, dst, exe = sys.argv[1], sys.argv[2], sys.argv[3]
text = open(src, "r", encoding="utf-8").read().replace("{{PASSTHROUGH_APP_EXECUTABLE}}", exe)
open(dst, "w", encoding="utf-8").write(text)
PY
  plutil -lint "${tmp_plist}" >/dev/null

  echo "$(bold "Installing") LaunchAgent ${AGENT_LABEL}"
  echo "  plist: ${user_plist}"
  echo "  exec:  ${app_exec} --agent"

  launchctl bootout "gui/${uid}/${AGENT_LABEL}" 2>/dev/null || true
  cp "${tmp_plist}" "${user_plist}"
  rm -f "${tmp_plist}"

  if ! launchctl bootstrap "gui/${uid}" "${user_plist}"; then
    echo "error: launchctl bootstrap failed for ${AGENT_LABEL}" >&2
    exit 1
  fi
  launchctl enable "gui/${uid}/${AGENT_LABEL}" 2>/dev/null || true
  if ! launchctl kickstart -k "gui/${uid}/${AGENT_LABEL}"; then
    echo "error: launchctl kickstart failed for ${AGENT_LABEL}" >&2
    exit 1
  fi

  # Confirm the job is actually running.
  local ready=0
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    if launchctl print "gui/${uid}/${AGENT_LABEL}" 2>/dev/null | grep -q 'state = running'; then
      ready=1
      break
    fi
    sleep 0.5
  done
  if [[ "${ready}" -ne 1 ]]; then
    echo "error: PassthroughAgent did not reach running state" >&2
    launchctl print "gui/${uid}/${AGENT_LABEL}" 2>&1 | head -40 || true
    echo "---- /tmp/novalink-passthrough-agent.log ----" >&2
    tail -40 /tmp/novalink-passthrough-agent.log 2>&1 || true
    exit 1
  fi

  echo "$(bold "LaunchAgent running.")"
  echo "  Log: /tmp/novalink-passthrough-agent.log"
  echo "  Mic: System Settings → Privacy & Security → Microphone → NovaLINK Audio Passthrough"
}

restart_helper() {
  if [[ ! -f "${LAUNCHD_HELPER_PLIST}" ]]; then
    echo "warning: ${LAUNCHD_HELPER_PLIST} missing; XPCHelper launchd job not restarted." >&2
    return
  fi
  echo "$(bold "Restarting") ${HELPER_LABEL}"
  if ! sudo launchctl kickstart -k "system/${HELPER_LABEL}" 2>/dev/null; then
    sudo launchctl bootout system "${LAUNCHD_HELPER_PLIST}" 2>/dev/null || true
    sudo launchctl bootstrap system "${LAUNCHD_HELPER_PLIST}" 2>/dev/null \
      || sudo launchctl load "${LAUNCHD_HELPER_PLIST}" 2>/dev/null \
      || true
  fi
}

restart_coreaudiod() {
  echo "$(bold "Restarting coreaudiod")"
  if ! sudo launchctl kill SIGTERM system/com.apple.audio.coreaudiod 2>/dev/null; then
    sudo killall coreaudiod 2>/dev/null || true
  fi
  sleep 1
}

# ---------------------------------------------------------------------------

if [[ "${EUID}" -eq 0 ]]; then
  echo "error: run as a normal user; the script will prompt for sudo when needed." >&2
  exit 1
fi

echo "Config: ${CONFIG}"
echo "Dist:   ${ROOT}/build/dist"

sudo -v

if [[ "${UNINSTALL_ONLY}" == true ]]; then
  unload_agent_for_current_user
  remove_installed_driver
  if [[ "${RESTART_SERVICES}" == true ]]; then
    restart_coreaudiod
  fi
else
  # Driver/helper/app first, recycle audio services, THEN start the agent so it
  # attaches to the freshly restarted HAL/XPC stack.
  install_driver
  install_helper
  install_app
  if [[ "${RESTART_SERVICES}" == true ]]; then
    restart_helper
    restart_coreaudiod
  fi
  install_agent
fi

echo
echo "$(bold "Done.")"
if [[ "${UNINSTALL_ONLY}" != true ]]; then
  echo "Background agent hosts passthrough (no companion UI required)."
  echo "Verify:"
  echo "  launchctl print gui/\$(id -u)/${AGENT_LABEL} | grep state"
  echo "  tail -f /tmp/novalink-passthrough-agent.log"
fi
