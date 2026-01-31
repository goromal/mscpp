#include <catch2/catch.hpp>

#undef REACTOR_MODE
#define REACTOR_MODE 1

#include "mscpp/Trace.h"
#include "mscpp/ReactorScheduler.h"
#include "mscpp/ReactorFactory.h"
#include "mscpp/Reaction.h"
#include "mscpp/ReactionGraph.h"
#include "mscpp/ReactorWithPorts.h"  // For DECLARE_REACTOR_NAME
#include "example-services/Inputs.h"
#include "example-services/ServiceA.h"
#include "example-services/ServiceB.h"

#include <random>
#include <thread>
#include <chrono>

using namespace services;

// ============================================================================
// Test Setup: Simple Reactor with Tracing
// ============================================================================

inline constexpr char NameTracedReactor[] = "TracedReactor";

struct TracedStore {
    int counter = 0;
    std::string last_event = "";
};

using TracedContainer = MicroServiceContainer<>;

// Forward declare TracedReactor for reactions
class TracedReactor;

// Reactions for traced reactor
struct TracedHeartbeatReaction {
    void execute(TracedStore& store, TracedContainer& /*container*/, HeartbeatInput& /*input*/) {
        store.counter++;
        store.last_event = "heartbeat";
    }
};

struct TracedIncrementReaction {
    void execute(TracedStore& store, TracedContainer& /*container*/, IncrementInput& input) {
        store.counter += 10;
        store.last_event = "increment";
        input.setResult(BooleanResult{true});
    }
};

using TracedReactions = ReactionSet<TracedHeartbeatReaction, TracedIncrementReaction>;

// Simplified TracedReactor - not using full IReactor interface for tests
class TracedReactor {
public:
    TracedReactor(EventTracer* tracer = nullptr)
        : mStore{}, mTracer(tracer) {}

    size_t getId() const { return mId; }
    std::string getName() const { return "TracedReactor"; }

    void executeHeartbeat(const LogicalTag& tag) {
        if (mTracer) {
            mTracer->recordEvent(tag, EventType::HEARTBEAT, mId, getName(), "HeartbeatInput");
        }

        HeartbeatInput input;
        TracedHeartbeatReaction reaction;
        reaction.execute(mStore, mContainer, input);
    }

    // Custom method to inject inputs
    void sendIncrement(const LogicalTag& tag) {
        if (mTracer) {
            mTracer->recordEvent(tag, EventType::INPUT, mId, getName(), "IncrementInput");
        }

        IncrementInput input;
        TracedIncrementReaction reaction;
        reaction.execute(mStore, mContainer, input);
    }

    int getCounter() const { return mStore.counter; }
    std::string getLastEvent() const { return mStore.last_event; }

private:
    size_t mId{0};
    TracedStore mStore;
    TracedContainer mContainer;
    EventTracer* mTracer{nullptr};
};

// ============================================================================
// Trace: Basic Trace Recording
// ============================================================================

TEST_CASE("Trace: Basic Trace Recording", "[trace][trace]") {
    SECTION("EventTracer records events") {
        EventTracer tracer;

        LogicalTag tag1{LogicalTime(0), 0};
        LogicalTag tag2{LogicalTime(100), 0};
        LogicalTag tag3{LogicalTime(100), 1};

        tracer.recordEvent(tag1, EventType::HEARTBEAT, 0, "ReactorA", "HeartbeatInput");
        tracer.recordEvent(tag2, EventType::INPUT, 0, "ReactorA", "IncrementInput");
        tracer.recordEvent(tag3, EventType::REACTION, 0, "ReactorA", "IncrementReaction");

        const auto& trace = tracer.getTrace();
        REQUIRE(trace.size() == 3);

        const auto& events = trace.getEvents();
        REQUIRE(events[0].tag == tag1);
        REQUIRE(events[0].type == EventType::HEARTBEAT);
        REQUIRE(events[0].event_name == "HeartbeatInput");

        REQUIRE(events[1].tag == tag2);
        REQUIRE(events[1].type == EventType::INPUT);

        REQUIRE(events[2].tag == tag3);
        REQUIRE(events[2].type == EventType::REACTION);
    }

    SECTION("Tracer can be enabled/disabled") {
        EventTracer tracer;

        tracer.setEnabled(true);
        tracer.recordEvent(LogicalTag{LogicalTime(0), 0}, EventType::HEARTBEAT,
                          0, "Reactor", "Event1");
        REQUIRE(tracer.getTrace().size() == 1);

        tracer.setEnabled(false);
        tracer.recordEvent(LogicalTag{LogicalTime(0), 0}, EventType::HEARTBEAT,
                          0, "Reactor", "Event2");
        REQUIRE(tracer.getTrace().size() == 1); // Should not increase

        tracer.setEnabled(true);
        tracer.recordEvent(LogicalTag{LogicalTime(0), 0}, EventType::HEARTBEAT,
                          0, "Reactor", "Event3");
        REQUIRE(tracer.getTrace().size() == 2);
    }

    SECTION("Tracer can be cleared") {
        EventTracer tracer;

        tracer.recordEvent(LogicalTag{LogicalTime(0), 0}, EventType::HEARTBEAT,
                          0, "Reactor", "Event1");
        tracer.recordEvent(LogicalTag{LogicalTime(100), 0}, EventType::INPUT,
                          0, "Reactor", "Event2");

        REQUIRE(tracer.getTrace().size() == 2);

        tracer.clear();
        REQUIRE(tracer.getTrace().size() == 0);
    }
}

