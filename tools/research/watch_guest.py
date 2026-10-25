#!/usr/bin/env python3
"""Run ProjectStonehenge.exe under a minimal debugger and log every host write to up to 4 guest
addresses (x64 hardware data breakpoints), with the host call stack symbolised from the PDB.
Research tool for memory corruption (validation.md §2ag/§2ah); Windows only, no port changes.

Usage: python tools/research/watch_guest.py --elf <extracted disc>/SLUS_201.52 [--len 4] [--max-hits N] [--out FILE] --watch 0x004BA300[,0x...]
           -- <ProjectStonehenge.exe> <port arguments...>
--dump FILE writes guest RAM at hit --dump-hit (default 1). Each hit prints: hit number, guest address, old -> new word, then the stack (function+offset file:line).
The guest RAM base is found by scanning the process for the loaded SLUS_201.52 text (path in
--elf, the copy in the extracted-disc folder); breakpoints are armed once found.
"""
import argparse, ctypes, ctypes.wintypes as wt, sys, time

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
dbg = ctypes.WinDLL("dbghelp.dll", use_last_error=True)

DEBUG_ONLY_THIS_PROCESS = 0x2
EXCEPTION_DEBUG_EVENT, CREATE_THREAD_DEBUG_EVENT, CREATE_PROCESS_DEBUG_EVENT = 1, 2, 3
EXIT_THREAD_DEBUG_EVENT, EXIT_PROCESS_DEBUG_EVENT, LOAD_DLL_DEBUG_EVENT = 4, 5, 6
DBG_CONTINUE, DBG_EXCEPTION_NOT_HANDLED = 0x00010002, 0x80010001
EXC_BREAKPOINT, EXC_SINGLE_STEP, EXC_WX86_BREAKPOINT = 0x80000003, 0x80000004, 0x4000001F
CONTEXT_ALL = 0x10001F
CTX_SIZE = 0x4D0
OFF_FLAGS, OFF_DR0, OFF_DR6, OFF_DR7, OFF_RSP, OFF_RBP, OFF_RIP = 0x30, 0x48, 0x68, 0x70, 0x98, 0xA0, 0xF8


class STARTUPINFO(ctypes.Structure):
    _fields_ = [("cb", wt.DWORD), ("lpReserved", wt.LPWSTR), ("lpDesktop", wt.LPWSTR), ("lpTitle", wt.LPWSTR),
                ("dwX", wt.DWORD), ("dwY", wt.DWORD), ("dwXSize", wt.DWORD), ("dwYSize", wt.DWORD),
                ("dwXCountChars", wt.DWORD), ("dwYCountChars", wt.DWORD), ("dwFillAttribute", wt.DWORD),
                ("dwFlags", wt.DWORD), ("wShowWindow", wt.WORD), ("cbReserved2", wt.WORD),
                ("lpReserved2", ctypes.c_void_p), ("hStdInput", wt.HANDLE), ("hStdOutput", wt.HANDLE),
                ("hStdError", wt.HANDLE)]


class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [("hProcess", wt.HANDLE), ("hThread", wt.HANDLE), ("dwProcessId", wt.DWORD),
                ("dwThreadId", wt.DWORD)]


class DEBUG_EVENT(ctypes.Structure):
    _fields_ = [("code", wt.DWORD), ("pid", wt.DWORD), ("tid", wt.DWORD), ("pad", wt.DWORD),
                ("u", ctypes.c_uint64 * 20)]


class MBI(ctypes.Structure):
    _fields_ = [("BaseAddress", ctypes.c_uint64), ("AllocationBase", ctypes.c_uint64),
                ("AllocationProtect", wt.DWORD), ("PartitionId", wt.WORD), ("RegionSize", ctypes.c_uint64),
                ("State", wt.DWORD), ("Protect", wt.DWORD), ("Type", wt.DWORD)]


class ADDRESS64(ctypes.Structure):
    _fields_ = [("Offset", ctypes.c_uint64), ("Segment", wt.WORD), ("Mode", ctypes.c_int)]


class STACKFRAME64(ctypes.Structure):
    _fields_ = [("AddrPC", ADDRESS64), ("AddrReturn", ADDRESS64), ("AddrFrame", ADDRESS64),
                ("AddrStack", ADDRESS64), ("AddrBStore", ADDRESS64), ("FuncTableEntry", ctypes.c_void_p),
                ("Params", ctypes.c_uint64 * 4), ("Far", wt.BOOL), ("Virtual", wt.BOOL),
                ("Reserved", ctypes.c_uint64 * 3), ("KdHelp", ctypes.c_uint64 * 16)]


