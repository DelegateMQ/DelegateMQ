// JsonTopics.cpp
// @see https://github.com/DelegateMQ/DelegateMQ
// Shared exposure layer for JSON bridges.

#include "JsonTopics.h"
#include <algorithm>
#include <chrono>
#include <iostream>

namespace {
    constexpr size_t QUEUE_SIZE = 500;
    /// Minimum time between "queue full" reports, so sustained backpressure
    /// doesn't flood OnEvent().
    constexpr int64_t DROP_REPORT_INTERVAL_MS = 5000;

    int64_t SteadyMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
}

dmq::Signal<void(const std::string&)>& JsonTopics::OnEvent() {
    return GetInstance().onEvent;
}

void JsonTopics::RaiseEvent(const std::string& text) {
    auto& inst = GetInstance();
    if (inst.onEvent.Empty())
        std::cerr << "[JsonTopics] " << text << std::endl;
    else
        inst.onEvent(text);
}

void JsonTopics::EnsureThread() {
    auto& inst = GetInstance();
    std::lock_guard<std::mutex> lock(inst.mutex);
    if (inst.thread) return;
    // DROP: bridges must never stall DataBus publishers.
    inst.thread = std::make_unique<dmq::os::Thread>("JsonTopics", QUEUE_SIZE, dmq::os::FullPolicy::DROP);
    inst.thread->SetDroppedHandler(dmq::MakeDelegate(&JsonTopics::OnQueueFull));
    inst.thread->CreateThread();
}

void JsonTopics::Deliver(const std::string& topic, const std::string& json, const Options& options) {
    auto& inst = GetInstance();
    std::vector<ISink*> sinks;
    {
        std::lock_guard<std::mutex> lock(inst.mutex);
        if (options.latched)
            inst.latched[topic] = json;
        sinks = inst.sinks;
    }
    for (auto* sink : sinks)
        sink->OnMessage(topic, json, options);
}

void JsonTopics::AddSink(ISink* sink) {
    auto& inst = GetInstance();
    std::lock_guard<std::mutex> lock(inst.mutex);
    if (std::find(inst.sinks.begin(), inst.sinks.end(), sink) == inst.sinks.end())
        inst.sinks.push_back(sink);
}

void JsonTopics::RemoveSink(ISink* sink) {
    auto& inst = GetInstance();
    {
        std::lock_guard<std::mutex> lock(inst.mutex);
        inst.sinks.erase(std::remove(inst.sinks.begin(), inst.sinks.end(), sink), inst.sinks.end());
    }
    // A delivery already in progress may hold a copy of the old sink list:
    // wait (bounded) for the JsonTopics thread to finish its current message.
    // Not WAIT_INFINITE: the thread's DROP policy could discard this marker.
    if (inst.thread)
        (void)dmq::MakeDelegate(std::function<void()>([] {}), *inst.thread, std::chrono::seconds(1)).AsyncInvoke();
}

std::vector<std::pair<std::string, JsonTopics::Options>> JsonTopics::ExposedTopics() {
    auto& inst = GetInstance();
    std::lock_guard<std::mutex> lock(inst.mutex);
    return { inst.exposed.begin(), inst.exposed.end() };
}

std::vector<std::string> JsonTopics::AcceptedTopics() {
    auto& inst = GetInstance();
    std::lock_guard<std::mutex> lock(inst.mutex);
    std::vector<std::string> topics;
    for (const auto& [topic, handler] : inst.accepted)
        topics.push_back(topic);
    return topics;
}

std::vector<std::pair<std::string, std::string>> JsonTopics::LatchedValues() {
    auto& inst = GetInstance();
    std::lock_guard<std::mutex> lock(inst.mutex);
    return { inst.latched.begin(), inst.latched.end() };
}

bool JsonTopics::Inbound(const std::string& topic, const std::string& payload, const char* source) {
    auto& inst = GetInstance();
    InboundHandler handler;
    {
        std::lock_guard<std::mutex> lock(inst.mutex);
        auto it = inst.accepted.find(topic);
        if (it == inst.accepted.end()) return false;
        handler = it->second;
    }
    if (handler(payload))
        return true;
    RaiseEvent(std::string(source) + ": rejected command on " + topic + ": " + payload);
    return false;
}

void JsonTopics::OnQueueFull(size_t depth) {
    // Runs synchronously on the publishing thread: count, and report at most
    // once per interval.
    auto& inst = GetInstance();
    uint32_t count = ++inst.queueDrops;
    int64_t now = SteadyMs();
    int64_t last = inst.lastDropReportMs;
    if (now - last < DROP_REPORT_INTERVAL_MS || !inst.lastDropReportMs.compare_exchange_strong(last, now))
        return;
    inst.queueDrops -= count;
    RaiseEvent("queue full (" + std::to_string(depth) + " queued): dropped " + std::to_string(count)
               + " message(s) before JSON conversion");
}

void JsonTopics::Shutdown() {
    auto& inst = GetInstance();
    std::unique_ptr<dmq::os::Thread> thread;
    {
        std::lock_guard<std::mutex> lock(inst.mutex);
        inst.connections.clear();
        thread = std::move(inst.thread);
    }
    if (thread)
        thread->ExitThread();
    std::lock_guard<std::mutex> lock(inst.mutex);
    inst.exposed.clear();
    inst.accepted.clear();
    inst.latched.clear();
    inst.sinks.clear();
}
