#ifndef __FIBRE_SOCKET_HPP
#define __FIBRE_SOCKET_HPP

#include <fibre/bufchain.hpp>
#include <fibre/status.hpp>

namespace fibre {

struct WriteArgs {
    bool is_busy() {
        return status == kFibreBusy;
    }
    static WriteArgs busy() {
        return {{}, kFibreBusy};
    }

    BufChain buf;
    Status status;
};

struct WriteResult {
    bool is_busy() {
        return status == kFibreBusy;
    }
    static WriteResult busy() {
        return {kFibreBusy};
    }
    Status status;
    CBufIt end;
};

/**
 * @brief Bidirectional socket for layered frame streams.
 *
 * The socket follows push-semantics in both directions, that means the data
 * source writes to the data sink whenever data becomes available.
 */
class Socket {
public:
    /**
     * @brief Writes data to the socket (in its role as a sink).
     *
     * If the socket can handle the request synchronously without blocking the
     * return value indicates until which position the input data could be
     * consumed as well as the error code of the operation.
     *
     * If the socket cannot handle the request immediately it returns an error
     * code of kFibreBusy and the source must not call write() again until the
     * operation completes. Once the request completes (for instance as a result
     * of I/O activity), the actual result will be returned to the originating
     * socket via its on_write_done() function.
     *
     * The mechanism through which two sockets are connected is implementation-
     * specific.
     *
     *
     * If the input consists of more than zero chunks then the sink must either
     * process at least one chunk or return a status different from kFibreOk
     * (or both).
     *
     * If the input consists of zero chunks and the input status is different
     * from kFibreOk then the sink must return a status different from kFibreOk
     * too (usually identical to the input status).
     *
     * If the input consists of zero chunks and the input status is kFibreOk the
     * sink is allowed not to make progress (return kFibreOk), therefore the
     * source should avoid this.
     *
     * Once the sink returns a status other than kFibreOk and kFibreBusy it is
     * considerd closed and must not be written to anymore.
     */
    virtual WriteResult write(WriteArgs args) = 0;

    /**
     * @brief Informs the socket (in its role as a source) that a write
     * operation to a sink socket has completed.
     *
     * If the source can start a new write operation synchronously without
     * blocking it can do so by returning the corresponding status and buffers.
     *
     * If the source cannot start a new write operation synchronously it shall
     * return a status of kFibreBusy.
     *
     * If result holds a status other than kFibreOk (meaning that the sink
     * closed) the source must return a status different from kFibreOk and
     * kFibreBusy.
     */
    virtual WriteArgs on_write_done(WriteResult result) = 0;
};

template<typename T> struct UpfacingSocket : Socket {
    WriteResult write(WriteArgs args) final {
        return static_cast<T*>(this)->downstream_write(args);
    }
    WriteArgs on_write_done(WriteResult result) final {
        return static_cast<T*>(this)->on_upstream_write_done(result);
    }
};

template<typename T> struct DownfacingSocket : Socket {
    WriteResult write(WriteArgs args) final {
        return static_cast<T*>(this)->upstream_write(args);
    }
    WriteArgs on_write_done(WriteResult result) final {
        return static_cast<T*>(this)->on_downstream_write_done(result);
    }
};

struct TwoSidedSocket : UpfacingSocket<TwoSidedSocket>,
                        DownfacingSocket<TwoSidedSocket> {
    Socket* upfacing_socket() {
        return static_cast<UpfacingSocket<TwoSidedSocket>*>(this);
    }
    Socket* downfacing_socket() {
        return static_cast<DownfacingSocket<TwoSidedSocket>*>(this);
    }
    virtual WriteResult downstream_write(WriteArgs args) = 0;
    virtual WriteArgs on_upstream_write_done(WriteResult result) = 0;
    virtual WriteResult upstream_write(WriteArgs args) = 0;
    virtual WriteArgs on_downstream_write_done(WriteResult result) = 0;
};

}  // namespace fibre

#endif  // __FIBRE_SOCKET_HPP