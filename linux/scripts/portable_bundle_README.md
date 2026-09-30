# codex-lan-agent Linux portable bundle

This bundle targets Debian 12 compatible x86_64 Linux. It contains the GN
basic build, Qt 5 Core, and the non-glibc shared-library dependency closure.
LLVM/Clang is neither compiled nor linked.

No installation and no root access are required:

```bash
tar -xzf codex-lan-agent-linux-x86_64-gn-portable-cmd-YYYYMMDD.tar.gz
cd codex-lan-agent-linux-x86_64-gn-portable-cmd-YYYYMMDD
sha256sum -c SHA256SUMS

./bin/codex_lan_agent --config ./bin/kvm_agent.cfg health
./bin/directory_access --help
./bin/git_snapshot --help
```

Get the machine code without starting the server:

```bash
./bin/codex_lan_agent --config ./bin/kvm_agent.cfg serve
```

The command exits and prints `actual_code=XXXX-XXXX-XXXX-XXXX`. Start with:

```bash
./bin/codex_lan_agent --config ./bin/kvm_agent.cfg \
  serve --machine-code XXXX-XXXX-XXXX-XXXX
```

The route-only HTTP interface exposes authorized command execution through
`lan_agent_mcp_route` with `mode=call` and
`target_tool_name=lan_agent_run_command`. Supply the same machine code in the
`authorization` argument. Commands are audited, the working directory must be
under `remote_command_root`, executables are allowlisted, and shell execution
is disabled by default.

The configuration uses `${AGENT_DIR}`. Runtime data and logs stay inside the
extracted directory under `runtime/`, regardless of the extraction path.

Qt is dynamically linked from the files beside the executables in `bin/`.
Third-party package versions are recorded in `RUNTIME-MANIFEST.txt`, and
license texts are under `licenses/`.

Compatibility boundary: glibc and the Linux dynamic loader are intentionally
not bundled. Use Debian 12 or another x86_64 distribution with glibc 2.36 or
newer.
