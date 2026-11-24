"""Drive the C statistics engine from Python via ctypes.

Build the library first:  make lib
Run the demo:             python3 control.py
"""
import ctypes
import os

_HERE = os.path.dirname(os.path.abspath(__file__))


class DiagStats(ctypes.Structure):
    """Must mirror diag_stats_t in include/diag.h (same order)."""

    _fields_ = [
        ("samples", ctypes.c_uint32),
        ("min_val", ctypes.c_double),
        ("max_val", ctypes.c_double),
        ("avg_val", ctypes.c_double),
        ("range_val", ctypes.c_double),
        ("ema_val", ctypes.c_double),
        ("variance", ctypes.c_double),
        ("trend_slope", ctypes.c_double),
        ("z_score", ctypes.c_double),
        ("sum_y", ctypes.c_double),
        ("sum_xy", ctypes.c_double),
        ("prev_val", ctypes.c_double),
        ("initialized", ctypes.c_int),
    ]


def load_library(path=None):
    path = path or os.path.join(_HERE, "libembdiag.so")
    if not os.path.exists(path):
        raise FileNotFoundError(f"{path} not found - run 'make lib' first")
    lib = ctypes.CDLL(path)
    lib.embdiag_stats_init.argtypes = [ctypes.POINTER(DiagStats)]
    lib.embdiag_stats_init.restype = None
    lib.embdiag_stats_update.argtypes = [ctypes.POINTER(DiagStats), ctypes.c_double]
    lib.embdiag_stats_update.restype = ctypes.c_int
    return lib


class Stats:
    """Pythonic wrapper around one C statistics context."""

    def __init__(self, lib=None):
        self._lib = lib or load_library()
        self._st = DiagStats()
        self._lib.embdiag_stats_init(ctypes.byref(self._st))

    def update(self, value):
        rc = self._lib.embdiag_stats_update(ctypes.byref(self._st), float(value))
        if rc != 0:
            raise ValueError(f"value rejected by C engine: {value!r}")
        return self._st

    @property
    def state(self):
        return self._st


def main():
    stats = Stats()
    print("Feeding data from Python to C Engine...")
    data_stream = [10.5, 11.0, 12.5, 10.0, 25.0]  # 25.0 is an outlier
    for val in data_stream:
        st = stats.update(val)
        flag = " OUTLIER" if abs(st.z_score) > 3.0 else ""
        print(f"Python Input: {val} | C Engine Output -> "
              f"EMA: {st.ema_val:.4f}, Z-Score: {st.z_score:.2f}{flag}")
    print("\nDiagnostic complete. Control via Python works!")


if __name__ == "__main__":
    main()
