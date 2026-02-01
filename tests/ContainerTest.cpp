#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#include <catch2/catch.hpp>
#pragma GCC diagnostic pop

#include <string>
#include <memory>

#include "mscpp/MicroServiceContainer.h"
#include "mscpp/ReactorWithPorts.h"
#include "mscpp/Reaction.h"
#include "mscpp/Ports.h"
#include "mscpp/StateSet.h"
#include "example-services/Inputs.h"

using namespace services;

// ===========================================================================
// Shared collaborators that live in the container
// ===========================================================================

/** A configuration object a reactor might need at construction time. */
struct AppConfig
{
    int    tick_limit{10};
    double scale_factor{2.5};
    std::string label{"default"};
};

/** A shared logger stand-in — tracks every message written to it. */
struct SharedLogger
{
    std::vector<std::string> messages;

    void log(const std::string& msg)
    {
        messages.push_back(msg);
    }
};

// The container type used by the reactors below: two collaborators.
using AppContainer = MicroServiceContainer<AppConfig, SharedLogger>;

// ===========================================================================
// 1. Reaction-based reactor that reads both container members
// ===========================================================================

DECLARE_REACTOR_NAME(Accumulator);

struct StoreAccumulator
{
    int    total{0};
    int    ticks{0};
    bool   capped{false};
};

struct PortsAccumulator
{
    OutputPort<int> total_out;
};

class Accumulator;   // forward decl for Reaction

/**
 * On each heartbeat: increment total by (scale_factor rounded to int),
 * log via SharedLogger, and cap when tick_limit is reached.
 */
struct AccumulateReaction : public Reaction<
    Accumulator,
    0,
    TypeList<HeartbeatInput>,
    TypeList<>,
    TypeList<>
>
{
    void execute(StoreAccumulator& store,
                 PortsAccumulator& ports,
                 const AppContainer& container,
                 HeartbeatInput& /*input*/)
    {
        const auto& config = *container.get<AppConfig>();
        auto&       logger = *container.get<SharedLogger>();

        store.ticks++;
        store.total += static_cast<int>(config.scale_factor);

        logger.log("tick " + std::to_string(store.ticks) +
                   " total=" + std::to_string(store.total));

        if (store.ticks >= config.tick_limit)
        {
            store.capped = true;
            logger.log("capped at tick_limit=" + std::to_string(config.tick_limit));
        }

        ports.total_out.set(store.total);
    }
};

using ReactionsAccumulator = ReactionSet<AccumulateReaction>;

class Accumulator : public ReactorWithPorts<NameAccumulator, StoreAccumulator,
                                            PortsAccumulator, AppContainer,
                                            ReactionsAccumulator>
{
public:
    using Base = ReactorWithPorts<NameAccumulator, StoreAccumulator,
                                  PortsAccumulator, AppContainer,
                                  ReactionsAccumulator>;
    using Base::Base;

    void doHeartbeat(const LogicalTag& /*tag*/) override
    {
        HeartbeatInput input;
        AccumulateReaction reaction;
        reaction.execute(mStore, mPorts, mContainer, input);
    }
};

// ===========================================================================
// 2. FSM reactor that reads the container in state step() functions
// ===========================================================================

DECLARE_REACTOR_NAME(GatedCounter);

struct StoreGated
{
    int   count{0};
    bool  gate_open{false};
};

struct PortsGated
{
    InputPort<bool>  gate_cmd;    // true = open, false = close
    OutputPort<int>  count_out;
};

struct GatedIdleState : public State<GatedIdleState, 0>
{
    // Heartbeat while gate is closed: just log.
    size_t step(StoreGated& store, PortsGated& ports,
                const AppContainer& container,
                HeartbeatInput& /*input*/)
    {
        auto& logger = *container.get<SharedLogger>();

        // Latch a gate command if one arrived
        if (ports.gate_cmd.is_present() && ports.gate_cmd.get())
        {
            store.gate_open = true;
            logger.log("gate opened");
            return 1;   // → Running
        }

        logger.log("idle");
        return index();
    }
};

