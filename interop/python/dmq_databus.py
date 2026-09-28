"""
dmq_databus.py — Python library for DelegateMQ cross-language interop.

This module provides a high-level Python interface for communicating with C++
DelegateMQ applications. It uses a "Shared Native Core" architecture, wrapping
the native DmqInterop DLL/SO via ctypes.

Key Features:
- Native Performance: Network handling and reliability logic run on native threads.
- Reliable Sends: Each outgoing message is tracked until the peer ACKs it and
  resent on timeout; its outcome is reported through the status callback.
- Thread Safety: Callbacks are invoked on native background threads.
- Consistency: Shares the same wire-protocol and reliability layer as the C++ core.

Requirements:
- DmqInterop.dll (Windows) or libDmqInterop.so (Linux) must be built and accessible.
- pip install msgpack

Architecture Details: docs/INTEROP.md
"""

import ctypes
import os
import sys
import traceback
from enum import IntEnum

import msgpack


class SendStatus(IntEnum):
    """Outcome of one outgoing message (mirrors DmqSendStatus in DmqInterop.h)."""
    ACKED = 0            # Peer acknowledged the message. Final.
    TIMEOUT = 1          # An attempt timed out; a retry follows unless DELIVERY_FAILED is next.
    DELIVERY_FAILED = 2  # Abandoned: retries exhausted or the send failed immediately. Final.


class ErrorCode(IntEnum):
    """Error categories (mirrors DmqErrorCode in DmqInterop.h)."""
    EXCEPTION = 1         # Native exception; message has details.
    CAP_EXCEEDED = 2      # Too many unacknowledged messages; sends fail until slots free.
    PENDING_EXCEEDED = 3  # Timed-out messages piling up (link down or peer overloaded).
    FAULT = 4             # Library fault; the process aborts after the callback.
    WATCHDOG = 5          # Thread watchdog expired; the process aborts after the callback.
    CALLBACK = 6          # An exception in a user callback (raised by this wrapper).
    TRANSPORT = 7         # A transport couldn't be created (bind/join failed).


if sys.platform == "win32":
    _FUNCTYPE = ctypes.WINFUNCTYPE
else:
    _FUNCTYPE = ctypes.CFUNCTYPE

# The leading c_void_p is the C API's context pointer. The wrapper passes NULL:
# Python closures already carry their own state.
_MESSAGE_CB = _FUNCTYPE(None, ctypes.c_void_p, ctypes.c_uint16, ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint32)
_STATUS_CB = _FUNCTYPE(None, ctypes.c_void_p, ctypes.c_uint16, ctypes.c_uint16, ctypes.c_int)
_ERROR_CB = _FUNCTYPE(None, ctypes.c_void_p, ctypes.c_int, ctypes.c_uint16, ctypes.c_char_p)


