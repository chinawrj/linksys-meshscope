import json
import socket
import struct
import threading
import unittest
from unittest import mock

import linksys_mqtt_parent as mqtt_parent


class FakeProbeBroker:
    def __init__(self):
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.socket.bind(("127.0.0.1", 0))
        self.socket.listen(1)
        self.port = self.socket.getsockname()[1]
        self.error = None
        self.thread = threading.Thread(target=self._run, daemon=True)

    @staticmethod
    def _packet(connection):
        first = connection.recv(1)[0]
        multiplier = 1
        remaining = 0
        while True:
            byte = connection.recv(1)[0]
            remaining += (byte & 0x7F) * multiplier
            if not byte & 0x80:
                break
            multiplier *= 128
        body = bytearray()
        while len(body) < remaining:
            body.extend(connection.recv(remaining - len(body)))
        return first, bytes(body)

    def _run(self):
        try:
            connection, _address = self.socket.accept()
            with connection:
                first, _body = self._packet(connection)
                assert first == 0x10
                connection.sendall(b"\x20\x02\x00\x00")

                first, body = self._packet(connection)
                assert first == 0x82
                packet_id = body[:2]
                connection.sendall(b"\x90\x03" + packet_id + b"\x00")

                for expected_topic in mqtt_parent.STATUS_REFRESH_TOPICS:
                    first, body = self._packet(connection)
                    assert first == 0x32
                    topic_size = struct.unpack("!H", body[:2])[0]
                    topic_end = 2 + topic_size
                    topic = body[2:topic_end].decode("utf-8")
                    assert topic == expected_topic
                    publish_id = body[topic_end : topic_end + 2]
                    connection.sendall(b"\x40\x02" + publish_id)

                topic = b"network/TEST-NODE/DEVINFO"
                payload = json.dumps(
                    {"uuid": "TEST-NODE", "data": {"userAp5GH_channel": "149"}}
                ).encode()
                response = struct.pack("!H", len(topic)) + topic + payload
                connection.sendall(
                    b"\x30" + mqtt_parent._mqtt_varint(len(response)) + response
                )
                self._packet(connection)
        except Exception as exc:  # pragma: no cover - surfaced by the test
            self.error = exc
        finally:
            self.socket.close()

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *_args):
        self.thread.join(timeout=2)
        if self.error:
            raise self.error


class LinksysMQTTParentTests(unittest.TestCase):
    def setUp(self):
        self.main = {
            "id": "11111111-1111-4111-8111-111111111111",
            "name": "Gateway",
            "online": True,
            "isAuthority": True,
            "parentId": None,
        }
        self.yard = {
            "id": "22222222-2222-4222-8222-222222222222",
            "name": "Test Relay",
            "online": True,
            "isAuthority": False,
            "parentId": self.main["id"],
        }
        self.child = {
            "id": "33333333-3333-4333-8333-333333333333",
            "name": "Test Leaf",
            "online": True,
            "isAuthority": False,
            "connectionType": "Wireless",
            "parentId": self.main["id"],
            "parentName": "Main",
        }
        self.topology = {"nodes": [self.main, self.yard, self.child]}

    def test_varint_reference_values(self):
        self.assertEqual(b"\x00", mqtt_parent._mqtt_varint(0))
        self.assertEqual(b"\x7f", mqtt_parent._mqtt_varint(127))
        self.assertEqual(b"\x80\x01", mqtt_parent._mqtt_varint(128))
        self.assertEqual(b"\xff\x7f", mqtt_parent._mqtt_varint(16_383))

    def test_acl_probe_uses_only_strict_plan_a_round_trip(self):
        with FakeProbeBroker() as broker, mock.patch.object(mqtt_parent, "MQTT_PORT", broker.port):
            report = mqtt_parent.probe_acl("127.0.0.1", timeout=2)
        self.assertTrue(report["available"])
        self.assertTrue(report["roundTrip"])
        self.assertIn("DEVINFO", report["proof"])

    def test_radio_target_normalizes_uppercase_devinfo_uuid(self):
        records = {
            self.yard["id"]: {
                "uuid": self.yard["id"].upper(),
                "data": {
                    "userAp5GH_channel": "149",
                    "userAp5GH_bssid": "02:11:22:33:44:55",
                },
            }
        }
        target = mqtt_parent.radio_target(self.yard, "5GH", records)
        self.assertEqual(149, target.channel)
        self.assertEqual("02:11:22:33:44:55", target.bssid)

    def test_preflight_rejects_current_parent_and_descendant(self):
        with self.assertRaisesRegex(mqtt_parent.MQTTParentError, "already connected"):
            mqtt_parent.preflight(self.topology, self.child["id"], self.main["id"])
        self.yard["parentId"] = self.child["id"]
        with self.assertRaisesRegex(mqtt_parent.MQTTParentError, "descendant"):
            mqtt_parent.preflight(self.topology, self.child["id"], self.yard["id"])

    def test_publish_uses_uppercase_wire_uuid_and_nested_data(self):
        target = mqtt_parent.RadioTarget(
            parent_id=self.yard["id"],
            parent_name=self.yard["name"],
            band="5GH",
            channel=149,
            bssid="02:11:22:33:44:55",
        )
        client = mock.MagicMock()
        client.__enter__.return_value = client
        with mock.patch.object(mqtt_parent, "MQTTClient", return_value=client):
            report = mqtt_parent.publish_parent_request("192.0.2.1", self.child["id"], target)
        topic, payload = client.publish.call_args.args[:2]
        document = json.loads(payload)
        self.assertEqual(
            f"network/{self.child['id'].upper()}/BH/config",
            topic,
        )
        self.assertEqual(self.child["id"].upper(), document["uuid"])
        self.assertEqual("02:11:22:33:44:55", document["data"]["bssid"])
        self.assertTrue(report["accepted"])


if __name__ == "__main__":
    unittest.main()
