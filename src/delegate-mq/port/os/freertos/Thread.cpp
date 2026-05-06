#ifndef DMQ_THREAD_FREERTOS
#error "port/os/freertos/Thread.cpp requires DMQ_THREAD_FREERTOS. Remove this file from your build configuration or define DMQ_THREAD_FREERTOS."
#endif

#include "DelegateMQ.h"
#include "Thread.h"
#include "ThreadMsg.h"
#include "extras/util/Fault.h"
#include <cstdio>

#ifndef ASSERT_TRUE
#define ASSERT_TRUE(x) configASSERT(x)
#endif

namespace dmq::os {

using namespace dmq;
using namespace dmq::util;

//----------------------------------------------------------------------------
// Thread Constructor
//----------------------------------------------------------------------------
Thread::Thread(const std::string& threadName, size_t maxQueueSize, FullPolicy fullPolicy, dmq::Duration dispatchTimeout, const std::string& cpuName)
    : THREAD_NAME(threadName)
    , CPU_NAME(cpuName)
    , FULL_POLICY(fullPolicy)
    , m_dispatchTimeout(dispatchTimeout)
    , m_exit(false)
{
    m_queueSize = (maxQueueSize == 0) ? DEFAULT_QUEUE_SIZE : maxQueueSize;
    m_priority = configMAX_PRIORITIES > 2 ? configMAX_PRIORITIES - 2 : tskIDLE_PRIORITY + 1;
}

//----------------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------------
Thread::~Thread()
{
    ExitThread();

    {
        const std::lock_guard<dmq::RecursiveMutex> lock(GetWatchdogLock());
        Thread** pp = &GetWatchdogHead();
        while (*pp != nullptr)
        {
            if (*pp == this)
            {
                *pp = this->m_watchdogNext;
                this->m_watchdogNext = nullptr;
                break;
            }
            pp = &((*pp)->m_watchdogNext);
        }
    }

    if (m_exitSem) {
        vSemaphoreDelete(m_exitSem);
        m_exitSem = nullptr;
    }
}

//----------------------------------------------------------------------------
// SetStackMem (Static Stack Configuration)
//----------------------------------------------------------------------------
void Thread::SetStackMem(StackType_t* stackBuffer, uint32_t stackSizeInWords)
{
    if (stackBuffer && stackSizeInWords > 0) {
        m_stackBuffer = stackBuffer;
        m_stackSize = stackSizeInWords;
    }
}

//----------------------------------------------------------------------------
// CreateThread
//----------------------------------------------------------------------------
bool Thread::CreateThread(std::optional<dmq::Duration> watchdogTimeout)
{
    if (IsThreadCreated())
        return true;

    // 1. Create Synchronization Semaphore (Critical for cleanup)
    if (!m_exitSem) {
        m_exitSem = xSemaphoreCreateBinary();
        ASSERT_TRUE(m_exitSem != nullptr);
    }

    // 2. Create the Queue NOW (Synchronously)
    // We must do this BEFORE creating the task so it's ready for immediate use.
    if (!m_queue) {
        m_queue = xQueueCreate(m_queueSize, sizeof(ThreadMsg*));
        if (m_queue == nullptr) {
            printf("Error: Thread '%s' failed to create queue.\n", THREAD_NAME.c_str());
            return false;
        }
    }

    m_lastAliveTime.store(static_cast<uint32_t>(Timer::GetNow().time_since_epoch().count()));

    if (watchdogTimeout.has_value())
    {
        m_watchdogTimeout.store(static_cast<uint32_t>(watchdogTimeout.value().count()));

        const std::lock_guard<dmq::RecursiveMutex> lock(GetWatchdogLock());
        
        // Add to watchdog registry if not already present
        bool found = false;
        Thread* p = GetWatchdogHead();
        while (p != nullptr)
        {
            if (p == this)
            {
                found = true;
                break;
            }
            p = p->m_watchdogNext;
        }

        if (!found)
        {
            m_watchdogNext = GetWatchdogHead();
            GetWatchdogHead() = this;
        }
    }

    // 3. Create Task
    if (m_stackBuffer != nullptr)
    {
        // --- STATIC ALLOCATION ---
        m_thread = xTaskCreateStatic(
            (TaskFunction_t)&Thread::Process,
            THREAD_NAME.c_str(),
            m_stackSize,
            this,
            m_priority,
            m_stackBuffer,
            &m_tcb
        );
    }
    else
    {
        // --- DYNAMIC ALLOCATION (Heap) ---
        // Increase default stack to 4096 words (16KB) for safety
        const uint32_t DYNAMIC_STACK_SIZE = 4096; 
        
        BaseType_t xReturn = xTaskCreate(
            (TaskFunction_t)&Thread::Process,
            THREAD_NAME.c_str(),
            DYNAMIC_STACK_SIZE, 
            this,
            m_priority,
            &m_thread);

        if (xReturn != pdPASS) {
            printf("Error: Failed to create task '%s'. OOM?\n", THREAD_NAME.c_str());
            return false; 
        }
    }

    ASSERT_TRUE(m_thread != nullptr);
    return true;
}

//----------------------------------------------------------------------------
// ExitThread
//----------------------------------------------------------------------------
void Thread::ExitThread()
{
    if (m_queue) {
        m_exit.store(true);

        ThreadMsg* msg = new (std::nothrow) ThreadMsg(MSG_EXIT_THREAD);

        if (msg) {
            // Block until message is sent. This ensures the thread WILL receive it.
            // If the thread is deadlocked, we hang here, which is better than 
            // hanging at the semaphore after dropping the message.
            if (xQueueSend(m_queue, &msg, portMAX_DELAY) != pdPASS) {
                delete msg;
            }
        }

        if (xTaskGetCurrentTaskHandle() != m_thread && m_exitSem) {
            xSemaphoreTake(m_exitSem, portMAX_DELAY);
        }

        if (m_queue) {
            vQueueDelete(m_queue);
            m_queue = nullptr;
        }
        m_thread = nullptr;
    }
}

//----------------------------------------------------------------------------
// Getters / Setters
//----------------------------------------------------------------------------
TaskHandle_t Thread::GetThreadId() { return m_thread; }
TaskHandle_t Thread::GetCurrentThreadId() { return xTaskGetCurrentTaskHandle(); }
bool Thread::IsCurrentThread()
{
    return GetThreadId() == GetCurrentThreadId();
}

//----------------------------------------------------------------------------
// GetQueueSize
//----------------------------------------------------------------------------
size_t Thread::GetQueueSize()
{
    if (m_queue) {
        return (size_t)uxQueueMessagesWaiting(m_queue);
    }
    return 0;
}

void Thread::Sleep(dmq::Duration timeout) {
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count();
    vTaskDelay(pdMS_TO_TICKS(ms));
}

void Thread::SetThreadPriority(int priority) {
    m_priority = priority;
    if (m_thread) {
        vTaskPrioritySet(m_thread, (UBaseType_t)m_priority);
    }
}

//----------------------------------------------------------------------------
// DispatchDelegate
//----------------------------------------------------------------------------
bool Thread::DispatchDelegate(std::shared_ptr<dmq::DelegateMsg> msg)
{
    if (!m_queue) {
        printf("[Thread] Error: Dispatch called but queue is null (%s)\n", THREAD_NAME.c_str());
        return false; 
    }

    // If using XALLOCATOR explicit operator new required. See xallocator.h.
    ThreadMsg* threadMsg = new (std::nothrow) ThreadMsg(MSG_DISPATCH_DELEGATE, msg);

    if (threadMsg == nullptr) {
        printf("[Thread] CRITICAL: 'new ThreadMsg' returned NULL! System Heap full? (%s)\n", THREAD_NAME.c_str());
        return false;
    }
#if defined(DMQ_DATABUS_TOOLS)
    threadMsg->SetEnqueueTime(Timer::GetNow());
#endif

    // Compute send timeout based on policy.
    TickType_t timeout;
    if (FULL_POLICY == FullPolicy::TIMEOUT)
        timeout = pdMS_TO_TICKS(std::chrono::duration_cast<std::chrono::milliseconds>(m_dispatchTimeout).count());
    else
        timeout = 0;  // DROP and FAULT: non-blocking

    // High priority uses xQueueSendToFront; all others use FIFO SendToBack.
    BaseType_t status;
    if (msg->GetPriority() == Priority::HIGH)
        status = xQueueSendToFront(m_queue, &threadMsg, timeout);
    else
        status = xQueueSendToBack(m_queue, &threadMsg, timeout);

    if (status != pdPASS) {
        if (FULL_POLICY == FullPolicy::FAULT) {
            printf("[Thread] CRITICAL: Queue full on thread '%s'! TRIGGERING FAULT.\n", THREAD_NAME.c_str());
            ASSERT_TRUE(status == pdPASS);
        } else if (FULL_POLICY == FullPolicy::TIMEOUT) {
            printf("[Thread] WARNING: Queue post timed out on '%s' — possible deadlock. Message dropped.\n", THREAD_NAME.c_str());
        }
        delete threadMsg;
        return false;
    }

#if defined(DMQ_DATABUS_TOOLS)
    // Update monitoring stats
    size_t currentDepth = GetQueueSize();
    size_t oldDepth = m_queueDepthMaxWindow.load();
    while (currentDepth > oldDepth && !m_queueDepthMaxWindow.compare_exchange_weak(oldDepth, currentDepth));
    
    oldDepth = m_queueDepthMaxAll.load();
    while (currentDepth > oldDepth && !m_queueDepthMaxAll.compare_exchange_weak(oldDepth, currentDepth));
#endif

    return true;
}

//----------------------------------------------------------------------------
// Process & Run
//----------------------------------------------------------------------------
void Thread::Process(void* instance)
{
    Thread* thread = static_cast<Thread*>(instance);
    ASSERT_TRUE(thread != nullptr);
    thread->Run();
    vTaskDelete(NULL);
}

//----------------------------------------------------------------------------
// WatchdogCheckAll
//----------------------------------------------------------------------------
void Thread::WatchdogCheckAll()
{
    const std::lock_guard<dmq::RecursiveMutex> lock(GetWatchdogLock());
    Thread* p = GetWatchdogHead();
    while (p != nullptr)
    {
        p->WatchdogCheck();
        p = p->m_watchdogNext;
    }
}

//----------------------------------------------------------------------------
// WatchdogCheck
//----------------------------------------------------------------------------
void Thread::WatchdogCheck()
{
    auto now = static_cast<uint32_t>(Timer::GetNow().time_since_epoch().count());
    auto lastAlive = m_lastAliveTime.load();
    auto watchdogTimeout = m_watchdogTimeout.load();

    if (watchdogTimeout > 0)
    {
        auto delta = now - lastAlive;
        if (delta > watchdogTimeout)
        {
            WatchdogHandler(THREAD_NAME.c_str());
        }
    }
}

//----------------------------------------------------------------------------
// ThreadCheck
//----------------------------------------------------------------------------
void Thread::ThreadCheck()
{
    m_lastAliveTime.store(static_cast<uint32_t>(Timer::GetNow().time_since_epoch().count()));
}

void Thread::Run()
{
    ThreadMsg* msg = nullptr;
    while (!m_exit.load())
    {
        m_lastAliveTime.store(static_cast<uint32_t>(Timer::GetNow().time_since_epoch().count()));
        auto watchdogTimeout = m_watchdogTimeout.load();

        // If watchdog active, use a finite timeout so we can periodically update 
        // m_lastAliveTime while idle. Otherwise, block forever to save power.
        TickType_t waitTicks = portMAX_DELAY;
        if (watchdogTimeout > 0)
        {
            waitTicks = pdMS_TO_TICKS(watchdogTimeout / 4);
            if (waitTicks == 0) waitTicks = 1;
        }

        if (xQueueReceive(m_queue, &msg, waitTicks) == pdPASS)
        {
            if (!msg) continue;

            int msgId = msg->GetId();
            if (msgId == MSG_DISPATCH_DELEGATE)
            {
#if defined(DMQ_DATABUS_TOOLS)
                // Update latency stats before invoking
                dmq::Duration latency = Timer::GetNow() - msg->GetEnqueueTime();
                int64_t latNs = std::chrono::duration_cast<std::chrono::nanoseconds>(latency).count();
                
                {
                    const std::lock_guard<dmq::RecursiveMutex> lock(m_statsLock);
                    m_latencyTotalWindow += latNs;
                    m_latencyCountWindow += 1;
                    if (latNs > m_latencyMaxWindow) m_latencyMaxWindow = latNs;
                    if (latNs > m_latencyMaxAll) m_latencyMaxAll = latNs;
                    m_dispatchCountAll += 1;
                }
#endif

                auto delegateMsg = msg->GetData();
                ASSERT_TRUE(delegateMsg);
                auto invoker = delegateMsg->GetInvoker();
                ASSERT_TRUE(invoker);

#if defined(DMQ_DATABUS_TOOLS)
                dmq::TimePoint start = Timer::GetNow();
#endif
                bool success = invoker->Invoke(delegateMsg);
#if defined(DMQ_DATABUS_TOOLS)
                dmq::Duration invokeTime = Timer::GetNow() - start;
                int64_t invokeNs = std::chrono::duration_cast<std::chrono::nanoseconds>(invokeTime).count();
                {
                    const std::lock_guard<dmq::RecursiveMutex> lock(m_statsLock);
                    m_invokeTotalWindow += invokeNs;
                    m_invokeCountWindow += 1;
                    if (invokeNs > m_invokeMaxWindow) m_invokeMaxWindow = invokeNs;
                    if (invokeNs > m_invokeMaxAll) m_invokeMaxAll = invokeNs;
                }
#endif
                ASSERT_TRUE(success);
            }
            
            delete msg;

            if (msgId == MSG_EXIT_THREAD) {
                break;
            }
        }
    }

    if (m_exitSem) {
        xSemaphoreGive(m_exitSem);
    }
}

#if defined(DMQ_DATABUS_TOOLS)
//----------------------------------------------------------------------------
// SnapshotStats
//----------------------------------------------------------------------------
Thread::ThreadStats Thread::SnapshotStats()
{
    ThreadStats stats;
    stats.cpu_name = CPU_NAME;
    stats.thread_name = THREAD_NAME;
    stats.queue_depth = GetQueueSize();
    stats.queue_depth_max_window = m_queueDepthMaxWindow.exchange(stats.queue_depth);
    stats.queue_depth_max_all = m_queueDepthMaxAll.load();
    stats.queue_size_limit = m_queueSize;
    
    {
        const std::lock_guard<dmq::RecursiveMutex> lock(m_statsLock);
        uint32_t count = m_latencyCountWindow;
        m_latencyCountWindow = 0;
        int64_t total = m_latencyTotalWindow;
        m_latencyTotalWindow = 0;

        if (count > 0) {
            stats.latency_avg_ms = (float)total / (count * 1000000.0f);
        } else {
            stats.latency_avg_ms = 0.0f;
        }

        stats.latency_max_window_ms = (float)m_latencyMaxWindow / 1000000.0f;
        m_latencyMaxWindow = 0;
        stats.latency_max_all_ms = (float)m_latencyMaxAll / 1000000.0f;

        uint32_t iCount = m_invokeCountWindow;
        m_invokeCountWindow = 0;
        int64_t iTotal = m_invokeTotalWindow;
        m_invokeTotalWindow = 0;

        if (iCount > 0) {
            stats.invoke_avg_ms = (float)iTotal / (iCount * 1000000.0f);
        } else {
            stats.invoke_avg_ms = 0.0f;
        }

        stats.invoke_max_window_ms = (float)m_invokeMaxWindow / 1000000.0f;
        m_invokeMaxWindow = 0;
        stats.invoke_max_all_ms = (float)m_invokeMaxAll / 1000000.0f;

        stats.dispatch_count = m_dispatchCountAll;
    }

    return stats;
}
#endif

} // namespace dmq::os
