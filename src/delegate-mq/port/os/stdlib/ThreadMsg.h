#ifndef _THREAD_MSG_H
#define _THREAD_MSG_H

#include "delegate/DelegateOpt.h"

namespace dmq::os {

/// @brief A class to hold a platform-specific thread messsage that will be passed 
/// through the OS message queue. 
class ThreadMsg
{
public:
	/// Constructor
	/// @param[in] id - a unique identifier for the thread messsage
	/// @param[in] data - a pointer to the messsage data to be typecast
	///		by the receiving task based on the id value. 
	/// @pre The data pointer argument *must* be created on the heap.
	/// @post The destination thread will delete the heap allocated data once the 
	///		callback is complete.  
	ThreadMsg(int id, std::shared_ptr<dmq::DelegateMsg> data) :
		m_id(id), 
		m_data(data)
	{
	}

	int GetId() const { return m_id; } 

    std::shared_ptr<dmq::DelegateMsg> GetData() const { return m_data; }

	dmq::Priority GetPriority() const {
		return m_data ? m_data->GetPriority() : dmq::Priority::NORMAL;
	}

#if defined(DMQ_DATABUS_TOOLS)
	void SetEnqueueTime(dmq::TimePoint time) { m_enqueueTime = time; }
	dmq::TimePoint GetEnqueueTime() const { return m_enqueueTime; }
#endif

private:
	int m_id;
    std::shared_ptr<dmq::DelegateMsg> m_data;
#if defined(DMQ_DATABUS_TOOLS)
	dmq::TimePoint m_enqueueTime;
#endif

	// Use fixed-block memory allocator if DMQ_ALLOCATOR set
	XALLOCATOR
};

} // namespace dmq::os

#endif
