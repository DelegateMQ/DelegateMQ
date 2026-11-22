# Serial Port Transport Example

This project demonstrates how to use **DelegateMQ** to send structured data (serialized C++ objects) over a Serial Port (UART/RS-232).

It implements a reliable stream transport (`SerialTransport`) that:
1.  **Frames messages** using a length-prefixed protocol.
2.  **Serializes arguments** (like `std::list` and custom structs) into binary.
3.  **Invokes remote delegates** seamlessly across the serial link.

---

## ⚠️ Prerequisites: COM Ports

To run this demo, you need a pair of connected Serial Ports: the **sender** writes to one and the **receiver** listens on the other.

```
delegate_app [seconds] [sender-port] [receiver-port]     (defaults: 5, COM1, COM2)
```

Connect the two ports with a physical null-modem cable or a virtual port pair:

* **Windows:** a virtual COM-port pair driver such as com0com, or two USB-serial adapters with a null-modem cable.
* **Linux:** `socat` makes a virtual pair from two linked pseudo-terminals, no hardware needed:

  ```bash
  socat pty,raw,echo=0,link=/tmp/ttyDMQ0 pty,raw,echo=0,link=/tmp/ttyDMQ1 &
  ./build/delegate_app 5 /tmp/ttyDMQ0 /tmp/ttyDMQ1
  ```

The app exits with status 0 if the receiver got at least one message, else 1. `build_all.py` runs it this way on Linux.

---

## 🛠️ Step 1: Build Dependencies (libserialport)

This project depends on `libserialport`, which has no CMake build. `01_fetch_repos.py` clones it to `external/libserialport`, and `02_build_libs.py` (step 2 of `build_all.py`) builds it for you:

* **Windows:** MSBuild on `libserialport.vcxproj` (Debug, x64) with your installed Visual Studio's toolset.
* **Linux:** its autotools build, as a static library in `external/libserialport/.libs` (needs `autoconf`, `automake` and `libtool`).

To build it by hand instead: open `external/libserialport/libserialport.sln` in Visual Studio, retarget the project to your installed toolset if asked, and build **Debug | x64**.

The project uses `SERIALPORT_INCLUDE_DIR` and `SERIALPORT_LIBRARY_DIR` within `External.cmake` to locate the include and library files. Update if necessary. 

`libserialport` also supports macOS and other platforms.

---

## 🚀 Step 2: Build and Run the Demo

### 1. Configure CMake
Open a terminal in this directory (`example/sample-projects/transport/serialport`) and run:

```bash
cmake -B build .
```

### 2. Build and Run 
Build and run the application located within the `build` directory.