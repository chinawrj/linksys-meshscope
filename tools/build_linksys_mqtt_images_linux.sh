#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "Usage: $0 MX5300_OFFICIAL.img MX4200_OFFICIAL.img OUTPUT_DIRECTORY" >&2
    exit 64
fi

MX5300_IMAGE=""
MX4200_IMAGE=""
if [[ "$1" != "-" ]]; then MX5300_IMAGE=$(readlink -f "$1"); fi
if [[ "$2" != "-" ]]; then MX4200_IMAGE=$(readlink -f "$2"); fi
OUTPUT_DIR=$(readlink -m "$3")
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
OVERLAY_DIR="$REPO_ROOT/firmware-overlays/mqtt-parent-steering"
UBIREADER_BIN_DIR=${UBIREADER_BIN_DIR:-"$HOME/ubi-tools/bin"}
QEMU_ARM=${QEMU_ARM:-qemu-arm}
MX4200_MKSQUASHFS=${MX4200_MKSQUASHFS:-mksquashfs}

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
if [[ -n "$MX4200_IMAGE" ]]; then
    command -v "$MX4200_MKSQUASHFS" >/dev/null || {
        echo "Missing QSDK-compatible MX4200 mksquashfs: $MX4200_MKSQUASHFS" >&2
        exit 69
    }
fi
for executable in ubireader_extract_files ubireader_extract_images; do
    [[ -x "$UBIREADER_BIN_DIR/$executable" ]] || {
        echo "Missing UBI Reader executable: $UBIREADER_BIN_DIR/$executable" >&2
        exit 69
    }
done
for path in \
    "$SCRIPT_DIR/linksys_mx_repack.py" \
    "$SCRIPT_DIR/compare_filesystem_trees.py" \
    "$OVERLAY_DIR/plan-a-strict-steering-io.patch" \
    "$OVERLAY_DIR/plan-b-open-acl.patch" \
    "$OVERLAY_DIR/plan-c-all-acls-open.patch" \
    "$OVERLAY_DIR/ubi/mx5300.ini" \
    "$OVERLAY_DIR/ubi/mx4200.ini"; do
    [[ -f "$path" ]] || {
        echo "Missing build input: $path" >&2
        exit 66
    }
done
[[ -n "$MX5300_IMAGE" || -n "$MX4200_IMAGE" ]] || {
    echo "Provide at least one official image; use '-' for an omitted model" >&2
    exit 64
}
if [[ -n "$MX5300_IMAGE" ]]; then
    [[ -f "$MX5300_IMAGE" ]] || { echo "Missing MX5300 image" >&2; exit 66; }
fi
if [[ -n "$MX4200_IMAGE" ]]; then
    [[ -f "$MX4200_IMAGE" ]] || { echo "Missing MX4200 image" >&2; exit 66; }
fi
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
            patch_file="$OVERLAY_DIR/plan-a-strict-steering-io.patch"
            ;;
        plan-b)
            patch_file="$OVERLAY_DIR/plan-b-open-acl.patch"
            ;;
        plan-c)
            patch_file="$OVERLAY_DIR/plan-c-all-acls-open.patch"
            ;;
        *)
            echo "Unknown plan: $plan" >&2
            exit 64
            ;;
    esac
    sudo patch --batch --fuzz=0 --no-backup-if-mismatch \
        -p1 -d "$candidate" < "$patch_file"
    while IFS= read -r changed_path; do
        restore_metadata "$baseline" "$candidate" "$changed_path"
        [[ ! -e "$candidate/$changed_path.orig" ]]
    done < <(plan_changed_paths "$plan")
}

plan_changed_paths() {
    case "$1" in
        plan-a)
            printf '%s\n' etc/mosquitto/strict.acl
            ;;
        plan-b)
            printf '%s\n' etc/mosquitto/conf.d/default.conf
            ;;
        plan-c)
            printf '%s\n' \
                etc/mosquitto/conf.d/default.conf \
                etc/mosquitto/default.acl \
                etc/mosquitto/open.acl \
                etc/mosquitto/moderate.acl \
                etc/mosquitto/strict.acl
            ;;
        *)
            echo "Unknown plan: $1" >&2
            return 64
            ;;
    esac
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
    local plan=$3
    local current_size padding_size
    # MX4200's QSDK kernel deliberately changes the on-disk XZ compressor
    # options from the upstream 8-byte structure to a 12-byte structure.
    # Use the QSDK SquashFS 4.2 tool and reproduce the stock 216903 option
    # layout.  Deliberately disable BCJ candidates: all 503 XZ streams in the
    # stock image use plain LZMA2, while enabling the stock option flag's
    # apparent IA64/ARMTHUMB candidates with a newer liblzma caused 64 rebuilt
    # streams to select filters unavailable in the published MX4200 kernel.
    #   flags=0x00090000, bit_opts=0x0090, fb=64, dict=262144.
    # An upstream mksquashfs image passes the Linksys container check but the
    # kernel rejects its compressor options and automatically rolls it back.
    sudo "$MX4200_MKSQUASHFS" "$rootfs" "$build_dir/rootfs.squashfs" \
        -noappend -comp xz -b 262144 -no-xattrs -all-root \
        -Xpreset 9 -Xlc 0 -Xlp 2 -Xpb 2 -Xfb 64 \
        -processors 2 -no-progress >/dev/null
    python3 - "$build_dir/rootfs.squashfs" <<'PY'
