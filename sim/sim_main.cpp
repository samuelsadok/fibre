
#include "../test/test_node.hpp"
#include <fibre/fibre.hpp>
#include <fibre/logging.hpp>
#include <unordered_map>
#include <algorithm>
#include <variant>

namespace fibre {

namespace simulator {

struct Port;

struct Node {
    std::string name;
    std::unordered_map<std::string, Port*> ports;
};

struct Port {
    Node* node;
    std::string name;
};

class Simulator final : public EventLoop {
public:
    void send(Port* from, Port* to, float duration, Callback<void> on_delivery);

    void run();

    RichStatus post(Callback<void> callback) final;
    RichStatus register_event(int fd, uint32_t events,
                              Callback<void, uint32_t> callback) final;
    RichStatus deregister_event(int fd) final;
    RichStatus call_later(float delay, Callback<void> callback,
                          EventLoopTimer** p_timer) final;
    RichStatus cancel_timer(EventLoopTimer* timer) final;

    uint64_t t_ns = 0;

private:
    struct Event {
        uint64_t t_ns;
        Callback<void> trigger;
        Port* from;  // optional
        Port* to;    // optional
    };

    void add_event(Event evt);

    std::vector<Event> backlog;
};

}  // namespace simulator

struct FibreNode {
    FibreNode(simulator::Simulator* simulator, std::string name)
        : simulator_{simulator}, sim_node_{name} {}

    void start(bool enable_server, bool enable_client);

    void log(const char* file, unsigned line, int level, uintptr_t info0,
             uintptr_t info1, const char* text);

    simulator::Simulator* simulator_;
    simulator::Node sim_node_;
    TestNode impl_;
};

struct CanBus;

struct CanChannel final : AsyncStreamSource, AsyncStreamSink {
    void start_read(bufptr_t buffer, TransferHandle* handle,
                    Callback<void, ReadResult> completer) final;
    void cancel_read(TransferHandle transfer_handle) final;
    void start_write(cbufptr_t buffer, TransferHandle* handle,
                     Callback<void, WriteResult> completer) final;
    void cancel_write(TransferHandle transfer_handle) final;
    void on_delivery();

    CanBus* bus_;
    simulator::Port* port_;
    Callback<void, ReadResult> rx_completer_;
    Callback<void, WriteResult> tx_completer_;
    cbufptr_t tx_buf_;
    bufptr_t rx_buf_;
};

struct CanBus {
    CanBus(simulator::Simulator* simulator) : simulator_{simulator} {}

    void send(cbufptr_t buffer, CanChannel* from);
    void send_next();
    void on_sent();

    void join(FibreNode* node, std::string port);
    void leave(FibreNode* node, std::string port);

    std::vector<CanChannel*> tx_queue_;
    std::vector<std::shared_ptr<CanChannel>> connections_;
    simulator::Simulator* simulator_;

    bool busy = false;

    size_t mtu = 64;
    uint32_t bps = 1000000;
};

}  // namespace fibre

using namespace fibre;
using namespace fibre::simulator;

void Simulator::send(Port* from, Port* to, float duration,
                     Callback<void> on_delivery) {
    uint64_t duration_ns = duration * (float)1e9;
    add_event(Event{t_ns + duration_ns, on_delivery, from, to});
}

void Simulator::add_event(Event new_evt) {
    auto it = std::find_if(backlog.begin(), backlog.end(), [&](Event& evt) {
        return (evt.t_ns - t_ns) > (new_evt.t_ns - t_ns);
    });
    backlog.insert(it, new_evt);
}

void Simulator::run() {
    while (backlog.size()) {
        Event evt = backlog.front();
        backlog.erase(backlog.begin());
        t_ns = evt.t_ns;
        evt.trigger.invoke();
    }
}

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
RichStatus Simulator::call_later(float delay, Callback<void> callback,
                                 EventLoopTimer** p_timer) {
    uint64_t delay_ns = delay * (float)1e9;
    add_event({t_ns + delay_ns, callback, nullptr, nullptr});
    return F_MAKE_ERR("not implemented");
}
RichStatus Simulator::cancel_timer(EventLoopTimer* timer) {
    return F_MAKE_ERR("not implemented");
}

