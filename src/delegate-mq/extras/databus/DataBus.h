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
#include <array>
#include <functional>
#include <typeindex>
#include <atomic>
#include <type_traits>
#include <optional>
#include <new>

namespace dmq::databus {

namespace detail {
    // Helper class to implement QoS rate limiting without lambda captures.
    template <typename T>
    class RateLimiter {
        XALLOCATOR
    public:
        RateLimiter(dmq::UnicastDelegate<void(const T&)> func, uint32_t minSepRep)
            : m_func(std::move(func)), m_minSepRep(minSepRep), m_lastDeliveryRep(0) {}

        void Invoke(const T& data) {
            auto nowRep = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(dmq::Clock::now().time_since_epoch()).count());
            auto lastRep = m_lastDeliveryRep.load(std::memory_order_relaxed);
            if (nowRep - lastRep >= m_minSepRep) {
                m_lastDeliveryRep.store(nowRep, std::memory_order_relaxed);
                m_func(data);
            }
        }

    private:
        dmq::UnicastDelegate<void(const T&)> m_func;
        uint32_t m_minSepRep;
        std::atomic<uint32_t> m_lastDeliveryRep;
    };

    // Helper class to implement QoS filtering without heavy lambda captures.
    template <typename T>
    class Filter {
        XALLOCATOR
    public:
        Filter(dmq::UnicastDelegate<void(const T&)> func, dmq::UnicastDelegate<bool(const T&)> predicate)
            : m_func(std::move(func)), m_predicate(std::move(predicate)) {}

        void Invoke(const T& data) {
            if (m_predicate(data)) {
                m_func(data);
            }
        }

    private:
        dmq::UnicastDelegate<void(const T&)> m_func;
        dmq::UnicastDelegate<bool(const T&)> m_predicate;
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
        dmq::UnicastDelegate<void(const T&)> typedFunc;
        if constexpr (std::is_base_of_v<dmq::Delegate<void(const T&)>, std::decay_t<F>>)
            typedFunc = std::forward<F>(func);  // pre-formed delegate — direct assignment, no std::function
        else
            typedFunc = dmq::DelegateFunction<void(const T&)>(std::forward<F>(func));  // bridge callable → const T& signature
        return GetInstance().InternalSubscribe<T>(topic, std::move(typedFunc), thread, qos);
    }

