#pragma once

#include <chrono>
#include <cstdint>
#include <compare>
#include <ostream>
#include <stdexcept>
#include <limits>
#include <spdlog/fmt/fmt.h>

namespace services
{

/**
 * Logical time representation for deterministic reactor execution.
 *
 * In the reactor model, events are processed in logical time order rather than
 * physical arrival time. This ensures deterministic, reproducible execution.
 */
using LogicalTime = std::chrono::nanoseconds;

/**
 * A logical tag uniquely identifies a point in logical time.
 *
 * Tags consist of:
 * - time: The logical time (nanoseconds since epoch)
 * - microstep: A counter for simultaneous events at the same logical time
 *
 * Events at the same logical time are ordered by microstep. This allows
 * modeling of instantaneous cascading reactions while maintaining determinism.
 *
 * Example:
 *   Tag(100, 0) < Tag(100, 1) < Tag(101, 0)
 */
struct LogicalTag
{
    LogicalTime time{0};      // Logical time in nanoseconds
    uint32_t    microstep{0}; // Microstep counter for simultaneous events

    // Default constructor creates tag at time 0, microstep 0
    constexpr LogicalTag() = default;

    // Construct tag at specific time with microstep 0
    constexpr explicit LogicalTag(LogicalTime t) : time(t), microstep(0) {}

    // Construct tag at specific time and microstep
    constexpr LogicalTag(LogicalTime t, uint32_t m) : time(t), microstep(m) {}

    /**
     * Convenience constructor from nanosecond count.
     *
     * @param nanos Logical time in nanoseconds (microstep defaults to 0)
     */
    constexpr explicit LogicalTag(uint64_t nanos) : time(LogicalTime(nanos)), microstep(0) {}

    /**
     * Three-way comparison operator (C++20).
     *
     * Compares tags lexicographically: first by time, then by microstep.
     * Enables all comparison operators (<, <=, >, >=, ==, !=).
     *
     * @param other Tag to compare with
     * @return Ordering relationship (strong_ordering)
     */
    constexpr auto operator<=>(const LogicalTag& other) const
    {
        if (auto cmp = time <=> other.time; cmp != 0)
            return cmp;
        return microstep <=> other.microstep;
    }

    /**
     * Equality comparison operator.
     *
     * @param other Tag to compare with
     * @return true if both time and microstep are equal
     */
    constexpr bool operator==(const LogicalTag& other) const
    {
        return time == other.time && microstep == other.microstep;
    }

    /**
     * Advance to next microstep at same logical time.
     *
     * Used when an event triggers another event at the same logical time.
     * Maintains deterministic ordering through microstep increments.
     *
     * @return New tag at same time with microstep + 1
     * @throws std::overflow_error if microstep would exceed UINT32_MAX
     *
     * Example:
     *   Tag(100, 0).next_microstep() → Tag(100, 1)
     */
    constexpr LogicalTag next_microstep() const
    {
        if (microstep == std::numeric_limits<uint32_t>::max())
        {
            throw std::overflow_error("Microstep overflow: exceeded UINT32_MAX at logical time");
        }
        return LogicalTag{time, microstep + 1};
    }

    /**
     * Advance logical time by a delta, resetting microstep to 0.
     *
     * Used for scheduling future events at a later logical time.
     *
     * @param delta Time increment in nanoseconds
     * @return New tag at time + delta with microstep 0
     *
     * Example:
     *   Tag(100, 5).advance_time(50ns) → Tag(150, 0)
     */
    constexpr LogicalTag advance_time(LogicalTime delta) const
    {
        return LogicalTag{time + delta, 0};
    }

    /**
     * Check if this is the initial tag (time 0, microstep 0).
     *
     * @return true if this is the initial tag, false otherwise
     */
    constexpr bool is_initial() const
    {
        return time == LogicalTime(0) && microstep == 0;
    }
};

/**
 * Stream output operator for LogicalTag.
 *
 * Formats tag as "(time ns, µmicrostep)" for debugging and logging.
 *
 * @param os Output stream
 * @param tag Tag to output
 * @return Reference to output stream for chaining
 *
 * Example output: "(100 ns, µ2)"
 */
inline std::ostream& operator<<(std::ostream& os, const LogicalTag& tag)
{
    os << "(" << tag.time.count() << "ns, µ" << tag.microstep << ")";
    return os;
}

/**
 * Logging control for logical-time events.
 * Set LOG_LOGICAL_TIME=0 to suppress per-event trace output.
 */
#ifndef LOG_LOGICAL_TIME
#define LOG_LOGICAL_TIME 1
#endif

} // namespace services

/**
 * fmt formatter specialization for LogicalTag.
 *
 * Enables using LogicalTag with fmt/spdlog formatting libraries.
 * Formats as "(time ns, µmicrostep)".
 *
 * Example:
 *   fmt::format("Event at {}", tag); → "Event at (100 ns, µ2)"
 */
template<>
struct fmt::formatter<services::LogicalTag>
{
    constexpr auto parse(format_parse_context& ctx) -> decltype(ctx.begin())
    {
        return ctx.begin();
    }

    template<typename FormatContext>
    auto format(const services::LogicalTag& tag, FormatContext& ctx) const -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "({} ns, µ{})", tag.time.count(), tag.microstep);
    }
};
