import base64
import importlib.util
from pathlib import Path
import sys
import unittest


REPO_ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = REPO_ROOT / "tools" / "linksys_ble_jnap.py"
SPEC = importlib.util.spec_from_file_location("linksys_ble_jnap", MODULE_PATH)
ble_jnap = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = ble_jnap
SPEC.loader.exec_module(ble_jnap)


class LinksysBLEJNAPTests(unittest.TestCase):
    def test_app_reference_request_and_big_endian_length_frame(self):
        request = ble_jnap.build_request(
            "/nodes/setup/GetVersionInfo",
            {"probe": True},
        )
        expected = (
            "Host:www.linksyssmartwifi.com\n"
            "X-JNAP-Action:http://linksys.com/jnap/nodes/setup/GetVersionInfo\n"
            "X-JNAP-Authorization:Basic YWRtaW46YWRtaW4=\n"
            "Content-Type:application/json; charset=utf-8\n"
            '{"probe":true}\n'
        ).encode("ascii")

        self.assertEqual(expected, request.request)
        self.assertEqual(
            b"\x00\x03" + len(expected).to_bytes(2, "big"),
            request.length_frame,
        )

    def test_transcript_matches_control_point_state_machine(self):
        request = ble_jnap.build_request("/core/IsServiceSupported")
        transcript = request.transcript()

        self.assertEqual(ble_jnap.JNAP_SERVICE_UUID, transcript["service_uuid"])
        self.assertEqual(
            [
                "subscribe",
                "write",
                "write",
                "write",
                "write",
                "write",
                "unsubscribe",
                "read",
            ],
            [step["operation"] for step in transcript["steps"]],
        )
        self.assertEqual(
            "AAE=", transcript["steps"][1]["value_base64"]
        )
        self.assertEqual(
            "AAI=", transcript["steps"][2]["value_base64"]
        )
        self.assertEqual(
            "BAAAAA==", transcript["steps"][4]["after_notification_base64"]
        )
        self.assertEqual(
            request.request,
            base64.b64decode(transcript["steps"][4]["value_base64"]),
        )

    def test_redaction_omits_authorized_request_frame(self):
        request = ble_jnap.build_request(
            "/nodes/topologyoptimization/SteerNodeToParent",
            {"band": "5GH", "channel": 149, "bssid": "00:11:22:33:44:55"},
            ble_jnap.basic_authorization("admin", "not-a-real-password"),
        )
        transcript = request.transcript(include_request=False)

        self.assertNotIn("value_base64", transcript["steps"][4])
        self.assertEqual(len(request.request), transcript["request_length"])

    def test_rejects_header_injection_and_bad_authorization(self):
        with self.assertRaisesRegex(ValueError, "newline"):
            ble_jnap.build_request("/core/Reboot\nX-Evil: yes")
        with self.assertRaisesRegex(ValueError, "Basic"):
            ble_jnap.build_request("/core/Reboot", authorization="Bearer token")
        with self.assertRaisesRegex(ValueError, "colon"):
            ble_jnap.basic_authorization("bad:user", "password")


if __name__ == "__main__":
    unittest.main()
