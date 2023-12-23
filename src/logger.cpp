#include "logger.h"

#include "logger.h"

#include <cstdio>
#include <cstring>
#include <stdarg.h>
#include <string.h>

void Logger::LogOutput(LogLevel level, const char *message, ...) {
  const char *level_strings[6] = {"[FATAL]: ", "[ERROR]: ", "[WARN]:  ",
                                  "[INFO]:  ", "[DEBUG]: ", "[TRACE]: "};
  bool is_error = level < LOG_LEVEL_WARN;

  const int msg_length = 32000;
  char out_message[msg_length];
  memset(out_message, 0, sizeof(out_message));

  __builtin_va_list arg_ptr;
  va_start(arg_ptr, message);
  vsnprintf(out_message, msg_length, message, arg_ptr);
  va_end(arg_ptr);

  char out_message2[msg_length + 2];
  sprintf(out_message2, "%s%s\n", level_strings[level], out_message);

  const char *color_strings[] = {"1;31", "1;35", "1;33",
                                 "1;32", "1;34", "1;30"};

  if (is_error) {
    fprintf(stderr, "\033[%sm%s\033[0m", color_strings[level], out_message2);
  } else {
    printf("\033[%sm%s\033[0m", color_strings[level], out_message2);
  }
}