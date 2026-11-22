# Async SQLite (Library on Its Own Thread)

A sensor data logger that lets any thread use a blocking library, with no locks and no busy retries in the application, by giving the library a thread of its own. [Async-SQLite](https://github.com/DelegateMQ/Async-SQLite) wraps the SQLite C API so that every call runs on one `dmq::os::Thread`: a call from another thread becomes a message to that thread, and the caller waits for the result, waits with a timeout, or gets a `std::future`.

The same pattern fits any library that isn't thread-safe or blocks: a file system, a modem driver, a display.

| Part | Role |
| :--- | :--- |
| Async-wait delegate | Each `async::sqlite3_*()` call runs on the SQLite thread while the caller waits; with a timeout, the caller gets `SQLITE_BUSY` instead of waiting on. |
| Sending a whole function | `MakeDelegate(&InsertBatch, *async::sqlite3_get_thread(), WAIT_INFINITE)(db, 5, 500)` runs a complete transaction on the SQLite thread. Inside it, SQLite is called directly, and no other caller's statement can run in between. |
| `std::future` | `async::sqlite3_exec_future()` returns at once; the caller collects the result later. |
| `dmq::Signal` | `OnBatchStored` is fired on the SQLite thread and delivered to a display thread on that thread. |

## Tests

1. Four sensor threads insert 250 readings each at once. Every call returns `SQLITE_OK`, with no `SQLITE_BUSY` and no lock in the application, and no reading is lost or duplicated.
2. A 500-row transaction is sent to the SQLite thread as one function while another thread keeps inserting. The batch's rows have consecutive row IDs: no other insert ran inside the transaction. The other thread's inserts land before and after it.
3. `OnBatchStored` arrives on the display thread with the batch size.
4. A query started with `sqlite3_exec_future()` returns while the caller does other work, then reports every row.
5. With the SQLite thread busy (a 300 ms job), a call with a 50 ms timeout returns `SQLITE_BUSY` without waiting for the job; the next call without a timeout succeeds.

The program prints `ALL TESTS PASSED` and returns 0. The database is in memory (`:memory:`), so nothing is written to disk.

## Async-SQLite

`scripts/01_fetch_repos.py` clones Async-SQLite (v1.1.0, SQLite 3.54.1) into `external/Async-SQLite` (or `DMQ_DEPS_DIR`). Only its `SQLite/` folder is compiled: the SQLite amalgamation (`sqlite3.c`) and the wrapper (`async_sqlite3.cpp`), against this repository's DelegateMQ. To use another checkout, pass `-DASYNC_SQLITE_DIR=<path>`.

## Build and Run

```bash
python scripts/01_fetch_repos.py      # once, from the repository root

cmake -B build
cmake --build build --config Release

# Windows
build\Release\delegate_async_sqlite.exe
# Linux
./build/delegate_async_sqlite
```
