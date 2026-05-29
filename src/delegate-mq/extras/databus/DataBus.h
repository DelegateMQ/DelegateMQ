#ifndef DMQ_DATABUS_H
#define DMQ_DATABUS_H

#include "delegate/Signal.h"
#include "delegate/DelegateRemote.h"
#include "delegate/DelegateAsync.h"
#include "delegate/IThread.h"
#include "delegate/DelegateOpt.h"
#include "Participant.h"
#include "DataBusQos.h"
#include "SpyPacket.h"
#include "extras/util/Fault.h"
#include "extras/util/NetworkConnect.h"

#include <string>
#include <memory>
#include <mutex>
#include <array>
#include <functional>
#include <typeindex>
#include <atomic>
#include <type_traits>

namespace dmq::databus {

namespace detail {
    // Helper class to implement QoS rate limiting without heavy lambda captures.
    // Storing state in a class and capturing only a shared_ptr in std::function 
    // ensures the delegate fits within Small Buffer Optimization (SBO).
    template <typename T>
    class RateLimiter {
        XALLOCATOR
    public:
        RateLimiter(std::function<void(T)> func, uint32_t minSepRep)
            : m_func(std::move(func)), m_minSepRep(minSepRep), m_lastDeliveryRep(0) {}

        void Invoke(T data) {
            auto nowRep = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(dmq::Clock::now().time_since_epoch()).count());
            auto lastRep = m_lastDeliveryRep.load(std::memory_order_relaxed);
            if (nowRep - lastRep >= m_minSepRep) {
                m_lastDeliveryRep.store(nowRep, std::memory_order_relaxed);
                m_func(data);
            }
        }

    private:
        std::function<void(T)> m_func;
        uint32_t m_minSepRep;
        std::atomic<uint32_t> m_lastDeliveryRep;
    };

    // Helper class to implement QoS filtering without heavy lambda captures.
    template <typename T, typename F, typename P>
    class Filter {
        XALLOCATOR
    public:
        Filter(F func, P predicate) : m_func(std::move(func)), m_predicate(std::move(predicate)) {}

        void Invoke(T data) {
            if (m_predicate(data)) {
                m_func(data);
            }
        }

    private:
        F m_func;
        P m_predicate;
    };

    // Helper class to implement topic forwarding without heavy lambda captures.
    template <typename T>
    class TopicForwarder {
        XALLOCATOR
    public:
        TopicForwarder(dmq::xstring topic, bool localOnly) : m_topic(std::move(topic)), m_localOnly(localOnly) {}

        void Invoke(const T& msg);

    private:
        dmq::xstring m_topic;
        bool m_localOnly;
    };
}

// The DataBus is a central registry for topic-based communication.
// It allows components to publish and subscribe to data topics identified by strings.
//
// LIFETIME NOTE: This class assumes that any external ISerializer or ITransport objects 
// passed to it (e.g., via AddParticipant or RegisterSerializer) will outlive 
// the DataBus instance or its Reset() calls.
class DataBus {
public:
    // Subscribe to a topic with optional QoS and thread dispatching.
    // NOTE: Signal connection is established before LVC delivery to ensure 
    // no messages are missed.
    template <typename T, typename F>
    [[nodiscard]] static dmq::ScopedConnection Subscribe(const dmq::xstring& topic, F&& func, dmq::IThread* thread = nullptr, QoS qos = {}) {
        return GetInstance().InternalSubscribe<T>(topic, std::forward<F>(func), thread, qos);
    }

    // Subscribe to a topic with a filter.
    template <typename T, typename F, typename P>
    [[nodiscard]] static dmq::ScopedConnection SubscribeFilter(const dmq::xstring& topic, F&& func, P&& predicate, dmq::IThread* thread = nullptr, QoS qos = {}) {
        auto filter = dmq::xmake_shared<detail::Filter<T, std::decay_t<F>, std::decay_t<P>>>(std::forward<F>(func), std::forward<P>(predicate));
        return GetInstance().InternalSubscribe<T>(topic, [filter](T data) { filter->Invoke(data); }, thread, qos);
    }

    // Publish data to a topic.
    template <typename T>
    static void Publish(const dmq::xstring& topic, const T& data) {
        GetInstance().InternalPublish<T>(topic, data, false);
    }

    // Publish data to local subscribers only — does NOT forward to remote participants.
    // Used by AddIncomingTopic to prevent relay loops when a topic is both incoming
    // and outgoing on the same node.
    template <typename T>
    static void PublishLocal(const dmq::xstring& topic, const T& data) {
        GetInstance().InternalPublish<T>(topic, data, true);
    }

