from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[1]
OVERLAY = REPO_ROOT / "firmware-overlays" / "mqtt-parent-steering"
PLAN_A = OVERLAY / "plan-a-strict-steering-io.patch"
PLAN_B = OVERLAY / "plan-b-open-acl.patch"
BUILD_SCRIPT = REPO_ROOT / "tools" / "build_linksys_mqtt_images_linux.sh"
ACL_FIXTURE = REPO_ROOT / "tests" / "fixtures" / "mqtt-plan-a-test.acl"


class MQTTParentOverlayTests(unittest.TestCase):
    def test_plan_a_grants_exact_steering_io_permissions(self):
        added_topics = {
            line[1:]
            for line in PLAN_A.read_text(encoding="utf-8").splitlines()
            if line.startswith("+topic ")
        }

        self.assertEqual(
            {
                "topic write network/+/BH/config",
                "topic read network/+/DEVINFO",
                "topic read network/+/BH/status",
                "topic write network/BH/status_resend_all",
            },
            added_topics,
        )

    def test_plan_b_only_switches_the_existing_listener_to_open_acl(self):
        patch = PLAN_B.read_text(encoding="utf-8")

        self.assertIn("-acl_file %CONF_DIR%/strict.acl", patch)
        self.assertIn("+acl_file %CONF_DIR%/open.acl", patch)
        self.assertNotIn("+topic ", patch)

    def test_builder_checks_every_plan_a_permission_and_uses_distinct_name(self):
        script = BUILD_SCRIPT.read_text(encoding="utf-8")

        for permission in (
            "topic write network/+/BH/config",
            "topic read network/+/DEVINFO",
            "topic read network/+/BH/status",
            "topic write network/BH/status_resend_all",
        ):
            self.assertIn(permission, script)
        self.assertIn("MQTT_PLAN_A_STRICT_STEERING_IO.img", script)
        self.assertNotIn("MQTT_PLAN_A_STRICT_BH_CONFIG.img", script)

    def test_arm_broker_fixture_matches_plan_a_permissions(self):
        patch_permissions = {
            line[1:]
            for line in PLAN_A.read_text(encoding="utf-8").splitlines()
            if line.startswith("+topic ")
        }
        fixture_permissions = {
            line
            for line in ACL_FIXTURE.read_text(encoding="utf-8").splitlines()
            if line.startswith("topic ")
        }

        self.assertEqual(patch_permissions, fixture_permissions)


if __name__ == "__main__":
    unittest.main()
