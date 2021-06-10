#ifndef __FIBRE_SIMULATOR_HPP
#define __FIBRE_SIMULATOR_HPP

#include <fibre/../../mini_rng.hpp>
#include <fibre/callback.hpp>
#include <fibre/event_loop.hpp>
#include <fibre/logging.hpp>
#include <unordered_map>
#include <string>
#include <vector>

namespace fibre {
namespace simulator {

struct Port;
struct Simulator;

struct Node {
    Simulator* simulator_;
    std::string name;
    std::unordered_map<std::string, Port*> ports;

    void log(const char* file, unsigned line, int level, uintptr_t info0,
             uintptr_t info1, const char* text);

    Logger logger() {
        return Logger{MEMBER_CB(this, log), get_log_verbosity()};
    }
};

struct Port {
    Node* node;
    std::string name;
};

static inline std::ostream& operator<<(std::ostream& str, Port* port) {
    return str << port->node->name << "." << port->name;
}

class Simulator final : public EventLoop {
public:
    Simulator() {
        rng.seed(0, 1, 2, 3);
    }

    struct Event {
        uint64_t t_ns;
        Callback<void> trigger;
        Port* from;             // optional
        std::vector<Port*> to;  // optional
    };

    Event* send(Port* from, std::vector<Port*> to, float duration,
                Callback<void> on_delivery);
    Event* add_event(Event evt);
    void cancel(Event* evt);

    void run(size_t n_events, float dt);

    RichStatus post(Callback<void> callback) final;
    RichStatus register_event(int fd, uint32_t events,
                              Callback<void, uint32_t> callback) final;
    RichStatus deregister_event(int fd) final;
    RichStatus open_timer(Timer** p_timer, Callback<void> on_trigger) final;
    RichStatus close_timer(Timer* timer) final;

    uint64_t t_ns = 0;
    MiniRng rng;

private:

    std::vector<Event*> backlog;
};

}  // namespace simulator
}  // namespace fibre

#endif  // __FIBRE_SIMULATOR_HPP
