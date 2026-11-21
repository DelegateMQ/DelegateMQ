#ifndef DMQ_PARTICIPANT_H
#define DMQ_PARTICIPANT_H

#include "delegate/DelegateRemote.h"
#include "delegate/DelegateAsync.h"
#include "delegate/DelegateOpt.h"
#include "port/transport/common/ITransport.h"
#include "port/transport/common/DmqHeader.h"
#include "extras/dispatcher/RemoteChannel.h"
#include "extras/util/Fault.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <string>
#include <memory>

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

    /// @brief How long a received sequence number counts toward duplicate
    /// detection. Defaults to dmq::SEQ_HISTORY_EXPIRY (DMQ_SEQ_HISTORY_EXPIRY_MS).
    /// @details Set it to the sender's retry window (its ACK timeout times its
    /// retries): long enough that every retry of a message is caught, and no
    /// longer. A sender that restarts numbers its messages from the start again,
    /// so within this window its new messages look like retries of old ones and
    /// are dropped (reported through OnDuplicateDropped). Messages that must get
    /// through a restart carry their own session or sequence field (see
    /// example/sensor-node's MessageBase).
    void SetSeqHistoryExpiry(dmq::Duration expiry) {
        dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        m_seqHistoryExpiry = expiry;
    }

    /// @brief Fired when a received message is dropped as a duplicate: a retry of
    /// one already delivered, or a restarted sender reusing a sequence number
    /// within the history window (see SetSeqHistoryExpiry()). Retries are normal
    /// on a lossy link; a steady stream of drops from one sender after it
    /// restarted is not. Called on the thread that calls ProcessIncoming().
    /// Subscribers receive: (remoteId, seqNum, sender node ID).
    dmq::Signal<void(dmq::DelegateRemoteId, uint16_t, uint8_t)> OnDuplicateDropped;

    /// @brief How many received messages were dropped as duplicates.
    uint32_t DuplicatesDropped() const noexcept { return m_duplicatesDropped.load(std::memory_order_relaxed); }

    /// @brief Map a topic string to a remote ID for outbound sends to this
    /// participant.
    /// @details When local `DataBus::Publish()` sends to `topic`, it is
    /// serialized and sent to this participant using `remoteId` on the wire.
    /// Required for the sending side of remote distribution — see
    /// `docs/DATABUS.md`'s "Setup Checklist".
    /// @param topic     Topic string to map.
    /// @param remoteId  Wire identifier this topic is sent as. Must match the
    ///                  ID the receiving node registers via
    ///                  `DataBus::AddIncomingTopic<T>()` for the same topic.
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
    // Blocks for up to the transport's receive timeout when nothing arrives: for a
    // UDP SUB socket, DMQ_UDP_SUB_RECV_TIMEOUT_MS (2 s by default). A thread that
    // calls this between other work (from a timer, a polling loop) needs a short
    // timeout: call the transport's SetRecvTimeout() (e.g. 1-2 ms) after Create().
    // @return The result code from ITransport::Receive.
    int ProcessIncoming() {
        dmq::transport::DmqHeader header;
        // Views the transport's receive buffer: valid until its next Receive(),
        // and ProcessIncoming() is never called concurrently, so it stays valid
        // through the dispatch below
        dmq::ByteReader inputStream;

        int result;
        {
            // Serialize access to the transport read
            dmq::LockGuard<dmq::Mutex> receiveLock(m_receiveMutex);
            result = m_transport->Receive(inputStream, header);
        }

        if (result == 0) {
            // Validate header marker and protocol version
            if (!header.IsValid()) {
                OnChannelError(dmq::INVALID_REMOTE_ID, dmq::DelegateError::ERR_TRANSPORT_RECEIVE, 0);
                return -1; // Protocol error
            }

            dmq::DelegateRemoteId id = header.GetId();
            uint16_t seqNum = header.GetSeqNum();

            // An ACK was already handled inside the transport (it cleared the
            // pending entry in the transport monitor). It has no channel, so
            // it isn't an ERR_NO_DISPATCHER.
            if (id == dmq::ACK_REMOTE_ID)
                return 0;

            dmq::IRemoteInvoker* invoker = nullptr;
            std::shared_ptr<void> channelLifetime; // keeps channel alive across lock gap
            bool duplicate = false;
            {
                dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
                auto it = m_channels.find(id);
                if (it != m_channels.end()) {
                    // Filter out duplicate messages (retries)
                    if (m_history[HistoryKey(header.GetNodeId(), id)].is_duplicate(seqNum, dmq::Clock::now(), m_seqHistoryExpiry)) {
                        duplicate = true;
                    } else {
                        channelLifetime = it->second.channel;
                        invoker = it->second.invoker;
                    }
                }
            }

            // Dropped, not delivered: counted and reported (outside the lock),
            // never silently
            if (duplicate) {
                m_duplicatesDropped.fetch_add(1, std::memory_order_relaxed);
                OnDuplicateDropped(id, seqNum, header.GetNodeId());
                return 0;
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
    bool DispatchIfInterested(const dmq::xstring& topic, const T& data, dmq::ISerializer<void(T)>* serializer) {
        std::shared_ptr<dmq::RemoteChannel<void(T)>> channel;
        bool typeMismatch = false;
        dmq::DelegateRemoteId remoteId = 0;

        {
            dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
            auto it = m_topicToRemoteId.find(topic);
            if (it != m_topicToRemoteId.end()) {
                if (!serializer) {
                    return true;
                }
                remoteId = it->second;
                channel = GetOrCreateChannelLocked<T>(remoteId, *serializer, typeMismatch);
            } else {
                return false;
            }
        }

        if (typeMismatch) {
            OnChannelError(remoteId, dmq::DelegateError::ERR_TYPE_MISMATCH, 0);
            DMQ_ASSERT();
            return true;
        }

        if (channel) {
            if (m_sendThread) {
                // Static function, not a lambda (no capture to allocate) and not
                // a member (the queued message must not hold a raw `this`; the
                // Participant may be destroyed before the send thread runs it).
                // The channel shared_ptr keeps the channel alive until then.
                (void)dmq::MakeDelegate(&Participant::AsyncSend<T>, *m_sendThread).AsyncInvoke(channel, data);
            } else {
                (*channel)(data);
            }
        }
        return true;
    }

    template <typename T>
    void Send(const dmq::xstring& topic, const T& data, dmq::ISerializer<void(T)>& serializer) {
        DispatchIfInterested(topic, data, &serializer);
    }

    /// @brief Internal helper to execute a remote send on a background thread.
    /// @details Static so the queued message holds no `this`: the channel
    /// shared_ptr is all it needs, and the Participant may be gone by then.
    template <typename T>
    static void AsyncSend(std::shared_ptr<dmq::RemoteChannel<void(T)>> channel, T data) {
        if (channel) {
            (*channel)(data);
        }
    }

    /// @brief Register a local handler invoked when data arrives from this
    /// participant for `remoteId`, using a `std::function`.
    /// @details Required for the receiving side of remote distribution — see
    /// `docs/DATABUS.md`'s "Setup Checklist". Creates the participant's
    /// `RemoteChannel<void(T)>` for `remoteId` if one doesn't already exist.
    /// @tparam T          Message type this handler expects. Must match the
    ///                    type the sending node publishes as `remoteId`, or
    ///                    this reports ERR_TYPE_MISMATCH via
    ///                    `Participant::SubscribeError`/`DataBus::SubscribeError`
    ///                    and asserts.
    /// @param remoteId    Wire identifier to handle incoming data for.
    /// @param serializer  Deserializes the incoming wire bytes into a `T`.
    /// @param func        Called with each deserialized `T`.
    template <typename T>
    void RegisterHandler(dmq::DelegateRemoteId remoteId, dmq::ISerializer<void(T)>& serializer, std::function<void(T)> func) {
        bool typeMismatch = false;
        {
            dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
            auto channel = GetOrCreateChannelLocked<T>(remoteId, serializer, typeMismatch);
            if (channel) {
                // Use Bind() to register the callback for incoming calls.
                channel->Bind(func, remoteId);
            }
        }
        if (typeMismatch) {
            OnChannelError(remoteId, dmq::DelegateError::ERR_TYPE_MISMATCH, 0);
            DMQ_ASSERT();
        }
    }

    /// @brief Register a local handler invoked when data arrives from this
    /// participant for `remoteId`, using a raw lambda or functor.
    /// @details Same behavior as the `std::function` overload above, just
    /// without the `std::function` wrapping overhead for a plain callable.
    /// @tparam T          Message type this handler expects. Must match the
    ///                    type the sending node publishes as `remoteId`, or
    ///                    this reports ERR_TYPE_MISMATCH via
    ///                    `Participant::SubscribeError`/`DataBus::SubscribeError`
    ///                    and asserts.
    /// @tparam F          Callable type, deduced from `func`. Must be invocable
    ///                    as `void(T)`.
    /// @param remoteId    Wire identifier to handle incoming data for.
    /// @param serializer  Deserializes the incoming wire bytes into a `T`.
    /// @param func        Called with each deserialized `T`.
    template <typename T, typename F, typename = std::enable_if_t<dmq::trait::is_callable<F>::value>>
    void RegisterHandler(dmq::DelegateRemoteId remoteId, dmq::ISerializer<void(T)>& serializer, F&& func) {
        bool typeMismatch = false;
        {
            dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
            auto channel = GetOrCreateChannelLocked<T>(remoteId, serializer, typeMismatch);
            if (channel) {
                // Use Bind() to register the callback for incoming calls.
                channel->Bind(std::forward<F>(func), remoteId);
            }
        }
        if (typeMismatch) {
            OnChannelError(remoteId, dmq::DelegateError::ERR_TYPE_MISMATCH, 0);
            DMQ_ASSERT();
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
    void AttachErrorHandler(dmq::RemoteChannel<void(T)>* channel) {
        channel->SetErrorHandler(dmq::MakeDelegate(this, &Participant::OnChannelError));
    }

    // Type-instantiated but non-templated-TYPE deleter: &DeleteRemoteChannel<T> decays
    // to the same void(*)(void*) regardless of T, so the shared_ptr<void> control block
    // built from it in GetOrCreateChannelLocked below is not templated on T either.
    template <typename T>
    static void DeleteRemoteChannel(void* p) {
        delete static_cast<dmq::RemoteChannel<void(T)>*>(p);
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
                // snprintf, not a stringstream: no iostreams in the library (and
                // under DMQ_ALLOCATOR, xstring and std::string differ, so
                // std::to_string concatenation wouldn't compile)
                char name[32];
                snprintf(name, sizeof(name), "UnknownRemoteId:%u", static_cast<unsigned>(id));
                topic = name;
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
    std::shared_ptr<dmq::RemoteChannel<void(T)>> GetOrCreateChannelLocked(dmq::DelegateRemoteId remoteId, dmq::ISerializer<void(T)>& serializer, bool& outTypeMismatch) {
        std::shared_ptr<dmq::RemoteChannel<void(T)>> channel;
        outTypeMismatch = false;

        auto it = m_channels.find(remoteId);
        if (it != m_channels.end()) {
            // Type safety: catch remoteId reused with a different T
            auto itType = m_channelTypes.find(remoteId);
            if (itType != m_channelTypes.end()) {
                if (itType->second != dmq::TypeKeyOf<T>()) {
                    outTypeMismatch = true;
                    return nullptr;
                }
            }
            channel = std::static_pointer_cast<dmq::RemoteChannel<void(T)>>(it->second.channel);
        } else {
            auto* raw = new dmq::RemoteChannel<void(T)>(*m_transport, serializer);

            // Establish the remote ID for sending via operator().
            raw->SetRemoteId(remoteId);

            // Ensure error handler is attached to the new channel
            AttachErrorHandler(raw);

            // Erase to void* BEFORE constructing the shared_ptr, so the control block's
            // deduced pointer type is void (non-templated) rather than
            // RemoteChannel<void(T)>* -- this is what lets the control block be shared
            // across every distinct T instead of duplicated per signature.
            std::shared_ptr<void> channelVoid(static_cast<void*>(raw), &DeleteRemoteChannel<T>, ::dmq::stl_allocator<void>());
            channel = std::static_pointer_cast<dmq::RemoteChannel<void(T)>>(channelVoid);

            m_channels[remoteId] = { channelVoid, raw->GetEndpoint() };
            m_channelTypes.emplace(remoteId, dmq::TypeKeyOf<T>());
        }

        return channel;
    }

    dmq::transport::ITransport* m_transport;
    dmq::IThread* m_sendThread = nullptr;
    dmq::Duration m_seqHistoryExpiry = dmq::SEQ_HISTORY_EXPIRY;
    bool m_continuousErrors = false;
    dmq::RecursiveMutex m_mutex;
    dmq::Mutex m_receiveMutex;
    dmq::xmap<dmq::xstring, dmq::DelegateRemoteId> m_topicToRemoteId;
    dmq::xmap<dmq::DelegateRemoteId, ChannelInvoker> m_channels;
    dmq::xmap<dmq::DelegateRemoteId, dmq::TypeKey> m_channelTypes;
    dmq::xmap<dmq::xstring, uint16_t> m_reportedErrors;
    dmq::Signal<void(const dmq::xstring&, dmq::DelegateError)> m_errorSignal;

    // --- Duplicate Filtering ---
    // Retries reuse the original sequence number, so a recently seen number from
    // the same sender and remote ID is a duplicate. Sequence numbers are per
    // sender, so history is kept per (sender node ID, remote ID); senders that
    // leave their node ID unset (0) share one history per remote ID.
    // Entries expire after m_seqHistoryExpiry, which covers the sender's whole
    // retry window: an older match is a restarted sender reusing the number,
    // not a retry.
    static uint32_t HistoryKey(uint8_t nodeId, dmq::DelegateRemoteId id) {
        return (static_cast<uint32_t>(nodeId) << 16) | id;
    }

    struct SeqHistory {
        static constexpr size_t SIZE = DMQ_SEQ_HISTORY_SIZE;
        std::array<uint16_t, SIZE> buffer;
        std::array<dmq::TimePoint, SIZE> received;
        std::array<bool, SIZE> valid;
        size_t head = 0;

        SeqHistory() { valid.fill(false); }

        bool is_duplicate(uint16_t seq, dmq::TimePoint now, dmq::Duration expiry) {
            for (size_t i = 0; i < SIZE; ++i) {
                if (valid[i] && buffer[i] == seq) {
                    if (now - received[i] < expiry)
                        return true;
                    // Expired: a new message reusing the number. Refresh this
                    // slot rather than take a second one for the same number.
                    received[i] = now;
                    return false;
                }
            }
            buffer[head] = seq;
            received[head] = now;
            valid[head] = true;
            head = (head + 1) % SIZE;
            return false;
        }
    };
    dmq::xmap<uint32_t, SeqHistory> m_history;
    std::atomic<uint32_t> m_duplicatesDropped{ 0 };
};

} // namespace dmq::databus


#endif // DMQ_PARTICIPANT_H
