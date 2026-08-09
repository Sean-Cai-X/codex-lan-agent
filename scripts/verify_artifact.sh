#!/bin/bash
set -euo pipefail

DEPLOY_ROOT="$1"
MANIFEST_FILE="$2"

echo "=== Starting artifact integrity check ==="
cd "${DEPLOY_ROOT}"
sha256sum --check "../${MANIFEST_FILE}"
echo "✅ File hash manifest validation OK"

# 运行时功能自检
export PATH="${DEPLOY_ROOT}/bin:$PATH"
# 在这里追加项目专属二进制自检命令

echo "=== All verification complete ==="