import sys
from pathlib import Path

image = Path(sys.argv[1]).read_bytes()[:110]
expected = bytes.fromhex("0c 80 00 00 09 00 90 00 40 00 00 00 04 00")
actual = image[96:110]
if actual != expected:
    raise SystemExit(
        "MX4200 SquashFS has incompatible QSDK XZ options: "
        f"expected {expected.hex(' ')}, got {actual.hex(' ')}"
    )
PY
    if [[ "$plan" == "plan-c" ]]; then
        current_size=$(stat -c '%s' "$build_dir/rootfs.squashfs")
        if (( current_size > MX4200_ORIGINAL_SQUASHFS_SIZE )); then
            echo "Plan C SquashFS exceeds the official volume size" >&2
            exit 65
        fi
        padding_size=$((MX4200_ORIGINAL_SQUASHFS_SIZE - current_size))
        sudo python3 - "$build_dir/rootfs.squashfs" "$padding_size" <<'PY'
import sys
from pathlib import Path

path = Path(sys.argv[1])
remaining = int(sys.argv[2])
chunk = b"\xFF" * min(1024 * 1024, max(remaining, 1))
with path.open("ab") as handle:
    while remaining:
        piece = chunk[: min(len(chunk), remaining)]
        handle.write(piece)
        remaining -= len(piece)
PY
        [[ $(stat -c '%s' "$build_dir/rootfs.squashfs") -eq $MX4200_ORIGINAL_SQUASHFS_SIZE ]]
    fi
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

verify_mx4200_xz_streams() {
    local image=$1
    local report=$2
    python3 - "$image" "$report" <<'PY'
import json
import sys
from collections import Counter
from pathlib import Path

magic = b"\xfd7zXZ\x00"
names = {
    0x03: "Delta",
    0x04: "x86",
    0x05: "PowerPC",
    0x06: "IA64",
    0x07: "ARM",
    0x08: "ARMTHUMB",
    0x09: "SPARC",
    0x21: "LZMA2",
}


def read_vli(header, position):
    value = 0
    shift = 0
    while True:
        if position >= len(header) or shift >= 63:
            raise ValueError("invalid XZ variable-length integer")
        byte = header[position]
        position += 1
        value |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return value, position
        shift += 7


def first_block_chain(data, offset):
    header_size = (data[offset + 12] + 1) * 4
    header = data[offset + 12 : offset + 12 + header_size]
    if len(header) != header_size or header_size < 8:
        raise ValueError("truncated XZ block header")
    flags = header[1]
    if flags & 0x3C:
        raise ValueError(f"reserved XZ block flags set: {flags:#x}")
    position = 2
    if flags & 0x40:
        _, position = read_vli(header, position)
    if flags & 0x80:
        _, position = read_vli(header, position)
    chain = []
    for _ in range((flags & 0x03) + 1):
        filter_id, position = read_vli(header, position)
        properties_size, position = read_vli(header, position)
        position += properties_size
        if position > len(header) - 4:
            raise ValueError("XZ filter properties overrun block header")
        chain.append(names.get(filter_id, f"0x{filter_id:x}"))
    return tuple(chain)


image_path = Path(sys.argv[1])
report_path = Path(sys.argv[2])
data = image_path.read_bytes()
chains = Counter()
offset = 0
while True:
    offset = data.find(magic, offset)
    if offset < 0:
        break
    chains[first_block_chain(data, offset)] += 1
    offset += 1

expected = Counter({("LZMA2",): 503})
report = {
    "image": str(image_path),
    "ok": chains == expected,
    "stream_count": sum(chains.values()),
    "chains": {" + ".join(chain): count for chain, count in sorted(chains.items())},
    "expected": {"LZMA2": 503},
}
report_path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
if chains != expected:
    raise SystemExit(f"incompatible MX4200 XZ filter chains: {dict(chains)}")
PY
}

