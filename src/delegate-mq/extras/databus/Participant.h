#ifndef DMQ_PARTICIPANT_H
#define DMQ_PARTICIPANT_H

#include "delegate/DelegateRemote.h"
#include "delegate/DelegateAsync.h"
#include "delegate/DelegateOpt.h"
#include "port/transport/ITransport.h"
#include "port/transport/DmqHeader.h"
#include "extras/dispatcher/RemoteChannel.h"
#include "extras/util/Fault.h"
#include <string>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <typeindex>

namespace dmq::databus {

// A Participant represents a remote node in the DataBus network.
// It manages the transport and the remote channels for different signatures.
//
// LIFETIME NOTE: This class assumes that the ITransport object passed to the constructor 
// will outlive the Participant instance.
class Participant {
public:
    Participant(dmq::transport::ITransport& transport) : m_transport(&transport) {}

    // Set a thread to dispatch outgoing sends to. When set, Send() posts the
    // channel invocation to this thread rather than executing inline on the
    // calling thread. All remote sends are then serialized onto one thread,
    // preventing blocking I/O from stalling unrelated publishing threads.
    void SetSendThread(dmq::IThread* thread) { m_sendThread = thread; }

    // Add a remote topic mapping.
    // When local DataBus publishes to 'topic', it will be sent to this participant using 'remoteId'.
    void AddRemoteTopic(const std::string& topic, dmq::DelegateRemoteId remoteId) {
        std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        m_topicToRemoteId[topic] = remoteId;
    }

    // Process incoming data from the transport.
    // @return The result code from ITransport::Receive.
    int ProcessIncoming() {
        dmq::xstringstream is(std::ios::in | std::ios::out | std::ios::binary);
        dmq::transport::DmqHeader header;
        dmq::IRemoteInvoker* invoker = nullptr;

        int result = m_transport->Receive(is, header);
        if (result == 0) {
            // Validate header marker
            if (header.GetMarker() != dmq::transport::DmqHeader::MARKER) {
                return -1; // Protocol error
            }

            dmq::DelegateRemoteId id = header.GetId();
            uint16_t seqNum = header.GetSeqNum();

            // Filter out duplicate messages (retries)
            if (id != dmq::ACK_REMOTE_ID) {
                std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
                if (m_history[id].is_duplicate(seqNum)) {
                    return 0; // Silently drop duplicate
                }
            }

            std::shared_ptr<void> channelLifetime; // keeps channel alive across lock gap
            {
                std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
                auto it = m_channels.find(id);
                if (it != m_channels.end()) {
                    channelLifetime = it->second.channel;
                    invoker = it->second.invoker;
                }
            }

            // Invoke outside the lock to prevent deadlocks and allow re-entry
            if (invoker) {
                invoker->Invoke(is);
            }
        }
        return result;
    }

    // Send data for a topic.
    // If a send thread was set via SetSendThread(), the channel invocation is
    // dispatched asynchronously to that thread. Otherwise it executes inline.
    template <typename T>
    void Send(const std::string& topic, const T& data, dmq::ISerializer<void(T)>& serializer) {
        dmq::DelegateRemoteId remoteId;
        if (GetRemoteId(topic, remoteId)) {
            auto channel = GetOrCreateChannel<T>(remoteId, serializer);
            if (channel) {
                if (m_sendThread) {
                    auto ch = channel;
                    T d = data;
                    (void)dmq::MakeDelegate([ch, d]() { (*ch)(d); }, *m_sendThread).AsyncInvoke();
                } else {
                    (*channel)(data);
                }
            }
        }
    }

    // Register a local handler for a remote topic using a `std::function`.
    template <typename T>
    void RegisterHandler(dmq::DelegateRemoteId remoteId, dmq::ISerializer<void(T)>& serializer, std::function<void(T)> func) {
        std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        auto channel = std::make_shared<dmq::RemoteChannel<void(T)>>(*m_transport, serializer);

        // Use Bind() to register the callback for incoming calls.
        channel->Bind(func, remoteId);

        m_channels[remoteId] = { channel, channel->GetEndpoint() };
        m_channelTypes.emplace(remoteId, std::type_index(typeid(T)));
    }

    // Register a local handler for a remote topic using a raw lambda or functor.
    template <typename T, typename F, typename = std::enable_if_t<dmq::trait::is_callable<F>::value>>
    void RegisterHandler(dmq::DelegateRemoteId remoteId, dmq::ISerializer<void(T)>& serializer, F&& func) {
        std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        auto channel = std::make_shared<dmq::RemoteChannel<void(T)>>(*m_transport, serializer);

        // Use Bind() to register the callback for incoming calls.
        channel->Bind(std::forward<F>(func), remoteId);

        m_channels[remoteId] = { channel, channel->GetEndpoint() };
        m_channelTypes.emplace(remoteId, std::type_index(typeid(T)));
    }

private:
    struct ChannelInvoker {
        std::shared_ptr<void> channel;
        dmq::IRemoteInvoker* invoker = nullptr;
    };

    bool GetRemoteId(const std::string& topic, dmq::DelegateRemoteId& remoteId) {
        std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        auto it = m_topicToRemoteId.find(topic);
        if (it != m_topicToRemoteId.end()) {
            remoteId = it->second;
            return true;
        }
        return false;
    }

    template <typename T>
    std::shared_ptr<dmq::RemoteChannel<void(T)>> GetOrCreateChannel(dmq::DelegateRemoteId remoteId, dmq::ISerializer<void(T)>& serializer) {
        std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        auto it = m_channels.find(remoteId);
        if (it != m_channels.end()) {
            // Type safety: catch remoteId reused with a different T
            auto itType = m_channelTypes.find(remoteId);
            if (itType != m_channelTypes.end() && itType->second != std::type_index(typeid(T))) {
                ::dmq::util::FaultHandler(__FILE__, (unsigned short)__LINE__);
                return nullptr;
            }
            return std::static_pointer_cast<dmq::RemoteChannel<void(T)>>(it->second.channel);
        }

        auto channel = std::make_shared<dmq::RemoteChannel<void(T)>>(*m_transport, serializer);

        // Establish the remote ID for sending via operator().
        channel->SetRemoteId(remoteId);

        m_channels[remoteId] = { channel, channel->GetEndpoint() };
        m_channelTypes.emplace(remoteId, std::type_index(typeid(T)));
        return channel;
    }

    dmq::transport::ITransport* m_transport;
    dmq::IThread* m_sendThread = nullptr;
    dmq::RecursiveMutex m_mutex;
    std::unordered_map<std::string, dmq::DelegateRemoteId> m_topicToRemoteId;
    std::unordered_map<dmq::DelegateRemoteId, ChannelInvoker> m_channels;
    std::unordered_map<dmq::DelegateRemoteId, std::type_index> m_channelTypes;

    // --- Duplicate Filtering ---
    struct SeqHistory {
        static constexpr size_t SIZE = 8;
        uint16_t buffer[SIZE] = { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF };
        size_t head = 0;

        bool is_duplicate(uint16_t seq) {
            for (size_t i = 0; i < SIZE; ++i) {
                if (buffer[i] == seq) return true;
            }
            buffer[head] = seq;
            head = (head + 1) % SIZE;
            return false;
        }
    };
    std::unordered_map<dmq::DelegateRemoteId, SeqHistory> m_history;
};

} // namespace dmq::databus


#endif // DMQ_PARTICIPANT_H
