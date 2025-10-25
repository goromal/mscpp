#pragma once
#ifndef SPDLOG_ACTIVE_LEVEL
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#endif
// #define STRINGIFY2(x) #x
// #define STRINGIFY(x) STRINGIFY2(x)
// #pragma message("SPDLOG_ACTIVE_LEVEL = " STRINGIFY(SPDLOG_ACTIVE_LEVEL))
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace services
{

inline std::shared_ptr<spdlog::logger>& default_logger()
{
    static std::shared_ptr<spdlog::logger> logger = [] {
        auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        sink->set_pattern("%T.%e [%^%l%$] [%s:%#] %v");
        auto log = std::make_shared<spdlog::logger>("default", sink);
        spdlog::register_logger(log);
        spdlog::set_default_logger(log);
        spdlog::set_level(static_cast<spdlog::level::level_enum>(SPDLOG_ACTIVE_LEVEL));
        return log;
    }();
    return logger;
}

} // namespace services

// ---- Shorthand Macros ----
#define LOG_TRACE(...) SPDLOG_LOGGER_CALL(::services::default_logger().get(), spdlog::level::trace, __VA_ARGS__)
#define LOG_DEBUG(...) SPDLOG_LOGGER_CALL(::services::default_logger().get(), spdlog::level::debug, __VA_ARGS__)
#define LOG_INFO(...) SPDLOG_LOGGER_CALL(::services::default_logger().get(), spdlog::level::info, __VA_ARGS__)
#define LOG_WARN(...) SPDLOG_LOGGER_CALL(::services::default_logger().get(), spdlog::level::warn, __VA_ARGS__)
#define LOG_ERROR(...) SPDLOG_LOGGER_CALL(::services::default_logger().get(), spdlog::level::err, __VA_ARGS__)