"""Fail CI if local binaries, images, libraries or archives enter Git."""

import subprocess
import sys
from pathlib import PurePosixPath

tracked = subprocess.check_output(["git", "ls-files", "-z"]).decode().split("\0")
blocked_parts = {
    "third_party", "vendor", "vendored", "deps", "external", "AIbuild",
    "logs", "stage", "out", "dist", "release", "packages", "offline_debs",
}
blocked_suffixes = {
    ".exe", ".dll", ".lib", ".obj", ".o", ".a", ".pdb", ".ilk",
    ".so", ".dylib", ".zip", ".7z", ".rar", ".tar", ".gz",
    ".xz", ".bz2", ".msi", ".cab", ".deb", ".rpm", ".png",
    ".jpg", ".jpeg", ".gif", ".webp", ".bmp", ".ico", ".svg",
    ".tif", ".tiff", ".pdf",
}
violations = []
for name in filter(None, tracked):
    path = PurePosixPath(name)
    if any(part in blocked_parts for part in path.parts[:-1]):
        violations.append(name)
    elif path.suffix.lower() in blocked_suffixes:
        violations.append(name)
if violations:
    print("Forbidden tracked runtime artifacts:", *violations, sep="\n  ", file=sys.stderr)
    sys.exit(1)
print(f"Tracked-source policy OK ({len(tracked) - 1} files checked)")