verify_final_image() {
    local model=$1
    local plan=$2
    local baseline=$3
    local official=$4
    local image=$5
    local changed_path
    local -a compare_args=()
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

    while IFS= read -r changed_path; do
        compare_args+=(--allow-content "$changed_path")
    done < <(plan_changed_paths "$plan")
    sudo python3 "$SCRIPT_DIR/compare_filesystem_trees.py" \
        --baseline "$baseline" \
        --candidate "$final_rootfs" \
        "${compare_args[@]}" \
        --output "$OUTPUT_DIR/verification/$model-$plan-filesystem.json" \
        >/dev/null
    cmp -n "$UBI_OFFSET" "$official" "$image"
    run_fwcc "$baseline" "$image"
    printf 'PASS\n' > "$OUTPUT_DIR/verification/$model-$plan-fwcc.txt"
    if [[ "$model" == "mx4200" ]]; then
        verify_mx4200_xz_streams \
            "$image" "$OUTPUT_DIR/verification/$model-$plan-xz-streams.json"
    fi

    if [[ "$plan" == "plan-a" ]]; then
        for permission in \
            'topic write network/+/BH/config' \
            'topic read network/+/DEVINFO' \
            'topic read network/+/BH/status' \
            'topic write network/status_resend_all' \
            'topic write network/DEVINFO/status_resend_all' \
            'topic write network/BH/status_resend_all'; do
            sudo grep -Fxq "$permission" \
                "$final_rootfs/etc/mosquitto/strict.acl"
        done
        sudo cmp \
            "$baseline/etc/mosquitto/conf.d/default.conf" \
            "$final_rootfs/etc/mosquitto/conf.d/default.conf"
    elif [[ "$plan" == "plan-b" ]]; then
        sudo grep -Fxq 'acl_file %CONF_DIR%/open.acl' \
            "$final_rootfs/etc/mosquitto/conf.d/default.conf"
        sudo cmp \
            "$baseline/etc/mosquitto/strict.acl" \
            "$final_rootfs/etc/mosquitto/strict.acl"
    else
        sudo grep -Fxq 'acl_file %CONF_DIR%/open.acl' \
            "$final_rootfs/etc/mosquitto/conf.d/default.conf"
        for acl_name in default open moderate strict; do
            sudo grep -Fxq 'topic readwrite #' \
                "$final_rootfs/etc/mosquitto/$acl_name.acl"
        done
        [[ $(stat -c '%s' "$image") -eq $(stat -c '%s' "$official") ]]
        [[ $(stat -c '%s' "$final_ubi") -eq $MX4200_ORIGINAL_UBI_SIZE ]]
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
    local variant_dir="$WORK_DIR/$model-$plan"
    local rootfs="$variant_dir/rootfs"
    local image="$OUTPUT_DIR/$output_name"
    mkdir -p "$variant_dir"
    apply_plan "$baseline" "$rootfs" "$plan"
    if [[ "$model" == "mx5300" ]]; then
        build_mx5300_ubi "$rootfs" "$variant_dir"
    else
        build_mx4200_ubi "$rootfs" "$variant_dir" "$plan"
    fi
    python3 "$SCRIPT_DIR/linksys_mx_repack.py" \
        --original "$official" \
        --ubi "$variant_dir/rootfs.ubi" \
        --output "$image" \
        --expected-sku "$sku" \
        --expected-original-sha256 "$expected_sha" \
        > "$OUTPUT_DIR/verification/$model-$plan-container.json"
    verify_final_image \
        "$model" "$plan" "$baseline" "$official" "$image"
    sudo rm -rf -- "$variant_dir" "$WORK_DIR/verify-$model-$plan"
}

if [[ -n "$MX5300_IMAGE" ]]; then
    verify_sha256 "$MX5300_IMAGE" "$MX5300_EXPECTED_SHA256"
fi
if [[ -n "$MX4200_IMAGE" ]]; then
    verify_sha256 "$MX4200_IMAGE" "$MX4200_EXPECTED_SHA256"
fi

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

