# SPDX-License-Identifier: GPL-3.0-or-later
"""Read / write the memory of the running game (developer tools only; Windows, no extra packages)."""
import ctypes
import ctypes.wintypes as wt
import struct
import time

PROCESS_NAME = 'carrier_command.exe'

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
TH32CS_SNAPPROCESS, TH32CS_SNAPMODULE, TH32CS_SNAPMODULE32 = 0x2, 0x8, 0x10
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
PROCESS_ACCESS = 0x0008 | 0x0010 | 0x0020 | 0x0400          # vm operation / read / write, query


class PROCESSENTRY32W(ctypes.Structure):
    _fields_ = [('dwSize', wt.DWORD), ('cntUsage', wt.DWORD), ('th32ProcessID', wt.DWORD),
                ('th32DefaultHeapID', ctypes.c_void_p), ('th32ModuleID', wt.DWORD),
                ('cntThreads', wt.DWORD), ('th32ParentProcessID', wt.DWORD),
                ('pcPriClassBase', ctypes.c_long), ('dwFlags', wt.DWORD),
                ('szExeFile', ctypes.c_wchar * 260)]


class MODULEENTRY32W(ctypes.Structure):
    _fields_ = [('dwSize', wt.DWORD), ('th32ModuleID', wt.DWORD), ('th32ProcessID', wt.DWORD),
                ('GlblcntUsage', wt.DWORD), ('ProccntUsage', wt.DWORD),
                ('modBaseAddr', ctypes.c_void_p), ('modBaseSize', wt.DWORD),
                ('hModule', ctypes.c_void_p), ('szModule', ctypes.c_wchar * 256),
                ('szExePath', ctypes.c_wchar * 260)]


_v = ctypes.c_void_p
k32.CreateToolhelp32Snapshot.argtypes = [wt.DWORD, wt.DWORD]
k32.CreateToolhelp32Snapshot.restype = _v
k32.Process32FirstW.argtypes = [_v, ctypes.POINTER(PROCESSENTRY32W)]
k32.Process32NextW.argtypes = [_v, ctypes.POINTER(PROCESSENTRY32W)]
k32.Module32FirstW.argtypes = [_v, ctypes.POINTER(MODULEENTRY32W)]
k32.Module32NextW.argtypes = [_v, ctypes.POINTER(MODULEENTRY32W)]
k32.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
k32.OpenProcess.restype = _v
k32.CloseHandle.argtypes = [_v]
k32.ReadProcessMemory.argtypes = [_v, _v, _v, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
k32.WriteProcessMemory.argtypes = [_v, _v, _v, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]


class ProcError(Exception):
    pass


def find_process(name):
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    if snap in (None, INVALID_HANDLE_VALUE):
        return None
    try:
        e = PROCESSENTRY32W()
        e.dwSize = ctypes.sizeof(e)
        ok = k32.Process32FirstW(snap, ctypes.byref(e))
        while ok:
            if e.szExeFile.lower() == name:
                return e.th32ProcessID
            ok = k32.Process32NextW(snap, ctypes.byref(e))
    finally:
        k32.CloseHandle(snap)
    return None


def module_base(pid, name):
    """(base address, path) of a module loaded in the process"""
    for _ in range(20):
        snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)
        if snap in (None, INVALID_HANDLE_VALUE):
            time.sleep(0.5)        # ERROR_PARTIAL_COPY while the process is starting
            continue
        try:
            e = MODULEENTRY32W()
            e.dwSize = ctypes.sizeof(e)
            ok = k32.Module32FirstW(snap, ctypes.byref(e))
            while ok:
                if e.szModule.lower() == name:
                    return e.modBaseAddr, e.szExePath
                ok = k32.Module32NextW(snap, ctypes.byref(e))
        finally:
            k32.CloseHandle(snap)
        time.sleep(0.5)
    raise ProcError('module %s not found in the game process' % name)


class Process:
    def __init__(self, pid):
        self.pid = pid
        self.h = k32.OpenProcess(PROCESS_ACCESS, False, pid)
        if not self.h:
            raise ProcError('OpenProcess failed (error %d)' % ctypes.get_last_error())

    def read(self, addr, n):
        buf = ctypes.create_string_buffer(n)
        got = ctypes.c_size_t()
        if not k32.ReadProcessMemory(self.h, ctypes.c_void_p(addr), buf, n, ctypes.byref(got)) or got.value != n:
            raise ProcError('ReadProcessMemory(0x%x, %d) failed (error %d)' % (addr, n, ctypes.get_last_error()))
        return buf.raw

    def read_u32(self, addr):
        return struct.unpack('<I', self.read(addr, 4))[0]

    def write(self, addr, data):
        got = ctypes.c_size_t()
        if not k32.WriteProcessMemory(self.h, ctypes.c_void_p(addr), data, len(data), ctypes.byref(got)) \
                or got.value != len(data):
            raise ProcError('WriteProcessMemory(0x%x) failed (error %d)' % (addr, ctypes.get_last_error()))
