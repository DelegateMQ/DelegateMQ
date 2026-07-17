#ifndef DMQ_PARTICIPANT_H
#define DMQ_PARTICIPANT_H

#include "delegate/DelegateRemote.h"
#include "delegate/DelegateAsync.h"
#include "delegate/DelegateOpt.h"
#include "port/transport/ITransport.h"
#include "port/transport/DmqHeader.h"
#include "extras/dispatcher/RemoteChannel.h"
#include "extras/util/Fault.h"
#include <algorithm>
#include <string>
#include <memory>
#include <typeindex>

namespace dmq::databus {

// A Participant represents a remote node in the DataBus network.
// It manages the transport and the remote channels for different signatures.
//
// LIFETIME NOTE: This class assumes that the ITransport object passed to the constructor 
// will outlive the Participant instance.
class Participant {
    XALLOCATOR
    friend class DataBus;
public:
    Participant(dmq::transport::ITransport& transport) : m_transport(&transport) {}

    // Set a thread to serialize all outgoing sends.
    // When set, Send() posts the channel invocation to this thread rather than
    // executing inline on the caller's thread. This provides two benefits:
    //   1. Thread safety: concurrent Publish() calls from multiple threads are
    //      safe — each send is queued rather than racing on the shared stream.
    //   2. Non-blocking callers: blocking I/O in the transport cannot stall
    //      unrelated publishing threads.
    // Without a send thread, concurrent calls to Send() for the same remote ID
    // are a data race on the channel's internal stream buffer.
    void SetSendThread(dmq::IThread* thread) { m_sendThread = thread; }

    // Add a remote topic mapping.
    // When local DataBus publishes to 'topic', it will be sent to this participant using 'remoteId'.
    void AddRemoteTopic(const dmq::xstring& topic, dmq::DelegateRemoteId remoteId) {
        dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        m_topicToRemoteId[topic] = remoteId;
    }

    // Enable or disable continuous error mode (disables error latching).
private:
    void EnableContinuousErrors(bool enable) {
        m_continuousErrors = enable;
    }
public:

    // Subscribe to technical errors (serialization/dispatch) for this participant.
    [[nodiscard]] dmq::ScopedConnection SubscribeError(std::function<void(const dmq::xstring&, dmq::DelegateError)> func) {
        return m_errorSignal.Connect(dmq::MakeDelegate(std::move(func)));
    }

    // Process incoming data from the transport.
    // Must not be called concurrently — m_receiveMutex serializes the transport read.
    // @return The result code from ITransport::Receive.
    int ProcessIncoming() {
        dmq::transport::DmqHeader header;
        dmq::xstringstream inputStream(std::ios::in | std::ios::out | std::ios::binary);

        int result;
        {
            // Serialize access to the transport read
            dmq::LockGuard<dmq::Mutex> receiveLock(m_receiveMutex);
            result = m_transport->Receive(inputStream, header);
        }

        if (result == 0) {
            // Validate header marker
            if (header.GetMarker() != dmq::transport::DmqHeader::MARKER) {
                return -1; // Protocol error
            }

            dmq::DelegateRemoteId id = header.GetId();
            uint16_t seqNum = header.GetSeqNum();

            // Filter out duplicate messages (retries)
            if (id != dmq::ACK_REMOTE_ID) {
                dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
                if (m_history[id].is_duplicate(seqNum)) {
                    return 0; // Silently drop duplicate
                }
            }

            dmq::IRemoteInvoker* invoker = nullptr;
            std::shared_ptr<void> channelLifetime; // keeps channel alive across lock gap
            {
                dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
                auto it = m_channels.find(id);
                if (it != m_channels.end()) {
                    channelLifetime = it->second.channel;
                    invoker = it->second.invoker;
                }
            }

            // Invoke outside the lock to prevent deadlocks and allow re-entry
            if (invoker) {
                invoker->Invoke(inputStream);
            } else {
                OnChannelError(id, dmq::DelegateError::ERR_NO_DISPATCHER, 0);
            }
        }
        return result;
    }

