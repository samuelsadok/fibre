
import sys
import socket
import platform
import time
import traceback
import fibre.protocol
from fibre.threading_utils import wait_any

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
    """Set TCP keepalive on an open socket.

    sends a keepalive ping once every 3 seconds (interval_sec)
    """
    # scraped from /usr/include, not exported by python's socket module
    TCP_KEEPALIVE = 0x10
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
    sock.setsockopt(socket.IPPROTO_TCP, TCP_KEEPALIVE, interval_sec)
def set_keepalive_windows(sock, after_idle_sec, interval_sec, max_fails):
    sock.ioctl(socket.SIO_KEEPALIVE_VALS, (1, after_idle_sec*1000, interval_sec*1000))
def set_keepalive(sock, after_idle_sec=30, interval_sec=10, max_fails=5):
  if platform.system() == 'Windows':
    set_keepalive_windows(sock, after_idle_sec, interval_sec, max_fails)
  elif platform.system() == 'Darwin':
    set_keepalive_osx(sock, after_idle_sec, interval_sec, max_fails)
  else:
    set_keepalive_linux(sock, after_idle_sec, interval_sec, max_fails)


class TCPTransport(fibre.protocol.StreamSource, fibre.protocol.StreamSink):
  def __init__(self, dest_addr, dest_port, logger):
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

  def process_bytes(self, buffer):
    self.sock.send(buffer)

  def get_bytes(self, n_bytes, deadline):
    """
    Returns n bytes unless the deadline is reached, in which case the bytes
    that were read up to that point are returned. If deadline is None the
    function blocks forever. A deadline before the current time corresponds
    to non-blocking mode.
    """
    # convert deadline to seconds (floating point)
    timeout = None if deadline is None else max(deadline - time.monotonic(), 0)
    self.sock.settimeout(timeout)
    try:
      data = self.sock.recv(n_bytes, socket.MSG_WAITALL) # receive n_bytes
      return data
    except socket.timeout:
      # if we got a timeout data will still be none, so we call recv again
      # this time in non blocking state and see if we can get some data
      #return self.sock.recv(n_bytes, socket.MSG_DONTWAIT)
      #try:
      #  return self.sock.recv(n_bytes, socket.MSG_DONTWAIT)
      #except socket.timeout:
      raise TimeoutError

  def get_bytes_or_fail(self, n_bytes, deadline):
    result = self.get_bytes(n_bytes, deadline)
    if len(result) < n_bytes:
      raise TimeoutError("expected {} bytes but got only {}".format(n_bytes, len(result)))
    return result
  
  def get_mtu(self):
    return None # no MTU



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
      tcp_transport = fibre.tcp_transport.TCPTransport(dest_addr, dest_port, logger)
      stream2packet_input = fibre.protocol.PacketFromStreamConverter(tcp_transport)
      packet2stream_output = fibre.protocol.StreamBasedPacketSink(tcp_transport)
      channel = fibre.protocol.Channel(
              "TCP device {}:{}".format(dest_addr, dest_port),
              stream2packet_input, packet2stream_output,
              channel_termination_token, logger)
    except:
      logger.debug("TCP channel init failed. More info: " + traceback.format_exc())
      #pass
    else:
      callback(channel)
      channel._channel_broken.wait(cancellation_token=cancellation_token)
    time.sleep(1)
