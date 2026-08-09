import importlib.util
import json
from pathlib import Path
from unittest import mock
import sys
import unittest


REPO_ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = REPO_ROOT / "tools" / "linksys_mqtt_steer.py"
SPEC = importlib.util.spec_from_file_location("linksys_mqtt_steer", MODULE_PATH)
steer = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = steer
SPEC.loader.exec_module(steer)


class LinksysMQTTSteerTests(unittest.TestCase):
    def setUp(self):
        self.main = {
            "id": "11111111-1111-4111-8111-111111111111",
            "name": "Gateway",
            "ipAddress": "192.0.2.1",
            "online": True,
            "isAuthority": True,
            "parentId": None,
        }
        self.yard = {
            "id": "22222222-2222-4222-8222-222222222222",
            "name": "Test Relay",
            "ipAddress": "192.0.2.2",
            "online": True,
            "isAuthority": False,
            "parentId": self.main["id"],
        }
        self.big_tree = {
            "id": "33333333-3333-4333-8333-333333333333",
            "name": "Test Leaf",
            "ipAddress": "192.0.2.3",
            "online": True,
            "isAuthority": False,
            "connectionType": "Wireless",
            "parentId": self.yard["id"],
        }
        self.road = {
            "id": "44444444-4444-4444-8444-444444444444",
            "name": "Test Downstream",
            "ipAddress": "192.0.2.4",
            "online": True,
            "isAuthority": False,
            "parentId": self.big_tree["id"],
        }
        self.topology = {
            "nodes": [self.main, self.yard, self.big_tree, self.road]
        }

    def test_parses_compact_mqtt_devinfo_by_payload_uuid(self):
        messages = steer.parse_mqtt_lines(
            'network/master/DEVINFO {"uuid":"11111111-1111-4111-8111-111111111111",'
            '"type":"status","data":{"userAp5GH_channel":"161"}}\n'
            "ignored not-json\n"
        )
        records = steer.devinfo_by_uuid(messages)

        self.assertEqual([self.main["id"]], list(records))
        self.assertEqual("161", records[self.main["id"]]["data"]["userAp5GH_channel"])

    def test_resolves_uppercase_mqtt_uuid_for_lowercase_jnap_node(self):
        records = steer.devinfo_by_uuid(
            [
                (
                    "network/22222222-2222-4222-8222-222222222222/DEVINFO",
                    {
                        "uuid": "22222222-2222-4222-8222-222222222222",
                        "data": {
                            "userAp5GH_channel": "149",
                            "userAp5GH_bssid": "02:11:22:33:44:55",
                        },
                    },
                )
            ]
        )

        result = steer.resolve_parent_tuple(self.yard, "5GH", records, {})

        self.assertEqual("fresh MQTT DEVINFO", result.source)
        self.assertEqual(self.yard["id"], next(iter(records)))

    def test_parses_mosquitto_json_format_with_multiline_payload(self):
        payload = json.dumps(
            {
                "uuid": "22222222-2222-4222-8222-222222222222",
                "data": {"userAp5GH_channel": "149"},
            },
            indent=2,
        )
        line = json.dumps(
            {
                "tst": "2026-08-09T07:46:19Z",
                "topic": "network/22222222-2222-4222-8222-222222222222/DEVINFO",
                "qos": 0,
                "retain": 0,
                "payload": payload,
            }
        )

        records = steer.devinfo_by_uuid(steer.parse_mqtt_lines(line + "\n"))

        self.assertEqual("149", records[self.yard["id"]]["data"]["userAp5GH_channel"])

    def test_prefers_valid_devinfo_tuple(self):
        records = {
            self.yard["id"]: {
                "uuid": self.yard["id"],
                "data": {
                    "userAp5GH_channel": "149",
                    "userAp5GH_bssid": "02:11:22:33:44:55",
                },
            }
        }
        result = steer.resolve_parent_tuple(self.yard, "5GH", records, {})

        self.assertEqual("fresh MQTT DEVINFO", result.source)
        self.assertEqual(149, result.channel)
        self.assertEqual("02:11:22:33:44:55", result.bssid)

    def test_resolves_one_consistent_observed_parent_radio(self):
        raw = {
            "nodes/diagnostics/GetBackhaulInfo": {
                "result": "OK",
                "output": {
                    "backhaulDevices": [
                        {
                            "parentIPAddress": "192.0.2.1",
                            "wirelessConnectionInfo": {
                                "radioID": "5GH",
                                "channel": 161,
                                "apBSSID": "02:AA:BB:CC:DD:EE",
                            },
                        },
                        {
                            "parentIPAddress": "192.0.2.1",
                            "wirelessConnectionInfo": {
                                "radioID": "5GH",
                                "channel": 161,
                                "apBSSID": "02:AA:BB:CC:DD:EE",
                            },
                        },
                    ]
                },
            }
        }
        result = steer.resolve_parent_tuple(self.main, "5GH", {}, raw)

        self.assertEqual("fresh JNAP observed child link", result.source)
        self.assertEqual(161, result.channel)
        self.assertEqual("02:AA:BB:CC:DD:EE", result.bssid)

    def test_preflight_rejects_descendant_as_parent(self):
        with self.assertRaisesRegex(steer.SteeringError, "descendant"):
            steer.preflight(self.topology, self.big_tree, self.road)

    def test_preflight_rejects_current_parent(self):
        with self.assertRaisesRegex(steer.SteeringError, "already connected"):
            steer.preflight(self.topology, self.big_tree, self.yard)

    def test_find_node_accepts_exact_name_or_uuid(self):
        self.assertIs(self.big_tree, steer.find_node(self.topology, "test leaf"))
        self.assertIs(
            self.yard, steer.find_node(self.topology, self.yard["id"])
        )

    def test_publish_uses_canonical_uppercase_uuid_topic(self):
        target = steer.RadioTuple(
            parent_id=self.yard["id"],
            parent_name=self.yard["name"],
            band="5GH",
            channel=149,
            bssid="02:11:22:33:44:55",
            source="test",
        )
        expected_uuid = self.big_tree["id"].upper()
        response = {
            "topic": f"network/{expected_uuid}/BH/config",
            "payload": {"uuid": expected_uuid},
        }
        with mock.patch.object(
            steer.mqtt_parent,
            "publish_parent_request",
            return_value=response,
        ) as publish:
            result = steer.publish_steering("192.0.2.1", self.big_tree["id"], target)

        self.assertEqual(f"network/{expected_uuid}/BH/config", result["topic"])
        self.assertEqual(expected_uuid, result["payload"]["uuid"])
        publish.assert_called_once()


if __name__ == "__main__":
    unittest.main()
