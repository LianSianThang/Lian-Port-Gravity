"""Set this launcher's native icons and Windows taskbar identity (no shell settings)."""
import ctypes as C
from pathlib import Path
import sys
import uuid

APP_ID = 'LianPort.GravityRush.Launcher'


class GUID(C.Structure):
    _fields_ = [('data1', C.c_uint32), ('data2', C.c_uint16), ('data3', C.c_uint16),
                ('data4', C.c_ubyte * 8)]


class PROPERTYKEY(C.Structure):
    _fields_ = [('fmtid', GUID), ('pid', C.c_uint32)]


class VALUE(C.Union):
    _fields_ = [('pointer', C.c_void_p), ('storage', C.c_ubyte * 16)]


class PROPVARIANT(C.Structure):
    _fields_ = [('vt', C.c_uint16), ('reserved', C.c_uint16 * 3), ('value', VALUE)]


def guid(text):
    return GUID.from_buffer_copy(uuid.UUID(text).bytes_le)


def apply_identity(root, icon_path, command):
    if sys.platform != 'win32':
        return {}
    if C.sizeof(C.c_void_p) != 8:
        raise OSError('Lian Port requires 64-bit Windows.')
    user = C.WinDLL('user32', use_last_error=True)
    shell = C.WinDLL('shell32')
    ole = C.WinDLL('ole32')
    user.GetAncestor.argtypes = [C.c_void_p, C.c_uint]
    user.GetAncestor.restype = C.c_void_p
    user.LoadImageW.argtypes = [C.c_void_p, C.c_wchar_p, C.c_uint, C.c_int, C.c_int, C.c_uint]
    user.LoadImageW.restype = C.c_void_p
    user.SendMessageW.argtypes = [C.c_void_p, C.c_uint, C.c_size_t, C.c_ssize_t]
    user.SendMessageW.restype = C.c_ssize_t
    hwnd = user.GetAncestor(root.winfo_id(), 2)  # GA_ROOT: Tk's outer native window.
    if not hwnd:
        raise C.WinError(C.get_last_error())
    icon_path = str(Path(icon_path).resolve())
    handles = {}
    for kind, metric in ((0, 49), (1, 11)):  # ICON_SMALL / ICON_BIG; SM_CXSMICON / SM_CXICON.
        size = user.GetSystemMetrics(metric)
        handle = user.LoadImageW(None, icon_path, 1, size, size, 0x10)
        if not handle:
            raise C.WinError(C.get_last_error())
        user.SendMessageW(hwnd, 0x80, kind, handle)  # WM_SETICON.
        handles[kind] = handle
    # These icon handles belong to this window for the lifetime of its process.
    root._native_taskbar_icons = handles
    ole.CoInitializeEx.argtypes = [C.c_void_p, C.c_uint32]
    ole.CoInitializeEx.restype = C.c_int32
    initialized = ole.CoInitializeEx(None, 2)
    store = C.c_void_p()
    iid = guid('886d8eeb-8cf2-4446-8d02-cdba1dbdcf99')
    shell.SHGetPropertyStoreForWindow.argtypes = [C.c_void_p, C.POINTER(GUID), C.POINTER(C.c_void_p)]
    shell.SHGetPropertyStoreForWindow.restype = C.c_int32
    try:
        result = shell.SHGetPropertyStoreForWindow(hwnd, C.byref(iid), C.byref(store))
        if result < 0:
            raise OSError(f'Window property store failed: 0x{result & 0xffffffff:08x}')
        table = C.cast(store, C.POINTER(C.POINTER(C.c_void_p))).contents
        set_value = C.WINFUNCTYPE(C.c_int32, C.c_void_p, C.POINTER(PROPERTYKEY), C.POINTER(PROPVARIANT))(table[6])
        get_value = C.WINFUNCTYPE(C.c_int32, C.c_void_p, C.POINTER(PROPERTYKEY), C.POINTER(PROPVARIANT))(table[5])
        release = C.WINFUNCTYPE(C.c_uint32, C.c_void_p)(table[2])
        fmtid = guid('9f4c2855-9f79-4b39-a8d0-e1d42de1d5f3')
        values = {2: command, 3: icon_path + ',0', 4: 'Lian Port — Gravity Rush Remastered', 5: APP_ID}
        readback = {}
        try:
            # Set relaunch properties before the ID, which notifies the taskbar to refresh.
            for pid, text in values.items():
                key = PROPERTYKEY(fmtid, pid)
                buffer = C.create_unicode_buffer(text)
                value = PROPVARIANT()
                value.vt = 31  # VT_LPWSTR; SetValue copies this caller-owned string.
                value.value.pointer = C.cast(buffer, C.c_void_p).value
                result = set_value(store, C.byref(key), C.byref(value))
                if result < 0:
                    raise OSError(f'Taskbar property {pid} failed: 0x{result & 0xffffffff:08x}')
                result_value = PROPVARIANT()
                if get_value(store, C.byref(key), C.byref(result_value)) >= 0:
                    try:
                        readback[str(pid)] = C.wstring_at(result_value.value.pointer) if result_value.vt == 31 else None
                    finally:
                        ole.PropVariantClear(C.byref(result_value))
        finally:
            release(store)
        return {'properties': readback,
                'small_icon': user.SendMessageW(hwnd, 0x7f, 0, 0),
                'large_icon': user.SendMessageW(hwnd, 0x7f, 1, 0)}
    finally:
        if initialized >= 0:
            ole.CoUninitialize()
