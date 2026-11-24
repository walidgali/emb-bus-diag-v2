import math
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import control  # noqa: E402


class ControlTest(unittest.TestCase):
    def test_struct_matches_c_layout(self):
        # 4 (+4 pad) + 11 doubles + int (+4 pad) = 104, same as sizeof(diag_stats_t)
        self.assertEqual(__import__("ctypes").sizeof(control.DiagStats), 104)

    def test_basic_stats(self):
        s = control.Stats()
        s.update(10.0)
        st = s.update(14.0)
        self.assertEqual(st.samples, 2)
        self.assertEqual(st.avg_val, 12.0)
        self.assertEqual(st.range_val, 4.0)

    def test_outlier_detected_in_short_series(self):
        s = control.Stats()
        for v in [10.5, 11.0, 12.5, 10.0]:
            s.update(v)
        st = s.update(25.0)
        self.assertGreater(st.z_score, 3.0)

    def test_nan_rejected(self):
        s = control.Stats()
        with self.assertRaises(ValueError):
            s.update(math.nan)

    def test_slope(self):
        s = control.Stats()
        for i in range(1, 11):
            st = s.update(2.0 * i + 1.0)
        self.assertAlmostEqual(st.trend_slope, 2.0, places=9)


if __name__ == "__main__":
    unittest.main()
