import unittest

import numpy as np

from hopwins.analysis.cir import decode_i24_q24


class CirDecodeTests(unittest.TestCase):
    def test_signed_i24_q24_conversion(self) -> None:
        i_values, q_values = decode_i24_q24(
            b"\xff\xff\xff\x00\x00\x80"
            b"\xff\xff\x7f\x01\x00\x00"
        )

        np.testing.assert_array_equal(i_values, [-1, 0x7FFFFF])
        np.testing.assert_array_equal(q_values, [-0x800000, 1])


if __name__ == "__main__":
    unittest.main()
