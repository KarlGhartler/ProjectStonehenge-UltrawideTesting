#!/usr/bin/env python3
"""Sampling CPU profiler for ProjectStonehenge.exe (Windows, research only; no dependencies).

Starts the executable with the given arguments, waits, then samples every thread of the process
(suspend, read the context, walk the stack with dbghelp, resume) for a while and prints the
functions with the most samples: leaf ("self") and anywhere on the stack ("total").
Symbols come from the build's PDB through C:\\Windows\\System32\\dbghelp.dll.

Usage: sample_profile.py [--wait <s>] [--time <s>] [--interval <ms>] [--top <n>] [--waits 1] -- <exe> [args...]
--waits 1 keeps samples of waiting threads (where a thread blocks).
"""
import ctypes
import ctypes.wintypes as wt
import subprocess
import sys
import time
from collections import Counter

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
dbg = ctypes.WinDLL("dbghelp", use_last_error=True)

PROCESS_ALL_ACCESS = 0x1FFFFF
THREAD_ACCESS = 0x0002 | 0x0008 | 0x0040  # suspend/resume, get context, query information
TH32CS_SNAPTHREAD = 0x4
CONTEXT_FULL = 0x10000B
IMAGE_FILE_MACHINE_AMD64 = 0x8664


class THREADENTRY32(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ThreadID", wt.DWORD),
                ("th32OwnerProcessID", wt.DWORD), ("tpBasePri", wt.LONG), ("tpDeltaPri", wt.LONG),
                ("dwFlags", wt.DWORD)]


class SYMBOL_INFO(ctypes.Structure):
    _fields_ = [("SizeOfStruct", wt.ULONG), ("TypeIndex", wt.ULONG), ("Reserved", ctypes.c_uint64 * 2),
                ("Index", wt.ULONG), ("Size", wt.ULONG), ("ModBase", ctypes.c_uint64), ("Flags", wt.ULONG),
                ("Value", ctypes.c_uint64), ("Address", ctypes.c_uint64), ("Register", wt.ULONG),
                ("Scope", wt.ULONG), ("Tag", wt.ULONG), ("NameLen", wt.ULONG), ("MaxNameLen", wt.ULONG),
                ("Name", ctypes.c_char * 512)]


k32.OpenThread.restype = wt.HANDLE
k32.OpenProcess.restype = wt.HANDLE
k32.CreateToolhelp32Snapshot.restype = wt.HANDLE
dbg.SymFunctionTableAccess64.restype = ctypes.c_void_p
dbg.SymFunctionTableAccess64.argtypes = [wt.HANDLE, ctypes.c_uint64]
dbg.SymGetModuleBase64.restype = ctypes.c_uint64
dbg.SymGetModuleBase64.argtypes = [wt.HANDLE, ctypes.c_uint64]
dbg.StackWalk64.argtypes = [wt.DWORD, wt.HANDLE, wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p,
                            ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
dbg.SymFromAddr.argtypes = [wt.HANDLE, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(SYMBOL_INFO)]
dbg.SymInitialize.argtypes = [wt.HANDLE, ctypes.c_char_p, wt.BOOL]


def threads_of(pid):
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)
    te = THREADENTRY32()
    te.dwSize = ctypes.sizeof(te)
    out = []
    ok = k32.Thread32First(snap, ctypes.byref(te))
    while ok:
        if te.th32OwnerProcessID == pid:
            out.append(te.th32ThreadID)
        ok = k32.Thread32Next(snap, ctypes.byref(te))
    k32.CloseHandle(snap)
    return out


def main():
    args = sys.argv[1:]
    opts = {"--wait": 10.0, "--time": 10.0, "--interval": 2.0, "--top": 40, "--waits": 0}
    while args and args[0] != "--":
        opts[args[0]] = float(args[1])
        args = args[2:]
    if not args:
        print(__doc__)
        return 2
    cmd = args[1:]
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(opts["--wait"])
    hproc = k32.OpenProcess(PROCESS_ALL_ACCESS, False, proc.pid)
    dbg.SymSetOptions(0x2 | 0x4)  # SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS
    if not dbg.SymInitialize(hproc, None, True):
        print("SymInitialize failed", ctypes.get_last_error())
        return 1
    ctx_buf = (ctypes.c_byte * (1232 + 16))()
    base = (ctypes.addressof(ctx_buf) + 15) & ~15
    ctx = ctypes.c_void_p(base)
    frame = (ctypes.c_byte * 512)()
    fta = ctypes.cast(dbg.SymFunctionTableAccess64, ctypes.c_void_p)
    gmb = ctypes.cast(dbg.SymGetModuleBase64, ctypes.c_void_p)
    self_c, total_c, samples = Counter(), Counter(), 0
    names = {}
    sym = SYMBOL_INFO()

    def name(addr):
        if addr in names:
            return names[addr]
        sym.SizeOfStruct = 88
        sym.MaxNameLen = 511
        disp = ctypes.c_uint64()
        n = sym.Name.decode(errors="replace") if dbg.SymFromAddr(hproc, addr, ctypes.byref(disp), ctypes.byref(sym)) else "0x%X" % addr
        names[addr] = n
        return n

    handles = {}
    end = time.time() + opts["--time"]
    while time.time() < end and proc.poll() is None:
        for tid in threads_of(proc.pid):
            h = handles.get(tid) or k32.OpenThread(THREAD_ACCESS, False, tid)
            if not h:
                continue
            handles[tid] = h
            if k32.SuspendThread(h) == 0xFFFFFFFF:
                continue
            try:
                ctypes.memset(base, 0, 1232)
                ctypes.c_uint32.from_address(base + 0x30).value = CONTEXT_FULL
                if not k32.GetThreadContext(h, ctx):
                    continue
                rip = ctypes.c_uint64.from_address(base + 0xF8).value
                rsp = ctypes.c_uint64.from_address(base + 0x98).value
                rbp = ctypes.c_uint64.from_address(base + 0xA0).value
                ctypes.memset(frame, 0, 512)
                for off, val in ((0, rip), (32, rbp), (48, rsp)):
                    ctypes.c_uint64.from_buffer(frame, off).value = val
                    ctypes.c_uint32.from_buffer(frame, off + 12).value = 3  # AddrModeFlat
                stack = []
                for _ in range(48):
                    if not dbg.StackWalk64(IMAGE_FILE_MACHINE_AMD64, hproc, h, ctypes.byref(frame), ctx, None, fta, gmb, None):
                        break
                    pc = ctypes.c_uint64.from_buffer(frame, 0).value
                    if pc == 0:
                        break
                    stack.append(pc)
            finally:
                k32.ResumeThread(h)
            if not stack:
                continue
            fn = [name(a) for a in stack]
            # Skip threads that are only waiting.
            if not opts["--waits"] and any(w in fn[0] for w in ("NtWaitFor", "ZwWaitFor", "NtDelayExecution", "NtRemoveIoCompletion", "NtUserMsgWait", "ZwRemoveIoCompletion")):
                continue
            samples += 1
            self_c[fn[0]] += 1
            for f in set(fn):
                total_c[f] += 1
        time.sleep(opts["--interval"] / 1000.0)
    proc.kill()
    top = int(opts["--top"])
    print("busy samples:", samples)
    print("\n-- self --")
    for f, c in self_c.most_common(top):
        print("%6.1f%%  %s" % (100.0 * c / max(samples, 1), f))
    print("\n-- total --")
    for f, c in total_c.most_common(top):
        print("%6.1f%%  %s" % (100.0 * c / max(samples, 1), f))
    return 0


if __name__ == "__main__":
    sys.exit(main())
