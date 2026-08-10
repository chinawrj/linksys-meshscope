import re
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
COMMON = ROOT / "esphome_meshscope_common.yaml"
C5_CONFIG = ROOT / "esphome_meshscope_c5.yaml"
C3_CONFIG = ROOT / "esphome_meshscope_c3.yaml"
C6_CONFIG = ROOT / "esphome_meshscope_c6.yaml"
C5_CI_CONFIG = ROOT / "esphome_meshscope_c5.ci.yaml"
C3_CI_CONFIG = ROOT / "esphome_meshscope_c3.ci.yaml"
C6_CI_CONFIG = ROOT / "esphome_meshscope_c6.ci.yaml"
EDGE = ROOT / "esphome_includes" / "meshscope_edge.h"
GITIGNORE = ROOT / ".gitignore"
C5_EXAMPLE = ROOT / "esphome_meshscope_c5.local.example.yaml"
C3_EXAMPLE = ROOT / "esphome_meshscope_c3.local.example.yaml"
C6_EXAMPLE = ROOT / "esphome_meshscope_c6.local.example.yaml"
WIREGUARD_CONFIG = ROOT / "esphome_meshscope_wireguard.yaml"
WEB_HTML = ROOT / "mesh_web" / "index.html"
WEB_APP = ROOT / "mesh_web" / "app.js"
WEB_CSS = ROOT / "mesh_web" / "styles.css"


class ESPHomeMeshScopeTargetsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.common = COMMON.read_text(encoding="utf-8")
        cls.c5_config = C5_CONFIG.read_text(encoding="utf-8")
        cls.c3_config = C3_CONFIG.read_text(encoding="utf-8")
        cls.c6_config = C6_CONFIG.read_text(encoding="utf-8")
        cls.c5_ci_config = C5_CI_CONFIG.read_text(encoding="utf-8")
        cls.c3_ci_config = C3_CI_CONFIG.read_text(encoding="utf-8")
        cls.c6_ci_config = C6_CI_CONFIG.read_text(encoding="utf-8")
        cls.edge = EDGE.read_text(encoding="utf-8")
        cls.wireguard_config = WIREGUARD_CONFIG.read_text(encoding="utf-8")

    def test_shared_configuration_preserves_home_assistant_experience(self):
        for fragment in (
            "api:",
            "reboot_timeout: 5min",
            "encryption:",
            "ota:",
            "platform: sntp",
            "Router Connected",
            "Online Mesh Nodes",
            "Total Mesh Nodes",
            "Online Clients",
            "Weak Mesh Nodes",
            "Backhaul Total",
            "Topology Summary",
            "Last Topology Update",
            "Refresh Mesh Topology",
            "Topology Lock Active",
            "Topology Lock Issues",
            "Topology Lock Summary",
            "MQTT Parent Steering Available",
            "MQTT Parent Steering Mode",
            "Last Parent Steering Result",
        ):
            self.assertIn(fragment, self.common)

    def test_c5_retains_psram_and_existing_hardware_configuration(self):
        for fragment in (
            "!include esphome_meshscope_common.yaml",
            "board: esp32-c5-devkitc-1",
            "variant: esp32c5",
            "flash_size: 8MB",
            "psram:",
            "ignore_not_found: false",
            'CONFIG_SPIRAM_USE_MALLOC: "y"',
            'CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC: "y"',
            'CONFIG_MBEDTLS_DYNAMIC_BUFFER: "y"',
            'CONFIG_LWIP_MAX_SOCKETS: "12"',
            'CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL: "0"',
            'CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP: "y"',
        ):
            self.assertIn(fragment, self.c5_config)

    def test_c3_builds_without_psram_configuration(self):
        for fragment in (
            "!include esphome_meshscope_common.yaml",
            "board: esp32-c3-devkitm-1",
            "variant: esp32c3",
            "flash_size: 4MB",
            'CONFIG_MBEDTLS_DYNAMIC_BUFFER: "y"',
            'CONFIG_LWIP_MAX_SOCKETS: "12"',
        ):
            self.assertIn(fragment, self.c3_config)
        for forbidden in (
            "psram:",
            "CONFIG_SPIRAM",
            "CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC",
        ):
            self.assertNotIn(forbidden, self.c3_config)

    def test_c6_builds_without_psram_and_uses_native_usb_logging(self):
        for fragment in (
            "!include esphome_meshscope_common.yaml",
            "board: esp32-c6-devkitc-1",
            "variant: esp32c6",
            "flash_size: 4MB",
            "hardware_uart: USB_SERIAL_JTAG",
            'CONFIG_MBEDTLS_DYNAMIC_BUFFER: "y"',
            'CONFIG_LWIP_MAX_SOCKETS: "12"',
        ):
            self.assertIn(fragment, self.c6_config)
        for forbidden in (
            "psram:",
            "CONFIG_SPIRAM",
            "CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC",
        ):
            self.assertNotIn(forbidden, self.c6_config)

    def test_edge_psram_access_is_target_guarded(self):
        self.assertIn("#if defined(CONFIG_SPIRAM)", self.edge)
        self.assertIn("external_memory_size()", self.edge)
        self.assertIn("external_memory_free()", self.edge)
        self.assertEqual(self.edge.count('#include "esp_psram.h"'), 1)

    def test_edge_serves_required_api_and_refreshes_in_background(self):
        for fragment in (
            '"/api/status"',
            '"/api/topology"',
            '"/api/refresh"',
            '"/api/node-capabilities"',
            '"/api/node-sysinfo"',
            '"/api/node-radio-info"',
            '"/api/restart-node"',
            '"/api/node-steering-mode"',
            '"/api/topology-lock"',
            '"/api/mqtt-parent-steering"',
            '"/api/steer-node-parent"',
            '"/api/login"',
            '"/api/logout"',
            "authorize_web_request(request)",
            "managedConnection",
            "snapshotReady",
            "routerConnected",
            "config.max_open_sockets = 4",
            "jnap_mutex",
            "REFRESH_INTERVAL_MS = 10000",
            "xTaskCreate(",
            "stream_decompressed_response(",
            "compressed_response",
            "memory_mutex",
            "topology_lock_mutex",
            "HttpOnly; SameSite=Strict",
            "esp_fill_random",
            "WEB_SESSION_LIMIT = 4",
            "mqtt_steering_worker",
            "mqtt_steering_mutex",
        ):
            self.assertIn(fragment, self.edge)
        self.assertNotIn("WWW-Authenticate", self.edge)
        self.assertNotIn("web_authorization", self.edge)

    def test_dashboard_login_is_password_only_and_has_sign_out(self):
        html = WEB_HTML.read_text(encoding="utf-8")
        app = WEB_APP.read_text(encoding="utf-8")
        self.assertIn("Unlock MeshScope", html)
        self.assertIn("Dashboard password", html)
        self.assertIn("No username is required", "".join(
            path.read_text(encoding="utf-8")
            for path in (C5_EXAMPLE, C3_EXAMPLE, C6_EXAMPLE)
        ))
        self.assertNotIn('name="username"', html)
        self.assertIn('id="signOutButton"', html)
        self.assertIn('api("/api/login"', app)
        self.assertIn('api("/api/logout"', app)

    def test_topology_recovery_requires_explicit_acknowledgement(self):
        html = WEB_HTML.read_text(encoding="utf-8")
        app = WEB_APP.read_text(encoding="utf-8")
        css = WEB_CSS.read_text(encoding="utf-8")
        self.assertIn('id="topologyLockAcknowledgement"', html)
        self.assertIn("sends an exact MQTT Parent request", html)
        self.assertIn("!state.topologyLockAcknowledged", app)
        self.assertIn("#settingsButton::after", css)
        self.assertNotIn("\n  .button-quiet::after", css)
        self.assertNotIn(".legend {\n    display: none;", css)
        self.assertIn("Swipe horizontally to explore the complete topology.", html)
        self.assertIn(".map-scroll-hint", css)

    def test_topology_positions_remain_compatible_with_strict_csp(self):
        app = WEB_APP.read_text(encoding="utf-8")
        for fragment in (
            "function applyTopologyLayoutPositions(map)",
            'data-layout-left="${node.x}"',
            'data-layout-top="${node.y}"',
            "element.style.left = `${left}px`",
            "element.style.top = `${top}px`",
            "applyTopologyLayoutPositions(map)",
        ):
            self.assertIn(fragment, app)
        self.assertNotIn('style="left:', app)

    def test_topology_lock_is_persistent_observable_and_rate_limited(self):
        for fragment in (
            "TOPOLOGY_LOCK_ACTION_COOLDOWN_MS = 5 * 60 * 1000",
            "TOPOLOGY_LOCK_CONFIRMATIONS = 3",
            'TOPOLOGY_LOCK_NVS_NAMESPACE = "meshscope_lock"',
            "persist_topology_lock_locked()",
            "load_topology_lock()",
            "validate_topology_lock_mappings(",
            "evaluate_topology_lock(",
            '"nextActionInSeconds"',
            '"expectedParentOnline"',
            '"confirmations"',
            '"parent-offline"',
            '"steering-ready"',
            '"recoveryTransport", "mqtt"',
            "queue_topology_lock_mqtt_operation(",
            'mqtt_operation.origin = "topology-lock"',
            "restart_cooldowns.find(mapping.node_id)",
        ):
            self.assertIn(fragment, self.edge)
        self.assertRegex(
            self.edge,
            re.compile(
                r"node == nullptr \|\| parent == nullptr \|\| !node->online \|\|\s*"
                r"!parent->online \|\| node->authority",
                re.MULTILINE,
            ),
        )

    def test_topology_lock_uses_opposite_parent_backhaul_radio(self):
        self.assertIn('parent_uplink == "5GH") return "5GL"', self.edge)
        self.assertIn('parent_uplink == "5GL") return "5GH"', self.edge)
        self.assertIn("parent.authority", self.edge)
        evaluate = self.edge.split("static void evaluate_topology_lock(", 1)[1].split(
            "static bool collect_snapshot(", 1
        )[0]
        self.assertIn("queue_topology_lock_mqtt_operation", evaluate)
        self.assertNotIn('"core/Reboot"', evaluate)

    def test_client_details_are_adaptive_and_explicit(self):
        for fragment in (
            'meshscope_client_details: "auto"',
            '"${meshscope_client_details}"',
        ):
            self.assertIn(fragment, self.common)
        for fragment in (
            "ClientDetailsMode::AUTO",
            "ClientDetailsMode::FULL",
            "ClientDetailsMode::NODES_ONLY",
            '"nodes-only"',
            '"clientDetails"',
            '"clientDetailsRequested"',
            '"nodes/firmwareupdate/GetFirmwareUpdateStatus"',
            '"deviceIDs"',
        ):
            self.assertIn(fragment, self.edge)
        for example_path in (C5_EXAMPLE, C3_EXAMPLE, C6_EXAMPLE):
            self.assertIn(
                'meshscope_client_details: "auto"',
                example_path.read_text(encoding="utf-8"),
            )

    def test_mqtt_parent_steering_is_persistent_bounded_and_backgrounded(self):
        for fragment in (
            'meshscope_mqtt_parent_steering: "auto"',
            '"${meshscope_mqtt_parent_steering}"',
            'MQTT_NVS_NAMESPACE = "meshscope_mqtt"',
            'MQTT_NVS_MODE_KEY = "mode"',
            "MqttSteeringMode::AUTO",
            "MqttSteeringMode::FORCE_ON",
            "MqttSteeringMode::FORCE_OFF",
            "persist_mqtt_mode(",
            "load_mqtt_mode()",
            "MQTT_PACKET_LIMIT_INTERNAL = 16 * 1024",
            "MQTT_PACKET_LIMIT_EXTERNAL = 32 * 1024",
            'esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")',
            'mqtt_append_text(body, "#")',
            '"MQTT RX topic=%s payload_bytes=%u qos=%u"',
            '"MQTT RX payload topic=%s offset=%u/%u data=%s"',
            "if (received) vTaskDelay(1)",
            '"network/status_resend_all"',
            '"network/DEVINFO/status_resend_all"',
            '"network/BH/status_resend_all"',
            "BACKHAUL_PHY_REFRESH_INTERVAL_MS = 30 * 1000",
            "BACKHAUL_PHY_COLLECT_MS = 6 * 1000",
            'mqtt_json_scalar(packet.body, payload_offset, "phyRate"',
            'mqtt_json_scalar(packet.body, payload_offset, "phyRate_2"',
            "mqtt_refresh_backhaul_phy(",
            "backhaulPhyLinks",
            '"rateMbps"',
            '"rawRate"',
            '"ageSeconds"',
            '"stale"',
            '"/BH/config"',
            '"network/master/cmd/nodes_steering_start"',
            '"/WLAN/cmd/reconsider-backhaul"',
            '"network/master/cmd/nodes_temporary_blacklist"',
            'strcmp(method, "blacklist")',
            '"resetReason"',
            "MQTT_11V_FALLBACK_MS = 45 * 1000",
            "MQTT_BACKHAUL_MONITOR_MS = 45 * 1000",
            "MQTT_BLACKLIST_DURATION_SECONDS = 45",
            '"client_bssid"',
            '"ap_bssid"',
            '"ap_channel"',
            '"ap_uuid"',
            'json_string(wireless, "stationBSSID")',
            'strcmp(method, "11v")',
            'strcmp(method, "reconsider")',
            "ascii_upper(operation.child_id)",
            "MQTT_VERIFY_GENERATIONS = 2",
            "MQTT_VERIFICATION_TIMEOUT_MS = 180 * 1000",
            "mqtt_verify_operation(",
            "mqtt_operation_active_for_node(",
            "mqtt_radio_from_observed_child(",
            'json_string(wireless, "apBSSID")',
            'json_string(wireless, "radioID")',
            '"roundTrip", mqtt_available',
            '"testedAt", mqtt_last_probe_at.c_str()',
            '"transport", "mqtt-1883"',
            'mqtt_probe_requested ? "detecting"',
            '"202 Accepted"',
            '"channelMode", mqtt_operation.channel_mode.c_str()',
            'operation.channel_mode == "auto"',
            'strcmp(channel_mode, "auto") != 0',
            '"nodes/diagnostics/GetNodeNeighborInfo"',
            'mqtt_confirm_target_from_child_neighbors(',
            'target.source != "fresh Parent WLAN/serving_channels"',
            'current_generation() <= previous_generation',
            'mqtt_record_target(operation.id, target)',
            '"targetBssid", mqtt_operation.target_bssid.c_str()',
            '"targetChannel", mqtt_operation.target_channel',
            '"method", mqtt_operation.method.c_str()',
            "child_station_band = child.backhaul_band",
            "parent_association_trackable",
            '"requestedParentAssociationTrackable"',
        ):
            self.assertIn(fragment, self.common + self.edge)
        self.assertNotIn("mqtt_radio_complete_from_parent_uplink", self.edge)
        self.assertIn(
            'if (!blacklist_sent && operation.method == "auto"', self.edge
        )
        for example_path in (C5_EXAMPLE, C3_EXAMPLE, C6_EXAMPLE):
            self.assertIn(
                'meshscope_mqtt_parent_steering: "auto"',
                example_path.read_text(encoding="utf-8"),
            )

    def test_force_off_prevents_probe_and_publish_and_force_on_only_bypasses_gate(self):
        self.assertRegex(
            self.edge,
            re.compile(
                r"if \(mode == MqttSteeringMode::FORCE_OFF\) continue;",
                re.MULTILINE,
            ),
        )
        self.assertIn(
            "mode == MqttSteeringMode::AUTO && !capability_available",
            self.edge,
        )
        self.assertIn(
            "mqtt_mode != MqttSteeringMode::FORCE_OFF",
            self.edge,
        )
        self.assertNotIn("mqtt_mode == MqttSteeringMode::FORCE_ON && !mqtt_available", self.edge)

    def test_no_psram_collection_keeps_device_list_last_and_reuses_buffers(self):
        actions_start = self.edge.index("static const char *const READ_ACTIONS[]")
        actions_end = self.edge.index("};", actions_start)
        actions = self.edge[actions_start:actions_end]
        self.assertLess(
            actions.index('"wirelessap/GetRadioInfo3"'),
            actions.index('"devicelist/GetDevices3"'),
        )
        for fragment in (
            "device_receive_workspace",
            "standard_receive_workspace",
            "reuse_snapshot_compression_buffer(",
            "COMPRESSION_WINDOW = 8 * 1024",
            "response_size",
        ):
            self.assertIn(fragment, self.edge)

    def test_router_mutations_are_limited_to_reboot_parent_mqtt_and_node_steering_gate(self):
        self.assertRegex(
            self.edge,
            re.compile(
                r"jnap_request\(\s*node\.ip,\s*\"core/Reboot\"",
                re.MULTILINE,
            ),
        )
        self.assertRegex(
            self.edge,
            re.compile(
                r"jnap_request\(\s*parent\.ip,\s*\"core/Reboot\"",
                re.MULTILINE,
            ),
        )
        self.assertNotIn("FactoryReset", self.edge)
        self.assertIn("RESTART_COOLDOWN_MS = 90000", self.edge)
        self.assertIn("resolve_node(node_id, node)", self.edge)
        self.assertEqual(self.edge.count('"/BH/config"'), 1)
        self.assertEqual(
            self.edge.count('"network/master/cmd/nodes_steering_start"'), 3
        )
        self.assertEqual(self.edge.count('"/WLAN/cmd/reconsider-backhaul"'), 3)
        self.assertEqual(
            self.edge.count('"network/master/cmd/nodes_temporary_blacklist"'), 5
        )
        self.assertEqual(
            self.edge.count(
                '"nodes/topologyoptimization/SetTopologyOptimizationSettings2"'
            ),
            1,
        )
        self.assertIn('"isClientSteeringEnabled", client_steering', self.edge)
        self.assertIn('"isNodeSteeringEnabled", requested', self.edge)
        self.assertNotIn("SetRadioSettings", self.edge)
        self.assertNotIn("SetLANSettings", self.edge)

    def test_parent_health_restart_requires_qualifying_failures_and_safe_parent(self):
        for fragment in (
            "PARENT_STEERING_FAILURE_THRESHOLD = 2",
            "PARENT_HEALTH_RESTART_COOLDOWN_MS = 5 * 60 * 1000",
            'operation.method == "bh-config"',
            'operation.channel_mode == "exact"',
            'command_topic.find("BH/config")',
            "health.consecutive_failures++",
            "health.consecutive_failures = 0",
            "count_online_mesh_children(",
            "parent->authority",
            "parent_restart_request.pending = true",
            "process_parent_restart_request()",
            "parent_node_restart_remaining_ms_locked(",
            '"MQTT Parent steering is forced off"',
            '"failureThreshold"',
            '"targetParentOnlineChildren"',
            '"lastRequestPublished"',
            '"restartInSeconds"',
            'PARENT_HEALTH_NVS_NAMESPACE = "mesh_health"',
        ):
            self.assertIn(fragment, self.edge)
        namespace = re.search(
            r'PARENT_HEALTH_NVS_NAMESPACE = "([^"]+)"', self.edge
        ).group(1)
        self.assertLessEqual(len(namespace), 15)
        self.assertLess(
            self.edge.index("if (process_parent_restart_request()) continue;"),
            self.edge.index("MqttSteeringOperation operation;", self.edge.index("static void mqtt_steering_worker")),
        )

    def test_local_credentials_file_is_ignored(self):
        ignored = GITIGNORE.read_text(encoding="utf-8").splitlines()
        self.assertIn("esphome_meshscope_c5.local.yaml", ignored)
        self.assertIn("esphome_meshscope_c3.local.yaml", ignored)
        self.assertIn("esphome_meshscope_c6.local.yaml", ignored)

    def test_examples_require_unique_dashboard_and_api_credentials(self):
        for example_path in (C5_EXAMPLE, C3_EXAMPLE, C6_EXAMPLE):
            example = example_path.read_text(encoding="utf-8")
            self.assertIn("meshscope_router_password_b64:", example)
            self.assertIn("meshscope_dashboard_password:", example)
            self.assertNotIn("meshscope_web_username:", example)
            self.assertIn("meshscope_api_key:", example)
        for config in (
            self.common,
            self.c5_config,
            self.c3_config,
            self.c6_config,
        ):
            self.assertNotIn(
                "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=",
                config,
            )

    def test_optional_wireguard_preserves_local_routes_and_boot(self):
        for fragment in (
            'meshscope_wireguard_tunnel_network: "192.0.2.0/24"',
            'meshscope_wireguard_ha_network: "198.51.100.0/24"',
            '- "${meshscope_wireguard_tunnel_network}"',
            '- "${meshscope_wireguard_ha_network}"',
            'peer_persistent_keepalive: "${meshscope_wireguard_keepalive}"',
            "require_connection_to_proceed: false",
            "reboot_timeout: 5min",
            "WireGuard Peer Connected",
            "WireGuard Latest Handshake",
            "WireGuard Address",
            "MeshScope WireGuard URL",
        ):
            self.assertIn(fragment, self.wireguard_config)
        self.assertIn('meshscope_wireguard_netmask: "255.255.255.255"', self.wireguard_config)
        self.assertNotIn('"0.0.0.0/0"', self.wireguard_config)
        self.assertNotIn('"::/0"', self.wireguard_config)
        for target in (self.c5_config, self.c3_config, self.c6_config):
            self.assertNotIn("esphome_meshscope_wireguard.yaml", target)
        for ci_target in (self.c5_ci_config, self.c3_ci_config, self.c6_ci_config):
            self.assertIn("esphome_meshscope_wireguard.yaml", ci_target)
            self.assertIn('meshscope_wireguard_netmask: "255.255.255.255"', ci_target)
            self.assertIn('meshscope_wireguard_ha_network: "192.168.50.0/24"', ci_target)

    def test_c5_example_documents_optional_wireguard_without_real_keys(self):
        example = C5_EXAMPLE.read_text(encoding="utf-8")
        for fragment in (
            "# wireguard: !include esphome_meshscope_wireguard.yaml",
            "meshscope_wireguard_tunnel_network",
            "meshscope_wireguard_ha_network",
            "YOUR_DEVICE_PRIVATE_KEY",
            "YOUR_SERVER_PUBLIC_KEY",
            "YOUR_PRESHARED_KEY",
            'meshscope_wireguard_address: "10.23.0.48"',
            'meshscope_wireguard_netmask: "255.255.255.255"',
            'meshscope_wireguard_ha_network: "192.168.50.0/24"',
        ):
            self.assertIn(fragment, example)

    def test_embedded_web_assets_are_current(self):
        result = subprocess.run(
            [
                "python3",
                str(ROOT / "tools" / "generate_esp32_meshscope_assets.py"),
                "--check",
            ],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        generated = (
            ROOT / "esphome_includes" / "meshscope_web_assets.h"
        ).read_text(encoding="utf-8")
        self.assertIn('"/meshscope-bundle.js"', generated)
        self.assertNotIn('"/linksys-normalize.js"', generated)


if __name__ == "__main__":
    unittest.main()
