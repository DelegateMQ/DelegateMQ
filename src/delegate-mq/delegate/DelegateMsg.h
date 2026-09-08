#ifndef _DELEGATE_MSG_H
#define _DELEGATE_MSG_H

/// @file
/// @brief Delegate inter-thread message base class. 

#include "IInvoker.h"
#include "DelegateOpt.h"
#include "make_tuple_heap.h"
#include <tuple>
#include <memory>

namespace dmq {

// Async delegate message priority
//
// Contract every dmq::os::Thread port's queue implementation must uphold:
// a HIGH message is always dispatched ahead of any NORMAL message already
// waiting, but HIGH does NOT mean "jump to the very front" -- among multiple
// HIGH messages (or multiple NORMAL messages), FIFO order relative to each
// other must be preserved. I.e. per-priority-lane FIFO, not a LIFO stack.
//
// A single queue with a "send to front" primitive (FreeRTOS xQueueSendToFront,
// ThreadX tx_queue_front_send) gets this wrong: a second HIGH message sent to
// the front lands ahead of the first HIGH message, turning HIGH ordering into
// a stack. The correct shape -- used by every port -- is two lanes (one per
// priority) with new messages always pushed to the BACK of their lane, and
// the consumer draining the HIGH lane before the NORMAL lane:
//   - StdlibThread / Win32Thread: two std::deque, one shared mutex+condition_variable.
//   - ZephyrDelegateQueue:        two k_msgq,       drained via k_poll on both.
//   - CmsisRtos2: native osMessageQueuePut msg_prio gives this directly (no dual lane needed).
//   - FreeRTOSDelegateQueue / ThreadXDelegateQueue: two native queues, woken by
//     one shared counting semaphore "doorbell" (the RTOS-native stand-in for
//     the stdlib condition_variable, since neither queue API exposes one).
enum class Priority
{
	NORMAL,
	HIGH
};

/// @brief Base class for all delegate inter-thread messages
class DelegateMsg
{
public:
	/// Constructor
	/// @param[in] invoker - the invoker instance the delegate is registered with.
	DelegateMsg(std::shared_ptr<IThreadInvoker> invoker, Priority priority) :
		m_invoker(invoker), m_priority(priority)
	{
	}

	virtual ~DelegateMsg() = default;

	/// Get the delegate invoker instance the delegate is registered with.
	/// @return The invoker instance. 
	std::shared_ptr<IThreadInvoker> GetInvoker() const { return m_invoker; }

	/// Get the delegate message priority
	/// @return Delegate message priority
	Priority GetPriority() const { return m_priority; }

private:
	/// The IThreadInvoker instance used to invoke the target function 
    /// on the destination thread of control
	std::shared_ptr<IThreadInvoker> m_invoker;

	/// The delegate message priority
	Priority m_priority = Priority::NORMAL;

	// Use fixed-block memory allocator if DMQ_ALLOCATOR set
	XALLOCATOR
};

}

#endif
