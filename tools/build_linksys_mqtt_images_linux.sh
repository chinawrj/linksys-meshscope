#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "Usage: $0 MX5300_OFFICIAL.img MX4200_OFFICIAL.img OUTPUT_DIRECTORY" >&2
    exit 64
fi

MX5300_IMAGE=$(readlink -f "$1")
MX4200_IMAGE=$(readlink -f "$2")
OUTPUT_DIR=$(readlink -m "$3")
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
OVERLAY_DIR="$REPO_ROOT/firmware-overlays/mqtt-parent-steering"
UBIREADER_BIN_DIR=${UBIREADER_BIN_DIR:-"$HOME/ubi-tools/bin"}
QEMU_ARM=${QEMU_ARM:-qemu-arm}

MX5300_EXPECTED_SHA256=f51adff1395bb9d1b7f8d62fd080ba5c9f5e5589b6d22f92335792ba3261d651
MX4200_EXPECTED_SHA256=b0a954835c879822fd1a2da23f09a6ec37da69914aacf07d8038b06fa02fad25
PEB_SIZE=131072
UBI_OFFSET=6291456
TRAILER_SIZE=131072

required_commands=(
    cmp
    dd
    find
    jq
    mkfs.ubifs
    mksquashfs
    patch
    python3
    sha256sum
    stat
    sudo
    ubinize
    unsquashfs
)
for command_name in "${required_commands[@]}"; do
    command -v "$command_name" >/dev/null || {
        echo "Missing required command: $command_name" >&2
        exit 69
    }
done
for executable in ubireader_extract_files ubireader_extract_images; do
    [[ -x "$UBIREADER_BIN_DIR/$executable" ]] || {
        echo "Missing UBI Reader executable: $UBIREADER_BIN_DIR/$executable" >&2
        exit 69
    }
done
for path in \
    "$SCRIPT_DIR/linksys_mx_repack.py" \
    "$SCRIPT_DIR/compare_filesystem_trees.py" \
    "$OVERLAY_DIR/plan-a-strict-bh-config.patch" \
    "$OVERLAY_DIR/plan-b-open-acl.patch" \
    "$OVERLAY_DIR/ubi/mx5300.ini" \
    "$OVERLAY_DIR/ubi/mx4200.ini"; do
    [[ -f "$path" ]] || {
        echo "Missing build input: $path" >&2
        exit 66
    }
done
[[ -f "$MX5300_IMAGE" ]] || { echo "Missing MX5300 image" >&2; exit 66; }
[[ -f "$MX4200_IMAGE" ]] || { echo "Missing MX4200 image" >&2; exit 66; }
if [[ -e "$OUTPUT_DIR" ]]; then
    echo "Refusing to overwrite output directory: $OUTPUT_DIR" >&2
    exit 73
fi

verify_sha256() {
    local image=$1
    local expected=$2
    local actual
    actual=$(sha256sum "$image" | awk '{print $1}')
    if [[ "$actual" != "$expected" ]]; then
        echo "SHA-256 mismatch for $image" >&2
        echo "Expected: $expected" >&2
        echo "Actual:   $actual" >&2
        exit 65
    fi
}

extract_ubi() {
    local image=$1
    local output=$2
    local image_size ubi_size peb_count
    image_size=$(stat -c '%s' "$image")
    ubi_size=$((image_size - UBI_OFFSET - TRAILER_SIZE))
    if (( ubi_size <= 0 || ubi_size % PEB_SIZE != 0 )); then
        echo "Invalid Linksys IMG/UBI geometry: $image" >&2
        exit 65
    fi
    peb_count=$((ubi_size / PEB_SIZE))
    dd if="$image" of="$output" bs="$PEB_SIZE" skip=48 count="$peb_count" status=none
    [[ $(stat -c '%s' "$output") -eq $ubi_size ]]
}

