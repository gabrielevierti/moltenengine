// Log.hpp
// printf-style logging with a level in front. Messages go to stderr, so they do not
// mix with anything a tool prints on stdout.
#pragma once

#include <cstdarg>
#include <cstdio>
#include <functional>

namespace molten {

/// Receives every message as well as stderr: level is "info", "warning" or "error".
/// The editor's console sets this. Called on the thread that logs.
using LogSink = std::function<void(const char* level, const char* message)>;

inline LogSink& logSink() {
    static LogSink sink;
    return sink;
}

inline void logMessage(const char* level, const char* format, va_list arguments) {
    char message[2048];
    std::vsnprintf(message, sizeof(message), format, arguments);
    std::fprintf(stderr, "[%s] %s\n", level, message);
    if (logSink()) logSink()(level, message);
}

#if defined(__GNUC__)
#define MOLTEN_PRINTF_LIKE __attribute__((format(printf, 1, 2)))
#else
#define MOLTEN_PRINTF_LIKE
#endif

MOLTEN_PRINTF_LIKE inline void logInfo(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    logMessage("info", format, arguments);
    va_end(arguments);
}

/// For finding out what a game is doing while it is made: shown in the editor's console under
/// Debug, and easy to hide there.
MOLTEN_PRINTF_LIKE inline void logDebug(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    logMessage("debug", format, arguments);
    va_end(arguments);
}

MOLTEN_PRINTF_LIKE inline void logWarning(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    logMessage("warning", format, arguments);
    va_end(arguments);
}

MOLTEN_PRINTF_LIKE inline void logError(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    logMessage("error", format, arguments);
    va_end(arguments);
}

}  // namespace molten