    // Subscribe to a topic with a filter.
    template <typename T, typename F, typename P>
    [[nodiscard]] static dmq::ScopedConnection SubscribeFilter(const dmq::xstring& topic, F&& func, P&& predicate, dmq::IThread* thread = nullptr, QoS qos = {}) {
        dmq::UnicastDelegate<void(const T&)> funcUD;
        if constexpr (std::is_base_of_v<dmq::Delegate<void(const T&)>, std::decay_t<F>>)
            funcUD = std::forward<F>(func);
        else
            funcUD = dmq::DelegateFunction<void(const T&)>(std::forward<F>(func));

        dmq::UnicastDelegate<bool(const T&)> predUD;
        if constexpr (std::is_base_of_v<dmq::Delegate<bool(const T&)>, std::decay_t<P>>)
            predUD = std::forward<P>(predicate);
        else
            predUD = dmq::DelegateFunction<bool(const T&)>(std::forward<P>(predicate));

        auto filter = dmq::xmake_shared<detail::Filter<T>>(std::move(funcUD), std::move(predUD));
        // Capture filter by shared_ptr so the Filter stays alive for the connection's lifetime.
        return GetInstance().InternalSubscribe<T>(topic, dmq::DelegateFunction<void(const T&)>([filter](const T& data) { filter->Invoke(data); }), thread, qos);
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

    static void RemoveParticipant(std::shared_ptr<Participant> participant) {
        GetInstance().InternalRemoveParticipant(participant);
    }

    // Register a serializer for a topic (required for remote distribution).
    // Use this overload if the serializer is a long-lived object (e.g., static or 
    // global). The DataBus does NOT take ownership.
    template <typename T>
    static void RegisterSerializer(const dmq::xstring& topic, dmq::ISerializer<void(T)>& serializer) {
        auto shared = std::shared_ptr<void>(&serializer, [](void*) {}, ::dmq::stl_allocator<void>());
        GetInstance().InternalRegisterSerializer<T>(topic, std::move(shared));
    }

    // Register a serializer for a topic with ownership.
    // Use this overload to allow the DataBus to manage the serializer's lifetime 
    // through a shared_ptr.
    template <typename T>
    static void RegisterSerializer(const dmq::xstring& topic, std::shared_ptr<dmq::ISerializer<void(T)>> serializer) {
        GetInstance().InternalRegisterSerializer<T>(topic, std::static_pointer_cast<void>(serializer));
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
        participant.RegisterHandler(remoteId, serializer, [topic](const T& data) {
            PublishLocal(topic, data);
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
        participant.RegisterHandler(remoteId, serializer, [topic](const T& data) {
            Publish(topic, data);
        });
        participant.AddRemoteTopic(topic, remoteId);
    }

    // Register a stringifier for a topic to enable spying/logging.
    template <typename T, typename F>
    static void RegisterStringifier(const dmq::xstring& topic, F&& func) {
        dmq::UnicastDelegate<dmq::xstring(const T&)> ud;
        if constexpr (std::is_base_of_v<dmq::Delegate<dmq::xstring(const T&)>, std::decay_t<F>>)
            ud = std::forward<F>(func);
        else
            ud = dmq::DelegateFunction<dmq::xstring(const T&)>(std::forward<F>(func));
        GetInstance().InternalRegisterStringifier<T>(topic, std::move(ud));
    }

    // Enable/Disable Last Value Cache (LVC) for a topic.
    static void LastValueCache(const dmq::xstring& topic, bool enabled) {
        GetInstance().InternalLastValueCache(topic, enabled);
    }

    // Subscribe to all bus traffic (topic and stringified value).
    // NOTE: priority is only applied when thread != nullptr; passing a non-default
    // priority without a thread is a programming error and triggers FaultHandler.
    template <typename F>
    [[nodiscard]] static dmq::ScopedConnection Monitor(F&& func, dmq::IThread* thread = nullptr, dmq::Priority priority = dmq::Priority::NORMAL) {
        ASSERT_TRUE(thread || priority == dmq::Priority::NORMAL);

        dmq::UnicastDelegate<void(const SpyPacket&)> ud;
        if constexpr (std::is_base_of_v<dmq::Delegate<void(const SpyPacket&)>, std::decay_t<F>>)
            ud = std::forward<F>(func);
        else
            ud = dmq::DelegateFunction<void(const SpyPacket&)>(std::forward<F>(func));

        DataBus& instance = GetInstance();

        // Establish connection OUTSIDE the global DataBus lock to prevent
        // lock inversion deadlocks. Signal::Connect() is already thread-safe.
        if (thread) {
            auto del = dmq::MakeDelegate(std::function<void(const SpyPacket&)>(std::move(ud)), *thread);
            del.SetPriority(priority);
            return instance.m_monitorSignal.Connect(std::move(del));
        } else {
            return instance.m_monitorSignal.Connect(std::move(ud));
        }
    }

    /// Fired when a message is published but has no local or remote subscribers.
    template <typename F>
    [[nodiscard]] static dmq::ScopedConnection SubscribeUnhandled(F&& func) {
        dmq::UnicastDelegate<void(const dmq::xstring&)> ud;
        if constexpr (std::is_base_of_v<dmq::Delegate<void(const dmq::xstring&)>, std::decay_t<F>>)
            ud = std::forward<F>(func);
        else
            ud = dmq::DelegateFunction<void(const dmq::xstring&)>(std::forward<F>(func));
        return GetInstance().m_unhandledSignal.Connect(std::move(ud));
    }

    /// Fired when a message is published but a technical error occurs (e.g. serialization failure).
    template <typename F>
    [[nodiscard]] static dmq::ScopedConnection SubscribeError(F&& func) {
        dmq::UnicastDelegate<void(const dmq::xstring&, dmq::DelegateError)> ud;
        if constexpr (std::is_base_of_v<dmq::Delegate<void(const dmq::xstring&, dmq::DelegateError)>, std::decay_t<F>>)
            ud = std::forward<F>(func);
        else
            ud = dmq::DelegateFunction<void(const dmq::xstring&, dmq::DelegateError)>(std::forward<F>(func));
        return GetInstance().m_errorSignal.Connect(std::move(ud));
    }

    // Reset the DataBus (mostly for testing).
    static void ResetForTesting() {
        GetInstance().InternalReset();
    }

    // Enable or disable continuous error mode (disables error latching).
    static void EnableContinuousErrors(bool enable) {
        GetInstance().InternalEnableContinuousErrors(enable);
    }

private:
    void InternalEnableContinuousErrors(bool enable) {
        dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        m_continuousErrors = enable;
        for (size_t i = 0; i < m_participantCount; ++i) {
            if (m_participants[i]) {
                m_participants[i]->EnableContinuousErrors(enable);
            }
        }
    }

    void InternalReportLatchedError(const dmq::xstring& topic, dmq::DelegateError error) {
        bool shouldFire = false;
        {
            dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
            uint16_t bit = uint16_t(1u << static_cast<int>(error));
            auto& bits = m_reportedErrors[topic];
            if (!(bits & bit)) {
                bits |= bit;
                shouldFire = true;
            } else if (m_continuousErrors) {
                shouldFire = true;
            }
        }
        if (shouldFire) {
            m_errorSignal(topic, error);
        }
    }

    void InternalReportError(const dmq::xstring& topic, dmq::DelegateError error) {
        m_errorSignal(topic, error);
    }

    void InternalLastValueCache(const dmq::xstring& topic, bool enabled) {
        dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
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
    using SignalPtr = std::shared_ptr<dmq::Signal<void(const T&)>>;

    template <typename T>
    [[nodiscard]] dmq::ScopedConnection InternalSubscribe(const dmq::xstring& topic, dmq::UnicastDelegate<void(const T&)> typedFunc, dmq::IThread* thread, QoS qos) {
        SignalPtr<T> signal;

        // Wrap with min separation rate limiter if requested. Each subscriber gets its
        // own independent last-delivery timestamp, so different subscribers on the same
        // topic can have different (or no) rate limits without affecting each other.
        if (qos.minSeparation.has_value()) {
            auto minSepRep = static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(qos.minSeparation.value()).count());
            auto limiter = dmq::xmake_shared<detail::RateLimiter<T>>(std::move(typedFunc), minSepRep);
            // Capture limiter by shared_ptr so the RateLimiter stays alive for the connection's lifetime.
            // MakeDelegate(limiter.get(), ...) would store a raw pointer; the shared_ptr must be in the closure.
            typedFunc = dmq::DelegateFunction<void(const T&)>([limiter](const T& data) { limiter->Invoke(data); });
        }

        T* cachedValPtr = nullptr;
        std::optional<T> cachedVal;
        dmq::ScopedConnection conn;

        {
            dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);

            // 1. Enable LVC if requested (persists for topic lifetime until ResetForTesting)
            if (qos.lastValueCache) {
                m_topicQos[topic].lastValueCache = true;
            }

            // 2. Get or create signal with type safety check (std::type_index)
            signal = GetOrCreateSignal<T>(topic);
        }

        if (!signal) {
            // Type mismatch: report through the error signal for diagnosability,
            // then hard fault (wrong type is an invariant violation).
            InternalReportLatchedError(topic, dmq::DelegateError::ERR_TYPE_MISMATCH);
            ASSERT();
            return {};
        }

        // 3. Establish connection OUTSIDE the lock to prevent deadlock with Timer/Signal locks.
        // NOTE: There is a theoretical race where a publish happens between releasing the
        // DataBus lock and acquiring the Signal lock. However, both use RecursiveMutex
        // and InternalPublish also snapshots signals outside its lock, so this is
        // architecturally consistent with the "lock-free dispatch" pattern used elsewhere.
        using AsyncDelType = dmq::DelegateFunctionAsync<void(const T&)>;
        std::optional<AsyncDelType> asyncDel;
        if (thread) {
            asyncDel.emplace(dmq::MakeDelegate(std::function<void(const T&)>(std::move(typedFunc)), *thread));
            conn = signal->Connect(*asyncDel);
        } else {
            conn = signal->Connect(typedFunc);
        }

        {
            dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
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
                        cachedVal.emplace(*std::static_pointer_cast<T>(it->second.value));
                        cachedValPtr = &cachedVal.value();
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
            if (asyncDel)
                asyncDel->AsyncInvoke(*cachedValPtr);
            else
                typedFunc(*cachedValPtr);
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

        bool typeMismatch = false;
        {
            dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);

            // 1. Type safety: verify T matches the registered type for this topic.
            // Must be first — before any writes — so a mismatch never corrupts LVC.
            auto itType = m_typeIndices.find(topic);
            if (itType != m_typeIndices.end()) {
                if (itType->second != std::type_index(typeid(T))) {
                    typeMismatch = true;
                }
            } else {
                // Establish the topic's type on first use (mirrors GetOrCreateSignal and
                // the serializer registration paths below). Without this, a topic that's
                // never been subscribed to or serializer-registered would let a later
                // Publish<U> with a different U reach the LVC reuse block below and
                // reinterpret a live T object as U.
                m_typeIndices.emplace(topic, std::type_index(typeid(T)));
            }

            if (!typeMismatch) {
                // 2. Update LVC ONLY if enabled for this topic to save memory.
                // NOTE: QoS lastValueCache is currently "sticky" per topic. Once enabled
                // by any subscriber, it remains active for that topic until ResetForTesting().
                auto itQos = m_topicQos.find(topic);
                if (itQos != m_topicQos.end() && itQos->second.lastValueCache) {
                    auto itLvc = m_lastValues.find(topic);
                    if (itLvc != m_lastValues.end()) {
                        // Avoid hot-path allocation: reuse the existing memory block by
                        // destroying and copy-constructing in place, rather than
                        // copy-assigning. This only requires T to be copy-constructible
                        // (the same requirement as every other T published through
                        // DataBus) instead of also requiring T to be copy-assignable.
                        // The copy is built on the stack first so that if T's copy
                        // constructor throws, the cached slot is untouched rather than
                        // left destroyed with nothing reconstructed in its place.
                        T* cachedObj = static_cast<T*>(itLvc->second.value.get());
                        T replacement(data);
                        cachedObj->~T();
#if !defined(__cpp_exceptions) || defined(DMQ_ASSERTS)
                        ::new (static_cast<void*>(cachedObj)) T(std::move(replacement));
#else
                        try {
                            ::new (static_cast<void*>(cachedObj)) T(std::move(replacement));
                        }
                        catch (...) {
                            // The move constructor threw, leaving cachedObj's memory
                            // destroyed with no live object in it -- but the shared_ptr
                            // from xmake_shared<T> still expects to destroy a live T at
                            // this address whenever the entry is later replaced or the
                            // topic is reset/torn down. Reconstruct a valid object in
                            // place from a fresh copy of `data` (T is already required
                            // to be copy-constructible for use with DataBus) so that
                            // invariant holds again, then propagate the original
                            // exception to the caller.
                            try {
                                ::new (static_cast<void*>(cachedObj)) T(data);
                            }
                            catch (...) {
                                // Recovery copy also failed: the slot cannot be safely
                                // restored to a valid state. This is an unrecoverable
                                // invariant violation, not a normal operational error.
                                ASSERT();
                            }
                            throw;
                        }
#endif
                        itLvc->second.timestamp = now;
                    } else {
                        // First publish for this topic: perform the initial allocation
                        m_lastValues[topic] = LvcEntry{ dmq::xmake_shared<T>(data), now };
                    }
                }

                // 3. Prepare monitor data
                if (!m_monitorSignal.Empty()) {
                    hasMonitor = true;
                    auto itStr = m_stringifiers.find(topic);
                    if (itStr != m_stringifiers.end()) {
                        auto func = static_cast<dmq::UnicastDelegate<dmq::xstring(const T&)>*>(itStr->second.get());
                        strVal = (*func)(data);
                    }
                }

                // 4. Get signal and remote info. Only create Signal if there is local interest.
                auto itSig = m_signals.find(topic);
                if (itSig != m_signals.end()) {
                    signal = std::static_pointer_cast<dmq::Signal<void(const T&)>>(itSig->second);
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
        }

        if (typeMismatch) {
            InternalReportLatchedError(topic, dmq::DelegateError::ERR_TYPE_MISMATCH);
            ASSERT();
            return;
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
            bool anyInterested = false;
            for (size_t i = 0; i < participantSnapshotCount; ++i) {
                if (participantsSnapshot[i]->DispatchIfInterested(topic, data, serializer)) {
                    anyInterested = true;
                }
            }

            if (anyInterested) {
                if (!serializer) {
                    // Topic has remote interest but no serializer — fire once per topic.
                    InternalReportLatchedError(topic, dmq::DelegateError::ERR_NO_SERIALIZER);
                }
                handled = true;
            }
        }

        // 9. Notify if no one received the message
        if (!handled) {
            m_unhandledSignal(topic);
        }
    }

    void InternalRemoveParticipant(std::shared_ptr<Participant> participant) {
        dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        for (size_t i = 0; i < m_participantCount; ++i) {
            if (m_participants[i] == participant) {
                m_participantErrorConnections[i].Disconnect();
                for (size_t j = i; j < m_participantCount - 1; ++j) {
                    m_participants[j] = std::move(m_participants[j + 1]);
                    m_participantErrorConnections[j] = std::move(m_participantErrorConnections[j + 1]);
                }
                --m_participantCount;
                m_participants[m_participantCount].reset();
                break;
            }
        }
    }

    void InternalAddParticipant(std::shared_ptr<Participant> participant) {
        // Establish connection OUTSIDE the global DataBus lock to prevent
        // lock inversion deadlocks. Signal::Connect() is already thread-safe.
        auto conn = participant->SubscribeError(
            dmq::MakeDelegate(this, &DataBus::InternalReportLatchedError));

        dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
        ASSERT_TRUE(m_participantCount < dmq::MAX_PARTICIPANTS);
        m_participantErrorConnections[m_participantCount] = std::move(conn);
        participant->EnableContinuousErrors(m_continuousErrors);
        m_participants[m_participantCount++] = participant;
    }

    template <typename T>
    void InternalRegisterSerializer(const dmq::xstring& topic, std::shared_ptr<void> serializer) {
        bool typeMismatch = false;
        {
            dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);

            auto itType = m_typeIndices.find(topic);
            if (itType != m_typeIndices.end()) {
                if (itType->second != std::type_index(typeid(T))) typeMismatch = true;
            } else {
                m_typeIndices.emplace(topic, std::type_index(typeid(T)));
            }

            if (!typeMismatch) {
                m_serializers[topic] = std::move(serializer);
            }
        }
        if (typeMismatch) {
            InternalReportLatchedError(topic, dmq::DelegateError::ERR_TYPE_MISMATCH);
            ASSERT();
        }
    }

    template <typename T>
    void InternalRegisterStringifier(const dmq::xstring& topic, dmq::UnicastDelegate<dmq::xstring(const T&)> func) {
        bool typeMismatch = false;
        {
            dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);

            // Runtime Type Safety: Ensure topic is not registered with multiple types
            auto itType = m_typeIndices.find(topic);
            if (itType != m_typeIndices.end()) {
                if (itType->second != std::type_index(typeid(T))) typeMismatch = true;
            } else {
                m_typeIndices.emplace(topic, std::type_index(typeid(T)));
            }

            if (!typeMismatch) {
                // Use shared_ptr with custom deleter and stl_allocator.
                // Allocate function object from fixed-block pool using xnew.
                using DelegateType = dmq::UnicastDelegate<dmq::xstring(const T&)>;
                m_stringifiers[topic] = std::shared_ptr<void>(
                    dmq::xnew<DelegateType>(std::move(func)),
                    [](void* ptr) { dmq::xdelete(static_cast<DelegateType*>(ptr)); },
                    ::dmq::stl_allocator<void>()
                );
            }
        }
        if (typeMismatch) {
            InternalReportLatchedError(topic, dmq::DelegateError::ERR_TYPE_MISMATCH);
            ASSERT();
        }
    }

    void InternalReset() {
        dmq::LockGuard<dmq::RecursiveMutex> lock(m_mutex);
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
        m_unhandledSignal.Clear();
        m_errorSignal.Clear();
        m_reportedErrors.clear();
    }

    template <typename T>
    SignalPtr<T> GetOrCreateSignal(const dmq::xstring& topic) {
        // Assume lock is held by caller
        auto itType = m_typeIndices.find(topic);
        if (itType != m_typeIndices.end()) {
            // Runtime Type Safety: Catch same topic string used with different types
            if (itType->second != std::type_index(typeid(T))) return nullptr;
        } else {
            m_typeIndices.emplace(topic, std::type_index(typeid(T)));
        }

        auto it = m_signals.find(topic);
        if (it != m_signals.end()) {
            return std::static_pointer_cast<dmq::Signal<void(const T&)>>(it->second);
        }

        auto signal = dmq::xmake_shared<dmq::Signal<void(const T&)>>();
        m_signals[topic] = std::static_pointer_cast<void>(signal);
        return signal;
    }

    struct LvcEntry {
        std::shared_ptr<void> value;
        dmq::TimePoint timestamp;
    };

    bool m_continuousErrors = false;
    dmq::RecursiveMutex m_mutex;
    dmq::xmap<dmq::xstring, uint16_t> m_reportedErrors;
    dmq::xmap<dmq::xstring, std::shared_ptr<void>> m_signals;
    dmq::xmap<dmq::xstring, std::type_index> m_typeIndices;
    std::array<std::shared_ptr<Participant>, dmq::MAX_PARTICIPANTS> m_participants{};
    size_t m_participantCount = 0;
    dmq::xmap<dmq::xstring, std::shared_ptr<void>> m_serializers;
    dmq::xmap<dmq::xstring, LvcEntry> m_lastValues;
    dmq::xmap<dmq::xstring, QoS> m_topicQos;
    dmq::xmap<dmq::xstring, std::shared_ptr<void>> m_stringifiers;
    dmq::Signal<void(const SpyPacket&)> m_monitorSignal;
    dmq::Signal<void(const dmq::xstring& topic)> m_unhandledSignal;
    dmq::Signal<void(const dmq::xstring& topic, dmq::DelegateError error)> m_errorSignal;
    std::array<dmq::ScopedConnection, dmq::MAX_PARTICIPANTS> m_participantErrorConnections;
};

} // namespace dmq::databus


#endif // DMQ_DATABUS_H