extract_mx5300_rootfs() {
    local ubi=$1
    local extract_dir=$2
    local rootfs
    sudo "$UBIREADER_BIN_DIR/ubireader_extract_files" \
        -k -o "$extract_dir" "$ubi" >/dev/null
    rootfs=$(sudo find "$extract_dir" -mindepth 2 -maxdepth 2 \
        -type d -name ubifs -print -quit)
    [[ -n "$rootfs" ]] || {
        echo "MX5300 UBIFS root was not extracted" >&2
        exit 65
    }
    printf '%s\n' "$rootfs"
}

extract_mx4200_rootfs() {
    local ubi=$1
    local extract_dir=$2
    local volume rootfs
    mkdir -p "$extract_dir/volumes"
    "$UBIREADER_BIN_DIR/ubireader_extract_images" \
        -o "$extract_dir/volumes" "$ubi" >/dev/null
    volume=$(find "$extract_dir/volumes" -type f \
        -name '*vol-squashfs.ubifs' -print -quit)
    [[ -n "$volume" ]] || {
        echo "MX4200 SquashFS volume was not extracted" >&2
        exit 65
    }
    rootfs="$extract_dir/rootfs"
    sudo unsquashfs -no-progress -d "$rootfs" "$volume" >/dev/null
    printf '%s\n' "$rootfs"
}

restore_metadata() {
    local baseline=$1
    local candidate=$2
    local relative=$3
    local parent
    sudo cp --attributes-only --preserve=all \
        "$baseline/$relative" "$candidate/$relative"
    parent=$(dirname "$relative")
    sudo chown --reference="$baseline/$parent" "$candidate/$parent"
    sudo chmod --reference="$baseline/$parent" "$candidate/$parent"
    sudo touch -h --reference="$baseline/$parent" "$candidate/$parent"
}

apply_plan() {
    local baseline=$1
    local candidate=$2
    local plan=$3
    local patch_file changed_path
    sudo cp -a "$baseline" "$candidate"
    case "$plan" in
        plan-a)
            patch_file="$OVERLAY_DIR/plan-a-strict-bh-config.patch"
            changed_path=etc/mosquitto/strict.acl
            ;;
        plan-b)
            patch_file="$OVERLAY_DIR/plan-b-open-acl.patch"
            changed_path=etc/mosquitto/conf.d/default.conf
            ;;
        *)
            echo "Unknown plan: $plan" >&2
            exit 64
            ;;
    esac
    sudo patch --batch --fuzz=0 --no-backup-if-mismatch \
        -p1 -d "$candidate" < "$patch_file"
    restore_metadata "$baseline" "$candidate" "$changed_path"
    [[ ! -e "$candidate/$changed_path.orig" ]]
}

build_mx5300_ubi() {
    local rootfs=$1
    local build_dir=$2
    sudo mkfs.ubifs \
        -r "$rootfs" -m 2048 -e 126976 -c 615 -x zlib -F \
        -o "$build_dir/rootfs.ubifs"
    cp "$OVERLAY_DIR/ubi/mx5300.ini" "$build_dir/ubinize.ini"
    (
        cd "$build_dir"
        sudo ubinize \
            -o rootfs.ubi -p 131072 -m 2048 -s 2048 -O 2048 \
            -Q 1038325328 ubinize.ini
    )
    sudo chmod 0644 "$build_dir/rootfs.ubifs" "$build_dir/rootfs.ubi"
}

build_mx4200_ubi() {
    local rootfs=$1
    local build_dir=$2
    sudo mksquashfs "$rootfs" "$build_dir/rootfs.squashfs" \
        -noappend -comp xz -b 262144 -no-tailends -no-xattrs \
        -all-root -Xbcj arm -processors 2 >/dev/null
    cp "$OVERLAY_DIR/ubi/mx4200.ini" "$build_dir/ubinize.ini"
    (
        cd "$build_dir"
        sudo ubinize \
            -o rootfs.ubi -p 131072 -m 2048 -s 2048 -O 2048 \
            -Q 1903991695 ubinize.ini
    )
    sudo chmod 0644 "$build_dir/rootfs.squashfs" "$build_dir/rootfs.ubi"
}