    // Add a remote participant to the bus.
    static void AddParticipant(std::shared_ptr<Participant> participant) {
        GetInstance().InternalAddParticipant(participant);
    }

    // Register a serializer for a topic (required for remote distribution).
    template <typename T>
    static void RegisterSerializer(const dmq::xstring& topic, dmq::ISerializer<void(T)>& serializer) {
        GetInstance().InternalRegisterSerializer<T>(topic, serializer);
    }

    // Register an incoming remote topic and republish received data to the local bus only.
    // Uses PublishLocal — the message reaches local subscribers, the spy, and the LVC, but is
    // NOT re-forwarded to remote participants. This prevents relay loops when the same topic
    // is registered as both incoming and outgoing on the same node.
    //
    // For bridge/relay nodes that must forward incoming data to other remote participants,
    // use AddRelayTopic instead.
    template <typename T>
    static void AddIncomingTopic(const dmq::xstring& topic, dmq::DelegateRemoteId remoteId, Participant& participant, dmq::ISerializer<void(T)>& serializer) {
        auto forwarder = dmq::xmake_shared<detail::TopicForwarder<T>>(topic, true);
        participant.RegisterHandler<T>(remoteId, serializer, [forwarder](const T& msg) {
            forwarder->Invoke(msg);
        });
    }

    // Register an incoming remote topic and re-publish received data to ALL local subscribers
    // AND all registered remote participants (full Publish). Use this only on bridge/relay nodes
    // where the explicit intent is to forward incoming data to other remote nodes.
    //
    // WARNING: Using AddRelayTopic on a node that also has the same topic as an outgoing
    // (RegisterSerializer + AddParticipant) AND receives from a node that also relays will
    // create an infinite relay loop. Use AddIncomingTopic instead for subscriber-only nodes.
    template <typename T>
    static void AddRelayTopic(const dmq::xstring& topic, dmq::DelegateRemoteId remoteId, Participant& participant, dmq::ISerializer<void(T)>& serializer) {
        auto forwarder = dmq::xmake_shared<detail::TopicForwarder<T>>(topic, false);
        participant.RegisterHandler<T>(remoteId, serializer, [forwarder](const T& msg) {
            forwarder->Invoke(msg);
        });
    }

    // Register a stringifier for a topic to enable spying/logging.
    template <typename T>
    static void RegisterStringifier(const dmq::xstring& topic, std::function<dmq::xstring(const T&)> func) {
        GetInstance().InternalRegisterStringifier<T>(topic, std::move(func));
    }

    // Enable/Disable Last Value Cache (LVC) for a topic.
    static void LastValueCache(const dmq::xstring& topic, bool enabled) {
        GetInstance().InternalLastValueCache(topic, enabled);
    }

    // Subscribe to all bus traffic (topic and stringified value).
    // Monitor all traffic on the DataBus.
    // NOTE: priority is only applied when thread != nullptr; passing a non-default
    // priority without a thread is a programming error and triggers FaultHandler.
    [[nodiscard]] static dmq::ScopedConnection Monitor(std::function<void(const SpyPacket&)> func, dmq::IThread* thread = nullptr, dmq::Priority priority = dmq::Priority::NORMAL) {
        if (!thread && priority != dmq::Priority::NORMAL) {
            ::FaultHandler(__FILE__, (unsigned short)__LINE__);
            return {};
        }

        DataBus& instance = GetInstance();

        // Establish connection OUTSIDE the global DataBus lock to prevent 
        // lock inversion deadlocks. Signal::Connect() is already thread-safe.
        if (thread) {
            auto del = dmq::MakeDelegate(std::move(func), *thread);
            del.SetPriority(priority);
            return instance.m_monitorSignal.Connect(del);
        }
        return instance.m_monitorSignal.Connect(dmq::MakeDelegate(std::move(func)));
    }

    /// Fired when a message is published but has no local or remote subscribers.
    [[nodiscard]] static dmq::ScopedConnection SubscribeUnhandled(std::function<void(const dmq::xstring& topic)> func) {
        return GetInstance().m_unhandledSignal.Connect(dmq::MakeDelegate(std::move(func)));
    }

    /// Fired when a message is published but a technical error occurs (e.g. serialization failure).
    [[nodiscard]] static dmq::ScopedConnection SubscribeError(std::function<void(const dmq::xstring& topic, dmq::DelegateError error)> func) {
        return GetInstance().m_errorSignal.Connect(dmq::MakeDelegate(std::move(func)));
    }

    // Reset the DataBus (mostly for testing).
    static void ResetForTesting() {
        GetInstance().InternalReset();
    }

private:
    void InternalReportError(const dmq::xstring& topic, dmq::DelegateError error) {
        m_errorSignal(topic, error);
    }

