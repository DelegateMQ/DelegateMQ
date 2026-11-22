// patterns/async-sqlite
//
// A blocking library made thread-safe by giving it its own thread:
// Async-SQLite (https://github.com/DelegateMQ/Async-SQLite) runs every SQLite
// call on one DelegateMQ thread. This sample is a sensor data logger.
//
//  1. Several threads write at once with no lock and no SQLITE_BUSY retries:
//     each call is a message to the SQLite thread, handled one at a time.
//  2. A batch insert runs as one uninterrupted unit by sending the whole
//     function to the SQLite thread; calls from other threads wait their turn
//     and can't land inside the transaction.
//  3. A query returns a std::future, so the caller keeps working meanwhile.
//  4. A call with a timeout returns SQLITE_BUSY instead of blocking while the
//     SQLite thread is busy.
//  5. The SQLite thread announces each stored batch with a dmq::Signal; a
//     display thread receives it on its own thread.

#include "async_sqlite3.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace dmq;
using namespace std::chrono_literals;

static int s_failures = 0;

static void Check(bool ok, const char* what)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        s_failures++;
}

//----------------------------------------------------------------------------
// Helpers
//----------------------------------------------------------------------------

/// The SQLite thread: everything in async:: runs there.
static dmq::os::Thread& DbThread() { return *async::sqlite3_get_thread(); }

/// sqlite3_exec() callback storing the first column of the first row as an
/// integer. Runs on the SQLite thread.
static int StoreInt(void* out, int /*cols*/, char** values, char** /*names*/)
{
    *static_cast<long long*>(out) = values[0] ? std::stoll(values[0]) : 0;
    return 0;
}

/// Run a query returning one integer, from any thread (blocks until done).
static long long QueryInt(sqlite3* db, const std::string& sql)
{
    long long value = -1;
    async::sqlite3_exec(db, sql.c_str(), &StoreInt, &value, nullptr);
    return value;
}

static int InsertReading(sqlite3* db, int sensor, int seq, double value)
{
    char sql[128];
    snprintf(sql, sizeof(sql), "INSERT INTO readings VALUES (%d, %d, %.3f);", sensor, seq, value);
    return async::sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
}

//----------------------------------------------------------------------------
// Batch insert: sent to the SQLite thread as one function
//----------------------------------------------------------------------------

/// Fired on the SQLite thread after a batch commits: (sensor, rows).
static dmq::Signal<void(int, int)> OnBatchStored;

/// Runs on the SQLite thread, so it calls SQLite directly (::sqlite3_*). No
/// other database call can run until it returns.
static int InsertBatch(sqlite3* db, int sensor, int count)
{
    int rc = ::sqlite3_exec(db, "BEGIN;", nullptr, nullptr, nullptr);
    sqlite3_stmt* stmt = nullptr;
    if (rc == SQLITE_OK)
        rc = ::sqlite3_prepare_v2(db, "INSERT INTO readings VALUES (?, ?, ?);", -1, &stmt, nullptr);
    for (int i = 0; rc == SQLITE_OK && i < count; i++)
    {
        ::sqlite3_bind_int(stmt, 1, sensor);
        ::sqlite3_bind_int(stmt, 2, i);
        ::sqlite3_bind_double(stmt, 3, 20.0 + i * 0.01);
        rc = (::sqlite3_step(stmt) == SQLITE_DONE) ? SQLITE_OK : SQLITE_ERROR;
        ::sqlite3_reset(stmt);
    }
    ::sqlite3_finalize(stmt);
    if (rc == SQLITE_OK)
        rc = ::sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
    else
        ::sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);

    if (rc == SQLITE_OK)
        OnBatchStored(sensor, count);
    return rc;
}

/// Keeps the SQLite thread busy. Runs on the SQLite thread.
static void SlowJob(int ms)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

//----------------------------------------------------------------------------
// Display thread: receives OnBatchStored on its own thread
//----------------------------------------------------------------------------
static dmq::os::Thread s_displayThread("DisplayThread");
static std::atomic<int> s_batchRows{0};
static std::atomic<bool> s_batchOnDisplayThread{false};

static void OnBatchStoredDisplay(int sensor, int rows)
{
    printf("  [display] sensor %d: %d readings stored\n", sensor, rows);
    s_batchOnDisplayThread = s_displayThread.IsCurrentThread();
    s_batchRows = rows;
}

