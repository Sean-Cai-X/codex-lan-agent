# GN release build (no LLVM)

The GitHub Actions `release-gn-runtime-no-llvm` workflow builds from tracked
source, not from a local package. Windows x64 and Debian 12-compatible Linux
x86_64 use the same `linux/src` runtime source port and the tracked CLIPS core.
The `CODEX_LAN_AGENT_HAS_CLANG_AST` definition is deliberately absent.

Trigger the workflow with a new `vYYYY.MM.DD-gn-runtime` tag using
`workflow_dispatch`, or push such a tag. Both jobs must pass before a Release
is created. The GitHub Release assets are generated **inside** Actions. No
local executable, image, library or archive is added to Git.

On Linux, the portable tarball includes Qt 5 Core and the non-glibc dependency
closure downloaded through Debian 12 packages. It is not a static binary:
the target system still needs glibc 2.36 or newer. Windows x64 currently
packages the core agent executable; Linux also builds `optfile`,
`directory_access`, and `git_snapshot`.

After extracting, edit the generated config under `bin/` for your host and
run `bin/codex_lan_agent --config bin/<config> health`. To start `serve`, pass
the machine code printed by the program's authorization preflight. Keep
private config values and runtime data outside Git.