    void InternalLastValueCache(const dmq::xstring& topic, bool enabled) {
        std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        m_topicQos[topic].lastValueCache = enabled;
    }

    DataBus() = default;
    ~DataBus() = default;

    DataBus(const DataBus&) = delete;
    DataBus& operator=(const DataBus&) = delete;

    static DataBus& GetInstance() {
        static DataBus instance;
        return instance;
    }

    template <typename T>
    using SignalPtr = std::shared_ptr<dmq::Signal<void(T)>>;

    template <typename T, typename F>
    [[nodiscard]] dmq::ScopedConnection InternalSubscribe(const dmq::xstring& topic, F&& func, dmq::IThread* thread, QoS qos) {
        SignalPtr<T> signal;

        // Performance Note: Forced std::function construction for internal type management.
        std::function<void(T)> typedFunc = std::forward<F>(func);

        // Wrap with min separation rate limiter if requested. Each subscriber gets its
        // own independent last-delivery timestamp, so different subscribers on the same
        // topic can have different (or no) rate limits without affecting each other.
        if (qos.minSeparation.has_value()) {
            auto minSepRep = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(qos.minSeparation.value()).count());
            auto limiter = dmq::xmake_shared<detail::RateLimiter<T>>(std::move(typedFunc), minSepRep);
            typedFunc = [limiter](T data) { limiter->Invoke(data); };
        }

        T* cachedValPtr = nullptr;
        T cachedVal;
        dmq::ScopedConnection conn;

        {
            std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);

            // 1. Enable LVC if requested (persists for topic lifetime until ResetForTesting)
            if (qos.lastValueCache) {
                m_topicQos[topic].lastValueCache = true;
            }