class DmqDataBus:
    """
    Python wrapper for the native DmqInterop DLL.

    Callbacks run on native threads. An exception raised in a message or status
    callback is reported to the error callback as ErrorCode.CALLBACK instead of
    being lost. Without an error callback, errors are printed to stderr.

    Usable as a context manager: ``with DmqDataBus(path) as bus: ...`` stops the
    native threads on exit.
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
        self._dll.DmqInterop_SetReliability.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int]
        self._dll.DmqInterop_SetReliability.restype = ctypes.c_int
        self._dll.DmqInterop_Start.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_int, ctypes.c_char_p]
        self._dll.DmqInterop_Start.restype = ctypes.c_int
        self._dll.DmqInterop_RegisterCallback.argtypes = [ctypes.c_uint16, _MESSAGE_CB, ctypes.c_void_p]
        self._dll.DmqInterop_RegisterCallback.restype = None
        self._dll.DmqInterop_RegisterStatusCallback.argtypes = [_STATUS_CB, ctypes.c_void_p]
        self._dll.DmqInterop_RegisterStatusCallback.restype = None
        self._dll.DmqInterop_RegisterErrorCallback.argtypes = [_ERROR_CB, ctypes.c_void_p]
        self._dll.DmqInterop_RegisterErrorCallback.restype = None
        self._dll.DmqInterop_Send.argtypes = [ctypes.c_uint16, ctypes.POINTER(ctypes.c_uint8),
                                              ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint16)]
        self._dll.DmqInterop_Send.restype = ctypes.c_int
        self._dll.DmqInterop_Stop.argtypes = []
        self._dll.DmqInterop_Stop.restype = None

        self._on_error = None
        self._on_status = None
        # Native callback objects must stay referenced or ctypes frees them.
        self._native_callbacks = {}
        self._native_status_cb = _STATUS_CB(self._status_trampoline)
        self._native_error_cb = _ERROR_CB(self._error_trampoline)

        # Always registered, so native errors are reported (to stderr by default)
        # even before the application registers its own handlers.
        self._dll.DmqInterop_RegisterErrorCallback(self._native_error_cb, None)
        self._dll.DmqInterop_RegisterStatusCallback(self._native_status_cb, None)

    # ---- Lifecycle -------------------------------------------------------

    def set_reliability(self, enabled=True, timeout_ms=0, max_retries=-1):
        """
        Configure outgoing reliability. Call before start().

        enabled: True (default) tracks ACKs and retries; False sends
                 fire-and-forget with no status callbacks (for peers that don't ACK).
        timeout_ms: time to wait for an ACK per attempt; <= 0 keeps the default (2 s).
        max_retries: resends after the first attempt; < 0 keeps the default (3),
                     0 tracks ACKs without resending.
        """
        if self._dll.DmqInterop_SetReliability(1 if enabled else 0, timeout_ms, max_retries) != 0:
            raise RuntimeError("set_reliability() must be called before start()")

    def start(self, remote_host, recv_port, send_port, multicast_group):
        """
        remote_host: IP of the remote peer (unicast send/command channel).
        recv_port: local port to join for incoming messages.
        send_port: remote port to send messages to (ACKs come back on the same socket).
        multicast_group: multicast group address to join for the incoming channel,
        so multiple clients can receive the same stream concurrently.
        """
        res = self._dll.DmqInterop_Start(remote_host.encode('utf-8'), recv_port, send_port,
                                         multicast_group.encode('utf-8'))
        if res != 0:
            raise RuntimeError(f"DmqInterop_Start failed with error {res}")

    def stop(self):
        """Stop the native threads and close the sockets. Blocks until they exit."""
        self._dll.DmqInterop_Stop()

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.stop()
        return False

    # ---- Callbacks -------------------------------------------------------

    def register_callback(self, remote_id, callback):
        """Call callback(data: bytes) for each message received on remote_id."""
        def wrapper(_context, rid, data_ptr, length):
            try:
                callback(ctypes.string_at(data_ptr, length))
            except Exception:
                self._report_error(ErrorCode.CALLBACK, rid,
                                   f"Exception in callback for remote ID {rid}:\n{traceback.format_exc()}")

        native_cb = _MESSAGE_CB(wrapper)
        self._native_callbacks[remote_id] = native_cb
        self._dll.DmqInterop_RegisterCallback(remote_id, native_cb, None)

    def unregister_callback(self, remote_id):
        """Stop delivering messages for remote_id."""
        self._dll.DmqInterop_RegisterCallback(remote_id, _MESSAGE_CB(), None)
        self._native_callbacks.pop(remote_id, None)

    def register_status_callback(self, callback):
        """
        Call callback(remote_id: int, seq: int, status: SendStatus) as each sent
        message is acknowledged, times out, or is abandoned. seq matches the value
        returned by send().
        """
        self._on_status = callback

    def register_error_callback(self, callback):
        """Call callback(code: ErrorCode, remote_id: int, message: str) on errors."""
        self._on_error = callback

    # ---- Sending ---------------------------------------------------------

    def send(self, remote_id, data):
        """
        Send an object or raw bytes to a Remote ID and return its sequence number.
        If data is already 'bytes', it is sent directly. Otherwise, it is
        serialized using MessagePack. The delivery outcome arrives later through
        the status callback, tagged with the returned sequence number.
        """
        if isinstance(data, (bytes, bytearray)):
            payload = bytes(data)
        else:
            payload = msgpack.packb(data, use_bin_type=True)

        buf = (ctypes.c_uint8 * len(payload)).from_buffer_copy(payload)
        seq = ctypes.c_uint16(0)
        res = self._dll.DmqInterop_Send(remote_id, buf, len(payload), ctypes.byref(seq))
        if res != 0:
            raise RuntimeError(f"DmqInterop_Send failed with error {res}")
        return seq.value

    # ---- Internals -------------------------------------------------------

    def _status_trampoline(self, _context, remote_id, seq, status):
        cb = self._on_status
        if cb is None:
            return
        try:
            cb(remote_id, seq, SendStatus(status))
        except Exception:
            self._report_error(ErrorCode.CALLBACK, remote_id,
                               f"Exception in status callback:\n{traceback.format_exc()}")

    def _error_trampoline(self, _context, code, remote_id, msg):
        self._report_error(ErrorCode(code), remote_id, msg.decode('utf-8', errors='replace') if msg else "")

    def _report_error(self, code, remote_id, message):
        cb = self._on_error
        if cb is not None:
            try:
                cb(code, remote_id, message)
                return
            except Exception:
                message += f"\n(error callback raised:\n{traceback.format_exc()})"
        print(f"DmqDataBus error {code.name} (remote ID {remote_id}): {message}", file=sys.stderr)
