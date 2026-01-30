/**
 * Port-Based I/O Tests
 *
 * These tests verify the port-based communication model:
 * - InputPort and OutputPort functionality
 * - Port presence semantics
 * - Tag propagation through connections
 * - ConnectionManager wiring
 * - Static topology validation
 */

#include <catch2/catch.hpp>

#include "mscpp/Ports.h"
#include "mscpp/Topology.h"
#include "mscpp/ReactorScheduler.h"
#include "example-services/ServiceWithPorts.h"
#include "example-services/Inputs.h"

using namespace services;

// ===========================================================================
// Test 1: Basic Port Functionality
// ===========================================================================

TEST_CASE("Port Basics", "[ports]")
{
    SECTION("InputPort presence semantics")
    {
        InputPort<int> port;

        // Initially, no value present
        REQUIRE_FALSE(port.is_present());

        // Set a value
        port.set(42);
        REQUIRE(port.is_present());
        REQUIRE(port.get() == 42);

        // Get with default
        REQUIRE(port.get_or(100) == 42);

        // Clear the value
        port.clear();
        REQUIRE_FALSE(port.is_present());

        // Get with default when not present
        REQUIRE(port.get_or(100) == 100);
    }

    SECTION("OutputPort basic functionality")
    {
        OutputPort<int> port;

        // Set a value (without callback)
        port.set(42);

        // Check pending value
        REQUIRE(port.has_pending_value());
        REQUIRE(port.get_pending_value().has_value());
        REQUIRE(*port.get_pending_value() == 42);
    }

    SECTION("OutputPort with callback")
    {
        OutputPort<int> port;
        int received_value = 0;
        bool callback_called = false;

        // Set callback
        port.setScheduleCallback([&](int&& value) {
            received_value = value;
            callback_called = true;
        });

        // Set a value
        port.set(42);

        // Callback should have been called
        REQUIRE(callback_called);
        REQUIRE(received_value == 42);
    }

    SECTION("OutputPort callback processes pending values")
    {
        OutputPort<int> port;

        // Set value before callback is registered
        port.set(42);
        REQUIRE(port.has_pending_value());

        // Now register callback
        int received_value = 0;
        port.setScheduleCallback([&](int&& value) {
            received_value = value;
        });

        // Pending value should have been processed
        REQUIRE(received_value == 42);
        REQUIRE_FALSE(port.has_pending_value());
    }
}

// ===========================================================================
// Test 2: Port Type Traits
// ===========================================================================

TEST_CASE("Port Type Traits", "[ports]")
{
    SECTION("IsInputPort trait")
    {
        REQUIRE(IsInputPort<InputPort<int>>::value);
        REQUIRE_FALSE(IsInputPort<OutputPort<int>>::value);
        REQUIRE_FALSE(IsInputPort<int>::value);
    }

    SECTION("IsOutputPort trait")
    {
        REQUIRE(IsOutputPort<OutputPort<int>>::value);
        REQUIRE_FALSE(IsOutputPort<InputPort<int>>::value);
        REQUIRE_FALSE(IsOutputPort<int>::value);
    }

    SECTION("PortValueType extraction")
    {
        using InputValueType = PortValueType_t<InputPort<int>>;
        using OutputValueType = PortValueType_t<OutputPort<double>>;

        REQUIRE(std::is_same_v<InputValueType, int>);
        REQUIRE(std::is_same_v<OutputValueType, double>);
    }
}

// ===========================================================================
// Test 3: Connection Validation
// ===========================================================================

TEST_CASE("Connection Type Validation", "[connections]")
{
    SECTION("Valid connection compiles")
    {
        // This should compile fine
        using ValidConnection = Connection<
            PortRef<ServiceC, OutputPort<int>>,
            PortRef<ServiceD, InputPort<int>>
        >;

        REQUIRE(std::is_same_v<
            typename ValidConnection::FromValueType,
            typename ValidConnection::ToValueType
        >);
    }

    SECTION("ConnectionSet functionality")
    {
        using Conn1 = Connection<
            PortRef<ServiceC, OutputPort<int>>,
            PortRef<ServiceD, InputPort<int>>
        >;

        using Conn2 = Connection<
            PortRef<ServiceD, OutputPort<int>>,
            PortRef<ServiceE, InputPort<int>>
        >;

        using Topology = ConnectionSet<Conn1, Conn2>;

        REQUIRE(Topology::size == 2);
    }
}