            // 2. Get or create signal with type safety check (std::type_index)
            signal = GetOrCreateSignal<T>(topic);
        }

        if (!signal) {
            return {}; // Type mismatch or other failure
        }

        // 3. Establish connection OUTSIDE the lock to prevent deadlock with Timer/Signal locks.
        // NOTE: There is a theoretical race where a publish happens between releasing the 
        // DataBus lock and acquiring the Signal lock. However, both use RecursiveMutex 
        // and InternalPublish also snapshots signals outside its lock, so this is 
        // architecturally consistent with the "lock-free dispatch" pattern used elsewhere.
        if (thread) {
            conn = signal->Connect(dmq::MakeDelegate(typedFunc, *thread));
        } else {
            conn = signal->Connect(dmq::MakeDelegate(typedFunc));
        }

        {
            std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
            // 4. Prepare LVC delivery if enabled and available
            if (qos.lastValueCache) {
                auto it = m_lastValues.find(topic);
                if (it != m_lastValues.end()) {
                    // Check lifespan: skip delivery if the cached value is too old
                    bool expired = false;
                    if (qos.lifespan.has_value()) {
                        auto age = dmq::Clock::now() - it->second.timestamp;
                        expired = (age > qos.lifespan.value());
                    }
                    if (!expired) {
                        cachedVal = *std::static_pointer_cast<T>(it->second.value);
                        cachedValPtr = &cachedVal;
                    }
                }
            }
        }

        // 5. Dispatch LVC outside the lock to prevent deadlocks.
        // IMPORTANT: Because this happens after releasing the lock, a high-frequency
        // publisher on another thread could have already sent a new value to the
        // connected signal. The subscriber might receive the fresh value FIRST,
        // followed by this stale LVC value — a "rewind" to an older state.
        //
        // Recommended mitigation: embed a monotonic sequence number in every message
        // (see MessageBase) and reject stale arrivals in the subscriber using
        // dmq::util::MonotonicGuard::IsNewer(). This is an application-level guard
        // and is the correct fix — resolving the race inside the DataBus lock would
        // require holding the lock across the async dispatch, which deadlocks.
        if (cachedValPtr) {
            if (thread) {
                dmq::MakeDelegate(typedFunc, *thread).AsyncInvoke(*cachedValPtr);
            } else {
                typedFunc(*cachedValPtr);
            }
        }

        return conn;
    }

    template <typename T>
    void InternalPublish(const dmq::xstring& topic, const T& data, bool localOnly) {
        // Capture timestamp before lock acquisition for maximum accuracy and 
        // monotonic ordering using dmq::Clock.
        auto now = dmq::Clock::now();
        uint64_t timestamp = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();

        SignalPtr<T> signal;
        std::shared_ptr<void> serializerPtr;
        dmq::ISerializer<void(T)>* serializer = nullptr;
        std::array<std::shared_ptr<Participant>, dmq::MAX_PARTICIPANTS> participantsSnapshot;
        size_t participantSnapshotCount = 0;
        dmq::xstring strVal = "?";
        bool hasMonitor = false;

        {
            std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);

            // 1. Type safety: verify T matches the registered type for this topic.
            // Must be first — before any writes — so a mismatch never corrupts LVC.
            auto itType = m_typeIndices.find(topic);
            if (itType != m_typeIndices.end() && itType->second != std::type_index(typeid(T))) {
                ::dmq::util::FaultHandler(__FILE__, (unsigned short)__LINE__);
                return;
            }

            // 2. Update LVC ONLY if enabled for this topic to save memory.
            // NOTE: QoS lastValueCache is currently "sticky" per topic. Once enabled
            // by any subscriber, it remains active for that topic until ResetForTesting().
            auto itQos = m_topicQos.find(topic);
            if (itQos != m_topicQos.end() && itQos->second.lastValueCache) {
                m_lastValues[topic] = LvcEntry{ dmq::xmake_shared<T>(data), now };
            }

            // 3. Prepare monitor data
            if (!m_monitorSignal.Empty()) {
                hasMonitor = true;
                auto itStr = m_stringifiers.find(topic);
                if (itStr != m_stringifiers.end()) {
                    auto func = static_cast<std::function<dmq::xstring(const T&)>*>(itStr->second.get());
                    strVal = (*func)(data).c_str();
                }
            }

            // 4. Get signal and remote info. Only create Signal if there is local interest.
            auto itSig = m_signals.find(topic);
            if (itSig != m_signals.end()) {
                signal = std::static_pointer_cast<dmq::Signal<void(T)>>(itSig->second);
            }

            auto itSer = m_serializers.find(topic);
            if (itSer != m_serializers.end()) {
                serializerPtr = itSer->second;
                serializer = static_cast<dmq::ISerializer<void(T)>*>(serializerPtr.get());
            }

            // 5. Snapshot participants while locked to ensure atomicity between
            // local and remote dispatch sets.
            for (size_t i = 0; i < m_participantCount; ++i)
                participantsSnapshot[i] = m_participants[i];
            participantSnapshotCount = m_participantCount;
        }

        // 6. Dispatch Monitor outside lock to allow re-entry/prevent deadlocks
        if (hasMonitor) {
            SpyPacket packet{ topic, strVal, timestamp };
            m_monitorSignal(packet);
        }

        // 7. Local distribution
        bool handled = false;
        if (signal) {
            (*signal)(data);
            handled = true;
        }

        // 8. Remote distribution using the snapshot
        if (!localOnly) {
            Participant* interested[dmq::MAX_PARTICIPANTS];
            size_t interestedCount = 0;
            for (size_t i = 0; i < participantSnapshotCount; ++i) {
                dmq::DelegateRemoteId rid;
                if (participantsSnapshot[i]->GetRemoteId(topic, rid)) {
                    interested[interestedCount++] = participantsSnapshot[i].get();
                }
            }

            if (interestedCount > 0) {
                if (serializer) {
                    for (size_t i = 0; i < interestedCount; ++i) {
                        interested[i]->Send<T>(topic, data, *serializer);
                        handled = true;
                    }
                } else {
                    // HAZARD 3: Topic has remote interest but no serializer — fire once per topic.
                    bool shouldFire = false;
                    {
                        std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
                        constexpr uint8_t bit = uint8_t(1u << static_cast<int>(dmq::DelegateError::ERR_NO_SERIALIZER));
                        auto& bits = m_reportedErrors[topic];
                        if (!(bits & bit)) { bits |= bit; shouldFire = true; }
                    }
                    if (shouldFire)
                        m_errorSignal(topic, dmq::DelegateError::ERR_NO_SERIALIZER);
                    handled = true;
                }
            }
        }

        // 9. Notify if no one received the message
        if (!handled) {
            m_unhandledSignal(topic);
        }
    }

    void InternalAddParticipant(std::shared_ptr<Participant> participant) {
        // Establish connection OUTSIDE the global DataBus lock to prevent
        // lock inversion deadlocks. Signal::Connect() is already thread-safe.
        auto conn = participant->SubscribeError(
            dmq::MakeDelegate(this, &DataBus::InternalReportError));

        std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        if (m_participantCount < dmq::MAX_PARTICIPANTS) {
            m_participantErrorConnections[m_participantCount] = std::move(conn);
            m_participants[m_participantCount++] = participant;
        }
        else
            ::dmq::util::FaultHandler(__FILE__, (unsigned short)__LINE__);
    }

    template <typename T>
    void InternalRegisterSerializer(const dmq::xstring& topic, dmq::ISerializer<void(T)>& serializer) {
        std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);

        auto itType = m_typeIndices.find(topic);
        if (itType != m_typeIndices.end()) {
            if (itType->second != std::type_index(typeid(T))) {
                ::dmq::util::FaultHandler(__FILE__, (unsigned short)__LINE__);
                return;
            }
        } else {
            m_typeIndices.emplace(topic, std::type_index(typeid(T)));
        }

        // Use shared_ptr with no-op deleter because serializer is owned by caller
        m_serializers[topic] = std::shared_ptr<void>(&serializer, [](void*) {});
    }

    template <typename T>
    void InternalRegisterStringifier(const dmq::xstring& topic, std::function<dmq::xstring(const T&)> func) {
        std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);

        // Runtime Type Safety: Ensure topic is not registered with multiple types
        auto itType = m_typeIndices.find(topic);
        if (itType != m_typeIndices.end()) {
            if (itType->second != std::type_index(typeid(T))) {
                ::dmq::util::FaultHandler(__FILE__, (unsigned short)__LINE__);
                return;
            }
        } else {
            m_typeIndices.emplace(topic, std::type_index(typeid(T)));
        }

        // Use shared_ptr with custom deleter to fix memory leak
        m_stringifiers[topic] = std::shared_ptr<void>(
            new std::function<dmq::xstring(const T&)>(std::move(func)),
            [](void* ptr) { delete static_cast<std::function<dmq::xstring(const T&)>*>(ptr); }
        );
    }

    void InternalReset() {
        std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        m_signals.clear();
        for (size_t i = 0; i < m_participantCount; ++i) {
            if (m_participants[i]) m_participants[i]->ResetErrors();
            m_participants[i].reset();
            m_participantErrorConnections[i].Disconnect();
        }
        m_participantCount = 0;
        m_serializers.clear();
        m_lastValues.clear();
        m_topicQos.clear();
        m_stringifiers.clear();
        m_typeIndices.clear();
        m_monitorSignal.Clear();
        m_reportedErrors.clear();
    }

    template <typename T>
    SignalPtr<T> GetOrCreateSignal(const dmq::xstring& topic) {
        // Assume lock is held by caller
        auto itType = m_typeIndices.find(topic);
        if (itType != m_typeIndices.end()) {
            if (itType->second != std::type_index(typeid(T))) {
                // Runtime Type Safety: Catch same topic string used with different types
                ::dmq::util::FaultHandler(__FILE__, (unsigned short)__LINE__);
                return nullptr; 
            }
        } else {
            m_typeIndices.emplace(topic, std::type_index(typeid(T)));
        }

        auto it = m_signals.find(topic);
        if (it != m_signals.end()) {
            return std::static_pointer_cast<dmq::Signal<void(T)>>(it->second);
        }

        auto signal = dmq::xmake_shared<dmq::Signal<void(T)>>();
        m_signals[topic] = std::static_pointer_cast<void>(signal);
        return signal;
    }

    struct LvcEntry {
        std::shared_ptr<void> value;
        dmq::TimePoint timestamp;
    };

    dmq::RecursiveMutex m_mutex;
    xmap<dmq::xstring, uint8_t> m_reportedErrors;
    xmap<dmq::xstring, std::shared_ptr<void>> m_signals;
    xmap<dmq::xstring, std::type_index> m_typeIndices;
    std::array<std::shared_ptr<Participant>, dmq::MAX_PARTICIPANTS> m_participants{};
    size_t m_participantCount = 0;
    xmap<dmq::xstring, std::shared_ptr<void>> m_serializers;
    xmap<dmq::xstring, LvcEntry> m_lastValues;
    xmap<dmq::xstring, QoS> m_topicQos;
    xmap<dmq::xstring, std::shared_ptr<void>> m_stringifiers;
    dmq::Signal<void(const SpyPacket&)> m_monitorSignal;
    dmq::Signal<void(const dmq::xstring& topic)> m_unhandledSignal;
    dmq::Signal<void(const dmq::xstring& topic, dmq::DelegateError error)> m_errorSignal;
    std::array<dmq::ScopedConnection, dmq::MAX_PARTICIPANTS> m_participantErrorConnections;
};

// Definition of TopicForwarder::Invoke must follow DataBus definition
template <typename T>
void detail::TopicForwarder<T>::Invoke(const T& msg) {
    if (m_localOnly)
        DataBus::PublishLocal<T>(m_topic, msg);
    else
        DataBus::Publish<T>(m_topic, msg);
}

} // namespace dmq::databus


#endif // DMQ_DATABUS_H
