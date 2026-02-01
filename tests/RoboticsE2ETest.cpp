/**
 * End-to-End Robotics Test Suite
 *
 * A toy robotics pipeline demonstrating three interdependent reactor services
 * wired together via ports and connections.  Every feature exercised here
 * would appear in a real robotics daemon built on this framework:
 *
 *   IMU  ──(raw_imu)──►  KalmanFilter  ──(filtered_pose)──►  MotionPlanner
 *                              ▲                                     ▲
 *                              │                                     │
 *                         (reset_cmd)                           (waypoint_cmd)
 *
 * Service roles
 * ─────────────
 * 1. IMU (producer)
 *    - Emits a RawIMU reading on every heartbeat.  No input ports.
 *    - Models a sensor that publishes at a fixed rate.
 *
 * 2. KalmanFilter (multi-input transformer)
 *    - Input ports: raw_imu (RawIMU), reset_cmd (ResetCommand)
 *    - Output port: filtered_pose (FilteredPose)
 *    - Three reactions forming a dependency chain:
 *        ReadSensor   (level 0) – latches the raw reading into store
 *        FilterPose   (level 1, depends on ReadSensor) – applies the filter
 *        PublishEstimate (level 2, depends on FilterPose) – writes to output port
 *    - The reset_cmd port is a second distinct input (beyond heartbeat) that
 *      resets internal state — illustrating how a real estimator accepts
 *      calibration or re-initialisation commands.
 *
 * 3. MotionPlanner (FSM consumer)
 *    - Input ports: filtered_pose (FilteredPose), waypoint_cmd (Waypoint)
 *    - Output port: velocity_cmd (VelocityCommand)
 *    - FSM states: Idle → Planning → Executing → Idle
 *      * Idle:      waits for a waypoint; on receipt transitions to Planning
 *      * Planning:  computes a velocity from current pose + waypoint
 *      * Executing: emits the velocity command; transitions back to Idle
 *    - The waypoint_cmd port is a second distinct input that triggers the FSM
 *      transition, separate from the continuous pose stream.
 *
 * What is verified
 * ────────────────
 * • Port presence semantics (is_present / get / clear) across all three
 * • Multi-input reactors: KalmanFilter processes both raw_imu and reset_cmd
 * • Reaction dependency graph: three-level chain inside KalmanFilter
 * • Full pipeline propagation: IMU → KalmanFilter → MotionPlanner
 * • FSM lifecycle in MotionPlanner driven by port-delivered commands
 * • Deterministic tag-based event ordering via the scheduler's event queue
 * • ConnectionManager wiring (output → input across reactors)
 */

#include <catch2/catch.hpp>

#include "mscpp/Ports.h"
#include "mscpp/Topology.h"
#include "mscpp/Reaction.h"
#include "mscpp/ReactionGraph.h"
#include "mscpp/ReactorWithPorts.h"
#include "mscpp/ReactorScheduler.h"
#include "mscpp/StateSet.h"
#include "mscpp/MicroServiceContainer.h"

#include "example-services/Inputs.h"

using namespace services;

// ===========================================================================
// Shared data types
// ===========================================================================

/** Raw 6-DoF measurement from an IMU sensor. */
struct RawIMU
{
    double accel_x{0.0};
    double accel_y{0.0};
    double accel_z{0.0};
    double gyro_x{0.0};
    double gyro_y{0.0};
    double gyro_z{0.0};
};

/** Smoothed pose estimate produced by the Kalman filter. */
struct FilteredPose
{
    double x{0.0};   // position
    double y{0.0};
    double yaw{0.0}; // heading
};

/** External command to reset the filter's internal state. */
struct ResetCommand
{
    bool reset{false};
};

/** A goal location the planner should drive toward. */
struct Waypoint
{
    double target_x{0.0};
    double target_y{0.0};
};

/** Velocity command that the planner outputs to an actuator. */
struct VelocityCommand
{
    double vx{0.0};
    double vy{0.0};
};

// ===========================================================================
// 1. IMU  –  pure producer
// ===========================================================================

DECLARE_REACTOR_NAME(IMU);

struct StoreIMU
{
    int tick{0};                 // heartbeat counter
    RawIMU last_reading{};      // most recent synthetic reading
};

struct PortsIMU
{
    OutputPort<RawIMU> raw_imu;  // sensor output
};

class IMU;  // forward decl for Reaction template parameter

/**
 * On every heartbeat the IMU synthesises a reading and publishes it.
 * The synthetic signal is deterministic: position = tick, gyro = tick*0.1.
 */
struct IMUHeartbeatReaction : public Reaction<
    IMU,
    0,
    TypeList<HeartbeatInput>,
    TypeList<>,   // effects expressed via port
    TypeList<>    // no dependencies
>
{
    void execute(StoreIMU& store,
                 PortsIMU& ports,
                 const MicroServiceContainer<>& /*container*/,
                 HeartbeatInput& /*input*/)
    {
        store.tick++;

        RawIMU reading;
        reading.accel_x = static_cast<double>(store.tick);
        reading.accel_y = static_cast<double>(store.tick) * 0.5;
        reading.accel_z = 9.81;
        reading.gyro_x  = 0.0;
        reading.gyro_y  = 0.0;
        reading.gyro_z  = static_cast<double>(store.tick) * 0.1;

        store.last_reading = reading;
        ports.raw_imu.set(reading);
    }
};

using ReactionsIMU = ReactionSet<IMUHeartbeatReaction>;

class IMU : public ReactorWithPorts<NameIMU, StoreIMU, PortsIMU,
                                    MicroServiceContainer<>, ReactionsIMU>
{
public:
    using Base = ReactorWithPorts<NameIMU, StoreIMU, PortsIMU,
                                  MicroServiceContainer<>, ReactionsIMU>;
    using Base::Base;

    void doHeartbeat(const LogicalTag& /*tag*/) override
    {
        HeartbeatInput input;
        IMUHeartbeatReaction reaction;
        reaction.execute(mStore, mPorts, mContainer, input);
    }
    // No input ports; base-class clearPorts() default (no-op) is correct.
};

// ===========================================================================
// 2. KalmanFilter  –  multi-input transformer with reaction dependency chain
// ===========================================================================

DECLARE_REACTOR_NAME(KalmanFilter);

struct StoreKalman
{
    // Latched raw reading (written by ReadSensor)
    RawIMU          latched_raw{};
    bool            has_raw{false};

    // Filtered state (written by FilterPose)
    FilteredPose    estimate{};
    bool            estimate_ready{false};

    // Reset bookkeeping
    int             reset_count{0};

    // Staleness guard: last IMU tick we actually integrated
    int             last_seen_tick{-1};

    // Simple "filter": accumulate position from accelerometer ticks
    double          pos_x{0.0};
    double          pos_y{0.0};
    double          yaw{0.0};
};

