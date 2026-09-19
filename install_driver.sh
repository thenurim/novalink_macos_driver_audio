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
PASSTHROUGH_IDENTIFIER="life.thenurim.novalink.App"
PASSTHROUGH_ENTITLEMENTS="${ROOT}/NovaLINKApp/NovaLINKApp/NovaLINKApp.entitlements"

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

# TCC mic grants only stick when Info.plist is sealed into the CodeDirectory.
# Linker-signed (xcodebuild with CODE_SIGNING_ALLOWED=NO) leaves Info.plist unbound
# and re-prompts on every agent launch.
verify_passthrough_codesign() {
  local app="$1"
  local info
  info="$(codesign -dv --verbose=4 "${app}" 2>&1 || true)"
  if ! grep -q "Identifier=${PASSTHROUGH_IDENTIFIER}" <<<"${info}"; then
    echo "error: ${app} is not signed as ${PASSTHROUGH_IDENTIFIER}" >&2
    echo "${info}" >&2
    echo "Rebuild with: ./build_all.sh ${CONFIG}" >&2
    exit 1
  fi
  if grep -q 'Info.plist=not bound' <<<"${info}" || grep -q 'linker-signed' <<<"${info}"; then
    echo "error: ${app} has unbound/linker-signed codesign (TCC will re-prompt forever)" >&2
    echo "${info}" >&2
    echo "Rebuild with: ./build_all.sh ${CONFIG}" >&2
    exit 1
  fi
  if ! grep -q 'Info.plist entries=' <<<"${info}"; then
    echo "error: ${app} Info.plist is not bound into the signature" >&2
    echo "${info}" >&2
    exit 1
  fi
  codesign --verify --verbose=2 "${app}" >/dev/null
}