class SYMBOL_INFO(ctypes.Structure):
    _fields_ = [("SizeOfStruct", wt.ULONG), ("TypeIndex", wt.ULONG), ("Reserved", ctypes.c_uint64 * 2),
                ("Index", wt.ULONG), ("Size", wt.ULONG), ("ModBase", ctypes.c_uint64), ("Flags", wt.ULONG),
                ("Value", ctypes.c_uint64), ("Address", ctypes.c_uint64), ("Register", wt.ULONG),
                ("Scope", wt.ULONG), ("Tag", wt.ULONG), ("NameLen", wt.ULONG), ("MaxNameLen", wt.ULONG),
                ("Name", ctypes.c_char * 1024)]


class LINE64(ctypes.Structure):
    _fields_ = [("SizeOfStruct", wt.DWORD), ("Key", ctypes.c_void_p), ("LineNumber", wt.DWORD),
                ("FileName", ctypes.c_char_p), ("Address", ctypes.c_uint64)]


k32.WaitForDebugEvent.argtypes = [ctypes.POINTER(DEBUG_EVENT), wt.DWORD]
k32.ContinueDebugEvent.argtypes = [wt.DWORD, wt.DWORD, wt.DWORD]
k32.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_uint64, ctypes.c_void_p, ctypes.c_size_t,
                                  ctypes.POINTER(ctypes.c_size_t)]
k32.VirtualQueryEx.argtypes = [wt.HANDLE, ctypes.c_uint64, ctypes.POINTER(MBI), ctypes.c_size_t]
k32.VirtualQueryEx.restype = ctypes.c_size_t
k32.GetThreadContext.argtypes = [wt.HANDLE, ctypes.c_void_p]
k32.SetThreadContext.argtypes = [wt.HANDLE, ctypes.c_void_p]
k32.DebugBreakProcess.argtypes = [wt.HANDLE]
k32.OpenThread.restype = wt.HANDLE
dbg.SymInitialize.argtypes = [wt.HANDLE, ctypes.c_char_p, wt.BOOL]
dbg.SymLoadModuleEx.restype = ctypes.c_uint64
dbg.SymLoadModuleEx.argtypes = [wt.HANDLE, wt.HANDLE, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint64, wt.DWORD,
                                ctypes.c_void_p, wt.DWORD]
dbg.SymFromAddr.argtypes = [wt.HANDLE, ctypes.c_uint64, ctypes.POINTER(ctypes.c_uint64), ctypes.POINTER(SYMBOL_INFO)]
dbg.SymGetLineFromAddr64.argtypes = [wt.HANDLE, ctypes.c_uint64, ctypes.POINTER(wt.DWORD), ctypes.POINTER(LINE64)]
dbg.SymFunctionTableAccess64.restype = ctypes.c_void_p
dbg.SymFunctionTableAccess64.argtypes = [wt.HANDLE, ctypes.c_uint64]
dbg.SymGetModuleBase64.restype = ctypes.c_uint64
dbg.SymGetModuleBase64.argtypes = [wt.HANDLE, ctypes.c_uint64]
FTA = ctypes.WINFUNCTYPE(ctypes.c_void_p, wt.HANDLE, ctypes.c_uint64)(lambda p, a: dbg.SymFunctionTableAccess64(p, a))
GMB = ctypes.WINFUNCTYPE(ctypes.c_uint64, wt.HANDLE, ctypes.c_uint64)(lambda p, a: dbg.SymGetModuleBase64(p, a))
dbg.StackWalk64.argtypes = [wt.DWORD, wt.HANDLE, wt.HANDLE, ctypes.POINTER(STACKFRAME64), ctypes.c_void_p,
                            ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]


def read(proc, addr, n):
    buf = ctypes.create_string_buffer(n)
    got = ctypes.c_size_t(0)
    if not k32.ReadProcessMemory(proc, addr, buf, n, ctypes.byref(got)):
        return None
    return buf.raw[:got.value]


def elf_text(path):
    """64 bytes of the loaded image at guest 0x00100000 (first PT_LOAD of SLUS_201.52)."""
    import struct
    d = open(path, "rb").read()
    phoff, = struct.unpack_from("<I", d, 0x1C)
    phnum, = struct.unpack_from("<H", d, 0x2C)
    for i in range(phnum):
        typ, off, vaddr, _, filesz = struct.unpack_from("<5I", d, phoff + 32 * i)
        if typ == 1 and vaddr <= 0x00100100 < vaddr + filesz:
            o = off + 0x00100100 - vaddr
            return d[o:o + 64]
    raise SystemExit("no PT_LOAD at 0x00100100")