// ============================================================================
// Trace: Trace Comparison for Determinism
// ============================================================================

TEST_CASE("Trace: Trace Comparison for Determinism", "[trace][determinism]") {
    SECTION("Identical traces are equal") {
        ExecutionTrace trace1;
        ExecutionTrace trace2;

        LogicalTag tag1{LogicalTime(0), 0};
        LogicalTag tag2{LogicalTime(100), 0};

        trace1.addEvent(TraceEvent(tag1, EventType::HEARTBEAT, 0, "R", "Event1"));
        trace1.addEvent(TraceEvent(tag2, EventType::INPUT, 0, "R", "Event2"));

        trace2.addEvent(TraceEvent(tag1, EventType::HEARTBEAT, 0, "R", "Event1"));
        trace2.addEvent(TraceEvent(tag2, EventType::INPUT, 0, "R", "Event2"));

        REQUIRE(trace1 == trace2);
    }

    SECTION("Different traces are not equal") {
        ExecutionTrace trace1;
        ExecutionTrace trace2;

        LogicalTag tag1{LogicalTime(0), 0};
        LogicalTag tag2{LogicalTime(100), 0};

        trace1.addEvent(TraceEvent(tag1, EventType::HEARTBEAT, 0, "R", "Event1"));
        trace1.addEvent(TraceEvent(tag2, EventType::INPUT, 0, "R", "Event2"));

        trace2.addEvent(TraceEvent(tag1, EventType::HEARTBEAT, 0, "R", "Event1"));
        trace2.addEvent(TraceEvent(tag2, EventType::INPUT, 0, "R", "Event3")); // Different

        REQUIRE(trace1 != trace2);
    }

    SECTION("findFirstDifference identifies mismatch") {
        ExecutionTrace trace1;
        ExecutionTrace trace2;

        LogicalTag tag{LogicalTime(100), 0};

        trace1.addEvent(TraceEvent(tag, EventType::HEARTBEAT, 0, "R", "Event1"));
        trace2.addEvent(TraceEvent(tag, EventType::INPUT, 0, "R", "Event1"));

        std::string diff = trace1.findFirstDifference(trace2);
        REQUIRE(diff.find("Event 0 differs") != std::string::npos);
    }
}

// ============================================================================
// Trace: Multiple Runs Produce Identical Traces
// ============================================================================

TEST_CASE("Trace: Multiple Runs Produce Identical Traces", "[trace][determinism]") {
    auto runExperiment = []() -> ExecutionTrace {
        EventTracer tracer;
        TracedReactor reactor(&tracer);

        // Simulate execution
        LogicalTag tag0{LogicalTime(0), 0};
        LogicalTag tag1{LogicalTime(100'000'000), 0};
        LogicalTag tag2{LogicalTime(200'000'000), 0};

        reactor.executeHeartbeat(tag0);
        reactor.executeHeartbeat(tag1);
        reactor.sendIncrement(LogicalTag{LogicalTime(150'000'000), 0});
        reactor.executeHeartbeat(tag2);

        return tracer.getTrace();
    };

    SECTION("Three consecutive runs produce identical traces") {
        ExecutionTrace run1 = runExperiment();
        ExecutionTrace run2 = runExperiment();
        ExecutionTrace run3 = runExperiment();

        REQUIRE(run1.size() == run2.size());
        REQUIRE(run1.size() == run3.size());
        REQUIRE(run1.size() > 0);

        REQUIRE(run1 == run2);
        REQUIRE(run2 == run3);
        REQUIRE(run1 == run3);
    }
}

