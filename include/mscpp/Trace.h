#pragma once

#include "LogicalTime.h"
#include <string>
#include <vector>
#include <variant>
#include <memory>
#include <fstream>
#include <sstream>

// Optional JSON support - only if nlohmann_json is available
#ifdef __has_include
#  if __has_include(<nlohmann/json.hpp>)
#    include <nlohmann/json.hpp>
#    define MSCPP_HAS_JSON 1
#  else
#    define MSCPP_HAS_JSON 0
#  endif
#else
#  define MSCPP_HAS_JSON 0
#endif

namespace services {

// Forward declarations
class ReactorScheduler;

/**
 * @brief Event types that can be traced
 */
enum class EventType {
    HEARTBEAT,
    INPUT,
    REACTION,
    STATE_TRANSITION,
    PORT_SET,
    PORT_CLEAR
};

/**
 * @brief String representation of event types
 */
inline std::string eventTypeToString(EventType type) {
    switch (type) {
        case EventType::HEARTBEAT: return "HEARTBEAT";
        case EventType::INPUT: return "INPUT";
        case EventType::REACTION: return "REACTION";
        case EventType::STATE_TRANSITION: return "STATE_TRANSITION";
        case EventType::PORT_SET: return "PORT_SET";
        case EventType::PORT_CLEAR: return "PORT_CLEAR";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Parse event type from string
 */
inline EventType stringToEventType(const std::string& str) {
    if (str == "HEARTBEAT") return EventType::HEARTBEAT;
    if (str == "INPUT") return EventType::INPUT;
    if (str == "REACTION") return EventType::REACTION;
    if (str == "STATE_TRANSITION") return EventType::STATE_TRANSITION;
    if (str == "PORT_SET") return EventType::PORT_SET;
    if (str == "PORT_CLEAR") return EventType::PORT_CLEAR;
    throw std::runtime_error("Unknown event type: " + str);
}

/**
 * @brief A traced event in the reactor system
 */
struct TraceEvent {
    LogicalTag tag;                  // Logical timestamp
    EventType type;                   // Type of event
    size_t reactor_id;                // Which reactor
    std::string reactor_name;         // Human-readable reactor name
    std::string event_name;           // Specific event name (e.g., "IncrementInput")
    std::string details;              // Additional event-specific information

    TraceEvent() = default;

    TraceEvent(const LogicalTag& t, EventType et, size_t rid,
               const std::string& rname, const std::string& ename,
               const std::string& det = "")
        : tag(t), type(et), reactor_id(rid),
          reactor_name(rname), event_name(ename), details(det) {}

    /**
     * @brief Compare events for determinism verification
     * Events are equal if they have the same tag, type, reactor, and event name
     */
    bool operator==(const TraceEvent& other) const {
        return tag == other.tag &&
               type == other.type &&
               reactor_id == other.reactor_id &&
               event_name == other.event_name;
    }

    bool operator!=(const TraceEvent& other) const {
        return !(*this == other);
    }

    /**
     * @brief String representation for debugging
     */
    std::string toString() const {
        std::ostringstream oss;
        oss << "[" << tag << "] "
            << eventTypeToString(type) << " "
            << reactor_name << "::" << event_name;
        if (!details.empty()) {
            oss << " (" << details << ")";
        }
        return oss.str();
    }

#if MSCPP_HAS_JSON
    /**
     * @brief Convert to JSON for serialization
     */
    nlohmann::json toJson() const {
        return nlohmann::json{
            {"time_ns", tag.time.count()},
            {"microstep", tag.microstep},
            {"type", eventTypeToString(type)},
            {"reactor_id", reactor_id},
            {"reactor_name", reactor_name},
            {"event_name", event_name},
            {"details", details}
        };
    }

    /**
     * @brief Create from JSON
     */
    static TraceEvent fromJson(const nlohmann::json& j) {
        TraceEvent event;
        event.tag.time = LogicalTime(j["time_ns"].get<int64_t>());
        event.tag.microstep = j["microstep"].get<uint32_t>();
        event.type = stringToEventType(j["type"].get<std::string>());
        event.reactor_id = j["reactor_id"].get<size_t>();
        event.reactor_name = j["reactor_name"].get<std::string>();
        event.event_name = j["event_name"].get<std::string>();
        event.details = j["details"].get<std::string>();
        return event;
    }
#endif
};

/**
 * @brief A complete execution trace
 */
class ExecutionTrace {
public:
    ExecutionTrace() = default;

    /**
     * @brief Add an event to the trace
     */
    void addEvent(const TraceEvent& event) {
        events_.push_back(event);
    }

    /**
     * @brief Get all events
     */
    const std::vector<TraceEvent>& getEvents() const {
        return events_;
    }

    /**
     * @brief Get number of events
     */
    size_t size() const {
        return events_.size();
    }

    /**
     * @brief Clear all events
     */
    void clear() {
        events_.clear();
    }

    /**
     * @brief Compare two traces for equality (determinism verification)
     */
    bool operator==(const ExecutionTrace& other) const {
        if (events_.size() != other.events_.size()) {
            return false;
        }
        for (size_t i = 0; i < events_.size(); ++i) {
            if (events_[i] != other.events_[i]) {
                return false;
            }
        }
        return true;
    }

    bool operator!=(const ExecutionTrace& other) const {
        return !(*this == other);
    }

    /**
     * @brief Find first difference between traces (for debugging)
     */
    std::string findFirstDifference(const ExecutionTrace& other) const {
        if (events_.size() != other.events_.size()) {
            std::ostringstream oss;
            oss << "Trace sizes differ: " << events_.size()
                << " vs " << other.events_.size();
            return oss.str();
        }

        for (size_t i = 0; i < events_.size(); ++i) {
            if (events_[i] != other.events_[i]) {
                std::ostringstream oss;
                oss << "Event " << i << " differs:\n"
                    << "  Expected: " << events_[i].toString() << "\n"
                    << "  Got:      " << other.events_[i].toString();
                return oss.str();
            }
        }

        return "Traces are identical";
    }

    /**
     * @brief String representation for debugging
     */
    std::string toString() const {
        std::ostringstream oss;
        oss << "ExecutionTrace with " << events_.size() << " events:\n";
        for (const auto& event : events_) {
            oss << "  " << event.toString() << "\n";
        }
        return oss.str();
    }

#if MSCPP_HAS_JSON
    /**
     * @brief Save trace to JSON file
     */
    bool saveToFile(const std::string& filename) const {
        try {
            nlohmann::json j = nlohmann::json::array();
            for (const auto& event : events_) {
                j.push_back(event.toJson());
            }

            std::ofstream file(filename);
            if (!file.is_open()) {
                return false;
            }

            file << j.dump(2);  // Pretty print with 2-space indent
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    /**
     * @brief Load trace from JSON file
     */
    bool loadFromFile(const std::string& filename) {
        try {
            std::ifstream file(filename);
            if (!file.is_open()) {
                return false;
            }

            nlohmann::json j;
            file >> j;

            events_.clear();
            for (const auto& jevent : j) {
                events_.push_back(TraceEvent::fromJson(jevent));
            }

            return true;
        } catch (const std::exception&) {
            return false;
        }
    }
#else
    /**
     * @brief Save trace to JSON file (JSON support not available)
     */
    bool saveToFile(const std::string& /*filename*/) const {
        return false;  // JSON support not compiled in
    }

    /**
     * @brief Load trace from JSON file (JSON support not available)
     */
    bool loadFromFile(const std::string& /*filename*/) {
        return false;  // JSON support not compiled in
    }
#endif

private:
    std::vector<TraceEvent> events_;
};

/**
 * @brief Event tracer for recording reactor execution
 */
class EventTracer {
public:
    EventTracer() : enabled_(true) {}

    /**
     * @brief Enable/disable tracing
     */
    void setEnabled(bool enabled) {
        enabled_ = enabled;
    }

    bool isEnabled() const {
        return enabled_;
    }

    /**
     * @brief Record an event
     */
    void recordEvent(const TraceEvent& event) {
        if (enabled_) {
            trace_.addEvent(event);
        }
    }

    /**
     * @brief Convenience method to record event
     */
    void recordEvent(const LogicalTag& tag, EventType type, size_t reactor_id,
                     const std::string& reactor_name, const std::string& event_name,
                     const std::string& details = "") {
        if (enabled_) {
            trace_.addEvent(TraceEvent(tag, type, reactor_id, reactor_name, event_name, details));
        }
    }

    /**
     * @brief Get the current trace
     */
    const ExecutionTrace& getTrace() const {
        return trace_;
    }

    /**
     * @brief Get mutable trace
     */
    ExecutionTrace& getTrace() {
        return trace_;
    }

    /**
     * @brief Clear the trace
     */
    void clear() {
        trace_.clear();
    }

    /**
     * @brief Save trace to file
     */
    bool saveTrace(const std::string& filename) const {
        return trace_.saveToFile(filename);
    }

    /**
     * @brief Load trace from file
     */
    bool loadTrace(const std::string& filename) {
        return trace_.loadFromFile(filename);
    }

private:
    bool enabled_;
    ExecutionTrace trace_;
};

/**
 * @brief Trace replay mechanism
 *
 * This class allows replaying a recorded trace to verify deterministic execution.
 * It can inject events at the recorded logical times and verify they occur as expected.
 */
class TraceReplayer {
public:
    TraceReplayer() : current_index_(0) {}

    /**
     * @brief Set the trace to replay
     */
    void setTrace(const ExecutionTrace& trace) {
        trace_ = trace;
        current_index_ = 0;
    }

    /**
     * @brief Get the next event to replay
     */
    bool getNextEvent(TraceEvent& event) {
        if (current_index_ >= trace_.size()) {
            return false;
        }
        event = trace_.getEvents()[current_index_++];
        return true;
    }

    /**
     * @brief Check if there are more events to replay
     */
    bool hasMoreEvents() const {
        return current_index_ < trace_.size();
    }

    /**
     * @brief Reset replay to beginning
     */
    void reset() {
        current_index_ = 0;
    }

    /**
     * @brief Get current replay position
     */
    size_t getCurrentIndex() const {
        return current_index_;
    }

    /**
     * @brief Get total number of events
     */
    size_t getTotalEvents() const {
        return trace_.size();
    }

private:
    ExecutionTrace trace_;
    size_t current_index_;
};

} // namespace services
