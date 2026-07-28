import json
import os
from pathlib import Path
import stat
import subprocess
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[1]
HELPER = (
    REPO_ROOT
    / "firmware-overlays"
    / "ble-parent-steering"
    / "usr"
    / "bin"
    / "meshscope_parent_steer"
)


class BLEParentOverlayTests(unittest.TestCase):
    def setUp(self):
        self.tempdir = tempfile.TemporaryDirectory()
        self.root = Path(self.tempdir.name)
        self.events = self.root / "events"

        self.jsonparse = self._executable(
            "jsonparse",
            """#!/usr/bin/env python3
import json
import sys
data = json.load(sys.stdin)
value = data
for part in sys.argv[-1].split("."):
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
printf '%s\\n' "$*" >> "$MESHSCOPE_TEST_EVENT_LOG"
""",
        )

    def tearDown(self):
        self.tempdir.cleanup()

    def _executable(self, name, contents):
        path = self.root / name
        path.write_text(contents)
        path.chmod(path.stat().st_mode | stat.S_IXUSR)
        return path

    def _run(self, payload, authorization="Basic accepted"):
        environment = os.environ.copy()
        environment.update(
            {
                "MESHSCOPE_JSONPARSE_BIN": str(self.jsonparse),
                "MESHSCOPE_JNAP_CGI": str(self.jnap),
                "MESHSCOPE_SYSEVENT_BIN": str(self.sysevent),
                "MESHSCOPE_TEST_EVENT_LOG": str(self.events),
            }
        )
        return subprocess.run(
            [
                "sh",
                str(HELPER),
                "-u",
                authorization,
                "-j",
                json.dumps(payload, separators=(",", ":")),
            ],
            check=True,
            capture_output=True,
            text=True,
            env=environment,
        )

    def test_authenticated_tuple_writes_exact_stock_sysevents(self):
        result = self._run(
            {
                "band": "5GH",
                "channel": 149,
                "bssid": "AA:BB:CC:DD:EE:FF",
            }
        )

        response = json.loads(result.stdout)
        self.assertEqual("OK", response["result"])
        self.assertEqual("aa:bb:cc:dd:ee:ff", response["output"]["bssid"])
        self.assertEqual(
            [
                "set mqttsub::bh_channel 149",
                "set mqttsub::bh_bssid aa:bb:cc:dd:ee:ff",
                "set backhaul::set_intf 5GH",
            ],
            self.events.read_text().splitlines(),
        )

    def test_bad_authorization_never_writes_sysevents(self):
        result = self._run(
            {
                "band": "5GL",
                "channel": 36,
                "bssid": "00:11:22:33:44:55",
            },
            authorization="Basic rejected",
        )

        self.assertEqual("_ErrorUnauthorized", json.loads(result.stdout)["result"])
        self.assertFalse(self.events.exists())

    def test_invalid_tuple_never_writes_sysevents(self):
        result = self._run(
            {
                "band": "AUTO",
                "channel": "149; reboot",
                "bssid": "not-a-bssid",
            }
        )

        self.assertEqual("ErrorInvalidInput", json.loads(result.stdout)["result"])
        self.assertFalse(self.events.exists())


if __name__ == "__main__":
    unittest.main()