// ============================================================================
// Trace: Trace Serialization (Save/Load)
// ============================================================================

TEST_CASE("Trace: Trace Serialization", "[trace][serialization]") {
#if MSCPP_HAS_JSON
    const std::string filename = "/tmp/test_trace.json";

    SECTION("Trace can be saved and loaded") {
        ExecutionTrace original;

        original.addEvent(TraceEvent(
            LogicalTag{LogicalTime(0), 0},
            EventType::HEARTBEAT,
            0, "ReactorA", "Heartbeat", "counter=1"
        ));
        original.addEvent(TraceEvent(
            LogicalTag{LogicalTime(100), 0},
            EventType::INPUT,
            1, "ReactorB", "Increment", "delta=10"
        ));
        original.addEvent(TraceEvent(
            LogicalTag{LogicalTime(100), 1},
            EventType::REACTION,
            0, "ReactorA", "ProcessIncrement"
        ));

        // Save
        REQUIRE(original.saveToFile(filename));

        // Load
        ExecutionTrace loaded;
        REQUIRE(loaded.loadFromFile(filename));

        // Verify
        REQUIRE(loaded.size() == original.size());
        REQUIRE(loaded == original);

        // Clean up
        std::remove(filename.c_str());
    }

    SECTION("TraceEvent JSON round-trip") {
        TraceEvent original(
            LogicalTag{LogicalTime(123'456'789), 42},
            EventType::PORT_SET,
            7, "TestReactor", "output_port", "value=100"
        );

        nlohmann::json j = original.toJson();
        TraceEvent reconstructed = TraceEvent::fromJson(j);

        REQUIRE(reconstructed.tag.time.count() == 123'456'789);
        REQUIRE(reconstructed.tag.microstep == 42);
        REQUIRE(reconstructed.type == EventType::PORT_SET);
        REQUIRE(reconstructed.reactor_id == 7);
        REQUIRE(reconstructed.reactor_name == "TestReactor");
        REQUIRE(reconstructed.event_name == "output_port");
        REQUIRE(reconstructed.details == "value=100");
    }
#else
    SECTION("JSON support not available") {
        ExecutionTrace trace;
        trace.addEvent(TraceEvent(LogicalTag{LogicalTime(0), 0}, EventType::HEARTBEAT,
                                 0, "R", "Event1"));

        // Save/load should return false when JSON not available
        REQUIRE_FALSE(trace.saveToFile("/tmp/test.json"));
        REQUIRE_FALSE(trace.loadFromFile("/tmp/test.json"));

        // But trace still works for in-memory comparisons
        ExecutionTrace trace2;
        trace2.addEvent(TraceEvent(LogicalTag{LogicalTime(0), 0}, EventType::HEARTBEAT,
                                  0, "R", "Event1"));
        REQUIRE(trace == trace2);
    }
#endif
}

// ============================================================================
// Trace: Trace Replay
// ============================================================================

TEST_CASE("Trace: Trace Replay", "[trace][replay]") {
    SECTION("TraceReplayer can iterate through trace") {
        ExecutionTrace trace;
        trace.addEvent(TraceEvent(LogicalTag{LogicalTime(0), 0}, EventType::HEARTBEAT,
                                 0, "R", "Event1"));
        trace.addEvent(TraceEvent(LogicalTag{LogicalTime(100), 0}, EventType::INPUT,
                                 0, "R", "Event2"));
        trace.addEvent(TraceEvent(LogicalTag{LogicalTime(100), 1}, EventType::REACTION,
                                 0, "R", "Event3"));

        TraceReplayer replayer;
        replayer.setTrace(trace);

        REQUIRE(replayer.getTotalEvents() == 3);
        REQUIRE(replayer.hasMoreEvents());

        TraceEvent event;
        REQUIRE(replayer.getNextEvent(event));
        REQUIRE(event.event_name == "Event1");
        REQUIRE(replayer.getCurrentIndex() == 1);

        REQUIRE(replayer.getNextEvent(event));
        REQUIRE(event.event_name == "Event2");

        REQUIRE(replayer.getNextEvent(event));
        REQUIRE(event.event_name == "Event3");

        REQUIRE_FALSE(replayer.hasMoreEvents());
        REQUIRE_FALSE(replayer.getNextEvent(event));
    }

    SECTION("TraceReplayer can be reset") {
        ExecutionTrace trace;
        trace.addEvent(TraceEvent(LogicalTag{LogicalTime(0), 0}, EventType::HEARTBEAT,
                                 0, "R", "Event1"));
        trace.addEvent(TraceEvent(LogicalTag{LogicalTime(100), 0}, EventType::INPUT,
                                 0, "R", "Event2"));

        TraceReplayer replayer;
        replayer.setTrace(trace);

        TraceEvent event;
        replayer.getNextEvent(event);
        replayer.getNextEvent(event);
        REQUIRE_FALSE(replayer.hasMoreEvents());

        replayer.reset();
        REQUIRE(replayer.hasMoreEvents());
        REQUIRE(replayer.getCurrentIndex() == 0);

        REQUIRE(replayer.getNextEvent(event));
        REQUIRE(event.event_name == "Event1");
    }
}