def find_ram(proc, sig):
    addr = 0
    mbi = MBI()
    while k32.VirtualQueryEx(proc, addr, ctypes.byref(mbi), ctypes.sizeof(mbi)):
        if mbi.State == 0x1000 and mbi.RegionSize >= 0x02000000 and mbi.Protect & 0x04:
            for head in range(0, 0x10000, 0x10):
                b = read(proc, mbi.BaseAddress + head + 0x00100100, 64)
                if b == sig:
                    return mbi.BaseAddress + head
        addr = mbi.BaseAddress + mbi.RegionSize
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--watch", required=True, help="comma-separated guest addresses (up to 4)")
    ap.add_argument("--len", type=int, default=4, choices=[1, 2, 4, 8])
    ap.add_argument("--max-hits", type=int, default=200)
    ap.add_argument("--elf", required=True, help="SLUS_201.52 from the extracted-disc folder")
    ap.add_argument("--out", default=None)
    ap.add_argument("--dump", default=None, help="write guest RAM (32 MiB) to this file at hit --dump-hit")
    ap.add_argument("--dump-hit", type=int, default=1)
    ap.add_argument("cmd", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    cmd = a.cmd[1:] if a.cmd[:1] == ["--"] else a.cmd
    watches = [int(x, 16) & ~(a.len - 1) for x in a.watch.split(",")][:4]
    out = open(a.out, "w", encoding="utf-8") if a.out else sys.stdout

    def log(s):
        out.write(s + "\n")
        out.flush()

    sig = elf_text(a.elf)
    si = STARTUPINFO()
    si.cb = ctypes.sizeof(si)
    pi = PROCESS_INFORMATION()
    line = " ".join('"%s"' % c if " " in c else c for c in cmd)
    if not k32.CreateProcessW(None, ctypes.create_unicode_buffer(line), None, None, False,
                              DEBUG_ONLY_THIS_PROCESS, None, None, ctypes.byref(si), ctypes.byref(pi)):
        raise SystemExit("CreateProcess failed: %d" % ctypes.get_last_error())
    proc = pi.hProcess
    dbg.SymSetOptions(0x2 | 0x10)
    dbg.SymInitialize(proc, None, False)
    threads = {}
    image = 0
    ram = None
    armed = False
    break_pending = False
    hits = 0
    last = {w: None for w in watches}
    t0 = time.time()
    lenbits = {1: 0, 2: 1, 4: 3, 8: 2}[a.len]
    dr7 = 0
    for i in range(len(watches)):
        dr7 |= (1 << (2 * i)) | ((1 | (lenbits << 2)) << (16 + 4 * i))  # local enable, RW=01 write

    ctx = ctypes.create_string_buffer(CTX_SIZE + 16)
    cptr = (ctypes.addressof(ctx) + 15) & ~15

    def getctx(h):
        ctypes.c_uint32.from_address(cptr + OFF_FLAGS).value = CONTEXT_ALL
        return k32.GetThreadContext(h, cptr)

    def arm(h):
        if not getctx(h):
            return
        for i, w in enumerate(watches):
            ctypes.c_uint64.from_address(cptr + OFF_DR0 + 8 * i).value = ram + w
        ctypes.c_uint64.from_address(cptr + OFF_DR7).value = dr7
        ctypes.c_uint32.from_address(cptr + OFF_FLAGS).value = 0x100010
        k32.SetThreadContext(h, cptr)

    def sym(addr):
        s = SYMBOL_INFO()
        s.SizeOfStruct = ctypes.sizeof(SYMBOL_INFO) - 1024
        s.MaxNameLen = 1024
        disp = ctypes.c_uint64(0)
        name = "?"
        if dbg.SymFromAddr(proc, addr, ctypes.byref(disp), ctypes.byref(s)):
            name = "%s+0x%X" % (s.Name.decode(errors="replace"), disp.value)
        ln = LINE64()
        ln.SizeOfStruct = ctypes.sizeof(LINE64)
        d32 = wt.DWORD(0)
        where = ""
        if dbg.SymGetLineFromAddr64(proc, addr, ctypes.byref(d32), ctypes.byref(ln)):
            where = " %s:%d" % (ln.FileName.decode(errors="replace").split("AC4_recomp\\")[-1], ln.LineNumber)
        off = " exe+0x%X" % (addr - image) if image <= addr < image + 0x2000000 else ""
        return "%s%s%s" % (name, where, off)

    def stack(h):
        getctx(h)
        f = STACKFRAME64()
        f.AddrPC.Offset = ctypes.c_uint64.from_address(cptr + OFF_RIP).value
        f.AddrFrame.Offset = ctypes.c_uint64.from_address(cptr + OFF_RBP).value
        f.AddrStack.Offset = ctypes.c_uint64.from_address(cptr + OFF_RSP).value
        f.AddrPC.Mode = f.AddrFrame.Mode = f.AddrStack.Mode = 3
        frames = []
        while len(frames) < 24 and dbg.StackWalk64(0x8664, proc, h, ctypes.byref(f), cptr, None,
                                                    ctypes.cast(FTA, ctypes.c_void_p),
                                                    ctypes.cast(GMB, ctypes.c_void_p), None):
            if f.AddrPC.Offset == 0:
                break
            frames.append(f.AddrPC.Offset)
        return frames

    ev = DEBUG_EVENT()
    while True:
        if not k32.WaitForDebugEvent(ctypes.byref(ev), 250):
            if ram is None and image and time.time() - t0 > 0.5:
                ram = find_ram(proc, sig)
                if ram is not None:
                    log("guest RAM at host 0x%X; arming %s" % (ram, ", ".join("0x%08X" % w for w in watches)))
                    for w in watches:
                        b = read(proc, ram + w, 4)
                        last[w] = int.from_bytes(b, "little") if b else None
                    break_pending = True
                    k32.DebugBreakProcess(proc)
            continue
        cont = DBG_CONTINUE
        code = ev.code
        u = ev.u
        if code == CREATE_PROCESS_DEBUG_EVENT:
            # hFile, hProcess, hThread, lpBaseOfImage
            image = u[3]
            threads[ev.tid] = u[2]
            dbg.SymLoadModuleEx(proc, u[0], cmd[0].encode(), None, image, 0, None, 0)
        elif code == CREATE_THREAD_DEBUG_EVENT:
            threads[ev.tid] = u[0]
            if armed:
                arm(u[0])
        elif code == EXIT_THREAD_DEBUG_EVENT:
            threads.pop(ev.tid, None)
        elif code == EXIT_PROCESS_DEBUG_EVENT:
            log("process exited with code 0x%X after %d hit(s)" % (u[0] & 0xFFFFFFFF, hits))
            break
        elif code == LOAD_DLL_DEBUG_EVENT:
            if u[0]:
                k32.CloseHandle(wt.HANDLE(u[0]))
        elif code == EXCEPTION_DEBUG_EVENT:
            exc = u[0] & 0xFFFFFFFF
            exc_addr = u[2]
            first_chance = ctypes.c_uint32.from_buffer(ev, 16 + 152).value
            if exc in (EXC_BREAKPOINT, EXC_WX86_BREAKPOINT):
                if break_pending and not armed:
                    for h in threads.values():
                        arm(h)
                    armed = True
                    log("armed on %d thread(s)" % len(threads))
            elif exc == EXC_SINGLE_STEP and armed:
                h = threads.get(ev.tid)
                getctx(h)
                dr6 = ctypes.c_uint64.from_address(cptr + OFF_DR6).value
                which = [watches[i] for i in range(len(watches)) if dr6 >> i & 1]
                ctypes.c_uint64.from_address(cptr + OFF_DR6).value = 0
                ctypes.c_uint32.from_address(cptr + OFF_FLAGS).value = 0x100010
                k32.SetThreadContext(h, cptr)
                if which:
                    hits += 1
                    for w in which:
                        b = read(proc, ram + w, 4)
                        new = int.from_bytes(b, "little") if b else None
                        log("hit %d (%.1f s, thread %d): guest 0x%08X 0x%08X -> 0x%08X" %
                            (hits, time.time() - t0, ev.tid, w, last[w] or 0, new or 0))
                        last[w] = new
                    for pc in stack(h):
                        log("    " + sym(pc))
                    if a.dump and hits == a.dump_hit:
                        with open(a.dump, "wb") as df:
                            for o in range(0, 0x02000000, 0x100000):
                                df.write(read(proc, ram + o, 0x100000))
                        log("guest RAM written to " + a.dump)
                    if hits >= a.max_hits:
                        log("max hits reached; terminating")
                        k32.TerminateProcess(proc, 1)
                else:
                    cont = DBG_EXCEPTION_NOT_HANDLED
            else:
                cont = DBG_EXCEPTION_NOT_HANDLED
                if not first_chance:
                    log("second-chance exception 0x%08X at %s" % (exc, sym(exc_addr)))
                    h = threads.get(ev.tid)
                    if h:
                        for pc in stack(h):
                            log("    " + sym(pc))
        k32.ContinueDebugEvent(ev.pid, ev.tid, cont)


if __name__ == "__main__":
    main()