struct PortsKalman
{
    InputPort<RawIMU>       raw_imu;      // from IMU
    InputPort<ResetCommand> reset_cmd;    // external reset
    OutputPort<FilteredPose> filtered_pose; // to MotionPlanner
};

class KalmanFilter;  // forward decl

// The KalmanFilter container holds a pointer to the IMU so that its reactions
// can inspect the IMU's store directly (e.g. staleness checks).
using KalmanContainer = MicroServiceContainer<IMU>;

// ── Reaction 0: ReadSensor (level 0) ────────────────────────────────────────
// Latches the raw IMU reading (if present) into the store.
struct ReadSensorReaction : public Reaction<
    KalmanFilter,
    0,
    TypeList<HeartbeatInput>,
    TypeList<>,
    TypeList<>   // no dependencies – this is the root
>
{
    void execute(StoreKalman& store,
                 PortsKalman& ports,
                 const KalmanContainer& container,
                 HeartbeatInput& /*input*/)
    {
        // Also handle a reset command if one arrived
        if (ports.reset_cmd.is_present() && ports.reset_cmd.get().reset)
        {
            store.pos_x  = 0.0;
            store.pos_y  = 0.0;
            store.yaw    = 0.0;
            store.estimate = FilteredPose{};
            store.estimate_ready = false;
            store.reset_count++;
            store.last_seen_tick = -1;   // reset staleness tracker too
        }

        if (ports.raw_imu.is_present())
        {
            // Staleness guard: if the container holds an IMU reference, skip
            // integration when its tick hasn't advanced since we last saw it.
            // This catches the case where the filter heartbeats faster than the
            // sensor, or the same port value is delivered twice.
            auto imu_ptr = container.get<IMU>();
            if (imu_ptr)
            {
                int current_tick = imu_ptr->getStore().tick;
                if (current_tick == store.last_seen_tick)
                {
                    store.has_raw = false;   // stale – skip this cycle
                    return;
                }
                store.last_seen_tick = current_tick;
            }

            store.latched_raw = ports.raw_imu.get();
            store.has_raw     = true;
        }
        else
        {
            store.has_raw = false;
        }
    }
};

// ── Reaction 1: FilterPose (level 1, depends on ReadSensor) ─────────────────
// Integrates the latched reading into the running pose estimate.
struct FilterPoseReaction : public Reaction<
    KalmanFilter,
    1,
    TypeList<HeartbeatInput>,
    TypeList<>,
    TypeList<ReadSensorReaction>   // depends on ReadSensor
>
{
    void execute(StoreKalman& store,
                 PortsKalman& /*ports*/,
                 const KalmanContainer& /*container*/,
                 HeartbeatInput& /*input*/)
    {
        if (!store.has_raw)
            return;

        // Toy integration: position += accel (as if dt=1)
        store.pos_x += store.latched_raw.accel_x;
        store.pos_y += store.latched_raw.accel_y;
        store.yaw   += store.latched_raw.gyro_z;

        store.estimate = FilteredPose{store.pos_x, store.pos_y, store.yaw};
        store.estimate_ready = true;
    }
};

// ── Reaction 2: PublishEstimate (level 2, depends on FilterPose) ────────────
// Writes the filtered estimate to the output port.
struct PublishEstimateReaction : public Reaction<
    KalmanFilter,
    2,
    TypeList<HeartbeatInput>,
    TypeList<>,
    TypeList<FilterPoseReaction>   // depends on FilterPose
>
{
    void execute(StoreKalman& store,
                 PortsKalman& ports,
                 const KalmanContainer& /*container*/,
                 HeartbeatInput& /*input*/)
    {
        if (store.estimate_ready)
        {
            ports.filtered_pose.set(store.estimate);
        }
    }
};

using ReactionsKalman = ReactionSet<ReadSensorReaction,
                                    FilterPoseReaction,
                                    PublishEstimateReaction>;

class KalmanFilter : public ReactorWithPorts<NameKalmanFilter, StoreKalman, PortsKalman,
                                             KalmanContainer, ReactionsKalman>
{
public:
    using Base = ReactorWithPorts<NameKalmanFilter, StoreKalman, PortsKalman,
                                  KalmanContainer, ReactionsKalman>;
    using Base::Base;

    /**
     * Execute all three reactions in dependency order.
     * In production the ReactionExecutor drives this; here we call them
     * sequentially in the declared dependency order so the unit test
     * exercises the same logic path the executor would.
     */
    void doHeartbeat(const LogicalTag& /*tag*/) override
    {
        HeartbeatInput input;

        ReadSensorReaction    r0;
        FilterPoseReaction    r1;
        PublishEstimateReaction r2;

        r0.execute(mStore, mPorts, mContainer, input);
        r1.execute(mStore, mPorts, mContainer, input);
        r2.execute(mStore, mPorts, mContainer, input);
    }

    void clearPorts() override
    {
        mPorts.raw_imu.clear();
        mPorts.reset_cmd.clear();
    }
};

// ===========================================================================
// 3. MotionPlanner  –  FSM consumer
// ===========================================================================

DECLARE_REACTOR_NAME(MotionPlanner);

struct StorePlanner
{
    FilteredPose    current_pose{};
    bool            has_pose{false};

    Waypoint        current_waypoint{};
    bool            has_waypoint{false};

    VelocityCommand last_velocity{};
    int             commands_emitted{0};
};

struct PortsPlanner
{
    InputPort<FilteredPose>    filtered_pose;  // from KalmanFilter
    InputPort<Waypoint>        waypoint_cmd;   // external goal
    OutputPort<VelocityCommand> velocity_cmd;  // to actuator
};

// ── FSM States ───────────────────────────────────────────────────────────────

struct IdleStatePlanner;
struct PlanningStatePlanner;
struct ExecutingStatePlanner;

/**
 * Idle: waiting for a waypoint command.
 * On heartbeat, latch the current pose if present.
 * If a waypoint arrives, store it and transition to Planning.
 */
struct IdleStatePlanner : public State<IdleStatePlanner, 0>
{
    size_t step(StorePlanner& store, PortsPlanner& ports,
                const MicroServiceContainer<>& /*container*/,
                HeartbeatInput& /*input*/)
    {
        if (ports.filtered_pose.is_present())
        {
            store.current_pose = ports.filtered_pose.get();
            store.has_pose     = true;
        }

        if (ports.waypoint_cmd.is_present())
        {
            store.current_waypoint = ports.waypoint_cmd.get();
            store.has_waypoint     = true;
            return 1;   // transition to Planning
        }

        return index();   // stay Idle
    }
};

/**
 * Planning: compute a simple velocity vector toward the waypoint.
 * Always transitions to Executing on the next heartbeat.
 */
