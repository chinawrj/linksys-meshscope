"""Execute the production ESP32 health/restart state machine with host stubs.

No router, MQTT broker, NVS, or hardware is contacted. ESPHome CI separately
compiles the entire firmware for each supported target.
"""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EDGE = ROOT / "esphome_includes/meshscope_edge.h"


class ParentHealthRecoveryTest(unittest.TestCase):
    def test_production_state_machine(self):
        compiler = shutil.which("c++")
        self.assertIsNotNone(compiler, "A host C++ compiler is required")
        source = EDGE.read_text()
        declarations = []
        for name in ("NodeObservation", "ParentSteeringHealth", "ParentRestartRequest"):
            declarations.append(re.search(r"struct " + name + r" \{.*?\n};", source, re.S)[0])
        functions = []
        for name in ("find_observed_node", "count_online_mesh_children",
                     "evaluate_parent_steering_health", "record_parent_steering_outcome",
                     "process_parent_restart_request"):
            functions.append(re.search(
                r"static [^;{}]+\b" + name + r"\([^;{}]*\) \{.*?\n}", source, re.S)[0])
        with tempfile.TemporaryDirectory(prefix="meshscope-health-test-") as directory:
            folder = Path(directory)
            (folder / "production_types.h").write_text("\n".join(declarations))
            (folder / "production_functions.h").write_text("\n".join(functions))
            binary = folder / "health-test"
            build = subprocess.run([
                compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                "-I", str(folder), str(ROOT / "tests/fixtures/parent_health_driver.cpp"),
                "-o", str(binary),
            ], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_recovery_is_wired_to_new_collector_generations_and_persistence(self):
        source = EDGE.read_text()
        collector = source.split("static void collector_task(void *)", 1)[1].split(
            "static esp_err_t send_json", 1)[0]
        self.assertIn("evaluate_parent_steering_health(lock_observations, snapshot_generation)", collector)
        self.assertEqual(collector.count("evaluate_parent_steering_health({}, 0, false)"), 2)
        self.assertIn('json_string(item, "lastRecoveredAt")', source)
        self.assertEqual(source.count('item, "lastRecoveredAt", health.last_recovered_at.c_str()'), 2)


if __name__ == "__main__":
    unittest.main()
