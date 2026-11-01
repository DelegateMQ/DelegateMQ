// AllocTrack.cpp -- global operator new/delete tracking for DMQ_ALLOCATOR builds.
//
// Compiled only with DMQ_ALLOC_TRACK defined (Windows/MSVC). Replaces every global
// operator new/delete with malloc/free. While tracking is on (alloctrack::SetLoop()
// with a loop number > 0), each global operator new records its call stack into a
// fixed hash table of unique stacks (allocated once with VirtualAlloc, so the hook
// never calls operator new itself). alloctrack::Report() then turns tracking off,
// symbolizes the stacks with DbgHelp and writes a report grouped by the innermost
// frame inside src/delegate-mq: under DMQ_ALLOCATOR the library should allocate
// through the fixed-block allocator, so library frames here are candidates.

#if defined(DMQ_ALLOC_TRACK) && defined(_WIN32)

#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

#include <atomic>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <new>
#include <map>
#include <string>
#include <vector>
#include <algorithm>

namespace alloctrack {

constexpr int MAX_FRAMES = 32;
constexpr size_t TABLE_SIZE = 1u << 16;     // unique stacks
constexpr int MAX_LOOPS = 4;

struct Entry {
    uint64_t hash;                          // 0 = empty
    uint32_t nframes;
    void* frames[MAX_FRAMES];
    uint64_t count[MAX_LOOPS];
    uint64_t bytes;
};

static Entry* g_table = nullptr;
static std::atomic<int> g_loop{0};          // 0 = tracking off
static std::atomic_flag g_lock = ATOMIC_FLAG_INIT;
static std::atomic<uint64_t> g_total[MAX_LOOPS];
static std::atomic<uint64_t> g_overflow{0};
static thread_local bool t_inHook = false;

static void EnsureTable() {
    if (!g_table)
        g_table = static_cast<Entry*>(VirtualAlloc(nullptr, sizeof(Entry) * TABLE_SIZE,
                                                   MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
}

void SetLoop(int loop) {
    EnsureTable();
    g_loop.store(loop);
}

static void Record(size_t size) {
    int loop = g_loop.load(std::memory_order_relaxed);
    if (loop <= 0 || loop >= MAX_LOOPS || t_inHook || !g_table)
        return;
    t_inHook = true;

    void* frames[MAX_FRAMES];
    // Skip Record() and operator new itself
    USHORT n = CaptureStackBackTrace(2, MAX_FRAMES, frames, nullptr);
    uint64_t h = 1469598103934665603ull;    // FNV-1a over the frame addresses
    for (USHORT i = 0; i < n; i++) {
        h ^= reinterpret_cast<uint64_t>(frames[i]);
        h *= 1099511628211ull;
    }
    if (h == 0) h = 1;

    g_total[loop].fetch_add(1, std::memory_order_relaxed);
    while (g_lock.test_and_set(std::memory_order_acquire)) {}
    size_t idx = static_cast<size_t>(h) & (TABLE_SIZE - 1);
    bool stored = false;
    for (size_t probe = 0; probe < TABLE_SIZE; probe++) {
        Entry& e = g_table[(idx + probe) & (TABLE_SIZE - 1)];
        if (e.hash == h) { e.count[loop]++; e.bytes += size; stored = true; break; }
        if (e.hash == 0) {
            e.hash = h;
            e.nframes = n;
            memcpy(e.frames, frames, n * sizeof(void*));
            e.count[loop] = 1;
            e.bytes = size;
            stored = true;
            break;
        }
    }
    g_lock.clear(std::memory_order_release);
    if (!stored) g_overflow.fetch_add(1);
    t_inHook = false;
}

// ---------------------------------------------------------------- report

static std::string Lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    std::replace(s.begin(), s.end(), '/', '\\');
    return s;
}

struct Frame { std::string func, file; DWORD line = 0; };

static Frame Symbolize(void* addr, std::map<void*, Frame>& cache) {
    auto it = cache.find(addr);
    if (it != cache.end()) return it->second;
    Frame f;
    HANDLE proc = GetCurrentProcess();
    alignas(SYMBOL_INFO) char buf[sizeof(SYMBOL_INFO) + 512];
    auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
    memset(buf, 0, sizeof(buf));
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 511;
    DWORD64 disp = 0;
    if (SymFromAddr(proc, reinterpret_cast<DWORD64>(addr), &disp, sym))
        f.func = sym->Name;
    else
        f.func = "?";
    IMAGEHLP_LINE64 line{};
    line.SizeOfStruct = sizeof(line);
    DWORD ldisp = 0;
    if (SymGetLineFromAddr64(proc, reinterpret_cast<DWORD64>(addr), &ldisp, &line)) {
        f.file = line.FileName;
        f.line = line.LineNumber;
    }
    cache[addr] = f;
    return f;
}

static bool IsLibrary(const std::string& lf) { return lf.find("\\src\\delegate-mq\\") != std::string::npos; }

// Desktop-only ports and transports, which CLAUDE.md allows to use std:: primitives
static bool IsDesktopExempt(const std::string& lf) {
    static const char* dirs[] = {
        "\\port\\os\\stdlib\\", "\\port\\os\\win32\\", "\\port\\os\\posix\\", "\\port\\os\\qt\\",
        "\\port\\transport\\win32-", "\\port\\transport\\linux-", "\\port\\transport\\zeromq\\",
        "\\port\\transport\\nng\\", "\\port\\transport\\mqtt\\", "\\port\\transport\\serial\\" };
    for (const char* d : dirs)
        if (lf.find(d) != std::string::npos) return true;
    return false;
}

static bool IsTestOrSample(const std::string& lf) {
    return lf.find("\\delegatemq\\test\\") != std::string::npos ||
           lf.find("\\delegatemq\\example\\") != std::string::npos;
}

struct Group {
    std::string category, culprit;
    uint64_t count[MAX_LOOPS] = {};
    uint64_t bytes = 0;
    int stacks = 0;
    std::vector<Frame> sample;
};

void Report(const char* path) {
    g_loop.store(0);                         // DbgHelp and the report itself allocate
    t_inHook = true;
    if (!g_table) return;

    HANDLE proc = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS);
    SymInitialize(proc, nullptr, TRUE);

    std::map<void*, Frame> cache;
    std::map<std::string, Group> groups;

    for (size_t i = 0; i < TABLE_SIZE; i++) {
        const Entry& e = g_table[i];
        if (e.hash == 0) continue;
        std::vector<Frame> frames;
        for (uint32_t k = 0; k < e.nframes; k++)
            frames.push_back(Symbolize(e.frames[k], cache));

        // The innermost frame that is library or test/sample code owns the allocation.
        // A test target or test transport called through a delegate is innermost, so it
        // isn't blamed on the delegate that called it.
        std::string category = "OTHER (no library or test frame)", culprit;
        for (const Frame& f : frames) {
            std::string lf = Lower(f.file);
            if (IsLibrary(lf)) {
                category = IsDesktopExempt(lf) ? "LIBRARY desktop-only port (std:: allowed)" : "LIBRARY";
            } else if (IsTestOrSample(lf)) {
                category = "TEST/SAMPLE code";
            } else {
                continue;
            }
            culprit = f.func + "  " + f.file + ":" + std::to_string(f.line);
            break;
        }
        if (culprit.empty() && !frames.empty())
            culprit = frames[0].func;

        Group& g = groups[category + "|" + culprit];
        g.category = category;
        g.culprit = culprit;
        for (int l = 0; l < MAX_LOOPS; l++) g.count[l] += e.count[l];
        g.bytes += e.bytes;
        g.stacks++;
        if (g.sample.empty()) g.sample = frames;
    }

    std::vector<const Group*> sorted;
    for (auto& kv : groups) sorted.push_back(&kv.second);
    std::sort(sorted.begin(), sorted.end(), [](const Group* a, const Group* b) {
        if (a->category != b->category) return a->category < b->category;
        return (a->count[2] + a->count[3]) > (b->count[2] + b->count[3]);
    });

    FILE* fp = fopen(path, "w");
    if (!fp) return;
    fprintf(fp, "Global operator new calls while tracking (DMQ_ALLOCATOR build)\n");
    for (int l = 1; l < MAX_LOOPS; l++)
        if (g_total[l].load()) fprintf(fp, "  loop %d: %llu calls\n", l, (unsigned long long)g_total[l].load());
    size_t unique = 0;
    for (size_t i = 0; i < TABLE_SIZE; i++) if (g_table[i].hash) unique++;
    fprintf(fp, "  unique stacks: %zu, groups: %zu, table overflow (unrecorded calls): %llu\n\n",
            unique, groups.size(), (unsigned long long)g_overflow.load());

    std::map<std::string, uint64_t> catTotals;
    for (const Group* g : sorted) catTotals[g->category] += g->count[2] + g->count[3];
    fprintf(fp, "Totals by category (loops 2+3):\n");
    for (auto& kv : catTotals) fprintf(fp, "  %-45s %llu\n", kv.first.c_str(), (unsigned long long)kv.second);
    fprintf(fp, "\n");

    std::string lastCat;
    for (const Group* g : sorted) {
        if (g->category != lastCat) {
            fprintf(fp, "==================== %s ====================\n\n", g->category.c_str());
            lastCat = g->category;
        }
        fprintf(fp, "[loop2=%llu loop3=%llu bytes=%llu stacks=%d] %s\n",
                (unsigned long long)g->count[2], (unsigned long long)g->count[3],
                (unsigned long long)g->bytes, g->stacks, g->culprit.c_str());
        if (g->category.rfind("LIBRARY", 0) == 0) {
            for (const Frame& f : g->sample)
                fprintf(fp, "      %s  %s:%lu\n", f.func.c_str(), f.file.c_str(), f.line);
            fprintf(fp, "\n");
        }
    }
    fclose(fp);
    SymCleanup(proc);
    printf("AllocTrack: report written to %s\n", path);
}

} // namespace alloctrack