int main()
{
    Check(async::sqlite3_init_async() == SQLITE_OK, "SQLite thread started");
    s_displayThread.CreateThread();
    ScopedConnection batchConn = OnBatchStored.Connect(
        MakeDelegate(&OnBatchStoredDisplay, s_displayThread));

    sqlite3* db = nullptr;
    Check(async::sqlite3_open(":memory:", &db) == SQLITE_OK, "database opened");
    async::sqlite3_exec(db, "CREATE TABLE readings (sensor INTEGER, seq INTEGER, value REAL);",
        nullptr, nullptr, nullptr);

    // -----------------------------------------------------------------------
    printf("\nTest 1: Four sensor threads write at once\n");
    static constexpr int SENSORS = 4, READINGS = 250;
    std::atomic<int> errors{0};
    std::vector<std::thread> sensors;
    for (int s = 1; s <= SENSORS; s++)
    {
        sensors.emplace_back([db, s, &errors] {
            for (int i = 0; i < READINGS; i++)
                if (InsertReading(db, s, i, 20.0 + s + i * 0.1) != SQLITE_OK)
                    errors++;
        });
    }
    for (auto& t : sensors)
        t.join();
    Check(errors == 0, "every insert returned SQLITE_OK (no SQLITE_BUSY)");
    Check(QueryInt(db, "SELECT COUNT(*) FROM readings;") == SENSORS * READINGS, "1000 rows stored");
    Check(QueryInt(db, "SELECT COUNT(DISTINCT sensor || ':' || seq) FROM readings;") == SENSORS * READINGS,
        "no reading lost or duplicated");

    // -----------------------------------------------------------------------
    printf("\nTest 2: A batch transaction runs as one unit on the SQLite thread\n");
    // Sensor 6 keeps writing single rows while sensor 5's batch runs
    std::atomic<bool> writing{true};
    std::thread writer([db, &writing, &errors] {
        for (int i = 0; writing; i++)
            if (InsertReading(db, 6, i, 30.0) != SQLITE_OK)
                errors++;
    });
    std::this_thread::sleep_for(5ms);

    // Send the whole function to the SQLite thread and wait for its result
    static constexpr int BATCH = 500;
    int rc = MakeDelegate(&InsertBatch, DbThread(), WAIT_INFINITE)(db, 5, BATCH);
    writing = false;
    writer.join();

    Check(rc == SQLITE_OK, "batch committed");
    Check(QueryInt(db, "SELECT COUNT(*) FROM readings WHERE sensor = 5;") == BATCH, "500 batch rows");
    // The batch's rowids are consecutive: no single insert from sensor 6
    // landed inside the transaction
    Check(QueryInt(db, "SELECT MAX(rowid) - MIN(rowid) + 1 FROM readings WHERE sensor = 5;") == BATCH,
        "no other write interleaved with the batch");
    // Sensor 6 wrote before the batch, and its call waiting in the queue ran
    // right after it
    Check(QueryInt(db, "SELECT COUNT(*) FROM readings WHERE sensor = 6 AND rowid < "
                       "(SELECT MIN(rowid) FROM readings WHERE sensor = 5);") > 0 &&
          QueryInt(db, "SELECT COUNT(*) FROM readings WHERE sensor = 6 AND rowid > "
                       "(SELECT MAX(rowid) FROM readings WHERE sensor = 5);") > 0,
        "sensor 6 wrote before and after the batch");
    Check(errors == 0, "sensor 6's inserts all returned SQLITE_OK");

    // -----------------------------------------------------------------------
    printf("\nTest 3: OnBatchStored arrives on the display thread\n");
    // Queued to the display thread before InsertBatch returned; a blocking
    // call on that thread runs after it
    MakeDelegate(+[] {}, s_displayThread, WAIT_INFINITE)();
    Check(s_batchRows == BATCH, "display got the batch size");
    Check(s_batchOnDisplayThread, "callback ran on the display thread");

    // -----------------------------------------------------------------------
    printf("\nTest 4: A query returns a future; the caller keeps working\n");
    long long total = -1;
    std::future<int> pending = async::sqlite3_exec_future(db, "SELECT COUNT(*) FROM readings;",
        &StoreInt, &total, nullptr);
    int otherWork = 0;
    for (int i = 0; i < 1000; i++)
        otherWork += i;                 // caller's own work while the query runs
    Check(pending.get() == SQLITE_OK, "future query returned SQLITE_OK");
    const long long expected = QueryInt(db, "SELECT COUNT(*) FROM readings;");
    Check(total == expected && total > SENSORS * READINGS + BATCH, "future query counted every row");
    printf("  %lld rows (caller summed %d meanwhile)\n", total, otherWork);

    // -----------------------------------------------------------------------
    printf("\nTest 5: A call with a timeout doesn't wait for a busy SQLite thread\n");
    MakeDelegate(&SlowJob, DbThread())(300);     // occupy the SQLite thread
    const auto start = std::chrono::steady_clock::now();
    rc = async::sqlite3_exec(db, "SELECT 1;", nullptr, nullptr, nullptr, 50ms);
    const auto waited = std::chrono::steady_clock::now() - start;
    Check(rc == SQLITE_BUSY, "timed-out call returned SQLITE_BUSY");
    Check(waited < 250ms, "caller returned before the slow job finished");
    Check(async::sqlite3_exec(db, "SELECT 1;", nullptr, nullptr, nullptr) == SQLITE_OK,
        "the next call (no timeout) succeeds");

    // -----------------------------------------------------------------------
    Check(async::sqlite3_close(db) == SQLITE_OK, "database closed");
    batchConn.Disconnect();
    s_displayThread.ExitThread();
    async::sqlite3_shutdown();

    if (s_failures == 0)
    {
        printf("\nALL TESTS PASSED\n");
        return 0;
    }
    printf("\n%d CHECK(S) FAILED\n", s_failures);
    return 1;
}
