
#include <fibre/local_function.hpp>
#include <fibre/logging.hpp>

DEFINE_LOG_TOPIC(LOCAL_ENDPOINT);
USE_LOG_TOPIC(LOCAL_ENDPOINT);

namespace fibre {

std::unordered_map<Uuid, LocalEndpoint*> local_endpoints{};

int register_endpoint(Uuid uuid, LocalEndpoint* local_endpoint) {
    local_endpoints[uuid] = local_endpoint;
    return 0;
}

int unregister_endpoint(Uuid uuid) {
    auto it = local_endpoints.find(uuid);
    if (it == local_endpoints.end()) {
        FIBRE_LOG(E) << "attempt to unregister unknown endpoint";
        return -1;
    }
    local_endpoints.erase(it);
    return 0;
}

}
