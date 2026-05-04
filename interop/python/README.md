# DelegateMQ Python Interop Library

This is the thin Python wrapper for DelegateMQ. It communicates with C++ applications using the shared native DLL via `ctypes`.

Full documentation for architecture and usage:
👉 **[docs/INTEROP.md](../../docs/INTEROP.md)**

## Setup
1.  **Build Native DLL**: You must build the DLL in `interop/native` first.
2.  **Install Dependencies**: `pip install msgpack`

## Example
A complete Python sample script is available here:
👉 **[example/sample-interop/python/](../../example/sample-interop/python/)**
