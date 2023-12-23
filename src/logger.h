#pragma once

enum LogLevel {
  LOG_LEVEL_FATAL,
  LOG_LEVEL_ERROR,
  LOG_LEVEL_WARN,
  LOG_LEVEL_INFO,
  LOG_LEVEL_DEBUG,
  LOG_LEVEL_TRACE,
};

class Logger {
public:
  static void LogOutput(LogLevel level, const char *message, ...);
};

#define FATAL(message, ...)                                                    \
  Logger::LogOutput(LOG_LEVEL_FATAL, message, ##__VA_ARGS__);
#define ERROR(message, ...)                                                    \
  Logger::LogOutput(LOG_LEVEL_ERROR, message, ##__VA_ARGS__);
#define WARN(message, ...)                                                     \
  Logger::LogOutput(LOG_LEVEL_WARN, message, ##__VA_ARGS__);
#define INFO(message, ...)                                                     \
  Logger::LogOutput(LOG_LEVEL_INFO, message, ##__VA_ARGS__);

#ifndef NDEBUG
#define DEBUG(message, ...)                                                    \
  Logger::LogOutput(LOG_LEVEL_DEBUG, message, ##__VA_ARGS__);
#define TRACE(message, ...)                                                    \
  Logger::LogOutput(LOG_LEVEL_TRACE, message, ##__VA_ARGS__);
#else
#define DEBUG(message, ...)
#define TRACE(message, ...)
#endif