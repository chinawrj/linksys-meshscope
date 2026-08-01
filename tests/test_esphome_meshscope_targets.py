import re
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
COMMON = ROOT / "esphome_meshscope_common.yaml"
C5_CONFIG = ROOT / "esphome_meshscope_c5.yaml"
C3_CONFIG = ROOT / "esphome_meshscope_c3.yaml"
C6_CONFIG = ROOT / "esphome_meshscope_c6.yaml"
EDGE = ROOT / "esphome_includes" / "meshscope_edge.h"
GITIGNORE = ROOT / ".gitignore"
C5_EXAMPLE = ROOT / "esphome_meshscope_c5.local.example.yaml"
C3_EXAMPLE = ROOT / "esphome_meshscope_c3.local.example.yaml"
C6_EXAMPLE = ROOT / "esphome_meshscope_c6.local.example.yaml"
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
        cls.edge = EDGE.read_text(encoding="utf-8")

    def test_shared_configuration_preserves_home_assistant_experience(self):
        for fragment in (
            "api:",
            "reboot_timeout: 0s",
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
            '"/api/restart-node"',
            '"/api/topology-lock"',
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
        self.assertIn("Linksys chooses the parent after reboot", html)
        self.assertIn("!state.topologyLockAcknowledged", app)
        self.assertIn("#settingsButton::after", css)
        self.assertNotIn("\n  .button-quiet::after", css)

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
            '"restart-ready"',
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

    def test_only_allowed_router_mutation_is_node_reboot(self):
        self.assertRegex(
            self.edge,
            re.compile(
                r"jnap_request\(\s*node\.ip,\s*\"core/Reboot\"",
                re.MULTILINE,
            ),
        )
        self.assertNotIn("FactoryReset", self.edge)
        self.assertIn("RESTART_COOLDOWN_MS = 90000", self.edge)
        self.assertIn("resolve_node(node_id, node)", self.edge)

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
