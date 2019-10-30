
#include <fibre/local_endpoint.hpp>
#include <fibre/logging.hpp>

USE_LOG_TOPIC(LOCAL_ENDPOINT);

namespace fibre {

std::unordered_map<Uuid, LocalEndpoint*> local_endpoints{};

int register_endpoint(Uuid uuid, LocalEndpoint* local_endpoint) {
    local_endpoints[uuid] = local_endpoint;
    return 0;
}

LocalEndpoint* get_endpoint(Uuid uuid) {
    auto it = local_endpoints.find(uuid);
    return (it != local_endpoints.end()) ? it->second : nullptr;
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