struct PlanningStatePlanner : public State<PlanningStatePlanner, 1>
{
    size_t step(StorePlanner& store, PortsPlanner& /*ports*/,
                const MicroServiceContainer<>& /*container*/,
                HeartbeatInput& /*input*/)
    {
        // Toy planner: velocity = (waypoint - pose), clamped to unit magnitude
        double dx = store.current_waypoint.target_x - store.current_pose.x;
        double dy = store.current_waypoint.target_y - store.current_pose.y;
        double mag = std::sqrt(dx * dx + dy * dy);
        if (mag > 1e-9)
        {
            store.last_velocity = VelocityCommand{dx / mag, dy / mag};
        }
        else
        {
            store.last_velocity = VelocityCommand{0.0, 0.0};
        }

        return 2;   // transition to Executing
    }
};

/**
 * Executing: publish the velocity command, then go back to Idle.
 */
struct ExecutingStatePlanner : public State<ExecutingStatePlanner, 2>
{
    size_t step(StorePlanner& store, PortsPlanner& ports,
                const MicroServiceContainer<>& /*container*/,
                HeartbeatInput& /*input*/)
    {
        ports.velocity_cmd.set(store.last_velocity);
        store.commands_emitted++;

        return IdleStatePlanner::index();   // back to Idle
    }
};

using StatesetPlanner = StateSet<IdleStatePlanner,
                                 PlanningStatePlanner,
                                 ExecutingStatePlanner>;

class MotionPlanner : public ReactorWithPortsAndFSM<NameMotionPlanner, StorePlanner,
                                                     PortsPlanner, MicroServiceContainer<>,
                                                     StatesetPlanner>
{
public:
    using Base = ReactorWithPortsAndFSM<NameMotionPlanner, StorePlanner, PortsPlanner,
                                        MicroServiceContainer<>, StatesetPlanner>;
    using Base::Base;

    void doHeartbeat(const LogicalTag& /*tag*/) override
    {
        HeartbeatInput input;
        executeInput(input);
    }

    void clearPorts() override
    {
        mPorts.filtered_pose.clear();
        mPorts.waypoint_cmd.clear();
    }
};

// ===========================================================================
// Helper: manually propagate an OutputPort's pending value into an InputPort
// ===========================================================================
// The existing tests (PortTest.cpp) use this pattern: when the scheduler is not
// running, we transfer the pending value by hand.  This helper encapsulates it.

template<typename T>
static void transferPort(OutputPort<T>& out, InputPort<T>& in)
{
    REQUIRE(out.has_pending_value());
    in.set(*out.get_pending_value());
}

// ===========================================================================
// TEST 1 – IMU basic behaviour
// ===========================================================================

TEST_CASE("Robotics E2E: IMU producer", "[robotics][imu]")
{
    IMU imu;

    SECTION("Initial state")
    {
        REQUIRE(imu.getStore().tick == 0);
    }

    SECTION("First heartbeat produces a reading")
    {
        LogicalTag tag{LogicalTime(0), 0};
        imu.executeHeartbeat(tag);

        REQUIRE(imu.getStore().tick == 1);
        REQUIRE(imu.getPorts().raw_imu.has_pending_value());

        const RawIMU& r = *imu.getPorts().raw_imu.get_pending_value();
        REQUIRE(r.accel_x == 1.0);
        REQUIRE(r.accel_y == 0.5);
        REQUIRE(r.accel_z == 9.81);
        REQUIRE(r.gyro_z  == 0.1);
    }

    SECTION("Successive heartbeats increment deterministically")
    {
        LogicalTag tag{LogicalTime(0), 0};
        imu.executeHeartbeat(tag);   // tick 1
        imu.executeHeartbeat(tag);   // tick 2
        imu.executeHeartbeat(tag);   // tick 3

        REQUIRE(imu.getStore().tick == 3);
        const RawIMU& r = *imu.getPorts().raw_imu.get_pending_value();
        REQUIRE(r.accel_x == 3.0);
        REQUIRE(r.gyro_z  == Approx(0.3));
    }
}

// ===========================================================================
// TEST 2 – KalmanFilter: port presence and reset command
// ===========================================================================

TEST_CASE("Robotics E2E: KalmanFilter multi-input", "[robotics][kalman]")
{
    KalmanFilter kf;

    SECTION("No input ports present → no estimate produced")
    {
        LogicalTag tag{LogicalTime(0), 0};
        kf.executeHeartbeat(tag);

        REQUIRE_FALSE(kf.getStore().has_raw);
        REQUIRE_FALSE(kf.getStore().estimate_ready);
        REQUIRE_FALSE(kf.getPorts().filtered_pose.has_pending_value());
    }

    SECTION("Single raw reading produces an estimate")
    {
        RawIMU reading{2.0, 1.0, 9.81, 0.0, 0.0, 0.05};
        kf.getPorts().raw_imu.set(reading);

        LogicalTag tag{LogicalTime(100), 0};
        kf.executeHeartbeat(tag);

        REQUIRE(kf.getStore().has_raw);
        REQUIRE(kf.getStore().estimate_ready);
        REQUIRE(kf.getStore().estimate.x == 2.0);
        REQUIRE(kf.getStore().estimate.y == 1.0);
        REQUIRE(kf.getStore().estimate.yaw == 0.05);
        REQUIRE(kf.getPorts().filtered_pose.has_pending_value());
    }

    SECTION("Reset command clears accumulated state")
    {
        // Feed two readings to accumulate some state
        kf.getPorts().raw_imu.set(RawIMU{1.0, 1.0, 9.81, 0, 0, 0.1});
        kf.executeHeartbeat(LogicalTag{LogicalTime(0), 0});
        kf.clearPorts();

        kf.getPorts().raw_imu.set(RawIMU{1.0, 1.0, 9.81, 0, 0, 0.1});
        kf.executeHeartbeat(LogicalTag{LogicalTime(1), 0});
        kf.clearPorts();

        // State should have accumulated
        REQUIRE(kf.getStore().pos_x == 2.0);
        REQUIRE(kf.getStore().pos_y == 2.0);

        // Now send a reset command (with a reading – both ports present)
        kf.getPorts().reset_cmd.set(ResetCommand{true});
        kf.getPorts().raw_imu.set(RawIMU{5.0, 3.0, 9.81, 0, 0, 0.0});
        kf.executeHeartbeat(LogicalTag{LogicalTime(2), 0});

        // Reset should have zeroed state *before* the new reading was integrated
        // ReadSensor resets first, then latches the new raw; FilterPose integrates from zero.
        REQUIRE(kf.getStore().reset_count == 1);
        REQUIRE(kf.getStore().pos_x == 5.0);   // 0 + 5
        REQUIRE(kf.getStore().pos_y == 3.0);   // 0 + 3
    }

    SECTION("Reset without a raw reading does not produce estimate")
    {
        // Accumulate first
        kf.getPorts().raw_imu.set(RawIMU{1.0, 0.0, 9.81, 0, 0, 0});
        kf.executeHeartbeat(LogicalTag{LogicalTime(0), 0});
        kf.clearPorts();

        REQUIRE(kf.getStore().estimate_ready);

        // Reset only, no raw
        kf.getPorts().reset_cmd.set(ResetCommand{true});
        kf.executeHeartbeat(LogicalTag{LogicalTime(1), 0});

        // estimate_ready should be false (no new raw to integrate)
        REQUIRE_FALSE(kf.getStore().estimate_ready);
        REQUIRE(kf.getStore().pos_x == 0.0);
    }
}

