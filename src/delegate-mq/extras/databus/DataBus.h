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
#include <unordered_map>
#include <memory>
#include <mutex>
#include <vector>
#include <functional>
#include <typeindex>
#include <atomic>

namespace dmq::databus {

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
    static dmq::ScopedConnection Subscribe(const std::string& topic, F&& func, dmq::IThread* thread = nullptr, QoS qos = {}) {
        return GetInstance().InternalSubscribe<T>(topic, std::forward<F>(func), thread, qos);
    }

    // Subscribe to a topic with a filter.
    template <typename T, typename F, typename P>
    static dmq::ScopedConnection SubscribeFilter(const std::string& topic, F&& func, P&& predicate, dmq::IThread* thread = nullptr, QoS qos = {}) {
        auto filterFunc = [f = std::forward<F>(func), p = std::forward<P>(predicate)](T data) {
            if (p(data)) {
                f(data);
            }
        };
        return GetInstance().InternalSubscribe<T>(topic, std::move(filterFunc), thread, qos);
    }

    // Publish data to a topic.
    template <typename T>
    static void Publish(const std::string& topic, const T& data) {
        GetInstance().InternalPublish<T>(topic, data, false);
    }

    // Publish data to local subscribers only — does NOT forward to remote participants.
    // Used by AddIncomingTopic to prevent relay loops when a topic is both incoming
    // and outgoing on the same node.
    template <typename T>
    static void PublishLocal(const std::string& topic, const T& data) {
        GetInstance().InternalPublish<T>(topic, data, true);
    }

    // Add a remote participant to the bus.
    static void AddParticipant(std::shared_ptr<Participant> participant) {
        GetInstance().InternalAddParticipant(participant);
    }

