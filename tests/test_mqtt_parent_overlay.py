from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[1]
OVERLAY = REPO_ROOT / "firmware-overlays" / "mqtt-parent-steering"
PLAN_A = OVERLAY / "plan-a-strict-steering-io.patch"
PLAN_B = OVERLAY / "plan-b-open-acl.patch"
PLAN_C = OVERLAY / "plan-c-all-acls-open.patch"
BUILD_SCRIPT = REPO_ROOT / "tools" / "build_linksys_mqtt_images_linux.sh"
ACL_FIXTURE = REPO_ROOT / "tests" / "fixtures" / "mqtt-plan-a-test.acl"


class MQTTParentOverlayTests(unittest.TestCase):
    def test_plan_a_grants_exact_steering_io_permissions(self):
        patch = PLAN_A.read_text(encoding="utf-8")
        added_topics = {
            line[1:]
            for line in patch.splitlines()
            if line.startswith("+topic ")
        }

        self.assertEqual(
            {
                "topic write network/+/BH/config",
                "topic read network/+/DEVINFO",
                "topic read network/+/BH/status",
                "topic write network/status_resend_all",
                "topic write network/DEVINFO/status_resend_all",
                "topic write network/BH/status_resend_all",
            },
            added_topics,
        )
        self.assertIn("@@ -21,3 +21,11 @@", patch)

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
            "topic write network/status_resend_all",
            "topic write network/DEVINFO/status_resend_all",
            "topic write network/BH/status_resend_all",
        ):
            self.assertIn(permission, script)
        self.assertIn("MQTT_PLAN_A_STRICT_STEERING_IO.img", script)
        self.assertNotIn("MQTT_PLAN_A_STRICT_BH_CONFIG.img", script)

    def test_plan_c_opens_every_bundled_acl_and_is_fixed_size(self):
        patch = PLAN_C.read_text(encoding="utf-8")
        script = BUILD_SCRIPT.read_text(encoding="utf-8")

        for acl_name in ("default.acl", "open.acl", "moderate.acl", "strict.acl"):
            self.assertIn(f"a/etc/mosquitto/{acl_name}", patch)
        self.assertEqual(2, patch.count("+topic readwrite #"))
        self.assertGreaterEqual(patch.count("topic readwrite #"), 4)
        self.assertIn("+acl_file %CONF_DIR%/open.acl", patch)
        self.assertIn("MQTT_PLAN_C2_ALL_ACLS_QSDK_NO_BCJ.img", script)
        self.assertIn("MX4200_ORIGINAL_SQUASHFS_SIZE", script)
        self.assertIn("MX4200_ORIGINAL_UBI_SIZE", script)

    def test_mx4200_builder_requires_qsdk_xz_options(self):
        script = BUILD_SCRIPT.read_text(encoding="utf-8")

        self.assertIn("MX4200_MKSQUASHFS", script)
        self.assertNotIn("-Xbcj", script)
        self.assertIn("-Xpreset 9 -Xlc 0 -Xlp 2 -Xpb 2 -Xfb 64", script)
        self.assertIn(
            '0c 80 00 00 09 00 90 00 40 00 00 00 04 00',
            script,
        )
        self.assertIn('expected = Counter({("LZMA2",): 503})', script)
        self.assertIn("mx4200-plan-c-xz-streams.json", script.replace("$model-$plan", "mx4200-plan-c"))
        self.assertNotIn("-no-tailends", script)

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