// ===========================================================================
// TEST 3 – KalmanFilter: reaction dependency graph structure
// ===========================================================================

TEST_CASE("Robotics E2E: KalmanFilter reaction dependency graph", "[robotics][kalman][graph]")
{
    SECTION("Three-level dependency chain is correctly represented")
    {
        ReactionGraph graph;

        // Mirror the declarations in the KalmanFilter reactions
        size_t r0 = graph.addReaction("KalmanFilter::0", 0, 0);   // ReadSensor
        size_t r1 = graph.addReaction("KalmanFilter::1", 0, 1);   // FilterPose
        size_t r2 = graph.addReaction("KalmanFilter::2", 0, 2);   // PublishEstimate

        // FilterPose depends on ReadSensor
        graph.addDependency(r1, r0);
        // PublishEstimate depends on FilterPose
        graph.addDependency(r2, r1);

        auto topo = graph.computeTopologicalOrder();

        // Three distinct levels
        REQUIRE(topo.levels.size() == 3);
        REQUIRE(topo.levels[0].size() == 1);  // ReadSensor
        REQUIRE(topo.levels[1].size() == 1);  // FilterPose
        REQUIRE(topo.levels[2].size() == 1);  // PublishEstimate

        // Execution order must be 0 → 1 → 2
        REQUIRE(topo.execution_order[0] == r0);
        REQUIRE(topo.execution_order[1] == r1);
        REQUIRE(topo.execution_order[2] == r2);
    }

    SECTION("Reaction metadata matches declarations")
    {
        // ReadSensor: triggered by heartbeat, no dependencies
        REQUIRE(ReadSensorReaction::is_triggered_by<HeartbeatInput>());
        REQUIRE_FALSE(ReadSensorReaction::is_triggered_by<IncrementInput>());
        REQUIRE(ReadSensorReaction::index == 0);

        // FilterPose: depends on ReadSensor
        REQUIRE(FilterPoseReaction::is_triggered_by<HeartbeatInput>());
        REQUIRE(FilterPoseReaction::depends_on<ReadSensorReaction>());
        REQUIRE_FALSE(FilterPoseReaction::depends_on<PublishEstimateReaction>());
        REQUIRE(FilterPoseReaction::index == 1);

        // PublishEstimate: depends on FilterPose
        REQUIRE(PublishEstimateReaction::depends_on<FilterPoseReaction>());
        REQUIRE_FALSE(PublishEstimateReaction::depends_on<ReadSensorReaction>());
        REQUIRE(PublishEstimateReaction::index == 2);
    }

    SECTION("ReactionExecutor runs chain in correct order")
    {
        ReactionGraph graph;

        size_t r0 = graph.addReaction("KalmanFilter::0", 0, 0);
        size_t r1 = graph.addReaction("KalmanFilter::1", 0, 1);
        size_t r2 = graph.addReaction("KalmanFilter::2", 0, 2);

        graph.addDependency(r1, r0);
        graph.addDependency(r2, r1);

        ReactionExecutor executor(graph);

        std::vector<int> order;
        executor.registerReaction(r0, [&order]() { order.push_back(0); });
        executor.registerReaction(r1, [&order]() { order.push_back(1); });
        executor.registerReaction(r2, [&order]() { order.push_back(2); });

        executor.executeAll();

        REQUIRE(order == std::vector<int>{0, 1, 2});
    }
}

// ===========================================================================
// TEST 4 – MotionPlanner FSM lifecycle
// ===========================================================================

TEST_CASE("Robotics E2E: MotionPlanner FSM", "[robotics][planner][fsm]")
{
    MotionPlanner planner;

    SECTION("Starts in Idle (state 0)")
    {
        REQUIRE(planner.getCurrentState() == 0);
    }

    SECTION("Idle with no waypoint stays Idle")
    {
        planner.getPorts().filtered_pose.set(FilteredPose{1.0, 2.0, 0.0});
        planner.executeHeartbeat(LogicalTag{LogicalTime(0), 0});

        REQUIRE(planner.getCurrentState() == 0);   // still Idle
        REQUIRE(planner.getStore().has_pose);
        REQUIRE(planner.getStore().current_pose.x == 1.0);
    }

    SECTION("Waypoint arrival transitions Idle → Planning")
    {
        planner.getPorts().filtered_pose.set(FilteredPose{0.0, 0.0, 0.0});
        planner.getPorts().waypoint_cmd.set(Waypoint{3.0, 4.0});
        planner.executeHeartbeat(LogicalTag{LogicalTime(0), 0});

        REQUIRE(planner.getCurrentState() == 1);   // Planning
        REQUIRE(planner.getStore().has_waypoint);
        REQUIRE(planner.getStore().current_waypoint.target_x == 3.0);
    }

    SECTION("Planning computes velocity and transitions to Executing")
    {
        // Set up: pose at origin, waypoint at (3,4) → unit vector (0.6, 0.8)
        planner.getPorts().filtered_pose.set(FilteredPose{0.0, 0.0, 0.0});
        planner.getPorts().waypoint_cmd.set(Waypoint{3.0, 4.0});
        planner.executeHeartbeat(LogicalTag{LogicalTime(0), 0});   // Idle → Planning
        planner.clearPorts();

        planner.executeHeartbeat(LogicalTag{LogicalTime(1), 0});   // Planning → Executing

        REQUIRE(planner.getCurrentState() == 2);   // Executing

        // Velocity should be the unit vector toward (3,4)
        // magnitude of (3,4) = 5, so (0.6, 0.8)
        REQUIRE(planner.getStore().last_velocity.vx == Approx(0.6));
        REQUIRE(planner.getStore().last_velocity.vy == Approx(0.8));
    }

    SECTION("Executing emits command and returns to Idle")
    {
        // Drive through full cycle: Idle → Planning → Executing → Idle
        planner.getPorts().filtered_pose.set(FilteredPose{0.0, 0.0, 0.0});
        planner.getPorts().waypoint_cmd.set(Waypoint{1.0, 0.0});
        planner.executeHeartbeat(LogicalTag{LogicalTime(0), 0});   // → Planning
        planner.clearPorts();

        planner.executeHeartbeat(LogicalTag{LogicalTime(1), 0});   // → Executing
        planner.executeHeartbeat(LogicalTag{LogicalTime(2), 0});   // → Idle

        REQUIRE(planner.getCurrentState() == 0);   // back to Idle
        REQUIRE(planner.getStore().commands_emitted == 1);
        REQUIRE(planner.getPorts().velocity_cmd.has_pending_value());

        const VelocityCommand& vc = *planner.getPorts().velocity_cmd.get_pending_value();
        REQUIRE(vc.vx == Approx(1.0));
        REQUIRE(vc.vy == Approx(0.0));
    }

    SECTION("Already-at-waypoint produces zero velocity")
    {
        // Pose == waypoint
        planner.getPorts().filtered_pose.set(FilteredPose{5.0, 5.0, 0.0});
        planner.getPorts().waypoint_cmd.set(Waypoint{5.0, 5.0});
        planner.executeHeartbeat(LogicalTag{LogicalTime(0), 0});   // → Planning
        planner.clearPorts();

        planner.executeHeartbeat(LogicalTag{LogicalTime(1), 0});   // → Executing

        REQUIRE(planner.getStore().last_velocity.vx == Approx(0.0));
        REQUIRE(planner.getStore().last_velocity.vy == Approx(0.0));
    }
}

