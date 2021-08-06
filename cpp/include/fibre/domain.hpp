#ifndef __FIBRE_DOMAIN_HPP
#define __FIBRE_DOMAIN_HPP

#include <fibre_config.hpp>
#include <fibre/async_stream.hpp>
#include <fibre/base_types.hpp>
#include <fibre/callback.hpp>
#include <fibre/../../mini_rng.hpp> // TODO: move file
#include <fibre/node.hpp>
#include <fibre/pool.hpp>
#include <fibre/endpoint_connection.hpp>
#include <memory>
#include <unordered_map>

namespace fibre {

struct Fibre;
class Function; // defined in function.hpp
class Interface; // defined in interface.hpp
struct ChannelDiscoveryResult;
struct Object;

// TODO: legacy stuff - remove
struct ChannelDiscoveryContext;
struct LegacyProtocolPacketBased;
class LegacyObjectClient;
struct LegacyObject;

class Domain {
    friend struct Fibre;
public:
    void show_device_dialog(std::string backend);

#if FIBRE_ENABLE_CLIENT
    // TODO: add interface argument
    // TODO: support multiple discovery instances
    void start_discovery(Callback<void, Object*, Interface*, std::string> on_found_object, Callback<void, Object*> on_lost_object);
    void stop_discovery();
#endif

    void add_legacy_channels(ChannelDiscoveryResult result, const char* name); // TODO: deprecate

#if FIBRE_ENABLE_SERVER
    const Function* get_server_function(ServerFunctionId id);
    ServerObjectDefinition* get_server_object(ServerObjectId id);
#endif
    void on_found_node(const NodeId& node_id, FrameStreamSink* sink, const char* intf_name, Node** p_node);
    void on_lost_node(Node* node, FrameStreamSink* sink);

    void open_call(const std::array<uint8_t, 16>& call_id, uint8_t protocol, FrameStreamSink* return_path, Node* return_node, ConnectionInputSlot** slot);
    void close_call(ConnectionInputSlot* slot);
    
#if FIBRE_ENABLE_CLIENT
    void on_found_root_object(Object* obj, Interface* intf, std::string path);
    void on_lost_root_object(Object* obj);
#endif

    Fibre* ctx;

    // It is theoretically possible to run a node without node ID but if a
    // client is connected over two interfaces it cannot determine that the node
    // is the same and would detect it as two nodes. Maybe worth consideration
    // for very constrained nodes.
    NodeId node_id;
    MiniRng rng; // initialized from node_id seed and used to generate call IDs

private:
    bool connect_slots(Connection* conn, FrameStreamSink* sink);
    bool disconnect_slots(Connection* conn, FrameStreamSink* sink);

    void on_stopped_p(LegacyProtocolPacketBased* protocol, StreamStatus status);
    void on_stopped_s(LegacyProtocolPacketBased* protocol, StreamStatus status);

#if FIBRE_ALLOW_HEAP
    std::unordered_map<std::string, fibre::ChannelDiscoveryContext*> channel_discovery_handles;
#endif
#if FIBRE_ENABLE_CLIENT
    Callback<void, Object*, Interface*, std::string> on_found_object_;
    Callback<void, Object*> on_lost_object_;
    std::unordered_map<Object*, std::pair<Interface*, std::string>> root_objects_;
#endif

#if FIBRE_ENABLE_CLIENT == F_RUNTIME_CONFIG
    bool enable_client;
#endif

#if FIBRE_ENABLE_SERVER
    // TODO: selectable capacity
    Map<std::array<uint8_t, 16>, EndpointServerConnection, 3> server_connections;
#endif

#if FIBRE_ENABLE_CLIENT
    // TODO: selectable capacity
    Map<std::array<uint8_t, 16>, EndpointClientConnection, 3> client_connections;
#endif

#if FIBRE_ENABLE_CLIENT
    // TODO: selectable capacity
    Map<NodeId, Node, 16> nodes;
#endif
};

}

#endif // __FIBRE_DOMAIN_HPP