// ============================================================================
// Trace: Deterministic Execution with Different Input Orderings
// ============================================================================

TEST_CASE("Trace: Deterministic Execution with Controlled Inputs", "[trace][determinism]") {
    auto runWithInputSequence = [](const std::vector<int>& increments) -> ExecutionTrace {
        EventTracer tracer;
        TracedReactor reactor(&tracer);

        LogicalTag tag{LogicalTime(0), 0};
        reactor.executeHeartbeat(tag);

        for (size_t idx = 0; idx < increments.size(); ++idx) {
            tag = tag.next_microstep();
            reactor.sendIncrement(tag);
        }

        tag = tag.advance_time(LogicalTime(100'000'000));
        reactor.executeHeartbeat(tag);

        return tracer.getTrace();
    };

    SECTION("Same input sequence produces same trace") {
        std::vector<int> inputs = {1, 2, 3, 4, 5};

        ExecutionTrace run1 = runWithInputSequence(inputs);
        ExecutionTrace run2 = runWithInputSequence(inputs);
        ExecutionTrace run3 = runWithInputSequence(inputs);

        REQUIRE(run1 == run2);
        REQUIRE(run2 == run3);
    }

    SECTION("Different input sequences produce different traces") {
        std::vector<int> inputs1 = {1, 2, 3};
        std::vector<int> inputs2 = {1, 2, 3, 4};

        ExecutionTrace run1 = runWithInputSequence(inputs1);
        ExecutionTrace run2 = runWithInputSequence(inputs2);

        REQUIRE(run1 != run2);
        REQUIRE(run1.size() != run2.size());
    }
}

// ============================================================================
// Trace: Fuzz Testing with Random Inputs
// ============================================================================

TEST_CASE("Trace: Fuzz Testing with Random Inputs", "[trace][fuzz]") {
    auto runWithRandomSeed = [](unsigned int seed) -> ExecutionTrace {
        std::mt19937 rng(seed);
        std::uniform_int_distribution<int> dist(1, 10);

        EventTracer tracer;
        TracedReactor reactor(&tracer);

        // Generate random number of inputs
        int num_inputs = dist(rng);

        LogicalTag tag{LogicalTime(0), 0};
        reactor.executeHeartbeat(tag);

        for (int i = 0; i < num_inputs; ++i) {
            tag = tag.next_microstep();
            reactor.sendIncrement(tag);
        }

        tag = tag.advance_time(LogicalTime(100'000'000));
        reactor.executeHeartbeat(tag);

        return tracer.getTrace();
    };

    SECTION("Same random seed produces identical traces") {
        const unsigned int SEED = 42;

        ExecutionTrace run1 = runWithRandomSeed(SEED);
        ExecutionTrace run2 = runWithRandomSeed(SEED);
        ExecutionTrace run3 = runWithRandomSeed(SEED);

        REQUIRE(run1.size() == run2.size());
        REQUIRE(run1 == run2);
        REQUIRE(run2 == run3);
    }

    SECTION("Different random seeds may produce different traces") {
        ExecutionTrace run1 = runWithRandomSeed(42);
        ExecutionTrace run2 = runWithRandomSeed(43);

        // They might be different (but not guaranteed due to RNG)
        // This test mainly ensures the system handles various inputs
        REQUIRE(run1.size() > 0);
        REQUIRE(run2.size() > 0);
    }

    SECTION("Multiple runs with same seed maintain determinism") {
        const int NUM_RUNS = 5;
        std::vector<ExecutionTrace> runs;

        for (int i = 0; i < NUM_RUNS; ++i) {
            runs.push_back(runWithRandomSeed(12345));
        }

        // All runs should be identical
        for (int i = 1; i < NUM_RUNS; ++i) {
            REQUIRE(runs[0] == runs[i]);
        }
    }
}

