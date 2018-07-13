
import sys
import socket
import platform
import time
import traceback
import struct
import fcntl
import fibre.protocol
from fibre.threading_utils import wait_any, EventWaitHandle

# Linux ioctl commands
TIOCOUTQ = 0x5411

CANCELLATION_POLL_INTERVAL = 1


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


class TCPTransport(fibre.protocol.StreamSource, fibre.protocol.OutputChannel):
    def __init__(self, dest_addr, dest_port, logger):
        self._name = "tcp:" + str(dest_addr) + ":" + str(dest_port)
        self._logger = logger

        # TODO: FIXME: use IPv6
        # Problem: getaddrinfo fails if the resolver returns an
        # IPv4 address, but we are using AF_INET6
        #family = socket.AF_INET6 if socket.has_ipv6 else socket.AF_INET
        family = socket.AF_INET
        self.sock = socket.socket(family, socket.SOCK_STREAM)
        # TODO: Determine the right address to use from the list
        self.target = socket.getaddrinfo(dest_addr, dest_port, family)[0][4]
        # TODO: this blocks until a connection is established, or the system cancels it
        self.sock.connect(self.target)
        set_keepalive(self.sock)

        self._channel_broken = EventWaitHandle()
        self.kernel_send_buffer_size_ = self.sock.getsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF)

    def _close_and_raise(self):
        self._logger.debug(self._name + " broke")
        self._channel_broken.set()
        raise fibre.protocol.ChannelBrokenException()

    def get_min_non_blocking_bytes(self):
        buf = struct.pack('@l', 0)
        ret = fcntl.ioctl(self.sock, TIOCOUTQ, buf)
        pending_bytes, = struct.unpack('@l', ret)
        if pending_bytes > self.kernel_send_buffer_size_:
            raise Exception("lots of pending bytes")
        elif pending_bytes < 0:
            raise Exception("vacuum")
        return self.kernel_send_buffer_size_ - pending_bytes

    def process_bytes(self, buffer, timeout=None, cancellation_token=None):
        fibre.assert_bytes_type(buffer)
        n_sent = 0
        deadline = None if (timeout is None) else (time.monotonic() + timeout)
        while (n_sent < len(buffer)) and ((cancellation_token is None) or (not cancellation_token.set())):
            now = time.monotonic()
            if deadline is None:
                self.sock.settimeout(CANCELLATION_POLL_INTERVAL)
            elif deadline <= now:
                self.sock.settimeout(0)
            else:
                self.sock.settimeout(min(now - deadline, CANCELLATION_POLL_INTERVAL))
            try:
                n_sent += self.sock.send(buffer[n_sent:])
            except socket.error:
                self._close_and_raise()
        return n_sent

    def get_bytes(self, n_min, n_max, timeout=None, cancellation_token=None):
        """
        Returns n bytes unless the deadline is reached, in which case the bytes
        that were read up to that point are returned. If deadline is None the
        function blocks forever. A deadline before the current time corresponds
        to non-blocking mode.
        """
        received = []
        deadline = None if (timeout is None) else (time.monotonic() + timeout)
        while (n_min > len(received)) and ((cancellation_token is None) or (not cancellation_token.set())):
            now = time.monotonic()
            if deadline is None:
                self.sock.settimeout(CANCELLATION_POLL_INTERVAL)
            elif deadline <= now:
                self.sock.settimeout(0)
            else:
                self.sock.settimeout(min(now - deadline, CANCELLATION_POLL_INTERVAL))
            try:
                received += self.sock.recv(n_min - len(received), socket.MSG_WAITALL)
            except socket.error:
                self._close_and_raise()
        if (n_max > n_min):
            try:
                received += self.sock.recv(n_max - len(received), socket.MSG_DONTWAIT)
            except socket.error:
                self._close_and_raise()
        return received

        ## convert deadline to seconds (floating point)
        #timeout = None if deadline is None else max(deadline - time.monotonic(), 0)
        #try:
        #    self.sock.settimeout(timeout)
        #    data = self.sock.recv(n_bytes) # receive n_bytes
        #    return data
        #except socket.timeout:
        #    # if we got a timeout data will still be none, so we call recv again
        #    # this time in non blocking state and see if we can get some data
        #    #return self.sock.recv(n_bytes, socket.MSG_DONTWAIT)
        #    #try:
        #    #  return self.sock.recv(n_bytes, socket.MSG_DONTWAIT)
        #    #except socket.timeout:
        #    raise TimeoutError
        #except:
        #    self._close_and_raise()

    def get_bytes_or_fail(self, n_bytes, deadline):
        result = self.get_bytes(n_bytes, deadline)
        if len(result) < n_bytes:
            raise TimeoutError("expected {} bytes but got only {}".format(n_bytes, len(result)))
        return result
  
#  def get_mtu(self):
#    return None # no MTU



def discover_channels(path, serial_number, callback, cancellation_token, channel_termination_token, logger):
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
        try:
            logger.debug("TCP discover loop")
            channel = fibre.tcp_transport.TCPTransport(dest_addr, dest_port, logger)
            hand_shake = fibre.global_state.own_uuid.bytes
            logger.debug("sending own UUID")
            channel.process_bytes(hand_shake, timeout=None)
            logger.debug("waiting for remote UUID")
            channel.get_bytes(16, 16, timeout=None)
            logger.debug("handshake complete")
            #stream2packet_input = fibre.protocol.PacketFromStreamConverter(tcp_transport)
            #packet2stream_output = fibre.protocol.StreamBasedPacketSink(tcp_transport)
            #channel = fibre.protocol.Channel(
            #        "TCP device {}:{}".format(dest_addr, dest_port),
            #        stream2packet_input, packet2stream_output,
            #        channel_termination_token, logger)
        except:
            logger.debug("TCP channel init failed. More info: " + traceback.format_exc())
            #pass
            time.sleep(1)
        else:
            callback(channel)
            channel._channel_broken.wait(cancellation_token=cancellation_token)
