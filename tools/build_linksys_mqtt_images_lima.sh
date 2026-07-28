#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
VM_NAME=${MESH_FIRMWARE_VM:-mesh-firmware}
MX5300_IMAGE=${1:-/Users/rjwang/Downloads/FW_MX5300_1.1.12.210066_prod.img}
MX4200_IMAGE=${2:-"$REPO_ROOT/firmware-analysis/input/FW_MX4200_1.0.13.216903_prod.img"}
OUTPUT_PARENT=${3:-"$REPO_ROOT/firmware-analysis/work/mqtt-parent-images"}
RUN_ID=$(date -u '+%Y%m%dT%H%M%SZ')-$$
GUEST_STAGE="/home/rjwang.guest/meshscope-mqtt-builder-$RUN_ID"
HOST_OUTPUT="$OUTPUT_PARENT/$RUN_ID"

for command_name in limactl shasum; do
    command -v "$command_name" >/dev/null || {
        echo "Missing required command: $command_name" >&2
        exit 69
    }
done
[[ -f "$MX5300_IMAGE" ]] || {
    echo "Missing MX5300 official image: $MX5300_IMAGE" >&2
    exit 66
}
[[ -f "$MX4200_IMAGE" ]] || {
    echo "Missing MX4200 official image: $MX4200_IMAGE" >&2
    exit 66
}
if [[ -e "$HOST_OUTPUT" ]]; then
    echo "Refusing to overwrite output directory: $HOST_OUTPUT" >&2
    exit 73
fi

mkdir -p "$OUTPUT_PARENT"
limactl start "$VM_NAME" >/dev/null
limactl shell "$VM_NAME" -- mkdir -p \
    "$GUEST_STAGE/tools" \
    "$GUEST_STAGE/firmware-overlays" \
    "$GUEST_STAGE/inputs"

completed=0
cleanup() {
    if [[ $completed -eq 1 ]]; then
        limactl shell "$VM_NAME" -- rm -rf -- "$GUEST_STAGE"
    else
        echo "Build did not complete; guest work retained at:" >&2
        echo "  $VM_NAME:$GUEST_STAGE" >&2
    fi
}
trap cleanup EXIT

limactl copy \
    "$SCRIPT_DIR/build_linksys_mqtt_images_linux.sh" \
    "$SCRIPT_DIR/compare_filesystem_trees.py" \
    "$SCRIPT_DIR/linksys_mx_repack.py" \
    "$VM_NAME:$GUEST_STAGE/tools/"
limactl copy -r \
    "$REPO_ROOT/firmware-overlays/mqtt-parent-steering" \
    "$VM_NAME:$GUEST_STAGE/firmware-overlays/"
limactl copy \
    "$MX5300_IMAGE" \
    "$MX4200_IMAGE" \
    "$VM_NAME:$GUEST_STAGE/inputs/"

limactl shell "$VM_NAME" -- bash -lc \
    "cd '$GUEST_STAGE' && UBIREADER_BIN_DIR=/home/rjwang.guest/ubi-tools/bin bash tools/build_linksys_mqtt_images_linux.sh 'inputs/$(basename "$MX5300_IMAGE")' 'inputs/$(basename "$MX4200_IMAGE")' output"

mkdir "$HOST_OUTPUT"
limactl copy --backend=rsync -r \
    "$VM_NAME:$GUEST_STAGE/output/" \
    "$HOST_OUTPUT/"

(
    cd "$HOST_OUTPUT"
    shasum -a 256 -c SHA256SUMS
)

completed=1
echo "Four verified images are available at:"
echo "  $HOST_OUTPUT"
