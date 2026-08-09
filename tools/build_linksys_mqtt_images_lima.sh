#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
VM_NAME=${MESH_FIRMWARE_VM:-meshscope-firmware-builder}
MX5300_IMAGE=""
MX4200_IMAGE=""
OUTPUT_PARENT="$REPO_ROOT/firmware-analysis/work/mqtt-parent-images"
SETUP_ONLY=0
FMK_COMMIT=c72f45d7a062156125ae00cb867d8a614b50b963

usage() {
    cat <<'EOF'
Build verified Linksys MQTT firmware images inside an automatically provisioned Lima VM.

Usage:
  tools/build_linksys_mqtt_images_lima.sh --mx4200 OFFICIAL.img [--output-parent DIR]
  tools/build_linksys_mqtt_images_lima.sh --mx5300 OFFICIAL.img [--output-parent DIR]
  tools/build_linksys_mqtt_images_lima.sh --mx4200 OFFICIAL.img --mx5300 OFFICIAL.img
  tools/build_linksys_mqtt_images_lima.sh --setup-only

Legacy two-image form is also accepted:
  tools/build_linksys_mqtt_images_lima.sh MX5300_OFFICIAL.img MX4200_OFFICIAL.img [OUTPUT_PARENT]

The script never downloads vendor firmware. Supply an unmodified official IMG
whose SHA-256 matches the supported build documented in the overlay README.
EOF
}

