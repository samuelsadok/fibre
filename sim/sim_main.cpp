
#include "../cpp/mini_rng.hpp"
#include "../cpp/platform_support/can_adapter.hpp"
#include "../test/test_node.hpp"
#include "mock_can.hpp"
#include <fibre/fibre.hpp>
#include <fibre/logging.hpp>
#include <unordered_map>
#include <algorithm>
#include <variant>

namespace fibre {

struct FibreNode {
    FibreNode(simulator::Simulator* simulator, std::string name)
        : simulator_{simulator}, sim_node_{simulator, name} {}

    void start(bool enable_server, bool enable_client);

    void add_can_intf(simulator::SimCanInterface* intf) {

        //intf->start(1000000, 1000000, {}, {});
        CanAdapter* can_backend = new CanAdapter{simulator_, impl_.domain_, intf, intf->port_->name.data()};
        can_backend->start(0, 128);
    }

    simulator::Simulator* simulator_;
    simulator::Node sim_node_;
    TestNode impl_;
};

}  // namespace fibre

using namespace fibre;
using namespace fibre::simulator;



struct Pipe {};

Pipe* start_call(std::string destination) {
    return new Pipe{};
}

void FibreNode::start(bool enable_server, bool enable_client) {
    uint8_t node_id[16];
    simulator_->rng.get_random(node_id);

    impl_.start(simulator_, node_id, "", enable_server, enable_client, sim_node_.logger());


    /*
        // optimistic weak pipe
        auto p = fibre::start_call("the_destination_uuid");
        p->write("endp0 operation");

        // stong pipe
        auto p = fibre::
        impl_.domain_->
        */
}

int main() {
    printf("Starting Fibre server...\n");

    Simulator simulator;
    CanMedium can_medium{&simulator};

    FibreNode server{&simulator, "server"};
    FibreNode client{&simulator, "client"};

    // TODO: try both init orders
    client.start(false, true);
    server.start(true, false);

    server.add_can_intf(can_medium.new_intf(&server.sim_node_, "can0"));
    client.add_can_intf(can_medium.new_intf(&client.sim_node_, "can0"));
    can_medium.join({"server.can0", "client.can0"}, "the_can_bus");



    // TODO: remove hack
    //server.impl_.domain_->on_found_node(client.impl_.domain_->node_id);
    //client.impl_.domain_->on_found_node(server.impl_.domain_->node_id);

    simulator.run(200, 0.35f);

    printf("Simulation terminated.\n");
}