    // Send data for a topic.
    // If a send thread was set via SetSendThread(), the channel invocation is
    // dispatched asynchronously to that thread. Otherwise it executes inline.
    template <typename T>
    void Send(const dmq::xstring& topic, const T& data, dmq::ISerializer<void(T)>& serializer) {
        dmq::DelegateRemoteId remoteId;
        if (GetRemoteId(topic, remoteId)) {
            auto channel = GetOrCreateChannel<T>(remoteId, serializer);
            if (channel) {
                if (m_sendThread) {
                    // Use a member function to avoid potential shared_ptr circular 
                    // references. Pass arguments to AsyncInvoke.
                    (void)dmq::MakeDelegate(this, &Participant::AsyncSend<T>, *m_sendThread).AsyncInvoke(channel, data);
                } else {
                    (*channel)(data);
                }
            }
        }
    }

    /// @brief Internal helper to execute a remote send on a background thread.
    /// @details Using a member function prevents potential shared_ptr circular 
    /// references.
    template <typename T>
    void AsyncSend(std::shared_ptr<dmq::RemoteChannel<void(T)>> channel, T data) {
        if (channel) {
            (*channel)(data);
        }
    }

    // Register a local handler for a remote topic using a `std::function`.
    template <typename T>
    void RegisterHandler(dmq::DelegateRemoteId remoteId, dmq::ISerializer<void(T)>& serializer, std::function<void(T)> func) {
        dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        auto channel = dmq::xmake_shared<dmq::RemoteChannel<void(T)>>(*m_transport, serializer);

        // Use Bind() to register the callback for incoming calls.
        channel->Bind(func, remoteId);

        AttachErrorHandler(channel);

        m_channels[remoteId] = { channel, channel->GetEndpoint() };
        auto typeIdx = std::type_index(typeid(T));
        auto it = m_channelTypes.find(remoteId);
        if (it != m_channelTypes.end()) {
            ASSERT_TRUE(it->second == typeIdx);
        } else {
            m_channelTypes.emplace(remoteId, typeIdx);
        }
    }

    // Register a local handler for a remote topic using a raw lambda or functor.
    template <typename T, typename F, typename = std::enable_if_t<dmq::trait::is_callable<F>::value>>
    void RegisterHandler(dmq::DelegateRemoteId remoteId, dmq::ISerializer<void(T)>& serializer, F&& func) {
        dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        auto channel = dmq::xmake_shared<dmq::RemoteChannel<void(T)>>(*m_transport, serializer);

        // Use Bind() to register the callback for incoming calls.
        channel->Bind(std::forward<F>(func), remoteId);

        AttachErrorHandler(channel);

        m_channels[remoteId] = { channel, channel->GetEndpoint() };
        auto typeIdx = std::type_index(typeid(T));
        auto it = m_channelTypes.find(remoteId);
        if (it != m_channelTypes.end()) {
            ASSERT_TRUE(it->second == typeIdx);
        } else {
            m_channelTypes.emplace(remoteId, typeIdx);
        }
    }

private:
    // Get the remote ID for a topic.
    bool GetRemoteId(const dmq::xstring& topic, dmq::DelegateRemoteId& remoteId) {
        dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        auto it = m_topicToRemoteId.find(topic);
        if (it != m_topicToRemoteId.end()) {
            remoteId = it->second;
            return true;
        }
        return false;
    }

    // Get the topic name for a remote ID (error path only — linear scan is fine).
    bool GetTopicName(dmq::DelegateRemoteId remoteId, dmq::xstring& topic) {
        dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        for (auto& [t, id] : m_topicToRemoteId) {
            if (id == remoteId) { topic = t; return true; }
        }
        return false;
    }

    template <typename T>
    void AttachErrorHandler(std::shared_ptr<dmq::RemoteChannel<void(T)>>& channel) {
        channel->SetErrorHandler(dmq::MakeDelegate(this, &Participant::OnChannelError));
    }