image_count=0
if [[ -n "$MX5300_IMAGE" ]]; then
    extract_ubi "$MX5300_IMAGE" "$WORK_DIR/mx5300-original.ubi"
    MX5300_BASELINE=$(extract_mx5300_rootfs \
        "$WORK_DIR/mx5300-original.ubi" "$WORK_DIR/mx5300-baseline")
    rm -f "$WORK_DIR/mx5300-original.ubi"
    sudo test -x "$MX5300_BASELINE/bin/busybox"
    sudo test -x "$MX5300_BASELINE/usr/sbin/fwcc"
    build_variant \
        mx5300 plan-a "$MX5300_BASELINE" "$MX5300_IMAGE" MX5300 \
        "$MX5300_EXPECTED_SHA256" \
        FW_MX5300_1.1.12.210066_MQTT_PLAN_A_STRICT_STEERING_IO.img
    build_variant \
        mx5300 plan-b "$MX5300_BASELINE" "$MX5300_IMAGE" MX5300 \
        "$MX5300_EXPECTED_SHA256" \
        FW_MX5300_1.1.12.210066_MQTT_PLAN_B_OPEN_ACL.img
    image_count=$((image_count + 2))
fi
if [[ -n "$MX4200_IMAGE" ]]; then
    extract_ubi "$MX4200_IMAGE" "$WORK_DIR/mx4200-original.ubi"
    MX4200_ORIGINAL_UBI_SIZE=$(stat -c '%s' "$WORK_DIR/mx4200-original.ubi")
    MX4200_BASELINE=$(extract_mx4200_rootfs \
        "$WORK_DIR/mx4200-original.ubi" "$WORK_DIR/mx4200-baseline")
    MX4200_ORIGINAL_SQUASHFS=$(find "$WORK_DIR/mx4200-baseline/volumes" \
        -type f -name '*vol-squashfs.ubifs' -print -quit)
    [[ -n "$MX4200_ORIGINAL_SQUASHFS" ]]
    MX4200_ORIGINAL_SQUASHFS_SIZE=$(stat -c '%s' "$MX4200_ORIGINAL_SQUASHFS")
    rm -f "$WORK_DIR/mx4200-original.ubi"
    sudo test -x "$MX4200_BASELINE/bin/busybox"
    sudo test -x "$MX4200_BASELINE/usr/sbin/fwcc"
    build_variant \
        mx4200 plan-a "$MX4200_BASELINE" "$MX4200_IMAGE" MX4200 \
        "$MX4200_EXPECTED_SHA256" \
        FW_MX4200_1.0.13.216903_MQTT_PLAN_A_STRICT_STEERING_IO.img
    build_variant \
        mx4200 plan-b "$MX4200_BASELINE" "$MX4200_IMAGE" MX4200 \
        "$MX4200_EXPECTED_SHA256" \
        FW_MX4200_1.0.13.216903_MQTT_PLAN_B_OPEN_ACL.img
    build_variant \
        mx4200 plan-c "$MX4200_BASELINE" "$MX4200_IMAGE" MX4200 \
        "$MX4200_EXPECTED_SHA256" \
        FW_MX4200_1.0.13.216903_MQTT_PLAN_C2_ALL_ACLS_QSDK_NO_BCJ.img
    image_count=$((image_count + 3))
fi

(
    cd "$OUTPUT_DIR"
    sha256sum ./*.img > SHA256SUMS
)
cp "$OVERLAY_DIR/EXPERIMENT.md" "$OUTPUT_DIR/README.md"
jq -n \
    --arg mx5300_source "${MX5300_IMAGE:+$MX5300_EXPECTED_SHA256}" \
    --arg mx4200_source "${MX4200_IMAGE:+$MX4200_EXPECTED_SHA256}" \
    '{
      plans: {
        plan_a: "strict ACL plus BH/config write, DEVINFO/BH status read, and all required status resend permissions",
        plan_b: "LAN listener switched from strict.acl to stock open.acl",
        plan_c: "all bundled ACL files grant readwrite #; LAN listener uses open.acl; MX4200 UBI and IMG sizes match stock"
      },
      source_sha256: {
        MX5300_1_1_12_210066: (if $mx5300_source == "" then null else $mx5300_source end),
        MX4200_1_0_13_216903: (if $mx4200_source == "" then null else $mx4200_source end)
      },
      validation: {
        final_image_reextracted: true,
        unchanged_entry_metadata_and_content_compared: true,
        changed_file_metadata_compared: true,
        stock_fwcc_verify_signature: "PASS",
        fit_prefix_byte_identical: true,
        mx4200_plan_c_fixed_size: true
      }
    }' > "$OUTPUT_DIR/BUILD-MANIFEST.json"

build_complete=1
echo "Created and verified $image_count image(s) in: $OUTPUT_DIR"
