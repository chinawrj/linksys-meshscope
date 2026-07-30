import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "esphome_meshscope_c5.yaml"
EDGE = ROOT / "esphome_includes" / "meshscope_edge.h"
GITIGNORE = ROOT / ".gitignore"
EXAMPLE = ROOT / "esphome_meshscope_c5.local.example.yaml"


class ESPHomeMeshScopeC5Test(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.config = CONFIG.read_text(encoding="utf-8")
        cls.edge = EDGE.read_text(encoding="utf-8")

    def test_esphome_home_assistant_and_psram_are_enabled(self):
        for fragment in (
            "board: esp32-c5-devkitc-1",
            "flash_size: 8MB",
            "psram:",
            "ignore_not_found: false",
            'CONFIG_SPIRAM_USE_MALLOC: "y"',
            'CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC: "y"',
            'CONFIG_MBEDTLS_DYNAMIC_BUFFER: "y"',
            'CONFIG_LWIP_MAX_SOCKETS: "12"',
            'CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL: "0"',
            'CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP: "y"',
            "api:",
            "reboot_timeout: 0s",
            "encryption:",
            "ota:",
            "platform: sntp",
        ):
            self.assertIn(fragment, self.config)

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

    def test_example_requires_unique_web_and_api_credentials(self):
        example = EXAMPLE.read_text(encoding="utf-8")
        self.assertIn("meshscope_router_password_b64:", example)
        self.assertIn("meshscope_web_password:", example)
        self.assertNotIn(
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=",
            self.config,
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
