#ifndef __FIBRE_ENDPOINT_CONNECTION
#define __FIBRE_ENDPOINT_CONNECTION

#include <fibre/backport/variant.hpp>
#include <fibre/connection.hpp>
#include <cstddef>

namespace fibre {

using Cont0 = WriteArgs;
using Cont1 = WriteResult;
using Cont = std::variant<Cont0, Cont1>;

struct EndpointServerConnection : Connection {
    struct Call : Socket {
        WriteResult write(WriteArgs args) final;
        WriteArgs on_write_done(WriteResult result) final;
        EndpointServerConnection* parent_;
        WriteArgs pending;
        CBufIt footer_pos;
        Socket* socket_;
    };

    EndpointServerConnection(Domain* domain, std::array<uint8_t, 16> tx_call_id)
        : Connection{domain, tx_call_id, 0x01} {}

    WriteArgs on_tx_done(WriteResult result) final;
    WriteResult on_rx(WriteArgs args) final;

    Cont tx_logic(WriteArgs args);
    Cont tx_logic(WriteResult result);
    Cont rx_logic(WriteArgs args);
    Cont rx_logic(WriteResult result);

    void start_endpoint_operation(uint16_t endpoint_id, bool exchange);

    alignas(std::max_align_t) uint8_t call_frame[512];  // TODO: make customizable

    Call call0;  // no call pipelining supported currently

    bool rx_active = false;
    bool tx_active = false;

    uint8_t buf[4];
    size_t buf_offset = 0;

    WriteArgs pending;
    Chunk boundary[1] = {Chunk::frame_boundary(0)};
};

struct EndpointClientConnection : Connection {
    struct Call final : Socket {
        WriteResult write(WriteArgs args) final;
        WriteArgs on_write_done(WriteResult result) final;
        EndpointClientConnection* parent_;
        uint8_t header[4];
        Chunk chunks_[1];
        WriteArgs pending;
        CBufIt header_pos;
        CBufIt footer_pos;
        Socket* caller_;
    };

    EndpointClientConnection(Domain* domain, std::array<uint8_t, 16> tx_call_id)
        : Connection{domain, tx_call_id, 0x00} {}

    Socket* start_call(uint16_t ep_num, uint16_t json_crc,
                       std::vector<uint16_t> in_arg_ep_nums,
                       std::vector<uint16_t> out_arg_ep_nums, Socket* caller);

    WriteArgs on_tx_done(WriteResult result) final;
    WriteResult on_rx(WriteArgs args) final;

    Cont tx_logic(WriteArgs args);
    Cont tx_logic(WriteResult result);
    void tx_loop();
    Cont rx_logic(WriteArgs args);
    Cont rx_logic(WriteResult result);
    void rx_loop(Cont cont);

    std::vector<Call*> tx_queue_;
    std::vector<Call*> rx_queue_;

    WriteArgs pending;
    bool call_closed_ = false;
    Chunk boundary[1] = {Chunk::frame_boundary(0)};
};

}  // namespace fibre

#endif  // __FIBRE_ENDPOINT_CONNECTION