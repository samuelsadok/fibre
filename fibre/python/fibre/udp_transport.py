
import sys
import socket
import fibre.protocol
from fibre.core import object_from_channel

def noprint(x):
  pass

class UDPTransport(fibre.protocol.PacketSource, fibre.protocol.PacketSink):
  def __init__(self, dest_addr, dest_port, printer):
    self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    self.dest_addr = dest_addr
    self.dest_port = dest_port

  def process_packet(self, buffer):
    self.sock.sendto(buffer, (self.dest_addr, self.dest_port))

  def get_packet(self, deadline):
    # TODO: implement deadline
    data, addr = self.sock.recvfrom(1024)
    return data



def channel_from_udp_destination(dest_addr, dest_port, printer=noprint, device_stdout=noprint):
    """
    Inits a Fibre Protocol channel from a UDP hostname and port.
    """
    udp_transport = fibre.udp_transport.UDPTransport(dest_addr, dest_port, printer)
    return fibre.protocol.Channel(
            "UDP device {}:{}".format(dest_addr, dest_port),
            udp_transport, udp_transport,
            device_stdout)

def open_udp(destination, printer=noprint, device_stdout=noprint):
  try:
    dest_addr = ':'.join(destination.split(":")[:-1])
    dest_port = int(destination.split(":")[-1])
  except (ValueError, IndexError):
    raise Exception('"{}" is not a valid UDP destination. The format should be something like "localhost:1234".'
                    .format(destination))
  channel = channel_from_udp_destination(dest_addr, dest_port)
  udp_device = object_from_channel(channel, printer)
  return udp_device
