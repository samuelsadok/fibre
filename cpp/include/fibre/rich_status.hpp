#ifndef __FIBRE_RICH_STATUS_HPP
#define __FIBRE_RICH_STATUS_HPP

#include <fibre/config.hpp>

#include <stdlib.h>
#include <array>
#include <fibre/backport/optional.hpp>

#if FIBRE_ENABLE_TEXT_LOGGING
#include <string>
#include <iostream>
#include <sstream>
#endif


namespace fibre {

#if FIBRE_ENABLE_TEXT_LOGGING
using logstr = std::string;
#else
struct logstr {};
#endif

#if __cplusplus < 201703L
struct RichStatus {
#else
struct [[nodiscard]] RichStatus {
#endif
    RichStatus() : n_msgs(0) {}

    template<typename TFunc>
    RichStatus(TFunc msg_gen, const char* file, size_t line, const RichStatus& inner)
        : msgs_(inner.msgs_), n_msgs(inner.n_msgs) {
        if (n_msgs < msgs_.size()) {
            logstr msg;
#if FIBRE_ENABLE_TEXT_LOGGING
            std::ostringstream stream;
            msg_gen(stream);
            msg = stream.str();
#endif
            msgs_[n_msgs++] = {msg, file, line};
        }
    }


    struct StackFrame {
        logstr msg;
        const char* file;
        size_t line;
    };

    std::array<StackFrame, 4> msgs_;
    size_t n_msgs;

    bool is_error() const {
        return n_msgs > 0;
    }

    bool is_success() const {
        return !is_error();
    }

    template<typename TFunc>
    bool on_error(TFunc func) {
        if (is_error()) {
            func();
        }
        return is_error();
    }

    const char* inner_file() const { return n_msgs ? msgs_[0].file : nullptr; }
    size_t inner_line() const { return n_msgs ? msgs_[0].line : 0; }

    static RichStatus success() {
        return {};
    }
};


#if FIBRE_ENABLE_TEXT_LOGGING

static inline std::ostream& operator<<(std::ostream& stream, RichStatus const& status) { 
    for (size_t i = 0; i < status.n_msgs; ++i) {
        stream << "\n\t\tin " << status.msgs_[i].file << ":" << status.msgs_[i].line << ": " << status.msgs_[i].msg;
    }
    return stream;
}

#endif

template<typename T>
class RichStatusOr {
public:
    RichStatusOr(T val) : status_{RichStatus::success()}, val_{val} {}
    RichStatusOr(RichStatus status) : status_{status}, val_{std::nullopt} {}

    RichStatus status() { return status_; }
    T& value() { return *val_; }
    bool has_value() { return val_.has_value(); }

private:
    RichStatus status_;
    std::optional<T> val_;
};

}


#if FIBRE_ENABLE_TEXT_LOGGING

#define F_MAKE_ERR(msg) fibre::RichStatus{[&](std::ostream& str) { str << msg; }, __FILE__, __LINE__, fibre::RichStatus::success()}
#define F_AMEND_ERR(inner, msg) fibre::RichStatus{[&](std::ostream& str) { str << msg; }, __FILE__, __LINE__, (inner)}

#else

#define F_MAKE_ERR(msg) fibre::RichStatus{0, __FILE__, __LINE__, fibre::RichStatus::success()}
#define F_AMEND_ERR(inner, msg) fibre::RichStatus{0, __FILE__, __LINE__, (inner)}

#endif

/**
 * @brief Returns an error object from the current function if `expr` evaluates
 * to false.
 * 
 * The containing function must have a return type that is assignable from
 * RichStatus.
 * 
 * If `FIBRE_ENABLE_TEXT_LOGGING` is non-zero, `msg` is evaluated and attached
 * to the error object.
 */
#define F_RET_IF(expr, msg) \
    do { \
        bool __err = (expr); \
        if (__err) \
            return F_MAKE_ERR(msg); \
    } while (0)

/**
 * @brief Returns an error object from the current function if `status` is an
 * error.
 * 
 * The containing function must have a return type that is assignable from
 * RichStatus.
 * 
 * If `FIBRE_ENABLE_TEXT_LOGGING` is non-zero, `msg` is evaluated and attached
 * to the error object.
 */
#define F_RET_IF_ERR(status, msg) \
    do { \
        fibre::RichStatus __status = (status); \
        if (__status.is_error()) \
            return F_AMEND_ERR(__status, msg); \
    } while (0)

#endif // __FIBRE_RICH_STATUS_HPP