// ===========================================================================
// TEST 5 – Two-stage pipeline: IMU → KalmanFilter
// ===========================================================================

TEST_CASE("Robotics E2E: IMU → KalmanFilter pipeline", "[robotics][pipeline]")
{
    IMU imu;
    KalmanFilter kf;

    SECTION("Manual port transfer propagates reading to filter")
    {
        LogicalTag tag{LogicalTime(0), 0};

        // IMU produces
        imu.executeHeartbeat(tag);
        REQUIRE(imu.getPorts().raw_imu.has_pending_value());

        // Transfer IMU output → KalmanFilter input
        transferPort(imu.getPorts().raw_imu, kf.getPorts().raw_imu);

        // KalmanFilter processes
        kf.executeHeartbeat(tag);

        REQUIRE(kf.getStore().estimate_ready);
        REQUIRE(kf.getPorts().filtered_pose.has_pending_value());

        const FilteredPose& pose = *kf.getPorts().filtered_pose.get_pending_value();
        // tick=1 → accel_x=1, accel_y=0.5, gyro_z=0.1
        REQUIRE(pose.x   == Approx(1.0));
        REQUIRE(pose.y   == Approx(0.5));
        REQUIRE(pose.yaw == Approx(0.1));
    }

    SECTION("Three consecutive ticks accumulate correctly")
    {
        for (int t = 1; t <= 3; ++t)
        {
            LogicalTag tag{LogicalTime(static_cast<int64_t>(t) * 1'000'000), 0};
            imu.executeHeartbeat(tag);
            transferPort(imu.getPorts().raw_imu, kf.getPorts().raw_imu);
            kf.executeHeartbeat(tag);
            kf.clearPorts();
        }

        // After 3 ticks: pos_x = 1+2+3 = 6, pos_y = 0.5+1.0+1.5 = 3.0
        REQUIRE(kf.getStore().pos_x == Approx(6.0));
        REQUIRE(kf.getStore().pos_y == Approx(3.0));
        REQUIRE(kf.getStore().yaw   == Approx(0.1 + 0.2 + 0.3));
    }
}

// ===========================================================================
// TEST 6 – Full three-stage pipeline: IMU → KalmanFilter → MotionPlanner
// ===========================================================================

TEST_CASE("Robotics E2E: Full pipeline IMU → KalmanFilter → MotionPlanner",
          "[robotics][pipeline][e2e]")
{
    IMU imu;
    KalmanFilter kf;
    MotionPlanner planner;

    // Give the planner a waypoint so it will act when it gets a pose.
    // We inject the waypoint on the first tick along with the first pose.

    SECTION("Single-tick end-to-end: sensor reading reaches planner")
    {
        LogicalTag tag{LogicalTime(0), 0};

        // --- IMU tick ---
        imu.executeHeartbeat(tag);

        // --- Transfer IMU → KalmanFilter ---
        transferPort(imu.getPorts().raw_imu, kf.getPorts().raw_imu);

        // --- KalmanFilter tick ---
        kf.executeHeartbeat(tag);
        REQUIRE(kf.getPorts().filtered_pose.has_pending_value());

        // --- Transfer KalmanFilter → MotionPlanner ---
        transferPort(kf.getPorts().filtered_pose, planner.getPorts().filtered_pose);

        // Also inject a waypoint on this tick
        planner.getPorts().waypoint_cmd.set(Waypoint{10.0, 10.0});

        // --- MotionPlanner tick (Idle, waypoint present → Planning) ---
        planner.executeHeartbeat(tag);
        REQUIRE(planner.getCurrentState() == 1);  // Planning

        // The planner latched the pose from the filter
        REQUIRE(planner.getStore().has_pose);
        REQUIRE(planner.getStore().current_pose.x == Approx(1.0));
        REQUIRE(planner.getStore().current_pose.y == Approx(0.5));
    }

    SECTION("Multi-tick pipeline drives planner through full FSM cycle")
    {
        // Reset all services
        IMU            imu2;
        KalmanFilter   kf2;
        MotionPlanner  planner2;

        // Tick 0: inject waypoint + first sensor reading
        {
            LogicalTag tag{LogicalTime(0), 0};
            imu2.executeHeartbeat(tag);
            transferPort(imu2.getPorts().raw_imu, kf2.getPorts().raw_imu);
            kf2.executeHeartbeat(tag);
            transferPort(kf2.getPorts().filtered_pose, planner2.getPorts().filtered_pose);
            planner2.getPorts().waypoint_cmd.set(Waypoint{10.0, 0.0});

            planner2.executeHeartbeat(tag);   // Idle → Planning
            REQUIRE(planner2.getCurrentState() == 1);
            kf2.clearPorts();
            planner2.clearPorts();
        }

        // Tick 1: planner Planning → Executing (computes velocity)
        {
            LogicalTag tag{LogicalTime(1'000'000), 0};
            // Feed another IMU reading through (state accumulates)
            imu2.executeHeartbeat(tag);
            transferPort(imu2.getPorts().raw_imu, kf2.getPorts().raw_imu);
            kf2.executeHeartbeat(tag);
            // Don't feed new pose to planner this tick – it's in Planning,
            // which doesn't read the port.  That's fine; the planner already
            // has the pose it needs from tick 0.
            kf2.clearPorts();

            planner2.executeHeartbeat(tag);   // Planning → Executing
            REQUIRE(planner2.getCurrentState() == 2);

            // Velocity should point from pose(1, 0.5) toward waypoint(10, 0)
            double dx = 10.0 - 1.0;   // 9.0
            double dy =  0.0 - 0.5;   // -0.5
            double mag = std::sqrt(dx * dx + dy * dy);
            REQUIRE(planner2.getStore().last_velocity.vx == Approx(dx / mag));
            REQUIRE(planner2.getStore().last_velocity.vy == Approx(dy / mag));
            planner2.clearPorts();
        }

        // Tick 2: planner Executing → Idle (emits velocity command)
        {
            LogicalTag tag{LogicalTime(2'000'000), 0};

            planner2.executeHeartbeat(tag);   // Executing → Idle
            REQUIRE(planner2.getCurrentState() == 0);
            REQUIRE(planner2.getStore().commands_emitted == 1);
            REQUIRE(planner2.getPorts().velocity_cmd.has_pending_value());
        }
    }
}

