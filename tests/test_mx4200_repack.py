import importlib.util
from pathlib import Path
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = REPO_ROOT / "tools" / "mx4200_repack.py"
SPEC = importlib.util.spec_from_file_location("mx4200_repack", MODULE_PATH)
repack = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(repack)


class MX4200RepackTests(unittest.TestCase):
    def test_posix_cksum_matches_reference_vector(self):
        self.assertEqual(930766865, repack.posix_cksum(b"123456789"))

    def test_assemble_preserves_prefix_and_updates_footer(self):
        prefix = bytearray(repack.UBI_OFFSET)
        prefix[:8] = b"FIT-TEST"
        old_ubi = bytearray(3 * repack.PEB_SIZE)
        new_ubi = bytearray(4 * repack.PEB_SIZE)
        for image in (old_ubi, new_ubi):
            for offset in range(0, len(image), repack.PEB_SIZE):
                image[offset : offset + 4] = b"UBI#"

        footer = bytearray(repack.FOOTER_SIZE)
        footer[:9] = repack.LINKSYS_MAGIC
        footer[9:16] = b"0100040"
        footer[16:17] = b"7"
        footer[17:23] = b"MX4200"
        footer[32:40] = b"00000000"
        old_body = (
            bytes(prefix)
            + bytes(old_ubi)
            + b"\xFF" * (repack.PEB_SIZE - repack.FOOTER_SIZE)
        )
        footer[32:40] = f"{repack.posix_cksum(old_body):08X}".encode("ascii")

        with tempfile.TemporaryDirectory() as temp_dir:
            temp = Path(temp_dir)
            original = temp / "original.img"
            replacement = temp / "replacement.ubi"
            output = temp / "output.img"
            original.write_bytes(old_body + footer)
            replacement.write_bytes(new_ubi)

            manifest = repack.assemble_image(original, replacement, output)
            result = output.read_bytes()

        self.assertEqual(bytes(prefix), result[: repack.UBI_OFFSET])
        self.assertEqual(
            bytes(new_ubi),
            result[
                repack.UBI_OFFSET : repack.UBI_OFFSET + len(new_ubi)
            ],
        )
        self.assertEqual(4, manifest["replacement_ubi_pebs"])
        self.assertEqual(len(result), manifest["output_size"])
        self.assertEqual(
            f"{repack.posix_cksum(result[:-repack.FOOTER_SIZE]):08X}".encode(
                "ascii"
            ),
            result[-repack.FOOTER_SIZE + 32 : -repack.FOOTER_SIZE + 40],
        )


if __name__ == "__main__":
    unittest.main()
