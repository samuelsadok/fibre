
#include "simulator.hpp"
#include <fibre/rich_status.hpp>
#include <algorithm>

using namespace fibre;
using namespace fibre::simulator;

class SimulatorTimer final : public Timer {
    RichStatus set(float interval, TimerMode mode) final;
    void on_trigger();
public:
    Simulator* sim_;
    Callback<void> callback;
    Simulator::Event* evt_;
    bool periodic_;
    float interval_;
};

RichStatus Simulator::post(Callback<void> callback) {
    return F_MAKE_ERR("not implemented");
}
RichStatus Simulator::register_event(int fd, uint32_t events,
                                     Callback<void, uint32_t> callback) {
    return F_MAKE_ERR("not implemented");
}
RichStatus Simulator::deregister_event(int fd) {
    return F_MAKE_ERR("not implemented");
}

RichStatus Simulator::open_timer(Timer** p_timer, Callback<void> on_trigger) {
    SimulatorTimer* t = new SimulatorTimer{}; // deleted in close_timer()
    t->sim_ = this;
    t->callback = on_trigger;
    if (p_timer) {
        *p_timer = t;
    }
    return RichStatus::success();
}

RichStatus SimulatorTimer::set(float interval, TimerMode mode) {
    if (evt_) {
        sim_->cancel(evt_);
    }

    periodic_ = mode == TimerMode::kPeriodic;
    interval_ = interval;

    if (mode != TimerMode::kNever) {
        uint64_t delay_ns = interval_ * (float)1e9;
        evt_ = sim_->add_event({sim_->t_ns + delay_ns, MEMBER_CB(this, on_trigger), nullptr, {}});
    }

    return RichStatus::success();
}

void SimulatorTimer::on_trigger() {
    evt_ = nullptr;

    if (periodic_) {
        uint64_t delay_ns = interval_ * (float)1e9;
        evt_ = sim_->add_event({sim_->t_ns + delay_ns, MEMBER_CB(this, on_trigger), nullptr, {}});
    }

    callback.invoke();
}

RichStatus Simulator::close_timer(Timer* timer) {
    SimulatorTimer* t = static_cast<SimulatorTimer*>(timer);
    cancel(t->evt_);
    delete t;
    return RichStatus::success();
}

Simulator::Event* Simulator::send(Port* from, std::vector<Port*> to,
                                  float duration, Callback<void> on_delivery) {
    uint64_t duration_ns = duration * (float)1e9;
    return add_event(Event{t_ns + duration_ns, on_delivery, from, to});
}

Simulator::Event* Simulator::add_event(Event new_evt) {
    auto it = std::find_if(backlog.begin(), backlog.end(), [&](Event* evt) {
        return (evt->t_ns - t_ns) > (new_evt.t_ns - t_ns);
    });
    Event* evt = new Event{new_evt};
    backlog.insert(it, evt);
    return evt;
}

void Simulator::cancel(Event* evt) {
    backlog.erase(std::find(backlog.begin(), backlog.end(), evt));
}

void Simulator::run(size_t n_events, float dt) {
    uint64_t t_0 = t_ns;
    uint64_t dt_ns = (uint64_t)(dt * 1e9);

    for (;;) {
        if (!backlog.size()) {
            printf("No more events in queue.\n");
            return;
        } else if (!(n_events--)) {
            printf("Event limit reached.\n");
            return;
        } else if ((backlog.front()->t_ns - t_0) > dt_ns) {
            printf("Time limit reached.\n");
            return;
        }

        Event* evt = backlog.front();
        backlog.erase(backlog.begin());
        t_ns = evt->t_ns;
        evt->trigger.invoke();
        delete evt;
    }
}

void Node::log(const char* file, unsigned line, int level, uintptr_t info0,
               uintptr_t info1, const char* text) {
    switch ((LogLevel)level) {
        case LogLevel::kDebug:
            // std::cerr << "\x1b[93;1m"; // yellow
            break;
        case LogLevel::kError:
            std::cerr << "\x1b[91;1m";  // red
            break;
        default:
            break;
    }

    float sim_time = (float)simulator_->t_ns / 1e6;
    std::cerr << "t=" << sim_time << "ms " << name << " [" << file << ":"
              << line << "] " << text << "\x1b[0m" << std::endl;
}
