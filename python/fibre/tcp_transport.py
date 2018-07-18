
import sys
import socket
import platform
import time
import traceback
import struct
import fcntl
import uuid

import fibre
from fibre import OperationAbortedError
from fibre.threading_utils import wait_any, EventWaitHandle

# Linux ioctl commands
TIOCOUTQ = 0x5411

CANCELLATION_POLL_INTERVAL = 1

RX_PUMP_BUFFER_SIZE = 4192

# Keepalive from https://stackoverflow.com/questions/12248132/how-to-change-tcp-keepalive-timer-using-python-script
def set_keepalive_linux(sock, after_idle_sec, interval_sec, max_fails):
    """Set TCP keepalive on an open socket.

    It activates after 1 second (after_idle_sec) of idleness,
    then sends a keepalive ping once every 3 seconds (interval_sec),
    and closes the connection after 5 failed ping (max_fails), or 15 seconds
    """
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPIDLE, after_idle_sec)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPINTVL, interval_sec)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_KEEPCNT, max_fails)
def set_keepalive_osx(sock, after_idle_sec, interval_sec, max_fails):
    """Set TCP keepalive on an open socket."""
    # scraped from /usr/include, not exported by python's socket module
    TCP_KEEPALIVE = 0x10
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
    sock.setsockopt(socket.IPPROTO_TCP, TCP_KEEPALIVE, interval_sec)
def set_keepalive_windows(sock, after_idle_sec, interval_sec, max_fails):
    sock.ioctl(socket.SIO_KEEPALIVE_VALS, (1, after_idle_sec*1000, interval_sec*1000))

def set_keepalive(sock, after_idle_sec=30, interval_sec=10, max_fails=5):
    """
    Sets the keepalive settings for a socket.
    max_fails only has a effect on Linux
    """
    if platform.system() == 'Windows':
        set_keepalive_windows(sock, after_idle_sec, interval_sec, max_fails)
    elif platform.system() == 'Darwin':
        set_keepalive_osx(sock, after_idle_sec, interval_sec, max_fails)
    else:
        set_keepalive_linux(sock, after_idle_sec, interval_sec, max_fails)


class TCPTransport(fibre.StreamSource, fibre.OutputChannel):
    def __init__(self, sock, name, logger):
        super().__init__(resend_interval=1)
        self._sock = sock
        self._name = name
        self._logger = logger
        set_keepalive(self._sock)
        #self._channel_broken = EventWaitHandle()
        self._kernel_send_buffer_size = self._sock.getsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF)
        self._total_received = 0

    def close(self):
        self._logger.debug("closing " + self._name)
        self._sock.close()

    def _close_and_raise(self):
        self.close()
        #self._channel_broken.set()
        raise fibre.ChannelBrokenException()

    def get_min_non_blocking_bytes(self):
        buf = struct.pack('@l', 0)
        ret = fcntl.ioctl(self._sock, TIOCOUTQ, buf)
        pending_bytes, = struct.unpack('@l', ret)
        if pending_bytes > self._kernel_send_buffer_size:
            raise Exception("lots of pending bytes")
        elif pending_bytes < 0:
            raise Exception("vacuum")
        return self._kernel_send_buffer_size - pending_bytes

    def process_bytes(self, buffer, timeout=None, cancellation_token=None):
        fibre.assert_bytes_type(buffer)
        n_sent = 0
        deadline = None if (timeout is None) else (time.monotonic() + timeout)
        while (n_sent < len(buffer)) and ((cancellation_token is None) or (not cancellation_token.set())):
            now = time.monotonic()
            if deadline is None:
                self._sock.settimeout(CANCELLATION_POLL_INTERVAL)
            elif deadline <= now:
                self._sock.settimeout(0)
            else:
                self._sock.settimeout(min(now - deadline, CANCELLATION_POLL_INTERVAL))
            try:
                n_sent += self._sock.send(buffer[n_sent:])
            except socket.error:
                self._close_and_raise()
        return n_sent

    def get_bytes(self, n_min, n_max, timeout=None, cancellation_token=None):
        """
        Returns n bytes unless the deadline is reached, in which case the bytes
        that were read up to that point are returned. If timeout is None the
        function blocks forever.
        """
        received = b''
        deadline = None if (timeout is None) else (time.monotonic() + timeout)
        first_iteration = True
        while (n_min > len(received)):
            if (cancellation_token is not None) and cancellation_token.is_set():
                raise OperationAbortedError()
            now = time.monotonic()
            if deadline is None:
                self._sock.settimeout(CANCELLATION_POLL_INTERVAL)
            elif deadline <= now:
                self._sock.settimeout(0)
                if not first_iteration:
                    raise TimeoutError()
            else:
                self._sock.settimeout(min(now - deadline, CANCELLATION_POLL_INTERVAL))

            try:
                received += self._sock.recv(n_min - len(received), socket.MSG_WAITALL)
            except socket.timeout:
                # receive everything that is immediately available
                try:
                    self._sock.settimeout(0)
                    received += self._sock.recv(n_min - len(received), 0)
                except socket.timeout:
                    pass
                except socket.error as ex:
                    if ex.errno != 11: # Resource temporarily unavailable
                        self._close_and_raise()
            except socket.error:
                self._close_and_raise()

            first_iteration = False

        if (n_max > n_min):
            try:
                self._sock.settimeout(0)
                received += self._sock.recv(n_max - len(received), socket.MSG_DONTWAIT)
            except socket.error as ex:
                if ex.errno != 11: # Resource temporarily unavailable
                    self._close_and_raise()
        
        self._total_received += len(received)
        return received

    def get_bytes_or_fail(self, n_bytes, deadline):
        result = self.get_bytes(n_bytes, deadline)
        if len(result) < n_bytes:
            raise TimeoutError("expected {} bytes but got only {}".format(n_bytes, len(result)))
        return result