    void OnChannelError(dmq::DelegateRemoteId id, dmq::DelegateError error, dmq::DelegateErrorAux) {
        if (error == dmq::DelegateError::SUCCESS) return;
        bool shouldFire = false;
        dmq::xstring topic;
        {
            dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
            for (auto& [t, rid] : m_topicToRemoteId) {
                if (rid == id) { topic = t; break; }
            }
            if (topic.empty()) {
                // xostringstream keeps this portable: under DMQ_ALLOCATOR, xstring and
                // std::string are different types, so std::to_string concatenation
                // would not compile.
                dmq::xostringstream os;
                os << "UnknownRemoteId:" << id;
                topic = os.str();
            }
            uint16_t bit = uint16_t(1u << static_cast<int>(error));
            auto& bits = m_reportedErrors[topic];
            if (!(bits & bit)) {
                bits |= bit;
                shouldFire = true;
            } else if (m_continuousErrors) {
                shouldFire = true;
            }
        }
        if (shouldFire)
            m_errorSignal(topic, error);
    }

    void ResetErrors() {
        dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        m_reportedErrors.clear();
    }

    struct ChannelInvoker {
        std::shared_ptr<void> channel;
        dmq::IRemoteInvoker* invoker = nullptr;
    };

    template <typename T>
    std::shared_ptr<dmq::RemoteChannel<void(T)>> GetOrCreateChannel(dmq::DelegateRemoteId remoteId, dmq::ISerializer<void(T)>& serializer) {
        dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        std::shared_ptr<dmq::RemoteChannel<void(T)>> channel;

        auto it = m_channels.find(remoteId);
        if (it != m_channels.end()) {
            // Type safety: catch remoteId reused with a different T
            auto itType = m_channelTypes.find(remoteId);
            if (itType != m_channelTypes.end()) {
                if (itType->second != std::type_index(typeid(T))) {
                    // Report before faulting so error handlers can log the mismatch
                    OnChannelError(remoteId, dmq::DelegateError::ERR_TYPE_MISMATCH, 0);
                    ASSERT();
                }
            }
            channel = std::static_pointer_cast<dmq::RemoteChannel<void(T)>>(it->second.channel);
        } else {
            channel = dmq::xmake_shared<dmq::RemoteChannel<void(T)>>(*m_transport, serializer);

            // Establish the remote ID for sending via operator().
            channel->SetRemoteId(remoteId);

            m_channels[remoteId] = { channel, channel->GetEndpoint() };
            m_channelTypes.emplace(remoteId, std::type_index(typeid(T)));

            // Ensure error handler is attached to the new channel
            AttachErrorHandler(channel);
        }

        return channel;
    }

    dmq::transport::ITransport* m_transport;
    dmq::IThread* m_sendThread = nullptr;
    bool m_continuousErrors = false;
    dmq::RecursiveMutex m_mutex;
    dmq::Mutex m_receiveMutex;
    dmq::xmap<dmq::xstring, dmq::DelegateRemoteId> m_topicToRemoteId;
    dmq::xmap<dmq::DelegateRemoteId, ChannelInvoker> m_channels;
    dmq::xmap<dmq::DelegateRemoteId, std::type_index> m_channelTypes;
    dmq::xmap<dmq::xstring, uint16_t> m_reportedErrors;
    dmq::Signal<void(const dmq::xstring&, dmq::DelegateError)> m_errorSignal;

    // --- Duplicate Filtering ---
    struct SeqHistory {
        static constexpr size_t SIZE = DMQ_SEQ_HISTORY_SIZE;
        uint16_t buffer[SIZE];
        bool valid[SIZE];
        size_t head = 0;

        SeqHistory() { std::fill(valid, valid + SIZE, false); }

        bool is_duplicate(uint16_t seq) {
            for (size_t i = 0; i < SIZE; ++i) {
                if (valid[i] && buffer[i] == seq) return true;
            }
            buffer[head] = seq;
            valid[head] = true;
            head = (head + 1) % SIZE;
            return false;
        }
    };
    dmq::xmap<dmq::DelegateRemoteId, SeqHistory> m_history;
};

} // namespace dmq::databus


#endif // DMQ_PARTICIPANT_H