    // Register a serializer for a topic (required for remote distribution).
    template <typename T>
    static void RegisterSerializer(const std::string& topic, dmq::ISerializer<void(T)>& serializer) {
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
    static void AddIncomingTopic(const std::string& topic, dmq::DelegateRemoteId remoteId, Participant& participant, dmq::ISerializer<void(T)>& serializer) {
        participant.RegisterHandler<T>(remoteId, serializer, [topic](const T& msg) {
            DataBus::PublishLocal<T>(topic, msg);
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
    static void AddRelayTopic(const std::string& topic, dmq::DelegateRemoteId remoteId, Participant& participant, dmq::ISerializer<void(T)>& serializer) {
        participant.RegisterHandler<T>(remoteId, serializer, [topic](const T& msg) {
            DataBus::Publish<T>(topic, msg);
        });
    }

    // Register a stringifier for a topic to enable spying/logging.
    template <typename T>
    static void RegisterStringifier(const std::string& topic, std::function<std::string(const T&)> func) {
        GetInstance().InternalRegisterStringifier<T>(topic, std::move(func));
    }

    // Enable/Disable Last Value Cache (LVC) for a topic.
    static void LastValueCache(const std::string& topic, bool enabled) {
        GetInstance().InternalLastValueCache(topic, enabled);
    }

    // Subscribe to all bus traffic (topic and stringified value).
    // Monitor all traffic on the DataBus.
    // NOTE: priority is only applied when thread != nullptr; passing a non-default
    // priority without a thread is a programming error and triggers FaultHandler.
    static dmq::ScopedConnection Monitor(std::function<void(const SpyPacket&)> func, dmq::IThread* thread = nullptr, dmq::Priority priority = dmq::Priority::NORMAL) {
        if (!thread && priority != dmq::Priority::NORMAL) {
            ::FaultHandler(__FILE__, (unsigned short)__LINE__);
            return {};
        }
        DataBus& instance = GetInstance();
        std::lock_guard<dmq::RecursiveMutex> lock(instance.m_mutex);
        if (thread) {
            auto del = dmq::MakeDelegate(std::move(func), *thread);
            del.SetPriority(priority);
            return instance.m_monitorSignal.Connect(del);
        }
        return instance.m_monitorSignal.Connect(dmq::MakeDelegate(std::move(func)));
    }

    /// Fired when a message is published but has no local or remote subscribers.
    static dmq::ScopedConnection SubscribeUnhandled(std::function<void(const std::string& topic)> func) {
        return GetInstance().m_unhandledSignal.Connect(dmq::MakeDelegate(std::move(func)));
    }

    // Reset the DataBus (mostly for testing).
    static void ResetForTesting() {
        GetInstance().InternalReset();
    }

private:
    void InternalLastValueCache(const std::string& topic, bool enabled) {
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
    dmq::ScopedConnection InternalSubscribe(const std::string& topic, F&& func, dmq::IThread* thread, QoS qos) {
        SignalPtr<T> signal;

        // Performance Note: Forced std::function construction for internal type management.
        std::function<void(T)> typedFunc = std::forward<F>(func);

        // Wrap with min separation rate limiter if requested. Each subscriber gets its
        // own independent last-delivery timestamp, so different subscribers on the same
        // topic can have different (or no) rate limits without affecting each other.
        if (qos.minSeparation.has_value()) {
            auto minSepRep = static_cast<uint32_t>(qos.minSeparation.value().count());
            auto lastDeliveryRep = std::make_shared<std::atomic<uint32_t>>(0);
            auto inner = std::move(typedFunc);
            typedFunc = [inner = std::move(inner), minSepRep, lastDeliveryRep](T data) {
                auto nowRep = static_cast<uint32_t>(dmq::Clock::now().time_since_epoch().count());
                auto lastRep = lastDeliveryRep->load(std::memory_order_relaxed);
                if (nowRep - lastRep >= minSepRep) {
                    lastDeliveryRep->store(nowRep, std::memory_order_relaxed);
                    inner(data);
                }
            };
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
        // followed by this stale LVC value. Users of LVC should ensure their
        // logic handles potential out-of-order state arrival.
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
    void InternalPublish(const std::string& topic, const T& data, bool localOnly) {
        // Capture timestamp before lock acquisition for maximum accuracy and 
        // monotonic ordering using dmq::Clock.
        auto now = dmq::Clock::now();
        uint64_t timestamp = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();

        SignalPtr<T> signal;
        std::shared_ptr<void> serializerPtr;
        dmq::ISerializer<void(T)>* serializer = nullptr;
        std::vector<std::shared_ptr<Participant>> participantsSnapshot;
        std::string strVal = "?";
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
                m_lastValues[topic] = LvcEntry{ std::make_shared<T>(data), now };
            }

            // 3. Prepare monitor data
            if (!m_monitorSignal.Empty()) {
                hasMonitor = true;
                auto itStr = m_stringifiers.find(topic);
                if (itStr != m_stringifiers.end()) {
                    auto func = static_cast<std::function<std::string(const T&)>*>(itStr->second.get());
                    strVal = (*func)(data);
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
            participantsSnapshot = m_participants;
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
        if (!localOnly && serializer) {
            for (auto& participant : participantsSnapshot) {
                participant->Send<T>(topic, data, *serializer);
                handled = true;
            }
        }

        // 9. Notify if no one received the message
        if (!handled) {
            m_unhandledSignal(topic);
        }
    }

    void InternalAddParticipant(std::shared_ptr<Participant> participant) {
        std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        m_participants.push_back(participant);
    }

    template <typename T>
    void InternalRegisterSerializer(const std::string& topic, dmq::ISerializer<void(T)>& serializer) {
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
    void InternalRegisterStringifier(const std::string& topic, std::function<std::string(const T&)> func) {
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
            new std::function<std::string(const T&)>(std::move(func)),
            [](void* ptr) { delete static_cast<std::function<std::string(const T&)>*>(ptr); }
        );
    }

    void InternalReset() {
        std::lock_guard<dmq::RecursiveMutex> lock(m_mutex);
        m_signals.clear();
        m_participants.clear();
        m_serializers.clear();
        m_lastValues.clear();
        m_topicQos.clear();
        m_stringifiers.clear(); // shared_ptr correctly frees allocated functions
        m_typeIndices.clear();
        m_monitorSignal.Clear();
    }

    template <typename T>
    SignalPtr<T> GetOrCreateSignal(const std::string& topic) {
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

        auto signal = std::make_shared<dmq::Signal<void(T)>>();
        m_signals[topic] = std::static_pointer_cast<void>(signal);
        return signal;
    }

    struct LvcEntry {
        std::shared_ptr<void> value;
        dmq::TimePoint timestamp;
    };

    dmq::RecursiveMutex m_mutex;
    std::unordered_map<std::string, std::shared_ptr<void>> m_signals;
    std::unordered_map<std::string, std::type_index> m_typeIndices;
    std::vector<std::shared_ptr<Participant>> m_participants;
    std::unordered_map<std::string, std::shared_ptr<void>> m_serializers;
    std::unordered_map<std::string, LvcEntry> m_lastValues;
    std::unordered_map<std::string, QoS> m_topicQos;
    std::unordered_map<std::string, std::shared_ptr<void>> m_stringifiers;
    dmq::Signal<void(const SpyPacket&)> m_monitorSignal;
    dmq::Signal<void(const std::string& topic)> m_unhandledSignal;
};

} // namespace dmq::databus


#endif // DMQ_DATABUS_H
