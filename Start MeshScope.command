#!/bin/zsh
set -e

cd "$(dirname "$0")"

url="http://127.0.0.1:8765"

if curl -fsS "$url/api/status" >/dev/null 2>&1; then
  open "$url"
  exit 0
fi

python3 linksys_mesh_app.py &
meshscope_pid=$!

cleanup() {
  kill "$meshscope_pid" >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

for attempt in {1..30}; do
  if curl -fsS "$url/api/status" >/dev/null 2>&1; then
    open "$url"
    wait "$meshscope_pid"
    exit 0
  fi
  sleep 0.2
done

echo "MeshScope did not start. Press Return to close."
read
exit 1
