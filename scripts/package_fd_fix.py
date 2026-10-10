"""Package a locally verified Linux hotfix without changing the running gateway."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.request

root = Path(sys.argv[1]).resolve()
old = Path(sys.argv[2]).resolve()
exe = root / "out/fd-fix/codex_lan_agent"
delivery = root / "delivery"
bundle = delivery / "codex-lan-agent-linux-x86_64-gn-portable-cmd-20261009"
bundle.mkdir(parents=True, exist_ok=False)
shutil.copytree(old / "bin", bundle / "bin",
                ignore=shutil.ignore_patterns("*.cfg", "*.log", "*.lock"))
shutil.copy2(exe, bundle / "bin/codex_lan_agent")
shutil.copytree(old / "licenses", bundle / "licenses")
(bundle / "runtime/logs").mkdir(parents=True)
(bundle / "runtime/data").mkdir()
config = """workspace_root=${AGENT_DIR}/..
log_root=${AGENT_DIR}/../runtime/logs
data_root=${AGENT_DIR}/../runtime/data
listen_host=127.0.0.1
listen_port=18080
remote_command_enabled=true
remote_command_root=${AGENT_DIR}/..
remote_command_allow_shell=false
"""
(bundle / "bin/kvm_agent.cfg").write_text(config)
env = os.environ.copy()
env.pop("LD_PRELOAD", None)
env["LD_LIBRARY_PATH"] = str(bundle / "bin")
binary = str(bundle / "bin/codex_lan_agent")
subprocess.run([binary, "--config", str(bundle / "bin/kvm_agent.cfg"), "health"],
               env=env, check=True, timeout=15)
# Isolated lock, logs, data and loopback port: never touch the active gateway.
with tempfile.TemporaryDirectory(prefix="agent-fd-test-") as td:
    td = Path(td)
    cfg = td / "test.cfg"
    cfg.write_text("workspace_root=" + str(td) + "\nlog_root=" + str(td / "logs") +
                   "\ndata_root=" + str(td / "data") +
                   "\nlisten_host=127.0.0.1\nlisten_port=18992\n")
    env["XDG_RUNTIME_DIR"] = str(td)
    args = [binary, "--config", str(cfg), "serve", "--machine-code", sys.argv[3]]
    for iteration in range(2):
        with (td / ("serve-" + str(iteration) + ".log")).open("w") as log:
            server = subprocess.Popen(args, env=env, stdout=log, stderr=log,
                                      close_fds=True)
            try:
                for attempt in range(50):
                    if server.poll() is not None:
                        raise RuntimeError("isolated server exited: " +
                                           (td / ("serve-" + str(iteration) + ".log")).read_text())
                    try:
                        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
                        request = urllib.request.Request("http://127.0.0.1:18992/tools",
                            data=json.dumps({"name": "lan_agent_mcp_route",
                                             "arguments": {"mode": "identity"}}).encode(),
                            headers={"Content-Type": "application/json"})
                        with opener.open(request, timeout=5) as response:
                            assert response.status == 200
                        break
                    except OSError:
                        time.sleep(0.1)
                else:
                    raise RuntimeError("isolated health timeout")
                print("ISOLATED_START_HEALTH_PASS", iteration, flush=True)
            finally:
                if server.poll() is None:
                    server.terminate()
                server.wait(timeout=10)
(bundle / "HOTFIX.txt").write_text(
    "Linux x86_64 GN hotfix 2026-10-09; base commit 158c05a. No LLVM.\n"
    "Built and tested on Devuan 6 with GCC 14 and private build sysroot.\n"
    "Not a Debian 12 compatibility certification or GitHub CI release.\n"
    "Agent rebuilt; helper executables and their libraries reused from 20260922.\n"
    "Fix: close-on-exec instance lock; close inherited descriptors before child exec.\n"
    "Tests: ProcessRunner FD isolation; health; isolated serve/stop/start.\n"
    "Production gateway was NOT stopped or replaced.\n"
    "Default listen address is loopback. Configure LAN access explicitly.\n"
    "Set remote_command_root to your intended workspace; supply current machine code.\n"
    "Coordinate all users before switching. Old children may still hold OLD locks.\n"
    "Do not remove lock files or broadly kill unrelated processes.\n")
files = sorted(p for p in bundle.rglob("*") if p.is_file())
(bundle / "SHA256SUMS").write_text("".join(
    hashlib.sha256(p.read_bytes()).hexdigest() + "  " + p.relative_to(bundle).as_posix() + "\n"
    for p in files))
archive = delivery / (bundle.name + ".tar.gz")
with tarfile.open(archive, "w:gz") as tar:
    tar.add(bundle, arcname=bundle.name)
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
Path(str(archive) + ".sha256").write_text(digest + "  " + archive.name + "\n")
print("PACKAGE", archive, "SHA256", digest, flush=True)
