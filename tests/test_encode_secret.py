import unittest

from tools.encode_secret import encode_secret


class EncodeSecretTest(unittest.TestCase):
    def test_utf8_and_cpp_sensitive_characters_are_encoded(self):
        self.assertEqual(
            encode_secret('quote" slash\\ semicolon; café'),
            "cXVvdGUiIHNsYXNoXCBzZW1pY29sb247IGNhZsOp",
        )


if __name__ == "__main__":
    unittest.main()
