
from enum import Enum
import abc
from fibre.non_trivial_imports import ABC

class StreamStatus(Enum):
    OK = 0
    BUSY = 1
    CLOSED = 2

class StreamSource(ABC):
    @abc.abstractmethod
    def get_bytes(self, n_min, n_max, timeout=None, cancellation_token=None):
        pass

class StreamSink(ABC):
    @abc.abstractmethod
    def process_bytes(self, buffer, timeout=None, cancellation_token=None):
        """
        Processes the provided buffer.
        Returns the number of processed bytes.
        """
        pass
    def get_min_useful_bytes(self):
        return 1
    def get_min_non_blocking_bytes(self):
        return 0

class PacketSource(ABC):
    @abc.abstractmethod
    def get_packet(self, deadline):
        pass

class PacketSink(ABC):
    @abc.abstractmethod
    def get_mtu(self):
        pass

    @abc.abstractmethod
    def process_packet(self, packet):
        pass

class StreamChain(StreamSink):
    def __init__(self):
        self._streams = []

    def append(self, stream):
        self._streams.append(stream)

    def process_bytes(self, buffer):
        while buffer and self._streams:
            status, buffer = self._streams[0].process_bytes(buffer)
            if status != StreamStatus.CLOSED:
                return status, buffer
            self._streams = self._streams[1:]

        if self._streams:
            return StreamStatus.OK, buffer
        else:
            return StreamStatus.CLOSED, buffer

#class StreamToPacketSegmenter(StreamSink):
#    def __init__(self, output):
#        self._header = []
#        self._packet = []
#        self._packet_length = 0
#        self._output = output
#
#    def process_bytes(self, bytes):
#        """
#        Processes an arbitrary number of bytes. If one or more full packets are
#        are received, they are sent to this instance's output PacketSink.
#        Incomplete packets are buffered between subsequent calls to this function.
#        """
#
#        for byte in bytes:
#            if (len(self._header) < 3):
#                # Process header byte
#                self._header.append(byte)
#                if (len(self._header) == 1) and (self._header[0] != SYNC_BYTE):
#                    self._header = []
#                elif (len(self._header) == 2) and (self._header[1] & 0x80):
#                    self._header = [] # TODO: support packets larger than 128 bytes
#                elif (len(self._header) == 3) and calc_crc8(CRC8_INIT, self._header):
#                    self._header = []
#                elif (len(self._header) == 3):
#                    self._packet_length = self._header[1] + 2
#            else:
#                # Process payload byte
#                self._packet.append(byte)
#
#            # If both header and packet are fully received, hand it on to the packet processor
#            if (len(self._header) == 3) and (len(self._packet) == self._packet_length):
#                if calc_crc16(CRC16_INIT, self._packet) == 0:
#                    self._output.process_packet(self._packet[:-2])
#                self._header = []
#                self._packet = []
#                self._packet_length = 0


#class StreamBasedPacketSink(PacketSink):
#    def __init__(self, output):
#        self._output = output
#
#    def process_packet(self, packet):
#        if (len(packet) >= MAX_PACKET_SIZE):
#            raise NotImplementedError("packet larger than 127 currently not supported")
#
#        header = bytearray()
#        header.append(SYNC_BYTE)
#        header.append(len(packet))
#        header.append(calc_crc8(CRC8_INIT, header))
#
#        self._output.process_bytes(header)
#        self._output.process_bytes(packet)
#
#        # append CRC in big endian
#        crc16 = calc_crc16(CRC16_INIT, packet)
#        self._output.process_bytes(struct.pack('>H', crc16))
#    
#    def get_mtu(self):
#        return None # infinite MTU

#class PacketFromStreamConverter(PacketSource):
#    def __init__(self, input):
#        self._input = input
#    
#    def get_packet(self, deadline):
#        """
#        Requests bytes from the underlying input stream until a full packet is
#        received or the deadline is reached, in which case None is returned. A
#        deadline before the current time corresponds to non-blocking mode.
#        """
#        while True:
#            header = bytes()
#
#            # TODO: sometimes this call hangs, even though the device apparently sent something
#            header = header + self._input.get_bytes_or_fail(1, deadline)
#            if (header[0] != SYNC_BYTE):
#                #print("sync byte mismatch")
#                continue
#
#            header = header + self._input.get_bytes_or_fail(1, deadline)
#            if (header[1] & 0x80):
#                #print("packet too large")
#                continue # TODO: support packets larger than 128 bytes
#
#            header = header + self._input.get_bytes_or_fail(1, deadline)
#            if calc_crc8(CRC8_INIT, header) != 0:
#                #print("crc8 mismatch")
#                continue
#
#            packet_length = header[1] + 2
#            #print("wait for {} bytes".format(packet_length))
#            packet = self._input.get_bytes_or_fail(packet_length, deadline)
#            if calc_crc16(CRC16_INIT, packet) != 0:
#                #print("crc16 mismatch")
#                continue
#            return packet[:-2]
