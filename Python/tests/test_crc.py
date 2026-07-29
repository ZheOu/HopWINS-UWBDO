import unittest

from hopwins.protocol.crc import crc32_bzip2


class CrcTests(unittest.TestCase):
    def test_crc32_bzip2_check_value(self) -> None:
        self.assertEqual(crc32_bzip2(b"123456789"), 0xFC891918)


if __name__ == "__main__":
    unittest.main()
