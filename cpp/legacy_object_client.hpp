#ifndef __FIBRE_LEGACY_OBJECT_MODEL_HPP
#define __FIBRE_LEGACY_OBJECT_MODEL_HPP

#include <fibre/backport/variant.hpp>
#include <fibre/callback.hpp>
#include <fibre/fibre.hpp>
#include <fibre/function.hpp>
#include <fibre/interface.hpp>
#include <unordered_map>
#include <vector>
#include <memory>
#include <string>

struct json_value;

namespace fibre {

struct LegacyProtocolPacketBased;
struct Transcoder;
struct LegacyObject;
class LegacyObjectClient;
struct LegacyInterface;

// Lower 16 bits are the seqno. Upper 16 bits are all 1 for valid handles
// (such that seqno 0 doesn't cause the handle to be 0)
using EndpointOperationHandle = uint32_t;

struct EndpointOperationResult {
    EndpointOperationHandle op;
    StreamStatus status;
    const uint8_t* tx_end;
    uint8_t* rx_end;
};

struct LegacyFibreArg {
    std::string name;
    std::string app_codec;
    Transcoder* transcoder;
    size_t ep_num;
};


struct LegacyFunction final : Function {
    LegacyFunction(LegacyObjectClient* client, std::string name,
                    size_t ep_num, LegacyObject* obj,
                    std::vector<LegacyFibreArg> inputs,
                    std::vector<LegacyFibreArg> outputs)
        : Function{},
          client(client),
          name(name),
          ep_num(ep_num),
          obj_(obj),
          inputs_(inputs),
          outputs_(outputs) {}

    virtual Socket* start_call(
        Domain* domain, bufptr_t call_frame, Socket* caller) const final;

    FunctionInfo* get_info() const final;
    void free_info(FunctionInfo* info) const final;

    LegacyObjectClient* client;
    std::string name;
    size_t ep_num;  // 0 for property read/write/exchange functions
    LegacyObject*
        obj_;  // null for property read/write/exchange functions (all other
               // functions are associated with one object only)
    std::vector<LegacyFibreArg> inputs_;
    std::vector<LegacyFibreArg> outputs_;
};

struct LegacyFibreAttribute {
    std::string name;
    std::shared_ptr<LegacyObject> object;
};

struct LegacyInterface final : Interface {
    std::string name;
    std::vector<std::shared_ptr<Function>> functions;
    std::vector<LegacyFibreAttribute> attributes;

    InterfaceInfo* get_info() final;
    void free_info(InterfaceInfo* info) final;
    RichStatusOr<Object*> get_attribute(Object* parent_obj,
                                        size_t attr_id) final;
};

struct LegacyObject {
    Node* node;
    size_t ep_num;
    uint16_t json_crc;
    std::shared_ptr<LegacyInterface> intf;
};

using EndpointClientCallback = Callback<Socket*, uint16_t, uint16_t, std::vector<uint16_t>, std::vector<uint16_t>, Socket*>;

class LegacyObjectClient : public Socket {
public:
    void start(Node* node, Domain* domain_, EndpointClientCallback default_endpoint_client, std::string path);

    std::shared_ptr<LegacyInterface> get_property_interfaces(std::string codec,
                                                              bool write);
    std::shared_ptr<LegacyObject> load_object(json_value list_val);
    void load_json(cbufptr_t json);

    WriteResult write(WriteArgs args) final;
    WriteArgs on_write_done(WriteResult result) final;

    // call endpoint 0
    uint8_t data0[4] = {0x00, 0x00, 0x00, 0x00};

    Node* node_;
    Domain* domain_;
    EndpointClientCallback default_endpoint_client_;
    std::string path_; // TODO: get dynamically from node
    CBufIt tx_pos_;
    std::vector<uint8_t> json_;
    uint16_t json_crc_;
    std::vector<std::shared_ptr<LegacyObject>> objects_;
    std::shared_ptr<LegacyObject> root_obj_ = nullptr;
    Chunk chunks_[2];
    std::unordered_map<std::string, std::shared_ptr<LegacyInterface>>
        rw_property_interfaces;
    std::unordered_map<std::string, std::shared_ptr<LegacyInterface>>
        ro_property_interfaces;
};

template<typename T, typename... TArgs>
T* alloc_ctx(bufptr_t buf, TArgs... args) {
#if FIBRE_ALLOW_HEAP
    return new T{args...};
#else
    if (buf.size() >= sizeof(T)) {
        return new ((T*)buf.begin()) T{args...};
    }
    return nullptr;
#endif
}

template<typename T> void delete_ctx(T* ptr) {
#if FIBRE_ALLOW_HEAP
    delete ptr;
#else
    if (ptr) {
        ptr->~T();
        return;
    }
#endif
}

}  // namespace fibre

#endif  // __FIBRE_LEGACY_OBJECT_MODEL_HPP