// ============================================================================
// Trace: Event Type Coverage
// ============================================================================

TEST_CASE("Trace: Event Type Coverage", "[trace][types]") {
    SECTION("All event types can be recorded") {
        EventTracer tracer;
        LogicalTag tag{LogicalTime(0), 0};

        tracer.recordEvent(tag, EventType::HEARTBEAT, 0, "R", "heartbeat");
        tracer.recordEvent(tag, EventType::INPUT, 0, "R", "input");
        tracer.recordEvent(tag, EventType::REACTION, 0, "R", "reaction");
        tracer.recordEvent(tag, EventType::STATE_TRANSITION, 0, "R", "transition");
        tracer.recordEvent(tag, EventType::PORT_SET, 0, "R", "port_set");
        tracer.recordEvent(tag, EventType::PORT_CLEAR, 0, "R", "port_clear");

        const auto& events = tracer.getTrace().getEvents();
        REQUIRE(events.size() == 6);

        REQUIRE(events[0].type == EventType::HEARTBEAT);
        REQUIRE(events[1].type == EventType::INPUT);
        REQUIRE(events[2].type == EventType::REACTION);
        REQUIRE(events[3].type == EventType::STATE_TRANSITION);
        REQUIRE(events[4].type == EventType::PORT_SET);
        REQUIRE(events[5].type == EventType::PORT_CLEAR);
    }

    SECTION("Event type string conversion") {
        REQUIRE(eventTypeToString(EventType::HEARTBEAT) == "HEARTBEAT");
        REQUIRE(eventTypeToString(EventType::INPUT) == "INPUT");
        REQUIRE(eventTypeToString(EventType::REACTION) == "REACTION");
        REQUIRE(eventTypeToString(EventType::STATE_TRANSITION) == "STATE_TRANSITION");
        REQUIRE(eventTypeToString(EventType::PORT_SET) == "PORT_SET");
        REQUIRE(eventTypeToString(EventType::PORT_CLEAR) == "PORT_CLEAR");

        REQUIRE(stringToEventType("HEARTBEAT") == EventType::HEARTBEAT);
        REQUIRE(stringToEventType("INPUT") == EventType::INPUT);
        REQUIRE(stringToEventType("REACTION") == EventType::REACTION);
        REQUIRE(stringToEventType("STATE_TRANSITION") == EventType::STATE_TRANSITION);
        REQUIRE(stringToEventType("PORT_SET") == EventType::PORT_SET);
        REQUIRE(stringToEventType("PORT_CLEAR") == EventType::PORT_CLEAR);
    }
}

// ============================================================================
// Trace: Trace toString for Debugging
// ============================================================================

TEST_CASE("Trace: Trace String Representation", "[trace][debug]") {
    SECTION("TraceEvent toString") {
        TraceEvent event(
            LogicalTag{LogicalTime(100'000'000), 5},
            EventType::HEARTBEAT,
            0, "TestReactor", "HeartbeatInput", "counter=10"
        );

        std::string str = event.toString();
        REQUIRE(str.find("100000000ns") != std::string::npos);
        REQUIRE(str.find("µ5") != std::string::npos);
        REQUIRE(str.find("HEARTBEAT") != std::string::npos);
        REQUIRE(str.find("TestReactor") != std::string::npos);
        REQUIRE(str.find("HeartbeatInput") != std::string::npos);
        REQUIRE(str.find("counter=10") != std::string::npos);
    }

    SECTION("ExecutionTrace toString") {
        ExecutionTrace trace;
        trace.addEvent(TraceEvent(LogicalTag{LogicalTime(0), 0}, EventType::HEARTBEAT,
                                 0, "R", "Event1"));
        trace.addEvent(TraceEvent(LogicalTag{LogicalTime(100), 0}, EventType::INPUT,
                                 0, "R", "Event2"));

        std::string str = trace.toString();
        REQUIRE(str.find("ExecutionTrace with 2 events") != std::string::npos);
        REQUIRE(str.find("Event1") != std::string::npos);
        REQUIRE(str.find("Event2") != std::string::npos);
    }
}
