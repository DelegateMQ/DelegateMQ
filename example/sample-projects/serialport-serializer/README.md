# Serial Port Transport Example

This project demonstrates how to use **DelegateMQ** to send structured data (serialized C++ objects) over a Serial Port (UART/RS-232).

It implements a reliable stream transport (`SerialTransport`) that:
1.  **Frames messages** using a length-prefixed protocol.
2.  **Serializes arguments** (like `std::list` and custom structs) into binary.
3.  **Invokes remote delegates** seamlessly across the serial link.

---

## ⚠️ Prerequisites: COM Ports

To run this demo, you need a pair of connected Serial Ports.

* **Sender** is configured to write to **`COM1`**.
* **Receiver** is configured to listen on **`COM2`**.

Ensure these ports exist and are connected (e.g., via a physical null-modem cable or a virtual port pair driver).

*(If you need to use different port names, edit the `Sender.h` and `Receiver.h` files before building.)*

---

## 🛠️ Step 1: Build Dependencies (libserialport)

This project depends on `libserialport`, which has no CMake build. `01_fetch_repos.py` clones it to `external/libserialport`.

On Windows, `02_build_libs.py` (step 2 of `build_all.py`) builds it for you: it runs MSBuild on `libserialport.vcxproj` (Debug, x64) with your installed Visual Studio's toolset.

To build it by hand instead: open `external/libserialport/libserialport.sln` in Visual Studio, retarget the project to your installed toolset if asked, and build **Debug | x64**.

The project uses `SERIALPORT_INCLUDE_DIR` and `SERIALPORT_LIBRARY_DIR` within `External.cmake` to locate the include and library files. Update if necessary. 

The `libserialport` also supports Linux and other platforms.

---

## 🚀 Step 2: Build and Run the Demo

### 1. Configure CMake
Open a terminal in this directory (`example/sample-projects/serialport-serializer`) and run:

```bash
cmake -B build .
```

### 2. Build and Run 
Build and run the application located within the `build` directory.