// ===========================================================================
// TEST 7 – ConnectionManager wiring between all three reactors
// ===========================================================================

TEST_CASE("Robotics E2E: ConnectionManager topology", "[robotics][connections]")
{
    SECTION("All three connections wire correctly")
    {
        ReactorScheduler scheduler;
        ConnectionManager manager(&scheduler);

        IMU            imu;
        KalmanFilter   kf;
        MotionPlanner  planner;

        // Wire IMU.raw_imu → KalmanFilter.raw_imu
        manager.connect(imu.getPorts().raw_imu,
                        kf.getPorts().raw_imu,
                        0, 1, "IMU→KalmanFilter:raw_imu");

        // Wire KalmanFilter.filtered_pose → MotionPlanner.filtered_pose
        manager.connect(kf.getPorts().filtered_pose,
                        planner.getPorts().filtered_pose,
                        1, 2, "KalmanFilter→MotionPlanner:filtered_pose");

        // Verify connection metadata
        REQUIRE(manager.getConnections().size() == 2);

        REQUIRE(manager.getConnections()[0].from_reactor_id == 0);
        REQUIRE(manager.getConnections()[0].to_reactor_id   == 1);
        REQUIRE(manager.getConnections()[0].connection_name == "IMU→KalmanFilter:raw_imu");

        REQUIRE(manager.getConnections()[1].from_reactor_id == 1);
        REQUIRE(manager.getConnections()[1].to_reactor_id   == 2);
        REQUIRE(manager.getConnections()[1].connection_name == "KalmanFilter→MotionPlanner:filtered_pose");
    }

    SECTION("Static topology type validates port compatibility")
    {
        // These compile-time assertions verify that the Connection template
        // correctly validates OutputPort→InputPort with matching value types.
        using IMU_to_KF = Connection<
            PortRef<IMU,            OutputPort<RawIMU>>,
            PortRef<KalmanFilter,  InputPort<RawIMU>>
        >;

        using KF_to_Planner = Connection<
            PortRef<KalmanFilter,  OutputPort<FilteredPose>>,
            PortRef<MotionPlanner, InputPort<FilteredPose>>
        >;

        using RoboticsTopology = ConnectionSet<IMU_to_KF, KF_to_Planner>;

        // Compile-time checks
        REQUIRE(RoboticsTopology::size == 2);
        REQUIRE(RoboticsTopology::has_connection<IMU, KalmanFilter>());
        REQUIRE(RoboticsTopology::has_connection<KalmanFilter, MotionPlanner>());
        REQUIRE_FALSE(RoboticsTopology::has_connection<IMU, MotionPlanner>());
    }
}

// ===========================================================================
// TEST 8 – Determinism: replaying the same inputs produces identical results
// ===========================================================================

TEST_CASE("Robotics E2E: Deterministic replay", "[robotics][determinism]")
{
    SECTION("Two independent runs with identical inputs produce identical state")
    {
        auto run_pipeline = []() -> std::tuple<StoreKalman, StorePlanner>
        {
            IMU            imu;
            KalmanFilter   kf;
            MotionPlanner  planner;

            // Tick 0
            {
                LogicalTag tag{LogicalTime(0), 0};
                imu.executeHeartbeat(tag);
                transferPort(imu.getPorts().raw_imu, kf.getPorts().raw_imu);
                kf.executeHeartbeat(tag);
                transferPort(kf.getPorts().filtered_pose, planner.getPorts().filtered_pose);
                planner.getPorts().waypoint_cmd.set(Waypoint{5.0, 5.0});
                planner.executeHeartbeat(tag);   // Idle → Planning
                kf.clearPorts();
                planner.clearPorts();
            }

            // Tick 1
            {
                LogicalTag tag{LogicalTime(1'000'000), 0};
                imu.executeHeartbeat(tag);
                transferPort(imu.getPorts().raw_imu, kf.getPorts().raw_imu);
                kf.executeHeartbeat(tag);
                planner.executeHeartbeat(tag);   // Planning → Executing
                kf.clearPorts();
                planner.clearPorts();
            }

            // Tick 2
            {
                LogicalTag tag{LogicalTime(2'000'000), 0};
                planner.executeHeartbeat(tag);   // Executing → Idle
            }

            return {kf.getStore(), planner.getStore()};
        };

        auto [kf1, p1] = run_pipeline();
        auto [kf2, p2] = run_pipeline();

        // KalmanFilter state must be identical
        REQUIRE(kf1.pos_x       == kf2.pos_x);
        REQUIRE(kf1.pos_y       == kf2.pos_y);
        REQUIRE(kf1.yaw         == kf2.yaw);
        REQUIRE(kf1.reset_count == kf2.reset_count);

        // MotionPlanner state must be identical
        REQUIRE(p1.commands_emitted           == p2.commands_emitted);
        REQUIRE(p1.last_velocity.vx           == p2.last_velocity.vx);
        REQUIRE(p1.last_velocity.vy           == p2.last_velocity.vy);
        REQUIRE(p1.current_waypoint.target_x  == p2.current_waypoint.target_x);
        REQUIRE(p1.current_waypoint.target_y  == p2.current_waypoint.target_y);
    }
}

// ===========================================================================
// TEST 9 – Scheduler-driven event delivery (two reactors, one event loop)
// ===========================================================================

