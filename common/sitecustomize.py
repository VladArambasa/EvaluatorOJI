# Sonda Evaluatorului Olimpiadă pentru programele Python: la ieșirea normală scrie memoria
# maximă folosită în fișierul din variabila OLIEVAL_PROBE. Evaluatorul o activează prin PYTHONPATH.
import atexit as _ol_atexit
import os as _ol_os
import sys as _ol_sys


def _olieval_probe():
    p = _ol_os.environ.get("OLIEVAL_PROBE")
    if not p:
        return
    try:
        out = []
        if _ol_sys.platform.startswith("linux"):
            with open("/proc/self/status") as f:
                for line in f:
                    if line.startswith("VmHWM:"):
                        out.append("rss_kb " + line.split()[1])
        elif _ol_os.name == "nt":
            import ctypes
            from ctypes import wintypes

            class _PMC(ctypes.Structure):
                _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD),
                            ("PeakWorkingSetSize", ctypes.c_size_t), ("WorkingSetSize", ctypes.c_size_t),
                            ("q1", ctypes.c_size_t), ("q2", ctypes.c_size_t), ("q3", ctypes.c_size_t),
                            ("q4", ctypes.c_size_t), ("PagefileUsage", ctypes.c_size_t),
                            ("PeakPagefileUsage", ctypes.c_size_t)]
            k = ctypes.WinDLL("kernel32")
            k.GetCurrentProcess.restype = wintypes.HANDLE
            pmc = _PMC()
            pmc.cb = ctypes.sizeof(pmc)
            if k.K32GetProcessMemoryInfo(k.GetCurrentProcess(), ctypes.byref(pmc), pmc.cb):
                out.append("rss_kb %d" % (pmc.PeakWorkingSetSize // 1024))
        else:
            import resource
            r = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
            out.append("rss_kb %d" % (r // 1024 if _ol_sys.platform == "darwin" else r))
        import time as _ol_time
        out.append("cpu_ms %.3f" % (_ol_time.process_time() * 1000.0))
        with open(p, "w") as f:
            f.write("\n".join(out) + "\n")
    except Exception:
        pass


_ol_atexit.register(_olieval_probe)