def handle_connection(sock, cancellation_token, logger):
    channel = TCPTransport(sock, str(sock), logger)
    try:
        hand_shake = fibre.global_state.own_uuid.bytes
        logger.debug("sending own UUID")
        channel.process_bytes(hand_shake, timeout=None)
        logger.debug("waiting for remote UUID")
        remote_uuid_buf = channel.get_bytes(16, 16, timeout=None)
        remote_uuid = uuid.UUID(bytes=remote_uuid_buf)
        logger.debug("handshake with {} complete".format(remote_uuid))
        remote_node = fibre.get_remote_node(uuid)
        decoder = fibre.InputChannelDecoder(remote_node)

        remote_node.add_output_channel(channel)
        try:
            while not cancellation_token.is_set():
                n_min = min(decoder.get_min_useful_bytes(), RX_PUMP_BUFFER_SIZE)
                buf = channel.get_bytes(n_min, 4192, timeout=None, cancellation_token=cancellation_token)
                decoder.process_bytes(buf)
        finally:
            remote_node.remove_output_channel(channel)
    finally:
        channel.close()


def discover_channels(path, cancellation_token, logger):
    """
    Tries to connect to a TCP server based on the path spec.
    This function blocks until cancellation_token is set.
    Channels spawned by this function run until channel_termination_token is set.
    """
    try:
        dest_addr = ':'.join(path.split(":")[:-1])
        dest_port = int(path.split(":")[-1])
    except (ValueError, IndexError):
        raise Exception('"{}" is not a valid TCP destination. The format should be something like "localhost:1234".'
                        .format(path))

    while not cancellation_token.is_set():
        logger.debug("TCP discover loop on {}".format(path))

        targets = []
        targets += socket.getaddrinfo(dest_addr, dest_port, socket.AF_INET6, socket.SOCK_STREAM)
        targets += socket.getaddrinfo(dest_addr, dest_port, socket.AF_INET, socket.SOCK_STREAM)
        target = targets[0] # TODO: try targets one by one
        # target is a tuple (address_family, socket_kind, ?, ?, (addr, port))

        try:
            sock = socket.socket(target[0], target[1])
            # TODO: this blocks until a connection is established, or the system cancels it
            sock.connect(target[4])
            handle_connection(sock, cancellation_token, logger)
            #stream2packet_input = fibre.PacketFromStreamConverter(tcp_transport)
            #packet2stream_output = fibre.StreamBasedPacketSink(tcp_transport)
            #channel = fibre.Channel(
            #        "TCP device {}:{}".format(dest_addr, dest_port),
            #        stream2packet_input, packet2stream_output,
            #        channel_termination_token, logger)
        except:
            logger.debug("TCP channel failed. More info: " + traceback.format_exc())
            #pass
            time.sleep(1)
        else:
            pass
            #callback(channel)
            #channel._channel_broken.wait(cancellation_token=cancellation_token)
    logger.debug("TCP discover loop on {} is exiting".format(path))