struct GatedRunningState : public State<GatedRunningState, 1>
{
    // Heartbeat while gate is open: increment, output, log.
    size_t step(StoreGated& store, PortsGated& ports,
                const AppContainer& container,
                HeartbeatInput& /*input*/)
    {
        const auto& config = *container.get<AppConfig>();
        auto&       logger = *container.get<SharedLogger>();

        store.count++;
        ports.count_out.set(store.count);
        logger.log("count=" + std::to_string(store.count));

        // Close command shuts the gate
        if (ports.gate_cmd.is_present() && !ports.gate_cmd.get())
        {
            store.gate_open = false;
            logger.log("gate closed");
            return 0;   // → Idle
        }

        // Config-driven auto-close after tick_limit counts
        if (store.count >= config.tick_limit)
        {
            store.gate_open = false;
            logger.log("auto-closed at tick_limit=" + std::to_string(config.tick_limit));
            return 0;   // → Idle
        }

        return index();
    }
};

using StatesGated = StateSet<GatedIdleState, GatedRunningState>;

class GatedCounter : public ReactorWithPortsAndFSM<NameGatedCounter, StoreGated,
                                                     PortsGated, AppContainer,
                                                     StatesGated>
{
public:
    using Base = ReactorWithPortsAndFSM<NameGatedCounter, StoreGated,
                                        PortsGated, AppContainer,
                                        StatesGated>;
    using Base::Base;

    void doHeartbeat(const LogicalTag& /*tag*/) override
    {
        HeartbeatInput input;
        executeInput(input);
    }

    void clearPorts() override
    {
        mPorts.gate_cmd.clear();
    }
};

// ===========================================================================
// TEST CASES
// ===========================================================================

TEST_CASE("Container: compile-time type checks", "[container][types]")
{
    REQUIRE(AppContainer::Contains<AppConfig>::value  == true);
    REQUIRE(AppContainer::Contains<SharedLogger>::value == true);
    REQUIRE(AppContainer::Contains<int>::value         == false);

    REQUIRE(AppContainer::ContainsAllOf<MicroServiceContainer<AppConfig>>::value == true);
    REQUIRE(AppContainer::ContainsAllOf<MicroServiceContainer<SharedLogger>>::value == true);
    REQUIRE(AppContainer::ContainsAllOf<MicroServiceContainer<AppConfig, SharedLogger>>::value == true);
}

TEST_CASE("Container: construction and retrieval", "[container][basics]")
{
    auto cfg = std::make_shared<AppConfig>(AppConfig{5, 3.0, "test"});
    auto log = std::make_shared<SharedLogger>();

    AppContainer container(cfg, log);

    REQUIRE(container.size() == 2);
    REQUIRE(container.get<AppConfig>()->tick_limit    == 5);
    REQUIRE(container.get<AppConfig>()->scale_factor == 3.0);
    REQUIRE(container.get<AppConfig>()->label        == "test");
    REQUIRE(container.get<SharedLogger>()->messages.empty());
}

TEST_CASE("Container: shared state is visible across get() calls", "[container][shared]")
{
    auto cfg = std::make_shared<AppConfig>();
    auto log = std::make_shared<SharedLogger>();

    AppContainer container(cfg, log);

    // Mutate through one get(), observe through another
    container.get<SharedLogger>()->log("hello");
    container.get<SharedLogger>()->log("world");

    REQUIRE(container.get<SharedLogger>()->messages.size() == 2);
    REQUIRE(container.get<SharedLogger>()->messages[0]     == "hello");
    REQUIRE(container.get<SharedLogger>()->messages[1]     == "world");
}

