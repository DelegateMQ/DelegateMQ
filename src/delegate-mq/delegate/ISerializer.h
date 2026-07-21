#ifndef _ISERIALIZER_H
#define _ISERIALIZER_H

/// @file ISerializer.h
/// @brief Interface for custom argument serialization/deserialization.
/// @see https://github.com/DelegateMQ/DelegateMQ
/// David Lafreniere, 2025.

#include <iostream>

namespace dmq {

    template <class R>
    class ISerializer; // Not defined

    /// @TODO Implement the ISerializer interface if necessary.
    /// @brief Interface definition for serializing remote function arguments.
    ///
    /// @details
    /// When using Remote Delegates, the library needs a way to turn arbitrary C++ function arguments
    /// into a byte stream (for network transport) and back again. 
    ///
    /// Users must implement this interface for the specific function signatures they intend to call 
    /// remotely, OR use one of the pre-defined serializers provided in `port/serialize`
    /// (e.g., MessagePack, JSON, Cereal).
    /// 
    /// @tparam RetType The return type of the function signature.
    /// @tparam Args The variadic argument types of the function signature.
    template<class RetType, class... Args>
    class ISerializer<RetType(Args...)>
    {
    public:
        virtual ~ISerializer() = default;

        /// @brief Serializes function arguments into the output stream.
        /// 
        /// @details 
        /// Called by the `DelegateRemote` when the user invokes a remote delegate. 
        /// The implementation must write every argument in `args` into `os`.
        /// 
        /// @param[out] os The output stream (buffer) to write data to.
        /// @param[in] args The actual arguments passed to the delegate invocation.
        /// @return Reference to the output stream.
        virtual std::ostream& Write(std::ostream& os, const Args&... args) = 0;

        /// @brief Deserializes data from the input stream into function arguments.
        /// 
        /// @details
        /// Called by the receiving endpoint before invoking the target function.
        /// The implementation must read data from `is` and populate the references in `args`.
        /// 
        /// @note
        /// When deserializing pointer arguments like `const T*`, `args` will be a reference 
        /// to a `const T*` (`const T*&`). Because the object is conceptually const, writing 
        /// deserialized data directly to `*arg` is ill-formed. As a workaround, implementations 
        /// must use `const_cast<T*>(arg)` to cast away constness and write to the underlying 
        /// mutable storage provided by the framework, or allocate a new object if preferred.
        /// 
        /// @param[in] is The input stream (buffer) containing received data.
        /// @param[out] args References to the arguments where the data should be stored.
        /// @return Reference to the input stream.
        virtual std::istream& Read(std::istream& is, Args&... args) = 0;
    };
}

#endif