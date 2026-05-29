#ifndef _XALLOCATOR_H
#define _XALLOCATOR_H

#include <cstdint>
#include <stddef.h>

// @see https://github.com/endurodave/xallocator
// David Lafreniere

#ifdef __cplusplus
// Define AUTOMATIC_XALLOCATOR_INIT_DESTROY to automatically call xalloc_init() and
// xalloc_destroy() when using xallocator in C++ projects. On embedded systems that
// never exit, you can save 1-byte of RAM storage per translation unit by undefining
// AUTOMATIC_XALLOCATOR_INIT_DESTROY and calling xalloc_init() manually before the OS
// starts. 
#define AUTOMATIC_XALLOCATOR_INIT_DESTROY 
#ifdef AUTOMATIC_XALLOCATOR_INIT_DESTROY
/// If a C++ translation unit, create a static instance of XallocInitDestroy. Any C++
/// file including xallocator.h will have the xallocDestroy instance declared first
/// within the translation unit and thus will be constructed first. Destruction 
/// will occur in the reverse order so xallocInitDestroy is called last. This way,
/// any static user objects relying on xallocator will be destroyed first before 
/// xalloc_destroy() is called. 
/// Embedded systems that never exit can remove the XallocInitDestroy class entirely. 
class XallocInitDestroy
{
public:
	XallocInitDestroy();
	~XallocInitDestroy();
private:
	static int32_t refCount;
};
static XallocInitDestroy xallocInitDestroy;
#endif	// AUTOMATIC_XALLOCATOR_INIT_DESTROY
#endif	// __cplusplus

#ifdef __cplusplus
extern "C" {
#endif

#ifdef DMQ_ALLOCATOR_SAFEGUARDS
#define XALLOC_BLOCK_HEADER_SIZE 16
#define XALLOC_BLOCK_FOOTER_SIZE 4
#define XALLOC_MAGIC   0x414C4C4F  // "ALLO"
#define XALLOC_FREED   0xDEADBEEF
#define XALLOC_CANARY  0xAA55AA55
#else
#define XALLOC_BLOCK_HEADER_SIZE (sizeof(void*) > 4 ? 16 : 8)
#define XALLOC_BLOCK_FOOTER_SIZE 0
#endif

/// Forward declaration of Allocator class
#ifdef __cplusplus
class Allocator;
#endif

/// This function must be called exactly one time before the operating system
/// threading starts. If using xallocator exclusively in C files within your application
/// code, you must call this function before the OS starts. If using C++, client code
/// does not call xalloc_init. Let XallocInitDestroy() call xalloc_init automatically. 
/// Embedded systems that never exit can call xalloc_init() manually at startup
/// and eliminate XallocInitDestroy usage. When the system is still single threaded at 
/// startup, the xallocator API does not need mutex protection.
void xalloc_init();

/// This function must be called once when the application exits.  Never call xalloc_destroy() 
/// manually except if using xallocator in a C-only application. If using xallocator 
/// exclusively in C files within your application code, you must call this function before 
/// the program exits. If using C++, ~XallocInitDestroy() must call xalloc_destroy automatically.
/// Embedded systems that never exit need not call this function at all. 
void xalloc_destroy();

/// Get an Allocator instance based upon the client's requested block size.
/// @param[in] size - the client's requested block size.
/// @return An Allocator instance that handles blocks of the requested size.
#ifdef __cplusplus
Allocator* xallocator_get_allocator(size_t size);
#endif

/// Allocate a block of memory
/// @param[in] size - the size of the block to allocate. 
void *xmalloc(size_t size);

/// Frees a previously xalloc allocated block
/// @param[in] ptr - a pointer to a previously allocated memory using xalloc.
void xfree(void* ptr);

/// Reallocates an existing xalloc block to a new size
/// @param[in] ptr - a pointer to a previously allocated memory using xalloc.
/// @param[in] size - the size of the new block
void *xrealloc(void *ptr, size_t size);	

/// Output allocator statistics to the standard output
void xalloc_stats();

#ifdef DMQ_ALLOCATOR
    // Macro to overload new/delete with xalloc/xfree. Add macro to any class to enable
    // fixed-block memory allocation. Add to a base class provides fixed-block memory
    // for the base and all derived classes.
    // 
    // While std::make_shared is usually recommended for standard C++, in a fixed-block 
    // memory architecture, explicit 'new' is required to trigger the overloaded operator.
    //
    // Example:
    //   class MyMsg { 
    //       XALLOCATOR 
    //   };
    //
    //   // BAD: Allocates global memory (Bypasses XALLOCATOR)
    //   auto msg = std::make_shared<MyMsg>(); 
    //
    //   // GOOD: Allocates fixed-block memory (Uses XALLOCATOR)
    //   std::shared_ptr<MyMsg> msg(new MyMsg());
    #define XALLOCATOR \
        public: \
            void* operator new(size_t size) { \
                return xmalloc(size); \
            } \
            void* operator new(size_t size, void* mem) { \
                return mem; \
            } \
            void* operator new(size_t size, const std::nothrow_t& nt) { \
                return xmalloc(size); \
            } \
            void* operator new[](size_t size) { \
                return xmalloc(size); \
            } \
            void operator delete(void* pObject) { \
                xfree(pObject); \
            } \
            void operator delete(void* pObject, const std::nothrow_t& nt) { \
                xfree(pObject); \
            } \
            void operator delete[](void* pData) { \
                xfree(pData); \
            }
#endif

#ifdef __cplusplus 
}
#endif

#endif 