void FibreNode::start(bool enable_server, bool enable_client) {
    Logger logger{MEMBER_CB(this, log), get_log_verbosity()};
    impl_.start(simulator_, "", enable_server, enable_client, logger);
}

void FibreNode::log(const char* file, unsigned line, int level, uintptr_t info0,
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
    std::cerr << "t=" << sim_time << "ms " << sim_node_.name << " [" << file
              << ":" << line << "] " << text << "\x1b[0m" << std::endl;
}

void CanBus::join(FibreNode* node, std::string port_name) {
    // node->sim_node_.name = ;

    // TODO: check if exists
    Port* port = new Port{&node->sim_node_, port_name};
    node->sim_node_.ports[port_name] = port;

    auto channel = std::make_shared<CanChannel>();
    channel->bus_ = this;
    channel->port_ = port;

    connections_.push_back(channel);

    node->impl_.domain_->add_channels(
        {fibre::Status::kFibreOk, channel.get(), channel.get(), mtu, true});
}

void CanBus::send_next() {
    CanChannel* tx_channel = tx_queue_.front();
    busy = true;

    float duration = (float)(tx_channel->tx_buf_.size() * 8) / (float)bps;

    for (auto& rx_channel : connections_) {
        if (rx_channel.get() != tx_channel && rx_channel->rx_completer_) {
            size_t n_copy = std::min(mtu, std::min(tx_channel->tx_buf_.size(),
                                                   rx_channel->rx_buf_.size()));
            std::copy_n(tx_channel->tx_buf_.begin(), n_copy,
                        rx_channel->rx_buf_.begin());
            rx_channel->rx_buf_ = rx_channel->rx_buf_.skip(n_copy);

            simulator_->send(tx_channel->port_, rx_channel->port_, duration,
                             MEMBER_CB(rx_channel.get(), on_delivery));
        }
    }

    simulator_->call_later(duration, MEMBER_CB(this, on_sent), nullptr);
}

void CanBus::on_sent() {
    CanChannel* channel = tx_queue_.front();

    size_t n_sent = std::min(channel->tx_buf_.size(), mtu);
    channel->tx_completer_.invoke_and_clear(
        {kStreamOk, channel->tx_buf_.begin() + n_sent});

    tx_queue_.erase(tx_queue_.begin());
    busy = false;

    if (tx_queue_.size()) {
        send_next();
    }
}

void CanChannel::start_read(bufptr_t buffer, TransferHandle* handle,
                            Callback<void, ReadResult> completer) {
    rx_buf_ = buffer;
    rx_completer_ = completer;
}
void CanChannel::cancel_read(TransferHandle transfer_handle) {}

void CanChannel::on_delivery() {
    rx_completer_.invoke_and_clear({kStreamOk, rx_buf_.begin()});
}

void CanChannel::start_write(cbufptr_t buffer, TransferHandle* handle,
                             Callback<void, WriteResult> completer) {
    if (!tx_completer_) {
        tx_buf_ = buffer;
        tx_completer_ = completer;

        bus_->tx_queue_.push_back(this);

        if (!bus_->busy) {
            bus_->send_next();
        }
    }
}

void CanChannel::cancel_write(TransferHandle transfer_handle) {}

int main() {
    printf("Starting Fibre server...\n");

    Simulator simulator;

    FibreNode server{&simulator, "server"};
    FibreNode client{&simulator, "client"};

    CanBus busA{&simulator};

    server.start(true, false);
    client.start(false, true);

    busA.join(&server, "can0");
    busA.join(&client, "can0");

    simulator.run();

    printf("No more events in queue. Simulation terminated.\n");
}
