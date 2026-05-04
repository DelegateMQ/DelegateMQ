# DelegateMQ Interop

DelegateMQ supports cross-language communication between C++, C#, and Python using a **Shared Native Core** architecture.

Detailed documentation for the interop system, including architecture, reliability, and data synchronization strategies, can be found in the main documentation directory:

👉 **[docs/INTEROP.md](../docs/INTEROP.md)**

## Contents

-   **`native/`**: The C++ DLL core (`DmqInterop.dll`).
-   **`csharp/`**: The .NET library wrapper.
-   **`python/`**: The Python library wrapper.

## Quick Start
To build the required native component:
```powershell
cd native
cmake -B build .
cmake --build build --config Release
```

For a complete working demo, see **[example/sample-interop/](../example/sample-interop/README.md)**.