run_fwcc() {
    local rootfs=$1
    local image=$2
    sudo env \
        PATH="$rootfs/bin:$rootfs/usr/bin:/usr/bin:/bin" \
        "$QEMU_ARM" -L "$rootfs" "$rootfs/bin/busybox" \
        sh "$rootfs/usr/sbin/fwcc" verify_signature "$image"
    sudo rm -f /tmp/linksys.hdr
}

verify_final_image() {
    local model=$1
    local plan=$2
    local baseline=$3
    local official=$4
    local image=$5
    local changed_path=$6
    local verify_dir="$WORK_DIR/verify-$model-$plan"
    local final_ubi final_rootfs
    mkdir -p "$verify_dir"
    final_ubi="$verify_dir/final.ubi"
    extract_ubi "$image" "$final_ubi"
    if [[ "$model" == "mx5300" ]]; then
        final_rootfs=$(extract_mx5300_rootfs "$final_ubi" "$verify_dir/extract")
    else
        final_rootfs=$(extract_mx4200_rootfs "$final_ubi" "$verify_dir/extract")
    fi

    sudo python3 "$SCRIPT_DIR/compare_filesystem_trees.py" \
        --baseline "$baseline" \
        --candidate "$final_rootfs" \
        --allow-content "$changed_path" \
        --output "$OUTPUT_DIR/verification/$model-$plan-filesystem.json" \
        >/dev/null
    cmp -n "$UBI_OFFSET" "$official" "$image"
    run_fwcc "$baseline" "$image"
    printf 'PASS\n' > "$OUTPUT_DIR/verification/$model-$plan-fwcc.txt"

    if [[ "$plan" == "plan-a" ]]; then
        sudo grep -Fxq 'topic write network/+/BH/config' \
            "$final_rootfs/etc/mosquitto/strict.acl"
        sudo cmp \
            "$baseline/etc/mosquitto/conf.d/default.conf" \
            "$final_rootfs/etc/mosquitto/conf.d/default.conf"
    else
        sudo grep -Fxq 'acl_file %CONF_DIR%/open.acl' \
            "$final_rootfs/etc/mosquitto/conf.d/default.conf"
        sudo cmp \
            "$baseline/etc/mosquitto/strict.acl" \
            "$final_rootfs/etc/mosquitto/strict.acl"
    fi
}

build_variant() {
    local model=$1
    local plan=$2
    local baseline=$3
    local official=$4
    local sku=$5
    local expected_sha=$6
    local output_name=$7
    local changed_path=$8
    local variant_dir="$WORK_DIR/$model-$plan"
    local rootfs="$variant_dir/rootfs"
    local image="$OUTPUT_DIR/$output_name"
    mkdir -p "$variant_dir"
    apply_plan "$baseline" "$rootfs" "$plan"
    if [[ "$model" == "mx5300" ]]; then
        build_mx5300_ubi "$rootfs" "$variant_dir"
    else
        build_mx4200_ubi "$rootfs" "$variant_dir"
    fi
    python3 "$SCRIPT_DIR/linksys_mx_repack.py" \
        --original "$official" \
        --ubi "$variant_dir/rootfs.ubi" \
        --output "$image" \
        --expected-sku "$sku" \
        --expected-original-sha256 "$expected_sha" \
        > "$OUTPUT_DIR/verification/$model-$plan-container.json"
    verify_final_image \
        "$model" "$plan" "$baseline" "$official" "$image" "$changed_path"
    sudo rm -rf -- "$variant_dir" "$WORK_DIR/verify-$model-$plan"
}

verify_sha256 "$MX5300_IMAGE" "$MX5300_EXPECTED_SHA256"
verify_sha256 "$MX4200_IMAGE" "$MX4200_EXPECTED_SHA256"

