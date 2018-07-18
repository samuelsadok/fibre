#ifndef __FIBRE_LOGGING_HPP
#define __FIBRE_LOGGING_HPP

#include "cpp_utils.hpp"

#ifndef __FIBRE_HPP
#error "This file should not be included directly. Include fibre.hpp instead."
#endif

#include <iostream>
#include <mutex>

namespace fibre {

#define GET_LOG_TOPIC_NAME(NAME, STR) LOG_TOPIC_ ## NAME,
#define LOG_TOPIC_STR(NAME, STR) template<> struct topic_name<LOG_TOPIC_ ## NAME> { constexpr static const char * name = STR; };

enum log_topic_t {
    LOG_TOPICS(GET_LOG_TOPIC_NAME)
};

template<log_topic_t TOPIC>
struct topic_name;

LOG_TOPICS(LOG_TOPIC_STR)

template<log_topic_t TOPIC>
const char * get_topic_name() {
    return topic_name<TOPIC>::name;
}


enum log_config_t {
    ENABLED,
    DYNAMIC,
    OFF
};

enum log_level_t {
    INFO,
    WARNING
};

template<log_topic_t topic>
constexpr log_config_t get_config() { return DYNAMIC; }



template<typename TStream, typename T, typename... Ts>
void send_to_stream(TStream&& stream);

template<typename TStream>
void send_to_stream(TStream&& stream) { }

template<typename TStream, typename T, typename... Ts>
void send_to_stream(TStream&& stream, T&& value, Ts&&... values) {
    send_to_stream(std::forward<TStream>(stream) << std::forward<T>(value), std::forward<Ts>(values)...);
}


class Logger {
public:

    class new_entry {
    public:
        new_entry(std::unique_lock<std::mutex>&& lock)
            : lock_(std::move(lock)) {}
        new_entry(new_entry&& other)
            : lock_(std::move(other.lock_)) {}
        ~new_entry() {
            get_stream() << "\n";
        }
        std::ostream& get_stream() {
            return std::cerr;
        };
    private:
        std::unique_lock<std::mutex> lock_;
    };

    new_entry log(const char* topic, const char* filename, size_t line_no, const char *funcname) {
        std::unique_lock<std::mutex> lock(mutex_);
        std::cerr << std::dec << "[" << topic << "] ";
        //std::cerr << std::dec << filename << ":" << line_no << " in " << funcname << "(): ";
        return new_entry(std::move(lock));
    }
    
private:
    std::mutex mutex_;
};


Logger* get_logger();

template<log_topic_t TOPIC, log_level_t LEVEL, typename... Ts>
void log(const char *filename, size_t line_no, const char *funcname, Ts&& ... values) {
    if (get_config<TOPIC>() == OFF) {
        return;
    }

    Logger* logger = get_logger();
    Logger::new_entry entry = logger->log(get_topic_name<TOPIC>(), filename, line_no, funcname);
    send_to_stream(entry.get_stream(), std::forward<Ts>(values)...);
}


template<size_t I>
constexpr const char * get_file_name(const char (&file_path)[I]) {
    return file_path + make_const_string(file_path).after_last_index_of('/');
}


#define CONFIG_LOG_TOPIC(topic, mode) 

//CONFIG_LOG_TOPIC(GENERAL, DYNAMIC);
//CONFIG_LOG_TOPIC(SCHEDULER, DYNAMIC);
//CONFIG_LOG_TOPIC(TCP, DYNAMIC);
#define LOG_FIBRE_(topic, level, ...) \
    fibre::log<fibre::LOG_TOPIC_ ## topic, level>(fibre::get_file_name(__FILE__), __LINE__, __func__, __VA_ARGS__)

#define LOG_FIBRE(topic, ...) LOG_FIBRE_(topic, fibre::INFO, __VA_ARGS__)
#define LOG_FIBRE_W(topic, ...) LOG_FIBRE_(topic, fibre::WARNING, __VA_ARGS__)



/*
template<>
constexpr log_level_t is_enabled<CONFIG_LOG_GENERAL>() { return DYNAMIC; }
template<>
constexpr log_level_t is_enabled<CONFIG_LOG_TCP>() { return DYNAMIC; }
*/

//log(TCP, "sent ", hex(chunk), " bytes");

}

#endif // __FIBRE_LOGGING_HPP