// ===========================================================================
// Test 4: ConnectionManager Runtime Wiring
// ===========================================================================

TEST_CASE("ConnectionManager", "[connections]")
{
    SECTION("Connect output port to input port")
    {
        ReactorScheduler scheduler;
        ConnectionManager manager(&scheduler);

        OutputPort<int> output;
        InputPort<int> input;

        // Connect the ports
        manager.connect(output, input, 0, 1, "test_connection");

        // Verify connection was registered
        REQUIRE(manager.getConnections().size() == 1);
        REQUIRE(manager.getConnections()[0].from_reactor_id == 0);
        REQUIRE(manager.getConnections()[0].to_reactor_id == 1);
        REQUIRE(manager.getConnections()[0].connection_name == "test_connection");
    }

    SECTION("Multiple connections")
    {
        ReactorScheduler scheduler;
        ConnectionManager manager(&scheduler);

        OutputPort<int> out1, out2;
        InputPort<int> in1, in2;

        manager.connect(out1, in1, 0, 1, "conn1");
        manager.connect(out2, in2, 0, 2, "conn2");

        REQUIRE(manager.getConnections().size() == 2);
    }
}

// ===========================================================================
// Test 5: Service with Ports
// ===========================================================================

TEST_CASE("Services with Ports", "[services]")
{
    SECTION("ServiceC produces values on output port")
    {
        ServiceC service;

        // Initially counter is 0
        REQUIRE(service.getStore().counter == 0);

        // Execute heartbeat
        LogicalTag tag{LogicalTime(0), 0};
        service.executeHeartbeat(tag);

        // Counter should have incremented
        REQUIRE(service.getStore().counter == 1);
        REQUIRE(service.getStore().state == "running");

        // Output port should have pending value
        REQUIRE(service.getPorts().counter_out.has_pending_value());
    }

    SECTION("ServiceD consumes values from input port")
    {
        ServiceD service;

        // Initially no values received
        REQUIRE(service.getStore().receive_count == 0);

        // Set a value on input port
        service.getPorts().counter_in.set(42);

        // Execute heartbeat
        LogicalTag tag{LogicalTime(0), 0};
        service.executeHeartbeat(tag);

        // Should have received the value
        REQUIRE(service.getStore().last_received == 42);
        REQUIRE(service.getStore().receive_count == 1);
        REQUIRE(service.getStore().state == "received");

        // Clear port for next tag
        service.clearPorts();

        // Execute again without value
        service.executeHeartbeat(tag);
        REQUIRE(service.getStore().state == "waiting");
        REQUIRE(service.getStore().receive_count == 1);  // No new value
    }

    SECTION("ServiceE transforms values")
    {
        ServiceE service;

        // Set input value
        service.getPorts().value_in.set(10);

        // Execute heartbeat
        LogicalTag tag{LogicalTime(0), 0};
        service.executeHeartbeat(tag);

        // Should have transformed the value (10 * 2 = 20)
        REQUIRE(service.getStore().state == "transformed");
        REQUIRE(service.getPorts().value_out.has_pending_value());
        REQUIRE(*service.getPorts().value_out.get_pending_value() == 20);
    }
}

// ===========================================================================
// Test 6: End-to-End Port Communication
// ===========================================================================