mkdir -p "$OUTPUT_DIR/verification"
WORK_DIR=$(mktemp -d "${OUTPUT_DIR}.work.XXXXXX")
build_complete=0
cleanup() {
    if [[ $build_complete -eq 1 ]]; then
        sudo rm -rf -- "$WORK_DIR"
    else
        echo "Build failed; retained work directory: $WORK_DIR" >&2
    fi
}
trap cleanup EXIT

extract_ubi "$MX5300_IMAGE" "$WORK_DIR/mx5300-original.ubi"
MX5300_BASELINE=$(extract_mx5300_rootfs \
    "$WORK_DIR/mx5300-original.ubi" "$WORK_DIR/mx5300-baseline")
extract_ubi "$MX4200_IMAGE" "$WORK_DIR/mx4200-original.ubi"
MX4200_BASELINE=$(extract_mx4200_rootfs \
    "$WORK_DIR/mx4200-original.ubi" "$WORK_DIR/mx4200-baseline")
rm -f "$WORK_DIR/mx5300-original.ubi" "$WORK_DIR/mx4200-original.ubi"
sudo test -x "$MX5300_BASELINE/bin/busybox"
sudo test -x "$MX5300_BASELINE/usr/sbin/fwcc"
sudo test -x "$MX4200_BASELINE/bin/busybox"
sudo test -x "$MX4200_BASELINE/usr/sbin/fwcc"

build_variant \
    mx5300 plan-a "$MX5300_BASELINE" "$MX5300_IMAGE" MX5300 \
    "$MX5300_EXPECTED_SHA256" \
    FW_MX5300_1.1.12.210066_MQTT_PLAN_A_STRICT_BH_CONFIG.img \
    etc/mosquitto/strict.acl
build_variant \
    mx5300 plan-b "$MX5300_BASELINE" "$MX5300_IMAGE" MX5300 \
    "$MX5300_EXPECTED_SHA256" \
    FW_MX5300_1.1.12.210066_MQTT_PLAN_B_OPEN_ACL.img \
    etc/mosquitto/conf.d/default.conf
build_variant \
    mx4200 plan-a "$MX4200_BASELINE" "$MX4200_IMAGE" MX4200 \
    "$MX4200_EXPECTED_SHA256" \
    FW_MX4200_1.0.13.216903_MQTT_PLAN_A_STRICT_BH_CONFIG.img \
    etc/mosquitto/strict.acl
build_variant \
    mx4200 plan-b "$MX4200_BASELINE" "$MX4200_IMAGE" MX4200 \
    "$MX4200_EXPECTED_SHA256" \
    FW_MX4200_1.0.13.216903_MQTT_PLAN_B_OPEN_ACL.img \
    etc/mosquitto/conf.d/default.conf

(
    cd "$OUTPUT_DIR"
    sha256sum ./*.img > SHA256SUMS
)
cp "$OVERLAY_DIR/EXPERIMENT.md" "$OUTPUT_DIR/README.md"
jq -n \
    --arg mx5300_source "$MX5300_EXPECTED_SHA256" \
    --arg mx4200_source "$MX4200_EXPECTED_SHA256" \
    '{
      plans: {
        plan_a: "strict ACL plus network/+/BH/config write permission",
        plan_b: "LAN listener switched from strict.acl to stock open.acl"
      },
      source_sha256: {
        MX5300_1_1_12_210066: $mx5300_source,
        MX4200_1_0_13_216903: $mx4200_source
      },
      validation: {
        final_image_reextracted: true,
        unchanged_entry_metadata_and_content_compared: true,
        changed_file_metadata_compared: true,
        stock_fwcc_verify_signature: "PASS",
        fit_prefix_byte_identical: true
      }
    }' > "$OUTPUT_DIR/BUILD-MANIFEST.json"

build_complete=1
echo "Created and verified four images in: $OUTPUT_DIR"
