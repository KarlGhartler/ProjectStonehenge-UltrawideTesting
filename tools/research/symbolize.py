#!/usr/bin/env python3
"""Symbolise "ProjectStonehenge.exe+0x..." offsets from the session log (crash reports, invalid guest
accesses) with the executable's PDB (dbghelp, Windows only).

Usage: python tools/research/symbolize.py <ProjectStonehenge.exe> <offset> [<offset> ...]
       python tools/research/symbolize.py <ProjectStonehenge.exe> --log <session.log>
The executable must be the exact build that wrote the log (its PDB next to it).
With --log, every "ProjectStonehenge.exe+0x..." in the log is resolved once and printed.
"""
import ctypes, ctypes.wintypes as wt, sys
dbg = ctypes.WinDLL("dbghelp.dll", use_last_error=True)

class SYMBOL_INFO(ctypes.Structure):
    _fields_ = [("SizeOfStruct", wt.ULONG), ("TypeIndex", wt.ULONG), ("Reserved", ctypes.c_uint64 * 2),
                ("Index", wt.ULONG), ("Size", wt.ULONG), ("ModBase", ctypes.c_uint64), ("Flags", wt.ULONG),
                ("Value", ctypes.c_uint64), ("Address", ctypes.c_uint64), ("Register", wt.ULONG),
                ("Scope", wt.ULONG), ("Tag", wt.ULONG), ("NameLen", wt.ULONG), ("MaxNameLen", wt.ULONG),
                ("Name", ctypes.c_char * 1024)]

class LINE64(ctypes.Structure):
    _fields_ = [("SizeOfStruct", wt.DWORD), ("Key", ctypes.c_void_p), ("LineNumber", wt.DWORD),
                ("FileName", ctypes.c_char_p), ("Address", ctypes.c_uint64)]

dbg.SymInitialize.argtypes = [wt.HANDLE, ctypes.c_char_p, wt.BOOL]
dbg.SymLoadModuleEx.restype = ctypes.c_uint64
dbg.SymLoadModuleEx.argtypes = [wt.HANDLE, wt.HANDLE, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64, wt.DWORD,
                                ctypes.c_void_p, wt.DWORD]
dbg.SymFromAddr.argtypes = [wt.HANDLE, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(SYMBOL_INFO)]
dbg.SymGetLineFromAddr64.argtypes = [wt.HANDLE, ctypes.c_uint64, ctypes.POINTER(wt.DWORD), ctypes.POINTER(LINE64)]
dbg.SymSetOptions(0x2 | 0x10)  # UNDNAME | LOAD_LINES
h = wt.HANDLE(0x1234)
exe = sys.argv[1]
args = sys.argv[2:]
if args[:1] == ["--log"]:
    import re
    text = open(args[1], encoding="utf-8", errors="replace").read()
    args = sorted(set(re.findall(r"ProjectStonehenge\.exe\+0x([0-9a-fA-F]+)", text)), key=lambda a: int(a, 16))
assert dbg.SymInitialize(h, None, False)
base = 0x140000000
assert dbg.SymLoadModuleEx(h, None, exe.encode(), None, base, 0, None, 0), ctypes.get_last_error()
for a in args:
    addr = base + int(a, 16)
    sym = SYMBOL_INFO(); sym.SizeOfStruct = 88; sym.MaxNameLen = 1024
    disp = ctypes.c_uint64()
    n = sym.Name.decode() if dbg.SymFromAddr(h, addr, ctypes.byref(disp), ctypes.byref(sym)) else "?"
    ln = LINE64(); ln.SizeOfStruct = ctypes.sizeof(LINE64); d2 = wt.DWORD()
    loc = ""
    if dbg.SymGetLineFromAddr64(h, addr - 1, ctypes.byref(d2), ctypes.byref(ln)):
        loc = "%s:%d" % (ln.FileName.decode().split("AC4_recomp\\")[-1], ln.LineNumber)
    print("%s %s+0x%X %s" % (a, n, disp.value, loc))