TEST_CASE("Robotics E2E: Scheduler event delivery", "[robotics][scheduler]")
{
    SECTION("Scheduler processes IMU heartbeat and delivers output via connection")
    {
        ReactorScheduler scheduler;
        ConnectionManager manager(&scheduler);

        IMU          imu;
        KalmanFilter kf;

        // Wire IMU output to KalmanFilter input through the ConnectionManager.
        // When IMU's output port fires, the manager schedules a delivery event.
        manager.connect(imu.getPorts().raw_imu,
                        kf.getPorts().raw_imu,
                        0, 1, "imu_raw");

        // Manually schedule an IMU heartbeat at tag (0,0)
        LogicalTag t0{LogicalTime(0), 0};
        scheduler.scheduleEvent(t0, 0, [&imu, t0]() {
            imu.executeHeartbeat(t0);
        }, "IMU heartbeat");

        // Schedule a stop event at a later time so the loop terminates
        LogicalTag t_stop = t0.advance_time(LogicalTime(10'000'000));
        scheduler.scheduleEvent(t_stop, 99, [&scheduler]() {
            scheduler.stop();
        }, "stop");

        // Run the scheduler – it will process the IMU heartbeat, which fires
        // the output port, which (via ConnectionManager) schedules the delivery
        // event at (0, 1).  The scheduler then processes that delivery event,
        // setting the KalmanFilter's input port.
        scheduler.run();

        // After the scheduler stops, the KalmanFilter's input port should have
        // received the value that the IMU produced.
        REQUIRE(kf.getPorts().raw_imu.is_present());

        const RawIMU& delivered = kf.getPorts().raw_imu.get();
        REQUIRE(delivered.accel_x == 1.0);   // tick 1
        REQUIRE(delivered.accel_y == 0.5);
    }
}

// ===========================================================================
// TEST 10 – Daemon-style run: all three services driven by scheduler.run()
// ===========================================================================
//
// Everything above manually ticks each service — useful for isolating
// assertions between steps, but not how you'd actually deploy this.
//
// ReactorWithPorts now inherits from IReactor directly.  The base class
// handles self-rescheduling and port clearing whenever a scheduler is
// attached via setScheduler().  The production deployment is therefore:
//   1. Construct services (shared_ptr so the scheduler can hold them)
//   2. Call setScheduler() on each so the base class knows to reschedule
//   3. Wire OutputPorts to InputPorts via ConnectionManager
//   4. Inject any external commands as one-shot scheduled events
//   5. Call scheduler.run() — everything else is automatic
//
// ===========================================================================

TEST_CASE("Robotics E2E: Daemon-style scheduler-driven run", "[robotics][daemon]")
{
    SECTION("All three services run autonomously for N heartbeats")
    {
        // ── 1. Construct services ────────────────────────────────────
        auto imu     = std::make_shared<IMU>();
        auto kf      = std::make_shared<KalmanFilter>();
        auto planner = std::make_shared<MotionPlanner>();

        // ── 2. Create scheduler and connection manager ───────────────
        ReactorScheduler  scheduler;
        ConnectionManager manager(&scheduler);

        // Attach the scheduler so the base class reschedules heartbeats
        // and clears ports automatically after each tick.
        imu->setScheduler(&scheduler);
        kf->setScheduler(&scheduler);
        planner->setScheduler(&scheduler);

        // Register directly — no adapter needed.
        scheduler.registerReactor(imu);
        scheduler.registerReactor(kf);
        scheduler.registerReactor(planner);

        // ── 3. Wire port connections ─────────────────────────────────
        manager.connect(imu->getPorts().raw_imu,
                        kf->getPorts().raw_imu,
                        imu->getId(), kf->getId(), "IMU→KalmanFilter");

        manager.connect(kf->getPorts().filtered_pose,
                        planner->getPorts().filtered_pose,
                        kf->getId(), planner->getId(), "KalmanFilter→MotionPlanner");

        // ── 4. Inject external commands as one-shot events ───────────
        // Waypoint at 15 ms — after two heartbeats so the planner already
        // has a filtered pose to plan from.
        scheduler.scheduleEvent(LogicalTag{LogicalTime(15'000'000), 0},
                                planner->getId(),
                                [&planner]() {
                                    planner->getPorts().waypoint_cmd.set(Waypoint{100.0, 50.0});
                                }, "inject waypoint");

        // ── 5. Stop after 60 ms (6 heartbeat cycles) ────────────────
        scheduler.scheduleEvent(LogicalTag{LogicalTime(60'000'000), 0}, 99,
                                [&scheduler]() { scheduler.stop(); }, "stop");

        // ── 6. Run ───────────────────────────────────────────────────
        scheduler.run();

        // ── 7. Verify ────────────────────────────────────────────────

        REQUIRE(imu->getStore().tick >= 5);

        // KF sees each IMU reading one heartbeat late (delivery at next
        // microstep, consumed on next heartbeat).  After K IMU ticks KF
        // has integrated 1..K-1: pos_x = (K-1)*K/2.
        int k = imu->getStore().tick;
        double expected_pos_x = static_cast<double>((k - 1) * k) / 2.0;
        REQUIRE(kf->getStore().pos_x == Approx(expected_pos_x));

        // Planner completed at least one full FSM cycle
        REQUIRE(planner->getStore().commands_emitted >= 1);

        // Velocity points toward waypoint (100, 50) — both components positive
        REQUIRE(planner->getStore().last_velocity.vx > 0.0);
        REQUIRE(planner->getStore().last_velocity.vy > 0.0);

        // Unit vector check
        double vx  = planner->getStore().last_velocity.vx;
        double vy  = planner->getStore().last_velocity.vy;
        REQUIRE(std::sqrt(vx * vx + vy * vy) == Approx(1.0));
    }

    SECTION("Logical time advances monotonically through the pipeline")
    {
        auto imu = std::make_shared<IMU>();
        auto kf  = std::make_shared<KalmanFilter>();

        ReactorScheduler  scheduler;
        ConnectionManager manager(&scheduler);

        imu->setScheduler(&scheduler);
        kf->setScheduler(&scheduler);

        scheduler.registerReactor(imu);
        scheduler.registerReactor(kf);

        manager.connect(imu->getPorts().raw_imu,
                        kf->getPorts().raw_imu,
                        imu->getId(), kf->getId(), "IMU→KalmanFilter");

        scheduler.scheduleEvent(LogicalTag{LogicalTime(35'000'000), 0}, 99,
                                [&scheduler]() { scheduler.stop(); }, "stop");

        scheduler.run();

        REQUIRE(scheduler.getCurrentTag().time.count() >= 35'000'000);
        REQUIRE(kf->getStore().pos_x > 0.0);
    }

    SECTION("Pipeline produces same result across two independent runs")
    {
        auto run_once = []() -> std::pair<StoreKalman, StorePlanner>
        {
            auto imu     = std::make_shared<IMU>();
            auto kf      = std::make_shared<KalmanFilter>();
            auto planner = std::make_shared<MotionPlanner>();

            ReactorScheduler  sched;
            ConnectionManager mgr(&sched);

            imu->setScheduler(&sched);
            kf->setScheduler(&sched);
            planner->setScheduler(&sched);

            sched.registerReactor(imu);
            sched.registerReactor(kf);
            sched.registerReactor(planner);

            mgr.connect(imu->getPorts().raw_imu,
                        kf->getPorts().raw_imu,
                        imu->getId(), kf->getId(), "imu");
            mgr.connect(kf->getPorts().filtered_pose,
                        planner->getPorts().filtered_pose,
                        kf->getId(), planner->getId(), "kf");

            sched.scheduleEvent(LogicalTag{LogicalTime(15'000'000), 0},
                                planner->getId(),
                                [&planner]() {
                                    planner->getPorts().waypoint_cmd.set(Waypoint{100.0, 50.0});
                                }, "waypoint");

            sched.scheduleEvent(LogicalTag{LogicalTime(50'000'000), 0}, 99,
                                [&sched]() { sched.stop(); }, "stop");

            sched.run();
            return {kf->getStore(), planner->getStore()};
        };

        auto [kf1, p1] = run_once();
        auto [kf2, p2] = run_once();

        REQUIRE(kf1.pos_x       == kf2.pos_x);
        REQUIRE(kf1.pos_y       == kf2.pos_y);
        REQUIRE(kf1.yaw         == kf2.yaw);
        REQUIRE(kf1.reset_count == kf2.reset_count);

        REQUIRE(p1.commands_emitted          == p2.commands_emitted);
        REQUIRE(p1.last_velocity.vx          == p2.last_velocity.vx);
        REQUIRE(p1.last_velocity.vy          == p2.last_velocity.vy);
        REQUIRE(p1.current_waypoint.target_x == p2.current_waypoint.target_x);
        REQUIRE(p1.current_waypoint.target_y == p2.current_waypoint.target_y);
    }
}

