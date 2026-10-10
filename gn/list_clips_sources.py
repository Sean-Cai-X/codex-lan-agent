"""Emit the checked-in CLIPS C sources as a GN JSON list."""

import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
core = root / "src" / "clips_core"
sources = [
    "//src/clips_core/" + path.name
    for path in sorted(core.glob("*.c"))
    if path.name != "clips_standalone_main.c"
]
if not sources:
    raise SystemExit("No checked-in CLIPS core sources found")
print(json.dumps(sources))
