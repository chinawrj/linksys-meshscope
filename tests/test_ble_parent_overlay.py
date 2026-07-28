import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[1]
OVERLAY = REPO_ROOT / "firmware-overlays" / "ble-parent-steering"
DISPATCHER = OVERLAY / "usr" / "bin" / "meshscope_ble_dispatch"
HOOK_PATCH = OVERLAY / "btjnap-hook.patch"

DEVICE_ID = "11111111-2222-3333-4444-555555555555"
PARENT_ID = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"


class BLEParentOverlayTests(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.root = Path(self.tempdir.name)
        self.events = self.root / "events"

        self.jsonparse = self._executable(
            "jsonparse",
            """#!/usr/bin/env python3
import json
from pathlib import Path
import sys

args = sys.argv[1:]
if args and args[0] == "-f":
    data = json.loads(Path(args[1]).read_text())
    key = args[2]
else:
    data = json.load(sys.stdin)
    key = args[-1]
value = data
for part in key.split("."):
    value = value[part]
if isinstance(value, bool):
    print("true" if value else "false")
else:
    print(value)
""",
        )
        self.jnap = self._executable(
            "jnap",
            """#!/bin/sh
if [ "${HTTP_X_JNAP_AUTHORIZATION:-}" = "Basic accepted" ]; then
    printf '%s\\n' '{"result":"OK"}'
else
    printf '%s\\n' '{"result":"_ErrorUnauthorized"}'
fi
""",
        )
        self.sysevent = self._executable(
            "sysevent",
            """#!/bin/sh
if [ "$1" = "set" ]; then
    printf '%s\\n' "$*" >> "$MESHSCOPE_TEST_EVENT_LOG"
    exit 0
fi
if [ "$1" != "get" ]; then
    exit 1
fi
case "$2" in
    backhaul::status) printf '%s\\n' up ;;
    backhaul::media) printf '%s\\n' 2 ;;
    backhaul::intf) printf '%s\\n' ath11 ;;
    backhaul::preferred_chan) printf '%s\\n' 44 ;;
    backhaul::preferred_bssid) printf '%s\\n' 00:11:22:33:44:55 ;;
    backhaul::set_intf) printf '%s\\n' 5GL ;;
    mqttsub::bh_channel) printf '%s\\n' 44 ;;
    mqttsub::bh_bssid) printf '%s\\n' 00:11:22:33:44:55 ;;
esac
""",
        )
        self.syscfg = self._executable(
            "syscfg",
            f"""#!/bin/sh
case "$2" in
    device::uuid) printf '%s\\n' {DEVICE_ID} ;;
    smart_mode::mode) printf '%s\\n' 1 ;;
esac
""",
        )

    def tearDown(self):
        self.tempdir.cleanup()

    def _executable(self, name, contents):
        path = self.root / name
        path.write_text(contents)
        path.chmod(path.stat().st_mode | stat.S_IXUSR)
        return path

    def _run(
        self,
        action,
        payload=None,
        authorization="Basic accepted",
    ):
        environment = os.environ.copy()
        environment.update(
            {
                "MESHSCOPE_JSONPARSE_BIN": str(self.jsonparse),
                "MESHSCOPE_JNAP_CGI": str(self.jnap),
                "MESHSCOPE_SYSEVENT_BIN": str(self.sysevent),
                "MESHSCOPE_SYSCFG_BIN": str(self.syscfg),
                "MESHSCOPE_STATE_DIR": str(self.root),
                "MESHSCOPE_TEST_EVENT_LOG": str(self.events),
            }
        )
        return subprocess.run(
            [
                "sh",
                str(DISPATCHER),
                "-a",
                f"http://linksys.com/jnap/nodes/meshscope/{action}",
                "-u",
                authorization,
                "-j",
                json.dumps(payload or {}, separators=(",", ":")),
            ],
            check=True,
            capture_output=True,
            text=True,
            env=environment,
        )

    def _steer_payload(self):
        return {
            "requestId": "fixture-1",
            "expectedDeviceID": DEVICE_ID,
            "parentDeviceID": PARENT_ID,
            "band": "5GH",
            "channel": 149,
            "bssid": "AA:BB:CC:DD:EE:FF",
        }

    def test_capabilities_expose_bounded_advanced_actions(self):
        result = json.loads(self._run("GetCapabilities").stdout)

        self.assertEqual("OK", result["result"])
        self.assertEqual(
            [
                "GetCapabilities",
                "GetBackhaulStatus",
                "SteerToParent",
                "RollbackParent",
            ],
            result["output"]["actions"],
        )
        self.assertTrue(result["output"]["stockJnapPassthrough"])
        self.assertFalse(result["output"]["persistentParentPin"])
        self.assertFalse(self.events.exists())

    def test_status_preserves_current_and_requested_backhaul_data(self):
        result = json.loads(self._run("GetBackhaulStatus").stdout)
        output = result["output"]

        self.assertEqual(DEVICE_ID, output["deviceID"])
        self.assertEqual(1, output["mode"])
        self.assertEqual(
            {
                "status": "up",
                "media": "2",
                "interface": "ath11",
                "band": "5GL",
                "channel": 44,
                "bssid": "00:11:22:33:44:55",
                "tupleSource": "backhaul::preferred_*",
            },
            output["backhaul"],
        )
        self.assertEqual(
            {
                "band": "5GL",
                "channel": 44,
                "bssid": "00:11:22:33:44:55",
                "tupleSource": "mqttsub::bh_*",
            },
            output["requested"],
        )
        self.assertFalse(output["rollbackAvailable"])
        self.assertEqual({"available": False}, output["rollback"])
        self.assertIsNone(output["lastRequest"])
        self.assertFalse(self.events.exists())

    def test_authenticated_tuple_writes_exact_stock_sysevents_and_snapshot(self):
        result = self._run("SteerToParent", self._steer_payload())

        response = json.loads(result.stdout)
        self.assertEqual("OK", response["result"])
        self.assertEqual(PARENT_ID, response["output"]["parentDeviceID"])
        self.assertEqual("aa:bb:cc:dd:ee:ff", response["output"]["bssid"])
        self.assertEqual(
            [
                "set mqttsub::bh_channel 149",
                "set mqttsub::bh_bssid aa:bb:cc:dd:ee:ff",
                "set backhaul::set_intf 5GH",
            ],
            self.events.read_text().splitlines(),
        )
        previous = json.loads(
            (self.root / "meshscope_ble_parent.previous.json").read_text()
        )
        self.assertEqual(
            {
                "band": "5GL",
                "channel": 44,
                "bssid": "00:11:22:33:44:55",
            },
            previous,
        )
        status = json.loads(self._run("GetBackhaulStatus").stdout)["output"]
        self.assertTrue(status["rollbackAvailable"])
        self.assertEqual("5GL", status["rollback"]["band"])
        self.assertEqual("steer", status["lastRequest"]["operation"])
        self.assertEqual(PARENT_ID, status["lastRequest"]["parentDeviceID"])

    def test_rollback_replays_saved_tuple_once(self):
        self._run("SteerToParent", self._steer_payload())
        result = self._run(
            "RollbackParent",
            {
                "requestId": "fixture-rollback",
                "expectedDeviceID": DEVICE_ID,
            },
        )

        response = json.loads(result.stdout)
        self.assertEqual("OK", response["result"])
        self.assertEqual("5GL", response["output"]["band"])
        self.assertEqual(
            [
                "set mqttsub::bh_channel 149",
                "set mqttsub::bh_bssid aa:bb:cc:dd:ee:ff",
                "set backhaul::set_intf 5GH",
                "set mqttsub::bh_channel 44",
                "set mqttsub::bh_bssid 00:11:22:33:44:55",
                "set backhaul::set_intf 5GL",
            ],
            self.events.read_text().splitlines(),
        )
        self.assertFalse(
            (self.root / "meshscope_ble_parent.previous.json").exists()
        )
        second = json.loads(
            self._run(
                "RollbackParent",
                {
                    "requestId": "fixture-rollback-2",
                    "expectedDeviceID": DEVICE_ID,
                },
            ).stdout
        )
        self.assertEqual("ErrorNoRollback", second["result"])

    def test_bad_authorization_never_writes_sysevents(self):
        result = self._run(
            "SteerToParent",
            self._steer_payload(),
            authorization="Basic rejected",
        )

        self.assertEqual("_ErrorUnauthorized", json.loads(result.stdout)["result"])
        self.assertFalse(self.events.exists())

    def test_wrong_child_or_invalid_tuple_never_writes_sysevents(self):
        wrong_child = self._steer_payload()
        wrong_child["expectedDeviceID"] = (
            "99999999-2222-3333-4444-555555555555"
        )
        result = self._run("SteerToParent", wrong_child)
        self.assertEqual("ErrorInvalidInput", json.loads(result.stdout)["result"])

        malformed = self._steer_payload()
        malformed.update(
            {
                "band": "AUTO",
                "channel": "149; reboot",
                "bssid": "not-a-bssid",
            }
        )
        result = self._run("SteerToParent", malformed)
        self.assertEqual("ErrorInvalidInput", json.loads(result.stdout)["result"])
        self.assertFalse(self.events.exists())

    def test_existing_mutation_lock_rejects_request_without_events(self):
        (self.root / "meshscope_ble_parent.lock").mkdir()

        result = json.loads(
            self._run("SteerToParent", self._steer_payload()).stdout
        )

        self.assertEqual("ErrorBusy", result["result"])
        self.assertFalse(self.events.exists())

    def test_stock_wrapper_patch_forwards_only_meshscope_action(self):
        wrapper_root = self.root / "wrapper"
        (wrapper_root / "usr" / "bin").mkdir(parents=True)
        wrapper = wrapper_root / "usr" / "bin" / "btjnap"
        wrapper.write_text(
            """#!/bin/sh
while getopts ":a:u:j:" opt; do
    case $opt in
        a) ACTION=$OPTARG ;;
        u) AUTH=$OPTARG ;;
        j) JSON=$OPTARG ;;
    esac
done
ACTION=`echo $ACTION | sed -e "s/X-JNAP-Action[ ]*:[ ]*//"`
AUTH=`echo $AUTH | sed -e "s/X-JNAP-Authorization[ ]*:[ ]*//"`

export JNAP_CGI_MODULES_PATH="/JNAP/modules/wan"
"""
        )
        subprocess.run(
            ["patch", "-p0", "-i", str(HOOK_PATCH)],
            cwd=wrapper_root,
            check=True,
            capture_output=True,
            text=True,
        )
        capture = self._executable(
            "dispatch-capture",
            """#!/bin/sh
printf '%s\\n' "$*" > "$MESHSCOPE_DISPATCH_ARGS"
""",
        )
        args_file = self.root / "dispatch-args"
        environment = os.environ.copy()
        environment.update(
            {
                "MESHSCOPE_BLE_DISPATCH_BIN": str(capture),
                "MESHSCOPE_DISPATCH_ARGS": str(args_file),
            }
        )
        subprocess.run(
            [
                "sh",
                str(wrapper),
                "-a",
                "X-JNAP-Action: http://linksys.com/jnap/nodes/meshscope/GetBackhaulStatus",
                "-u",
                "Basic fixture",
                "-j",
                "{}",
            ],
            check=True,
            env=environment,
        )

        self.assertEqual(
            "-a http://linksys.com/jnap/nodes/meshscope/GetBackhaulStatus "
            "-u Basic fixture -j {}",
            args_file.read_text().strip(),
        )

    def test_dispatcher_is_executable_in_repository(self):
        self.assertTrue(DISPATCHER.stat().st_mode & stat.S_IXUSR)
        self.assertIsNotNone(shutil.which("sh"))


if __name__ == "__main__":
    unittest.main()
