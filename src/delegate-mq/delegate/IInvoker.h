#ifndef _IINVOKER_H
#define _IINVOKER_H

/// @file
/// @brief Delegate inter-thread invoker base class. 

#include <memory>
#include "DelegateOpt.h"

namespace dmq {

class DelegateMsg;

/// @brief Abstract base class to support asynchronous delegate function invoke
/// on destination thread of control. Used internally by the delegate library, 
/// not user application code. 
class IThreadInvoker
{
public:
    virtual ~IThreadInvoker() = default;

	/// Called to invoke the bound target function by the destination thread of control.
	/// @param[in] msg The incoming delegate message.
	/// @return `true` if function was invoked; `false` if failed. 
	virtual bool Invoke(std::shared_ptr<DelegateMsg> msg) = 0;
};

/// @brief Abstract base class to support remote delegate function invoke
/// to a remote system. Used internally by the delegate library, not
/// user application code.
class IRemoteInvoker
{
public: 
    virtual ~IRemoteInvoker() = default;

    /// Called to invoke the bound target function by the remote system. 
    /// @param[in] is The incoming remote message's bytes.
    /// @return `true` if function was invoked; `false` if failed. 
    virtual bool Invoke(ByteReader& is) = 0;
};

}

#endif