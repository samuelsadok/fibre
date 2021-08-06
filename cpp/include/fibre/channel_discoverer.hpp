#ifndef __FIBRE_CHANNEL_DISCOVERER
#define __FIBRE_CHANNEL_DISCOVERER

#include "async_stream.hpp"
#include <fibre/bufchain.hpp>
#include <fibre/status.hpp>
#include <fibre/multiplexer.hpp>
#include <string>

namespace fibre {

class Domain; // defined in domain.hpp
struct Node; // defined in node.hpp
class Logger; // defined in logging.hpp
class EventLoop; // defined in event_loop.hpp
struct RichStatus; // defined in rich_status.hpp
struct TxPipe;

struct ChannelDiscoveryResult {
    Status status;
    AsyncStreamSource* rx_channel;
    AsyncStreamSink* tx_channel;
    size_t mtu;
    bool packetized;
};


struct FrameStreamSink {
    virtual bool open_output_slot(uintptr_t* p_slot_id, Node* dest) = 0;
    virtual bool close_output_slot(uintptr_t slot_id) = 0;
    virtual bool start_write(TxTaskChain tasks) = 0;
    virtual void cancel_write() = 0;

    Multiplexer multiplexer_{this};
};

struct ChannelDiscoveryContext {};

class ChannelDiscoverer {
public:
    // TODO: maybe we should remove "handle" because a discovery can also be
    // uniquely identified by domain.
    virtual void start_channel_discovery(
        Domain* domain,
        const char* specs, size_t specs_len,
        ChannelDiscoveryContext** handle) = 0;
    virtual RichStatus stop_channel_discovery(ChannelDiscoveryContext* handle) = 0;
    virtual RichStatus show_device_dialog();

    static bool try_parse_key(const char* begin, const char* end, const char* key, const char** val_begin, const char** val_end);
    static bool try_parse_key(const char* begin, const char* end, const char* key, int* val);
    static bool try_parse_key(const char* begin, const char* end, const char* key, std::string* val);
};

struct Backend : ChannelDiscoverer {
    virtual ~Backend() {};
    virtual RichStatus init(EventLoop* event_loop, Logger logger) = 0;
    virtual RichStatus deinit() = 0;
};

}

#endif // __FIBRE_CHANNEL_DISCOVERER