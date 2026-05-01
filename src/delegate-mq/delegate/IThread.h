#ifndef _ITHREAD_H
#define _ITHREAD_H

/// @file IThread.h
/// @brief Interface for cross-thread delegate dispatching.
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.

#include "DelegateMsg.h"

namespace dmq {

/// @TODO Implement the IThread interface if necessary.
/// @brief A base class for a delegate enabled execution thread. Implemented by 
/// application code if asynchronous delegates are used. 
/// 
/// @details Each platform specific implementation must inherit from `IThread`
/// and provide an implementation for `DispatchDelegate()`. The `DispatchDelegate()`
/// function is called by the source thread to initiate an asynchronous function call
/// onto the destination thread of control.
class IThread
{
public:
	/// Destructor
	virtual ~IThread() = default;

	/// @brief Enqueues a delegate message for execution on this thread.
	///
	/// @details
	/// This function is called by the *source* thread (the caller). The implementation must
	/// thread-safely transfer ownership of the `msg` into the target thread's processing queue.
	///
	/// Once the message is received by the target thread's main loop, that loop is responsible
	/// for calling `IInvoker::Invoke(msg)` to actually execute the function.
	///
	/// @param[in] msg A shared pointer to the delegate message. This pointer must remain valid
	/// until the target thread finishes execution.
	/// @return true if the message was successfully enqueued, false otherwise.
	virtual bool DispatchDelegate(std::shared_ptr<DelegateMsg> msg) = 0;

	/// @brief Returns true if the calling thread is this thread.
	/// Used to decide whether to marshal an event or execute inline.
	virtual bool IsCurrentThread() = 0;
};

}

#endif