sign_passthrough_app() {
  local app="$1"
  local bin="${app}/Contents/MacOS/NovaLINK Audio Passthrough"
  if [[ ! -f "${PASSTHROUGH_ENTITLEMENTS}" ]]; then
    echo "error: missing entitlements: ${PASSTHROUGH_ENTITLEMENTS}" >&2
    exit 1
  fi
  # Prefer signing as the installing user so LaunchAgent (gui session) matches.
  # Fall back to sudo if /Applications is not writable.
  if [[ -w "${bin}" ]]; then
    codesign --force --sign - \
      --identifier "${PASSTHROUGH_IDENTIFIER}" \
      --entitlements "${PASSTHROUGH_ENTITLEMENTS}" \
      --options runtime \
      "${bin}"
    codesign --force --deep --sign - \
      --identifier "${PASSTHROUGH_IDENTIFIER}" \
      --entitlements "${PASSTHROUGH_ENTITLEMENTS}" \
      --options runtime \
      "${app}"
  else
    sudo codesign --force --sign - \
      --identifier "${PASSTHROUGH_IDENTIFIER}" \
      --entitlements "${PASSTHROUGH_ENTITLEMENTS}" \
      --options runtime \
      "${bin}"
    sudo codesign --force --deep --sign - \
      --identifier "${PASSTHROUGH_IDENTIFIER}" \
      --entitlements "${PASSTHROUGH_ENTITLEMENTS}" \
      --options runtime \
      "${app}"
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

# launchctl bootstrap returns EIO (5) when the service is disabled, still
# registered, or the domain is briefly unsettled. Never leave the job disabled
# across a reinstall — that makes every subsequent bootstrap fail with EIO until
# a legacy `load -w` happens to clear it.
bootout_agent_for_uid() {
  local uid="$1"
  local plist="${2:-}"
  local domain="gui/${uid}"
  local service="${domain}/${AGENT_LABEL}"
  local i

  # Stop any leftover agent/UI process holding the instance lock.
  pkill -x "NovaLINK Audio Passthrough" 2>/dev/null || true
  sleep 0.3
  # Only escalate if still alive — avoid blasting SIGKILL on every path.
  if pgrep -x "NovaLINK Audio Passthrough" >/dev/null 2>&1; then
    pkill -9 -x "NovaLINK Audio Passthrough" 2>/dev/null || true
  fi

  launchctl bootout "${service}" 2>/dev/null || true
  if [[ -n "${plist}" && -f "${plist}" ]]; then
    launchctl bootout "${domain}" "${plist}" 2>/dev/null || true
    launchctl unload "${plist}" 2>/dev/null || true
  fi
  # Wait until the process is actually gone so coreaudiod is not left serving a
  # half-dead HAL client (that pattern drives coreaudiod to 100%+ CPU).
  for i in 1 2 3 4 5 6 7 8 9 10; do
    if ! pgrep -x "NovaLINK Audio Passthrough" >/dev/null 2>&1; then
      break
    fi
    sleep 0.2
  done
  # Keep enabled so the next bootstrap/kickstart can succeed.
  launchctl enable "${service}" 2>/dev/null || true
}

agent_is_registered() {
  local uid="$1"
  launchctl print "gui/${uid}/${AGENT_LABEL}" >/dev/null 2>&1
}

unload_agent_for_current_user() {
  local uid user_plist
  uid="$(id -u)"
  user_plist="${HOME}/Library/LaunchAgents/${AGENT_PLIST_NAME}"
  bootout_agent_for_uid "${uid}" "${user_plist}"
  # Uninstall only: prevent auto-reload of a leftover plist until we delete it.
  launchctl disable "gui/${uid}/${AGENT_LABEL}" 2>/dev/null || true
  rm -f "${user_plist}"
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
  echo "$(bold "Verifying") dist passthrough codesign (Info.plist must be bound)"
  verify_passthrough_codesign "${src}"
  echo "$(bold "Installing") ${CONFIG} passthrough app"
  echo "  from: ${src}"
  echo "  to:   ${DEFAULT_APP_INSTALL}"
  install_bundle_ditto "${src}" "${DEFAULT_APP_INSTALL}"
  require_same_sha \
    "${src}/Contents/MacOS/NovaLINK Audio Passthrough" \
    "${DEFAULT_APP_INSTALL}/Contents/MacOS/NovaLINK Audio Passthrough"
  # ditto/chown can leave the on-disk signature stale on some hosts — re-seal and verify.
  echo "$(bold "Re-sealing") installed passthrough codesign"
  sign_passthrough_app "${DEFAULT_APP_INSTALL}"
  verify_passthrough_codesign "${DEFAULT_APP_INSTALL}"
  echo "$(bold "Installed app.") codesign OK (Info.plist bound)"
}

bootstrap_or_kickstart_agent() {
  local uid="$1"
  local plist="$2"
  local domain="gui/${uid}"
  local service="${domain}/${AGENT_LABEL}"
  local attempt err_file

  err_file="$(mktemp)"
  # Clear any sticky disabled bit from older installers / failed runs.
  launchctl enable "${service}" 2>/dev/null || true

  for attempt in 1 2 3; do
    if agent_is_registered "${uid}"; then
      if launchctl kickstart -k "${service}" 2>"${err_file}"; then
        rm -f "${err_file}"
        return 0
      fi
      echo "warning: kickstart failed (attempt ${attempt}/3); re-bootstrapping:" >&2
      sed 's/^/  /' "${err_file}" >&2 || true
      bootout_agent_for_uid "${uid}" "${plist}"
      sleep 1
    fi

    if launchctl bootstrap "${domain}" "${plist}" 2>"${err_file}"; then
      launchctl enable "${service}" 2>/dev/null || true
      launchctl kickstart -k "${service}" 2>/dev/null \
        || launchctl kickstart "${service}" 2>/dev/null \
        || true
      rm -f "${err_file}"
      return 0
    fi

    # EIO usually means "already loaded" (or was disabled). Enable + kickstart
    # without another bootout — tearing down and retrying bootstrap often loops
    # on the same error.
    if grep -qiE 'Input/output error|error = 5|Already loaded|service already loaded' "${err_file}"; then
      launchctl enable "${service}" 2>/dev/null || true
      if launchctl kickstart -k "${service}" 2>/dev/null \
          || agent_is_registered "${uid}"; then
        rm -f "${err_file}"
        return 0
      fi
    fi

    echo "warning: launchctl bootstrap attempt ${attempt}/3 failed:" >&2
    sed 's/^/  /' "${err_file}" >&2 || true
    bootout_agent_for_uid "${uid}" "${plist}"
    launchctl enable "${service}" 2>/dev/null || true
    sleep 1
  done

  # Legacy path: load -w also clears the disabled bit.
  if launchctl load -w "${plist}" 2>"${err_file}"; then
    launchctl start "${AGENT_LABEL}" 2>/dev/null || true
    rm -f "${err_file}"
    return 0
  fi

  echo "error: launchctl bootstrap failed for ${AGENT_LABEL}" >&2
  sed 's/^/  /' "${err_file}" >&2 || true
  rm -f "${err_file}"
  return 1
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

  local tmp_plist user_plist uid domain service
  tmp_plist="$(mktemp)"
  user_plist="${HOME}/Library/LaunchAgents/${AGENT_PLIST_NAME}"
  uid="$(id -u)"
  domain="gui/${uid}"
  service="${domain}/${AGENT_LABEL}"
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

  cp "${tmp_plist}" "${user_plist}"
  rm -f "${tmp_plist}"
  launchctl enable "${service}" 2>/dev/null || true

  if ! start_agent_and_verify "${uid}" "${user_plist}"; then
    exit 1
  fi

  echo "$(bold "LaunchAgent ready.") (status bar should be visible)"
  echo "  Log: /tmp/novalink-passthrough-agent.log"
  echo "  Mic: allow once in System Settings → Privacy & Security → Microphone"
  echo "       (life.thenurim.novalink.App / NovaLINK Audio Passthrough)."
  echo "       With a bound Info.plist signature the grant should stick across relaunches."
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

wait_for_coreaudiod_and_novalink_hal() {
  echo "$(bold "Waiting") for coreaudiod + NovaLINK HAL settle..."
  local i
  for i in $(seq 1 40); do
    if pgrep -x coreaudiod >/dev/null 2>&1; then
      break
    fi
    sleep 0.25
  done
  # Generous settle: loading the HAL plugin right after a recycle is when
  # coreaudiod is most likely to spin if clients attach too early.
  # Never probe via system_profiler/CoreAudio here — those calls can hang.
  sleep 10
  if [[ -d "${INSTALLED_DRIVER_PATH}" ]]; then
    echo "coreaudiod up; driver bundle present at ${INSTALLED_DRIVER_PATH}"
  else
    echo "warning: driver bundle missing at ${INSTALLED_DRIVER_PATH}" >&2
  fi
}

restart_coreaudiod() {
  echo "$(bold "Restarting coreaudiod")"
  local old_pid new_pid i
  old_pid="$(pgrep -x coreaudiod | head -1 || true)"

  # Soft recycle only. kill -9 leaves CoreAudio/HAL in a thrashing state that
  # routinely pegs coreaudiod above 100% CPU until reboot.
  if ! sudo launchctl kill SIGTERM system/com.apple.audio.coreaudiod 2>/dev/null; then
    sudo killall -TERM coreaudiod 2>/dev/null || true
  fi

  for i in $(seq 1 60); do
    new_pid="$(pgrep -x coreaudiod | head -1 || true)"
    if [[ -n "${new_pid}" && "${new_pid}" != "${old_pid}" ]]; then
      return 0
    fi
    if [[ -z "${old_pid}" && -n "${new_pid}" ]]; then
      return 0
    fi
    # Old process still exiting — keep waiting; do not escalate to SIGKILL.
    sleep 0.25
  done
  echo "warning: coreaudiod did not respawn cleanly (old=${old_pid:-none} new=${new_pid:-none})" >&2
}

# launchctl "state = running" is not enough — a wedged agent stays "running" while
# blocked forever inside CoreAudio HAL init (no status item; lock held so companion
# handoff also fails). Require a startup log line printed after nib/UI setup begins.
agent_log_ready_since_marker() {
  local log="$1"
  local marker="$2"
  # awk avoids macOS BSD grep quirks with -A / -- option ordering.
  awk -v marker="${marker}" '
    $0 == marker { seen = 1; next }
    seen && /agent playthrough host ready|Permission denied|grant Microphone access/ {
      found = 1
      exit
    }
    END { exit found ? 0 : 1 }
  ' "${log}" 2>/dev/null
}

start_agent_and_verify() {
  local uid="$1"
  local plist="$2"
  local service="gui/${uid}/${AGENT_LABEL}"
  local log="/tmp/novalink-passthrough-agent.log"
  local attempt marker ready i

  for attempt in 1 2; do
    marker="==== novalink-install $(date '+%Y-%m-%d %H:%M:%S') attempt ${attempt} ===="
    echo "${marker}" >>"${log}" 2>/dev/null || true

    if ! bootstrap_or_kickstart_agent "${uid}" "${plist}"; then
      return 1
    fi

    ready=0
    for i in $(seq 1 50); do
      if launchctl print "${service}" 2>/dev/null | grep -q 'state = running' \
          && agent_log_ready_since_marker "${log}" "${marker}"; then
        ready=1
        break
      fi
      sleep 0.5
    done

    if [[ "${ready}" -eq 1 ]]; then
      return 0
    fi

    echo "warning: PassthroughAgent running but not ready. Stopping it and retrying once." >&2
    echo "         (Not restarting coreaudiod again — that pegs CPU.)" >&2
    bootout_agent_for_uid "${uid}" "${plist}"
    sleep 2
  done

  echo "error: PassthroughAgent did not become ready" >&2
  launchctl print "${service}" 2>&1 | head -40 || true
  echo "---- ${log} ----" >&2
  tail -60 "${log}" 2>&1 || true
  return 1
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
  # Stop agent first so coreaudiod is not recycling under a live HAL client.
  bootout_agent_for_uid "$(id -u)" "${HOME}/Library/LaunchAgents/${AGENT_PLIST_NAME}"

  install_driver
  install_helper
  install_app
  if [[ "${RESTART_SERVICES}" == true ]]; then
    # Order matters: recycle audio first, then helper, then agent.
    # Helper/agent attaching during coreaudiod plugin load causes CPU spikes.
    restart_coreaudiod
    wait_for_coreaudiod_and_novalink_hal
    restart_helper
    sleep 1
  fi
  install_agent
fi

echo
echo "$(bold "Done.")"
if [[ "${UNINSTALL_ONLY}" != true ]]; then
  echo "Background agent hosts passthrough (status bar icon; companion window optional)."
  echo "Verify:"
  echo "  launchctl print gui/\$(id -u)/${AGENT_LABEL} | grep state"
  echo "  tail -f /tmp/novalink-passthrough-agent.log"
fi
