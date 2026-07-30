import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
COMMON = ROOT / "esphome_meshscope_common.yaml"
C5_CONFIG = ROOT / "esphome_meshscope_c5.yaml"
C3_CONFIG = ROOT / "esphome_meshscope_c3.yaml"
EDGE = ROOT / "esphome_includes" / "meshscope_edge.h"
GITIGNORE = ROOT / ".gitignore"
C5_EXAMPLE = ROOT / "esphome_meshscope_c5.local.example.yaml"
C3_EXAMPLE = ROOT / "esphome_meshscope_c3.local.example.yaml"


class ESPHomeMeshScopeTargetsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.common = COMMON.read_text(encoding="utf-8")
        cls.c5_config = C5_CONFIG.read_text(encoding="utf-8")
        cls.c3_config = C3_CONFIG.read_text(encoding="utf-8")
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
            "authorize_web_request(request)",
            "WWW-Authenticate",
            "managedConnection",
            "snapshotReady",
            "routerConnected",
            "config.max_open_sockets = 4",
            "jnap_mutex",
            "REFRESH_INTERVAL_MS = 10000",
            "xTaskCreate(",
        ):
            self.assertIn(fragment, self.edge)

    def test_only_allowed_router_mutation_is_node_reboot(self):
        mutations = [
            line
            for line in self.edge.splitlines()
            if "jnap_request(" in line and '"core/' in line
        ]
        reboot = [line for line in mutations if '"core/Reboot"' in line]
        self.assertEqual(len(reboot), 1)
        self.assertNotIn("FactoryReset", self.edge)
        self.assertIn("RESTART_COOLDOWN_MS = 90000", self.edge)
        self.assertIn("resolve_node(node_id, node)", self.edge)

    def test_local_credentials_file_is_ignored(self):
        ignored = GITIGNORE.read_text(encoding="utf-8").splitlines()
        self.assertIn("esphome_meshscope_c5.local.yaml", ignored)
        self.assertIn("esphome_meshscope_c3.local.yaml", ignored)

    def test_examples_require_unique_web_and_api_credentials(self):
        for example_path in (C5_EXAMPLE, C3_EXAMPLE):
            example = example_path.read_text(encoding="utf-8")
            self.assertIn("meshscope_router_password_b64:", example)
            self.assertIn("meshscope_web_password:", example)
            self.assertIn("meshscope_api_key:", example)
        for config in (self.common, self.c5_config, self.c3_config):
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