// ===========================================================================
// TEST 11 – KalmanFilter staleness guard via container-held IMU reference
// ===========================================================================
//
// KalmanContainer holds a shared_ptr<IMU>.  ReadSensorReaction reads
// imu->getStore().tick to detect duplicate / stale readings: if the IMU's tick
// hasn't advanced since the filter last integrated, the filter skips that
// heartbeat.  This exercises the container's get<>() path at runtime and
// verifies that cross-reactor store inspection actually influences behaviour.
//
// ===========================================================================

TEST_CASE("Robotics E2E: KalmanFilter staleness guard via container IMU",
          "[robotics][kalman][container][staleness]")
{
    SECTION("Duplicate port value is skipped when IMU tick is unchanged")
    {
        auto imu = std::make_shared<IMU>();
        KalmanContainer container(imu);
        KalmanFilter    kf(container);

        LogicalTag tag{LogicalTime(0), 0};

        // ── Tick the IMU once (tick becomes 1) and push its reading ──────
        imu->executeHeartbeat(tag);
        REQUIRE(imu->getStore().tick == 1);

        transferPort(imu->getPorts().raw_imu, kf.getPorts().raw_imu);

        // First KF heartbeat: tick=1 is new → integrates normally
        kf.executeHeartbeat(tag);
        REQUIRE(kf.getStore().last_seen_tick == 1);
        REQUIRE(kf.getStore().estimate_ready);
        REQUIRE(kf.getStore().pos_x == Approx(1.0));   // accel_x at tick 1

        double pos_x_after_first = kf.getStore().pos_x;

        // ── Feed the *same* port value again without advancing the IMU ───
        // (simulates duplicate delivery or filter heartbeating faster than sensor)
        kf.getPorts().raw_imu.set(imu->getStore().last_reading);

        kf.executeHeartbeat(tag);

        // Staleness guard fired: pos_x must not have changed
        REQUIRE(kf.getStore().last_seen_tick == 1);   // still 1
        REQUIRE(kf.getStore().pos_x == pos_x_after_first);
    }

    SECTION("New IMU tick allows integration to proceed")
    {
        auto imu = std::make_shared<IMU>();
        KalmanContainer container(imu);
        KalmanFilter    kf(container);

        LogicalTag tag{LogicalTime(0), 0};

        // Tick 1
        imu->executeHeartbeat(tag);
        transferPort(imu->getPorts().raw_imu, kf.getPorts().raw_imu);
        kf.executeHeartbeat(tag);
        kf.clearPorts();

        REQUIRE(kf.getStore().pos_x == Approx(1.0));

        // Tick 2 – IMU advances
        imu->executeHeartbeat(tag);
        REQUIRE(imu->getStore().tick == 2);
        transferPort(imu->getPorts().raw_imu, kf.getPorts().raw_imu);
        kf.executeHeartbeat(tag);

        // tick=2 is new → integrated: pos_x = 1 + 2 = 3
        REQUIRE(kf.getStore().last_seen_tick == 2);
        REQUIRE(kf.getStore().pos_x == Approx(3.0));
    }

    SECTION("Reset clears staleness tracker; next reading integrates from zero")
    {
        auto imu = std::make_shared<IMU>();
        KalmanContainer container(imu);
        KalmanFilter    kf(container);

        LogicalTag tag{LogicalTime(0), 0};

        // Two ticks to accumulate state
        imu->executeHeartbeat(tag);   // tick 1
        transferPort(imu->getPorts().raw_imu, kf.getPorts().raw_imu);
        kf.executeHeartbeat(tag);
        kf.clearPorts();

        imu->executeHeartbeat(tag);   // tick 2
        transferPort(imu->getPorts().raw_imu, kf.getPorts().raw_imu);
        kf.executeHeartbeat(tag);
        kf.clearPorts();

        REQUIRE(kf.getStore().pos_x == Approx(3.0));   // 1 + 2

        // Send reset + a fresh reading (tick 3)
        imu->executeHeartbeat(tag);   // tick 3
        kf.getPorts().reset_cmd.set(ResetCommand{true});
        transferPort(imu->getPorts().raw_imu, kf.getPorts().raw_imu);
        kf.executeHeartbeat(tag);

        // Reset zeroed state; tick 3 is new after the reset → integrated from 0
        REQUIRE(kf.getStore().reset_count == 1);
        REQUIRE(kf.getStore().last_seen_tick == 3);
        REQUIRE(kf.getStore().pos_x == Approx(3.0));   // 0 + accel_x@tick3 = 3
    }

    SECTION("Default-constructed KalmanFilter (no IMU in container) skips guard")
    {
        // When the container holds a null shared_ptr<IMU> the staleness check
        // is entirely bypassed — every heartbeat with a present port integrates.
        KalmanFilter kf;   // default ctor → KalmanContainer with null IMU ptr

        LogicalTag tag{LogicalTime(0), 0};

        RawIMU reading{5.0, 2.0, 9.81, 0, 0, 0.1};
        kf.getPorts().raw_imu.set(reading);
        kf.executeHeartbeat(tag);

        REQUIRE(kf.getStore().pos_x == Approx(5.0));
        REQUIRE(kf.getStore().last_seen_tick == -1);   // never updated

        // Feed the exact same reading again – no guard, so it integrates again
        kf.getPorts().raw_imu.set(reading);
        kf.executeHeartbeat(tag);

        REQUIRE(kf.getStore().pos_x == Approx(10.0));  // 5 + 5
    }
}