TEST_CASE("Container: reaction-based reactor reads config and logger", "[container][reaction]")
{
    auto cfg = std::make_shared<AppConfig>(AppConfig{3, 2.5, "reaction-test"});
    auto log = std::make_shared<SharedLogger>();

    AppContainer container(cfg, log);
    Accumulator  acc(container);

    LogicalTag tag{LogicalTime(0), 0};

    // Three heartbeats — scale_factor=2.5 truncates to 2 per tick
    acc.executeHeartbeat(tag);
    acc.executeHeartbeat(tag);
    acc.executeHeartbeat(tag);

    REQUIRE(acc.getStore().ticks == 3);
    REQUIRE(acc.getStore().total == 6);          // 2 * 3
    REQUIRE(acc.getStore().capped == true);      // tick_limit == 3

    // Logger captured all three tick messages plus the cap message
    REQUIRE(log->messages.size() == 4);
    REQUIRE(log->messages[0] == "tick 1 total=2");
    REQUIRE(log->messages[1] == "tick 2 total=4");
    REQUIRE(log->messages[2] == "tick 3 total=6");
    REQUIRE(log->messages[3] == "capped at tick_limit=3");

    // Output port fired on every tick
    REQUIRE(acc.getPorts().total_out.has_pending_value());
    REQUIRE(*acc.getPorts().total_out.get_pending_value() == 6);
}

TEST_CASE("Container: FSM reactor reads config and logger across states", "[container][fsm]")
{
    auto cfg = std::make_shared<AppConfig>(AppConfig{3, 1.0, "fsm-test"});
    auto log = std::make_shared<SharedLogger>();

    AppContainer container(cfg, log);
    GatedCounter gc(container);

    LogicalTag tag{LogicalTime(0), 0};

    // ── Phase 1: idle heartbeats while gate is closed ──────────────────
    gc.executeHeartbeat(tag);
    gc.executeHeartbeat(tag);

    REQUIRE(gc.getCurrentState() == 0);   // still Idle
    REQUIRE(gc.getStore().count   == 0);
    REQUIRE(log->messages.size()  == 2);
    REQUIRE(log->messages[0]      == "idle");
    REQUIRE(log->messages[1]      == "idle");

    // ── Phase 2: open the gate → transition to Running ─────────────────
    gc.getPorts().gate_cmd.set(true);
    gc.executeHeartbeat(tag);
    gc.clearPorts();

    REQUIRE(gc.getCurrentState() == 1);   // Running
    REQUIRE(log->messages.back() == "gate opened");

    // ── Phase 3: three Running heartbeats; auto-close at tick_limit=3 ──
    gc.executeHeartbeat(tag);   // count=1
    gc.executeHeartbeat(tag);   // count=2
    gc.executeHeartbeat(tag);   // count=3 → auto-close

    REQUIRE(gc.getCurrentState() == 0);   // back to Idle
    REQUIRE(gc.getStore().count   == 3);
    REQUIRE(gc.getStore().gate_open == false);
    REQUIRE(log->messages.back() == "auto-closed at tick_limit=3");

    // Output port fired on every Running tick
    REQUIRE(gc.getPorts().count_out.has_pending_value());
    REQUIRE(*gc.getPorts().count_out.get_pending_value() == 3);
}

TEST_CASE("Container: explicit gate-close command mid-run", "[container][fsm][close]")
{
    auto cfg = std::make_shared<AppConfig>(AppConfig{100, 1.0, "close-test"});
    auto log = std::make_shared<SharedLogger>();

    AppContainer container(cfg, log);
    GatedCounter gc(container);

    LogicalTag tag{LogicalTime(0), 0};

    // Open gate
    gc.getPorts().gate_cmd.set(true);
    gc.executeHeartbeat(tag);
    gc.clearPorts();
    REQUIRE(gc.getCurrentState() == 1);

    // Two counts
    gc.executeHeartbeat(tag);   // count=1
    gc.executeHeartbeat(tag);   // count=2

    // Send close command
    gc.getPorts().gate_cmd.set(false);
    gc.executeHeartbeat(tag);   // count=3, then closes
    gc.clearPorts();

    REQUIRE(gc.getCurrentState() == 0);   // Idle
    REQUIRE(gc.getStore().count   == 3);
    REQUIRE(log->messages.back() == "gate closed");
}