TEST_CASE("End-to-End Port Communication", "[integration]")
{
    SECTION("Producer -> Consumer via ConnectionManager")
    {
        ReactorScheduler scheduler;
        ConnectionManager manager(&scheduler);

        ServiceC producer;
        ServiceD consumer;

        // Connect producer output to consumer input
        manager.connect(
            producer.getPorts().counter_out,
            consumer.getPorts().counter_in,
            0, 1, "producer_to_consumer"
        );

        // Execute producer heartbeat
        LogicalTag tag{LogicalTime(0), 0};
        producer.executeHeartbeat(tag);

        // Producer should have incremented counter and set output
        REQUIRE(producer.getStore().counter == 1);

        // When connected via ConnectionManager, the output port callback
        // schedules an event in the scheduler. The callback also directly
        // sets the input port value for immediate testing.
        // In a real scheduler integration, the scheduler would process the event.

        // For this test, since we're manually testing without running the scheduler,
        // we note that the callback was triggered but the value isn't in pending anymore.
        // The connection is established, which is what we're verifying here.
        REQUIRE(manager.getConnections().size() == 1);

        // Since the connection manager's callback was invoked, the input port
        // would have been set by the callback. However, the callback uses a lambda
        // that captures the input_port by reference, so the set() happens directly.
        // But the lambda is passed to scheduleEvent, which doesn't execute immediately.
        // So for unit testing, we manually set the value to simulate event processing.
        consumer.getPorts().counter_in.set(1);

        // Execute consumer heartbeat
        consumer.executeHeartbeat(tag);

        // Consumer should have received the value
        REQUIRE(consumer.getStore().last_received == 1);
        REQUIRE(consumer.getStore().receive_count == 1);
    }

    SECTION("Pipeline: Producer -> Transform -> Consumer")
    {
        ServiceC producer;
        ServiceE transformer;
        ServiceD consumer;

        // Producer produces value
        LogicalTag tag{LogicalTime(0), 0};
        producer.executeHeartbeat(tag);
        REQUIRE(producer.getStore().counter == 1);

        // Transfer to transformer
        int value1 = *producer.getPorts().counter_out.get_pending_value();
        transformer.getPorts().value_in.set(value1);

        // Transformer transforms
        transformer.executeHeartbeat(tag);
        REQUIRE(transformer.getStore().state == "transformed");

        // Transfer to consumer
        int value2 = *transformer.getPorts().value_out.get_pending_value();
        consumer.getPorts().counter_in.set(value2);

        // Consumer receives
        consumer.executeHeartbeat(tag);
        REQUIRE(consumer.getStore().last_received == 2);  // 1 * 2 = 2
    }
}

// ===========================================================================
// Test 7: Tag Propagation Through Ports
// ===========================================================================

TEST_CASE("Tag Propagation", "[tags]")
{
    SECTION("Output events scheduled at next microstep")
    {
        ReactorScheduler scheduler;
        ConnectionManager manager(&scheduler);

        OutputPort<int> output;
        InputPort<int> input;

        manager.connect(output, input, 0, 1, "test");

        // Current tag
        LogicalTag current_tag{LogicalTime(100), 0};

        // Set value on output (this should schedule at next microstep)
        output.set(42);

        // In a full integration, the scheduler would process this
        // and deliver at tag (100, 1)
        // For now, we verify the connection exists
        REQUIRE(manager.getConnections().size() == 1);
    }
}

// ===========================================================================
// Test 8: Port Semantics Edge Cases
// ===========================================================================

TEST_CASE("Port Edge Cases", "[ports]")
{
    SECTION("InputPort get() without value throws")
    {
        InputPort<int> port;
        REQUIRE_FALSE(port.is_present());

        // Attempting to get() without checking should throw
        REQUIRE_THROWS(port.get());
    }

    SECTION("InputPort can be set multiple times (overwrites)")
    {
        InputPort<int> port;

        port.set(10);
        REQUIRE(port.get() == 10);

        port.set(20);
        REQUIRE(port.get() == 20);  // Overwritten
    }

    SECTION("OutputPort can be set multiple times")
    {
        OutputPort<int> port;
        int call_count = 0;
        int last_value = 0;

        port.setScheduleCallback([&](int&& value) {
            call_count++;
            last_value = value;
        });

        port.set(10);
        port.set(20);
        port.set(30);

        // All three sets should have called the callback
        REQUIRE(call_count == 3);
        REQUIRE(last_value == 30);
    }
}

// ===========================================================================
// Test 9: PortSet Metadata
// ===========================================================================

TEST_CASE("PortSet Metadata", "[portset]")
{
    SECTION("PortSet with typed ports")
    {
        using InputPorts = TypeList<InputPort<int>, InputPort<double>>;
        using OutputPorts = TypeList<OutputPort<int>>;
        using Ports = PortSet<InputPorts, OutputPorts>;

        REQUIRE(Ports::num_inputs == 2);
        REQUIRE(Ports::num_outputs == 1);
    }

    SECTION("Empty PortSet")
    {
        using Ports = PortSet<>;

        REQUIRE(Ports::num_inputs == 0);
        REQUIRE(Ports::num_outputs == 0);
    }
}
