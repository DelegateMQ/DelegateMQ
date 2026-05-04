"""
dmq_databus.py — Python library for DelegateMQ cross-language interop.

This module provides a high-level Python interface for communicating with C++ 
DelegateMQ applications. It uses a "Shared Native Core" architecture, wrapping 
the native DmqInterop DLL/SO via ctypes.

Key Features:
- Native Performance: Network handling and reliability logic run on a native thread.
- Thread Safety: Callbacks are invoked on a native background thread.
- Consistency: Shares the same wire-protocol and reliability layer as the C++ core.

Requirements:
- DmqInterop.dll (Windows) or libDmqInterop.so (Linux) must be built and accessible.
- pip install msgpack

Architecture Details: docs/INTEROP.md
"""

import ctypes
import os
import sys
import msgpack

class DmqDataBus:
    """
    Python wrapper for the native DmqInterop DLL.
    """
    def __init__(self, dll_path=None):
        if dll_path is None:
            # Default to looking in the same directory as the script
            dll_name = "DmqInterop.dll" if sys.platform == "win32" else "libDmqInterop.so"
            dll_path = os.path.join(os.path.dirname(__file__), dll_name)
        
        if not os.path.exists(dll_path):
            raise FileNotFoundError(f"Could not find native library at {dll_path}")

        self._dll = ctypes.CDLL(dll_path)
        
        # Define C-API signatures
        self._dll.DmqInterop_Start.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int]
        self._dll.DmqInterop_Start.restype = ctypes.c_int
        
        self._CALLBACK_TYPE = ctypes.WINFUNCTYPE(None, ctypes.c_uint16, ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint32) if sys.platform == "win32" else \
                             ctypes.CFUNCTYPE(None, ctypes.c_uint16, ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint32)
        
        self._dll.DmqInterop_RegisterCallback.argtypes = [ctypes.c_uint16, self._CALLBACK_TYPE]
        self._dll.DmqInterop_RegisterCallback.restype = None
        
        self._ERROR_CALLBACK_TYPE = ctypes.WINFUNCTYPE(None, ctypes.c_char_p) if sys.platform == "win32" else \
                                   ctypes.CFUNCTYPE(None, ctypes.c_char_p)
        
        self._dll.DmqInterop_RegisterErrorCallback.argtypes = [self._ERROR_CALLBACK_TYPE]
        self._dll.DmqInterop_RegisterErrorCallback.restype = None

        self._dll.DmqInterop_Send.argtypes = [ctypes.c_uint16, ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint32]
        self._dll.DmqInterop_Send.restype = ctypes.c_int
        
        self._dll.DmqInterop_Stop.argtypes = []
        self._dll.DmqInterop_Stop.restype = None

        self._callbacks = {}
        # Keep references to the wrapper functions to prevent GC
        self._native_callbacks = {}
        self._native_error_callback = None

    def start(self, remote_host, recv_port, send_port):
        res = self._dll.DmqInterop_Start(remote_host.encode('utf-8'), recv_port, send_port)
        if res != 0:
            raise RuntimeError(f"DmqInterop_Start failed with error {res}")

    def register_error_callback(self, callback):
        def wrapper(msg):
            callback(msg.decode('utf-8'))
        
        native_cb = self._ERROR_CALLBACK_TYPE(wrapper)
        self._native_error_callback = native_cb
        self._dll.DmqInterop_RegisterErrorCallback(native_cb)

    def register_callback(self, remote_id, callback):
        def wrapper(rid, data_ptr, length):
            # Convert raw pointer to bytes
            data = ctypes.string_at(data_ptr, length)
            callback(data)

        native_cb = self._CALLBACK_TYPE(wrapper)
        self._native_callbacks[remote_id] = native_cb
        self._dll.DmqInterop_RegisterCallback(remote_id, native_cb)

    def send(self, remote_id, obj):
        payload = msgpack.packb(obj)
        ptr = (ctypes.c_uint8 * len(payload))(*payload)
        res = self._dll.DmqInterop_Send(remote_id, ptr, len(payload))
        if res != 0:
            raise RuntimeError(f"DmqInterop_Send failed with error {res}")

    def stop(self):
        self._dll.DmqInterop_Stop()