// ---------------------------------------------------------------- overloads

static void* TrackedAlloc(size_t size) {
    alloctrack::Record(size);
    void* p = malloc(size ? size : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
static void* TrackedAllocNoThrow(size_t size) noexcept {
    alloctrack::Record(size);
    return malloc(size ? size : 1);
}
static void* TrackedAlignedAlloc(size_t size, std::align_val_t al) {
    alloctrack::Record(size);
    void* p = _aligned_malloc(size ? size : 1, static_cast<size_t>(al));
    if (!p) throw std::bad_alloc();
    return p;
}

void* operator new(size_t size) { return TrackedAlloc(size); }
void* operator new[](size_t size) { return TrackedAlloc(size); }
void* operator new(size_t size, const std::nothrow_t&) noexcept { return TrackedAllocNoThrow(size); }
void* operator new[](size_t size, const std::nothrow_t&) noexcept { return TrackedAllocNoThrow(size); }
void* operator new(size_t size, std::align_val_t al) { return TrackedAlignedAlloc(size, al); }
void* operator new[](size_t size, std::align_val_t al) { return TrackedAlignedAlloc(size, al); }
void* operator new(size_t size, std::align_val_t al, const std::nothrow_t&) noexcept {
    alloctrack::Record(size);
    return _aligned_malloc(size ? size : 1, static_cast<size_t>(al));
}
void* operator new[](size_t size, std::align_val_t al, const std::nothrow_t&) noexcept {
    alloctrack::Record(size);
    return _aligned_malloc(size ? size : 1, static_cast<size_t>(al));
}

void operator delete(void* p) noexcept { free(p); }
void operator delete[](void* p) noexcept { free(p); }
void operator delete(void* p, size_t) noexcept { free(p); }
void operator delete[](void* p, size_t) noexcept { free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { free(p); }
void operator delete(void* p, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete(void* p, size_t, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete[](void* p, size_t, std::align_val_t) noexcept { _aligned_free(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { _aligned_free(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { _aligned_free(p); }

#endif // DMQ_ALLOC_TRACK && _WIN32
