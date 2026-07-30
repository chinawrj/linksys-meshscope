import unittest
from unittest.mock import patch

import linksys_mesh_app


def response(output):
    return {"result": "OK", "output": output}


class LinksysMeshAppTest(unittest.TestCase):
    def test_router_action_allowlist_is_read_only(self):
        read_only_prefixes = ("Get", "Check")
        for action in linksys_mesh_app.READ_ONLY_ACTIONS:
            operation = action.rsplit("/", 1)[-1]
            self.assertTrue(
                operation.startswith(read_only_prefixes),
                f"non-read-only action was added to the allowlist: {action}",
            )

        with self.assertRaisesRegex(linksys_mesh_app.RouterError, "non-read-only"):
            linksys_mesh_app.jnap_call(
                linksys_mesh_app.RouterSession(host="10.0.0.1", password="test-only"),
                "nodes/topologyoptimization/SetTopologyOptimizationSettings2",
            )
        with self.assertRaisesRegex(linksys_mesh_app.RouterError, "unapproved"):
            linksys_mesh_app.jnap_mutating_call(
                linksys_mesh_app.RouterSession(host="10.0.0.1", password="test-only"),
                "core/FactoryReset",
            )

    def test_normalize_topology_includes_parent_clients_and_steering(self):
        master_id = "master"
        child_id = "child"
        client_id = "client"
        raw = {
            "core/GetDeviceInfo": response(
                {
                    "manufacturer": "Linksys",
                    "modelNumber": "MX42",
                    "firmwareVersion": "1.0.13.216903",
                }
            ),
            "devicelist/GetDevices3": response(
                {
                    "revision": 3,
                    "devices": [
                        {
                            "deviceID": master_id,
                            "friendlyName": "Main",
                            "isAuthority": True,
                            "nodeType": "Master",
                            "model": {"modelNumber": "MX42"},
                            "unit": {"firmwareVersion": "1.0.13.216903"},
                            "connections": [
                                {"macAddress": "00:00:00:00:00:01", "ipAddress": "10.0.0.1"}
                            ],
                        },
                        {
                            "deviceID": child_id,
                            "friendlyName": "Patio",
                            "nodeType": "Slave",
                            "model": {"modelNumber": "WHW03"},
                            "unit": {"firmwareVersion": "2.1.20"},
                            "connections": [
                                {"macAddress": "00:00:00:00:00:02", "ipAddress": "10.0.0.2"}
                            ],
                        },
                        {
                            "deviceID": client_id,
                            "friendlyName": "Phone",
                            "model": {"modelNumber": "iPhone"},
                            "unit": {},
                            "knownMACAddresses": ["00:00:00:00:00:03"],
                            "connections": [
                                {
                                    "macAddress": "00:00:00:00:00:03",
                                    "ipAddress": "10.0.0.3",
                                    "parentDeviceID": child_id,
                                }
                            ],
                        },
                    ],
                }
            ),
            "nodes/diagnostics/GetBackhaulInfo": response(
                {
                    "backhaulDevices": [
                        {
                            "deviceUUID": child_id,
                            "ipAddress": "10.0.0.2",
                            "parentIPAddress": "10.0.0.1",
                            "connectionType": "Wireless",
                            "wirelessConnectionInfo": {
                                "radioID": "5GH",
                                "channel": 149,
                                "stationRSSI": -58,
                            },
                            "speedMbps": "512.5",
                        }
                    ]
                }
            ),
            "networkconnections/GetNetworkConnections2": response({"connections": []}),
            "nodes/networkconnections/GetNodesWirelessNetworkConnections": response(
                {
                    "nodeWirelessConnections": [
                        {
                            "deviceID": child_id,
                            "connections": [
                                {
                                    "macAddress": "00:00:00:00:00:03",
                                    "negotiatedMbps": 433,
                                    "wireless": {"band": "5GHz", "signalDecibels": -61},
                                }
                            ],
                        }
                    ]
                }
            ),
            "nodes/topologyoptimization/GetTopologyOptimizationSettings2": response(
                {"isClientSteeringEnabled": True, "isNodeSteeringEnabled": True}
            ),
            "router/GetWANStatus3": response(
                {"wanStatus": "Connected", "wanConnection": {"wanType": "DHCP"}}
            ),
            "router/GetLANSettings": response({"ipAddress": "10.0.0.1"}),
            "wirelessap/GetRadioInfo3": response({"radios": []}),
        }

        result = linksys_mesh_app.normalize_topology("10.0.0.1", raw)
        child = next(node for node in result["nodes"] if node["id"] == child_id)
        client = next(item for item in result["clients"] if item["id"] == client_id)

        self.assertEqual(child["parentId"], master_id)
        self.assertEqual(child["parentName"], "Main")
        self.assertEqual(child["clientCount"], 1)
        self.assertEqual(client["nodeId"], child_id)
        self.assertEqual(client["nodeName"], "Patio")
        self.assertEqual(client["speedMbps"], 433)
        self.assertTrue(result["network"]["clientSteeringEnabled"])
        self.assertTrue(result["network"]["nodeSteeringEnabled"])
        self.assertEqual(result["network"]["nodeSteeringMode"], "automatic")
        self.assertFalse(result["network"]["manualParentSelectionAvailable"])
        self.assertEqual(result["network"]["documentedRestartScope"], "single-node")
        self.assertTrue(result["network"]["individualNodeRestartAvailable"])
        self.assertEqual(result["network"]["individualNodeRestartProbe"], "firmware-confirmed")
        self.assertEqual(
            result["network"]["individualNodeRestartEvidence"],
            "reset_slave_nodes-direct-jnap",
        )
        self.assertEqual(
            result["network"]["manualParentSelectionEvidence"],
            "firmware-internal-confirmed",
        )
        self.assertEqual(child["managementUrl"], "https://10.0.0.2/ca")

    def test_demo_topology_preserves_full_hierarchy_and_client_counts(self):
        topology = linksys_mesh_app.build_demo_topology()
        nodes = {node["name"]: node for node in topology["nodes"]}

        self.assertTrue(topology["meta"]["demo"])
        self.assertEqual(topology["summary"]["nodesOnline"], 6)
        self.assertEqual(topology["summary"]["clientsOnline"], 22)
        self.assertEqual(nodes["Atrium"]["parentName"], "Main")
        self.assertEqual(nodes["Garage"]["parentName"], "Atrium")
        self.assertEqual(nodes["Bedroom"]["parentName"], "Office")
        self.assertEqual(nodes["Atrium"]["clientCount"], 0)
        self.assertEqual(nodes["Office"]["clientCount"], 10)
        self.assertEqual(nodes["Bedroom"]["clientCount"], 2)
        self.assertEqual(nodes["Garage"]["clientCount"], 1)
        self.assertEqual(
            sum(node["clientCount"] for node in topology["nodes"]),
            topology["summary"]["clientsOnline"],
        )

    def test_demo_state_allows_read_only_review_and_rejects_restart(self):
        state = linksys_mesh_app.MeshState()
        topology = state.enable_demo()

        self.assertTrue(state.status()["connected"])
        self.assertTrue(state.status()["demo"])
        self.assertEqual(state.refresh()["summary"], topology["summary"])
        report = state.probe_node("demo-big-tree")
        self.assertTrue(report["demo"])
        self.assertTrue(report["manualParentSelection"]["firmwareInternalPathDiscovered"])
        self.assertTrue(report["individualRestart"]["disabledInDemo"])
        with self.assertRaisesRegex(linksys_mesh_app.RouterError, "Demo mode"):
            state.restart_node("demo-big-tree")

    def test_probe_node_uses_synchronized_credentials_and_read_only_actions(self):
        state = linksys_mesh_app.MeshState()
        state.session = linksys_mesh_app.RouterSession(
            host="10.0.0.1",
            password="test-only",
            connected_at="2026-01-01T00:00:00+00:00",
        )
        state.cache = {
            "network": {},
            "nodes": [
                {
                    "id": "big",
                    "name": "Atrium",
                    "online": True,
                    "ipAddress": "10.0.0.2",
                }
            ],
        }

        def fake_call(session, action):
            self.assertEqual(session.host, "10.0.0.2")
            self.assertEqual(session.password, "test-only")
            if action == "core/CheckAdminPassword":
                return {"result": "OK"}
            if action == "core/GetDeviceInfo":
                return response(
                    {
                        "modelNumber": "WHW03",
                        "firmwareVersion": "2.1.20",
                        "services": [
                            linksys_mesh_app.JNAP_PREFIX + "core/Core7",
                            linksys_mesh_app.JNAP_PREFIX + "nodes/setup/Setup3",
                        ],
                    }
                )
            if action == "nodes/smartmode/GetDeviceMode":
                return response({"mode": "Slave"})
            if action == "nodes/topologyoptimization/GetTopologyOptimizationSettings2":
                return response(
                    {"isClientSteeringEnabled": True, "isNodeSteeringEnabled": True}
                )
            self.fail(f"unexpected action: {action}")

        with patch("linksys_mesh_app.jnap_call", side_effect=fake_call):
            report = state.probe_node("big")

        self.assertTrue(report["credentialsSynchronized"])
        self.assertEqual(report["deviceMode"], "Slave")
        self.assertEqual(report["managementUrl"], "https://10.0.0.2/ca")
        self.assertTrue(report["individualRestart"]["visibleInCaSupportUi"])
        self.assertFalse(report["individualRestart"]["executed"])
        self.assertFalse(report["individualRestart"]["hasTargetDeviceId"])
        self.assertFalse(report["manualParentSelection"]["available"])
        self.assertEqual(report["manualParentSelection"]["transport"], "not-available")
        self.assertTrue(
            report["manualParentSelection"]["firmwareInternalPathDiscovered"]
        )

    def test_restart_targets_selected_online_node_without_confirmation_payload(self):
        state = linksys_mesh_app.MeshState()
        state.session = linksys_mesh_app.RouterSession(
            host="10.0.0.1",
            password="test-only",
            connected_at="2026-01-01T00:00:00+00:00",
        )
        state.cache = {
            "network": {},
            "nodes": [
                {
                    "id": "big",
                    "name": "Atrium",
                    "online": True,
                    "ipAddress": "10.0.0.2",
                }
            ],
        }

        def fake_reboot(session, action):
            self.assertEqual(session.host, "10.0.0.2")
            self.assertEqual(session.password, "test-only")
            self.assertEqual(action, "core/Reboot")
            return {"result": "OK"}

        with patch("linksys_mesh_app.jnap_mutating_call", side_effect=fake_reboot):
            result = state.restart_node("big")

        self.assertTrue(result["accepted"])
        self.assertEqual(result["scope"], "single-node")
        self.assertEqual(result["requestedThroughNode"]["name"], "Atrium")

        with patch("linksys_mesh_app.jnap_mutating_call") as reboot:
            with self.assertRaisesRegex(linksys_mesh_app.RouterError, "already restarting"):
                state.restart_node("big")
            reboot.assert_not_called()

        state.node_restart_cooldowns.clear()
        state.cache["nodes"][0]["online"] = False
        with patch("linksys_mesh_app.jnap_mutating_call") as reboot:
            with self.assertRaisesRegex(linksys_mesh_app.RouterError, "offline"):
                state.restart_node("big")
            reboot.assert_not_called()


if __name__ == "__main__":
    unittest.main()
