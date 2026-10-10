#!/usr/bin/env bash
set -euo pipefail

bundle_name="${1:-codex-lan-agent-linux-x86_64-gn-portable-20260922}"
source_bin="${SOURCE_BIN_DIR:-/opt/codex-lan-agent-gn-stage/bin}"
delivery_root="${DELIVERY_ROOT:-/root/delivery}"
bundle_root="${delivery_root}/${bundle_name}"
if [[ -n "${REMOTE_COMMAND_ROOT:-}" ]]; then
  remote_command_root_value="${REMOTE_COMMAND_ROOT}"
else
  remote_command_root_value='${AGENT_DIR}/..'
fi

if [[ ! "${bundle_name}" =~ ^codex-lan-agent-linux-x86_64-gn-portable(-cmd)?-[0-9]{8}$ ]]; then
  echo "refusing unsafe bundle name: ${bundle_name}" >&2
  exit 2
fi
case "${bundle_root}" in
  "${delivery_root}"/*) ;;
  *) echo "refusing bundle path outside delivery root" >&2; exit 2 ;;
esac

command -v patchelf >/dev/null 2>&1 || {
  echo "patchelf is required to produce a portable bundle" >&2
  exit 2
}

rm -rf -- "${bundle_root}"
install -d "${bundle_root}/bin" "${bundle_root}/licenses" \
  "${bundle_root}/runtime/data" "${bundle_root}/runtime/logs"

for executable in codex_lan_agent optfile directory_access git_snapshot; do
  install -m 0755 "${source_bin}/${executable}" "${bundle_root}/bin/${executable}"
done

cat >"${bundle_root}/bin/kvm_agent.cfg" <<'EOF'
workspace_root=${AGENT_DIR}/..
log_root=${AGENT_DIR}/../runtime/logs
data_root=${AGENT_DIR}/../runtime/data
dialog_slices_root=${AGENT_DIR}/../runtime/data/dialog_slices
session_dispatch_root=${AGENT_DIR}/../runtime/data/session_dispatch
remote_session_slices_root=${AGENT_DIR}/../runtime/data/remote_session_slices
listen_host=0.0.0.0
listen_port=18080
generation_endpoint=http://127.0.0.1:8095/v1/chat/completions
embedding_endpoint=http://127.0.0.1:8096/v1/embeddings
local_chat_endpoint=http://127.0.0.1:8080/local-chat
task_timeout_sec=1800
EOF
printf 'remote_command_enabled=true\nremote_command_root=%s\nremote_command_allow_shell=false\n' \
  "${remote_command_root_value}" >>"${bundle_root}/bin/kvm_agent.cfg"

runtime_libraries=(
  libQt5Core.so.5
  libstdc++.so.6
  libgcc_s.so.1
  libz.so.1
  libdouble-conversion.so.3
  libicui18n.so.72
  libicuuc.so.72
  libicudata.so.72
  libpcre2-16.so.0
  libpcre2-8.so.0
  libzstd.so.1
  libglib-2.0.so.0
)

for soname in "${runtime_libraries[@]}"; do
  library_path="$(ldconfig -p | awk -v name="${soname}" '$1 == name { print $NF; exit }')"
  if [[ -z "${library_path}" || ! -f "${library_path}" ]]; then
    echo "required runtime library not found: ${soname}" >&2
    exit 2
  fi
  cp -L "${library_path}" "${bundle_root}/bin/${soname}"
  chmod 0644 "${bundle_root}/bin/${soname}"
done

# Executables locate bundled libraries beside themselves. Bundled libraries
# also locate their transitive dependencies in the same directory.
for elf_file in "${bundle_root}/bin/codex_lan_agent" \
                "${bundle_root}/bin/optfile" \
                "${bundle_root}/bin/directory_access" \
                "${bundle_root}/bin/git_snapshot" \
                "${bundle_root}/bin/"*.so.*; do
  patchelf --set-rpath '$ORIGIN' "${elf_file}"
done

runtime_packages=(
  libqt5core5a
  libstdc++6
  libgcc-s1
  zlib1g
  libdouble-conversion3
  libicu72
  libpcre2-16-0
  libpcre2-8-0
  libzstd1
  libglib2.0-0
)

: >"${bundle_root}/RUNTIME-MANIFEST.txt"
for package_name in "${runtime_packages[@]}"; do
  dpkg-query -W "${package_name}" >>"${bundle_root}/RUNTIME-MANIFEST.txt"
  copyright_path="/usr/share/doc/${package_name}/copyright"
  if [[ -e "${copyright_path}" ]]; then
    cp -L "${copyright_path}" \
      "${bundle_root}/licenses/${package_name}.copyright"
  fi
done

cp "$(dirname "${BASH_SOURCE[0]}")/portable_bundle_README.md" \
  "${bundle_root}/README.md"

(
  cd "${bundle_root}"
  find . -type f ! -name SHA256SUMS -print0 \
    | sort -z \
    | xargs -0 sha256sum >SHA256SUMS
)

tar --sort=name --owner=0 --group=0 --numeric-owner \
  -czf "${delivery_root}/${bundle_name}.tar.gz" \
  -C "${delivery_root}" "${bundle_name}"
(
  cd "${delivery_root}"
  sha256sum "${bundle_name}.tar.gz" >"${bundle_name}.tar.gz.sha256"
)

echo "bundle_status=ok"
echo "bundle_root=${bundle_root}"
echo "archive=${delivery_root}/${bundle_name}.tar.gz"