if (( $# > 0 )) && [[ "$1" != --* ]]; then
    if (( $# < 2 || $# > 3 )); then usage >&2; exit 64; fi
    MX5300_IMAGE=$1
    MX4200_IMAGE=$2
    if (( $# == 3 )); then OUTPUT_PARENT=$3; fi
else
    while (( $# )); do
        case "$1" in
            --mx5300)
                (( $# >= 2 )) || { usage >&2; exit 64; }
                MX5300_IMAGE=$2
                shift 2
                ;;
            --mx4200)
                (( $# >= 2 )) || { usage >&2; exit 64; }
                MX4200_IMAGE=$2
                shift 2
                ;;
            --output-parent)
                (( $# >= 2 )) || { usage >&2; exit 64; }
                OUTPUT_PARENT=$2
                shift 2
                ;;
            --setup-only)
                SETUP_ONLY=1
                shift
                ;;
            --help|-h)
                usage
                exit 0
                ;;
            *)
                echo "Unknown option: $1" >&2
                usage >&2
                exit 64
                ;;
        esac
    done
fi

command -v limactl >/dev/null || {
    echo "Missing Lima. On macOS install it with: brew install lima" >&2
    exit 69
}
if ! limactl list --format '{{.Name}}' | grep -Fxq "$VM_NAME"; then
    echo "Creating the isolated $VM_NAME build VM..."
    limactl start --name="$VM_NAME" --cpus=2 --memory=2 --disk=16 \
        --tty=false template:ubuntu-lts
else
    limactl start "$VM_NAME" --tty=false >/dev/null
fi

GUEST_HOME=$(limactl shell "$VM_NAME" -- bash -lc 'printf %s "$HOME"')
GUEST_CACHE="$GUEST_HOME/.cache/meshscope-firmware-builder/v1"
GUEST_UBIREADER_BIN_DIR=${MESH_UBIREADER_BIN_DIR:-"$GUEST_CACHE/ubi-tools/bin"}
GUEST_MX4200_MKSQUASHFS=${MESH_MX4200_MKSQUASHFS:-"$GUEST_CACHE/firmware-mod-kit/src/others/squashfs-4.2/squashfs-tools/mksquashfs"}

if ! limactl shell "$VM_NAME" -- test -x "$GUEST_UBIREADER_BIN_DIR/ubireader_extract_images" || \
   ! limactl shell "$VM_NAME" -- test -x "$GUEST_MX4200_MKSQUASHFS"; then
    echo "Provisioning the pinned firmware toolchain (first run only)..."
    limactl shell "$VM_NAME" -- bash -lc "
        set -euo pipefail
        export DEBIAN_FRONTEND=noninteractive
        sudo apt-get update
        sudo apt-get install -y --no-install-recommends \
            build-essential ca-certificates git jq liblzma-dev mtd-utils patch \
            python3 python3-pip python3-venv qemu-user rsync sudo squashfs-tools \
            xz-utils zlib1g-dev
        mkdir -p '$GUEST_CACHE'
        if [[ ! -x '$GUEST_CACHE/ubi-tools/bin/ubireader_extract_images' ]]; then
            python3 -m venv '$GUEST_CACHE/ubi-tools'
            '$GUEST_CACHE/ubi-tools/bin/pip' install --disable-pip-version-check \
                'ubi-reader==0.8.14'
        fi
        if [[ ! -d '$GUEST_CACHE/firmware-mod-kit/.git' ]]; then
            mkdir -p '$GUEST_CACHE/firmware-mod-kit'
            git -C '$GUEST_CACHE/firmware-mod-kit' init
            git -C '$GUEST_CACHE/firmware-mod-kit' remote add origin \
                https://github.com/rampageX/firmware-mod-kit.git
        fi
        git -C '$GUEST_CACHE/firmware-mod-kit' fetch --depth=1 origin '$FMK_COMMIT'
        git -C '$GUEST_CACHE/firmware-mod-kit' checkout --detach --force FETCH_HEAD
        make -C '$GUEST_CACHE/firmware-mod-kit/src/others/squashfs-4.2/squashfs-tools' \
            -j2 XZ_SUPPORT=1 LZMA_XZ_SUPPORT=1 XATTR_SUPPORT= \
            mksquashfs unsquashfs
    "
fi

if (( SETUP_ONLY )); then
    echo "Firmware builder is ready in Lima instance: $VM_NAME"
    exit 0
fi
[[ -n "$MX5300_IMAGE" || -n "$MX4200_IMAGE" ]] || { usage >&2; exit 64; }
for image in "$MX5300_IMAGE" "$MX4200_IMAGE"; do
    [[ -z "$image" || -f "$image" ]] || {
        echo "Missing official image: $image" >&2
        exit 66
    }
done
command -v shasum >/dev/null || { echo "Missing required command: shasum" >&2; exit 69; }

RUN_ID=$(date -u '+%Y%m%dT%H%M%SZ')-$$
GUEST_STAGE="$GUEST_CACHE/runs/$RUN_ID"
HOST_OUTPUT="$OUTPUT_PARENT/$RUN_ID"
if [[ -e "$HOST_OUTPUT" ]]; then
    echo "Refusing to overwrite output directory: $HOST_OUTPUT" >&2
    exit 73
fi

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

GUEST_MX5300=-
GUEST_MX4200=-
if [[ -n "$MX5300_IMAGE" ]]; then
    limactl copy "$MX5300_IMAGE" "$VM_NAME:$GUEST_STAGE/inputs/"
    GUEST_MX5300="inputs/$(basename "$MX5300_IMAGE")"
fi
if [[ -n "$MX4200_IMAGE" ]]; then
    limactl copy "$MX4200_IMAGE" "$VM_NAME:$GUEST_STAGE/inputs/"
    GUEST_MX4200="inputs/$(basename "$MX4200_IMAGE")"
fi

limactl shell "$VM_NAME" -- bash -lc \
    "cd '$GUEST_STAGE' && UBIREADER_BIN_DIR='$GUEST_UBIREADER_BIN_DIR' MX4200_MKSQUASHFS='$GUEST_MX4200_MKSQUASHFS' bash tools/build_linksys_mqtt_images_linux.sh '$GUEST_MX5300' '$GUEST_MX4200' output"

mkdir -p "$OUTPUT_PARENT"
mkdir "$HOST_OUTPUT"
limactl copy --backend=rsync -r \
    "$VM_NAME:$GUEST_STAGE/output/" \
    "$HOST_OUTPUT/"

(
    cd "$HOST_OUTPUT"
    shasum -a 256 -c SHA256SUMS
)

completed=1
image_count=$(find "$HOST_OUTPUT" -maxdepth 1 -type f -name '*.img' | wc -l | tr -d ' ')
echo "$image_count verified image(s) are available at:"
echo "  $HOST_OUTPUT"
