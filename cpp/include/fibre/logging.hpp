#ifndef __FIBRE_LOGGING_HPP
#define __FIBRE_LOGGING_HPP

#include <fibre/config.hpp>

#include <fibre/callback.hpp>
#include <stdint.h>

#if FIBRE_ENABLE_TEXT_LOGGING
#include <sstream>
#include <iostream>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#include "windows.h" // required for GetLastError()
#endif
#endif

namespace fibre {

enum class LogLevel : int {
    kError = 1,
    kDebug = 4,
    kTrace = 5,
};

/**
 * @brief Log function callback type
 * 
 * @param ctx: An opaque user defined context pointer.
 * @param file: The file name of the call site. Valid until the program terminates.
 * @param line: The line number of the call site.
 * @param info0: A general purpose information parameter. The meaning of this depends on the call site.
 * @param info1: A general purpose information parameter. The meaning of this depends on the call site.
 * @param text: Text to log. Valid only for the duration of the log call. Always
 *        Null if Fibre is compiled with FIBRE_ENABLE_TEXT_LOGGING=0.
 */
typedef Callback<void, const char* /* file */, unsigned /* line */, int /* level */, uintptr_t /* info0 */, uintptr_t /* info1 */, const char* /* text */> log_fn_t;

class Logger {
public:
    Logger(log_fn_t impl, LogLevel verbosity)
        : impl_{impl}, verbosity_{verbosity} {}

    template<typename TFunc>
    void log(const char* file, unsigned line, int level, uintptr_t info0, uintptr_t info1, TFunc text_gen) const {
        if (level <= (int)verbosity_) {
            const char* c_str = nullptr;
#if FIBRE_ENABLE_TEXT_LOGGING
            std::ostringstream stream;
            text_gen(stream);
            std::string str = stream.str();
            c_str = str.c_str();
#endif
            impl_.invoke(file, line, level, info0, info1, c_str);
        }
    }

    static Logger none() {
        return {
            {[](void*, const char*, unsigned, int, uintptr_t, uintptr_t, const char*){}, nullptr},
            (LogLevel)-1
        };
    }

private:
    log_fn_t impl_;
    LogLevel verbosity_;
};

/**
 * @brief Returns the log verbosity as configured by the environment variable
 * `FIBRE_LOG`.
 * 
 * On platforms that don't have environment variables (like embedded systems)
 * this returns LogLevel::kError.
 */
static inline LogLevel get_log_verbosity() {
    const char * var_val = std::getenv("FIBRE_LOG");
    if (var_val) {
        unsigned long num = strtoul(var_val, nullptr, 10);
        return (LogLevel)num;
    } else {
        return LogLevel::kError;
    }
}

}

/**
 * @brief Tag type to print the last system error
 * 
 * The statement `std::out << sys_err();` will print the last system error
 * in the following format: "error description (errno)".
 * This is based on `GetLastError()` (Windows) or `errno` (all other systems).
 */
struct sys_err {};

#if FIBRE_ENABLE_TEXT_LOGGING

namespace std {
static inline std::ostream& operator<<(std::ostream& stream, const sys_err&) {
#if defined(_WIN32) || defined(_WIN64)
    auto error_code = GetLastError();
#else
    auto error_code = errno;
#endif
    return stream << strerror(error_code) << " (" << error_code << ")";
}
}

#endif

namespace fibre {

template<typename T, typename TFunc>
const T& with(const T& val, TFunc func) {
    func(val);
    return val;
}

}

#if FIBRE_ENABLE_TEXT_LOGGING
#define STR_BUILDER(msg) ([&](std::ostream& str) { str << msg; })
#else
#define STR_BUILDER(msg) (0)
#endif

#define F_LOG_IF(logger, expr, msg) \
    fibre::with((bool)(expr), [&](bool __expr) { \
        if (__expr) (logger).log(__FILE__, __LINE__, (int)fibre::LogLevel::kError, 0, 0, STR_BUILDER(msg)); \
    })

#define F_LOG_IF_ERR(logger, status, msg) \
    fibre::with((status), [&](const fibre::RichStatus& __status) { \
        if (__status.is_error()) (logger).log(__FILE__, __LINE__, (int)fibre::LogLevel::kError, (uintptr_t)__status.inner_file(), __status.inner_line(), STR_BUILDER(msg << ": " << __status)); \
    }).is_error()

#define F_LOG_T(logger, msg) \
    (logger).log(__FILE__, __LINE__, (int)fibre::LogLevel::kTrace, 0, 0, STR_BUILDER(msg))

#define F_LOG_D(logger, msg) \
    (logger).log(__FILE__, __LINE__, (int)fibre::LogLevel::kDebug, 0, 0, STR_BUILDER(msg))

#define F_LOG_E(logger, msg) \
    (logger).log(__FILE__, __LINE__, (int)fibre::LogLevel::kError, 0, 0, STR_BUILDER(msg))

// TODO: fix
#define F_LOG_W F_LOG_E

#endif // __FIBRE_LOGGING_HPP
