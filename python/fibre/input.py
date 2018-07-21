
import struct
import threading

import fibre
from fibre import StreamSink

class SuspendedInputPipe(object):
    def __init__(self, offset=0, crc=fibre.crc.CRC16_INIT):
        self.offset = offset
        self.crc = crc

class InputPipe(object):
    def __init__(self, remote_node, pipe_id, suspended_input_pipe):
        self._remote_node = remote_node
        self._logger = remote_node.get_logger()
        self.pipe_id = pipe_id
        self._pos = suspended_input_pipe.offset
        self._crc = suspended_input_pipe.crc
        self._input_handler = None
        self._lock = threading.Lock()
    
    def set_input_handler(self, input_handler):
        self._input_handler = input_handler

    def close(self):
        with self._lock:
            return SuspendedInputPipe(self._pos, self._crc)

    def process_chunk(self, buffer, offset, crc):
        if offset > self._pos:
            self._logger.warn("disjoint chunk reassembly not implemented")
            return

        if offset + len(buffer) <= self._pos:
            self._logger.warn("duplicate data received")
            return

        if offset < self._pos:
            diff = self._pos - offset
            crc = fibre.calc_crc16(crc, buffer[:diff])
            buffer = buffer[diff:]
            offset += diff
        
        if crc != self._crc:
            self._logger.warn("received dangling chunk: expected CRC {:04X} but got {:04X}".format(self._crc, crc))
            return

        if self._input_handler is None:
            self._logger.warn("the pipe {} has no input handler".format(self.pipe_id))
            return

        with self._lock:
            self._input_handler.process_bytes(buffer)
            self._pos = offset + len(buffer)
            self._crc = fibre.calc_crc16(self._crc, buffer)

class InputChannelDecoder(StreamSink):
    def __init__(self, remote_node):
        self._remote_node = remote_node
        self._logger = remote_node.get_logger()
        self._header_buf = b''
        self._in_header = True
        self._pipe_id = 0
        self._chunk_offset = 0
        self._chunk_length = 0
        self._chunk_crc = 0
    
    def process_bytes(self, buffer, timeout=None, cancellation_token=None):
        self._logger.debug("input channel decoder processing {} bytes".format(len(buffer)))
        while buffer:
            if self._in_header:
                remaining_header_length = 8 - len(self._header_buf)
                self._header_buf += buffer[:remaining_header_length]
                buffer = buffer[remaining_header_length:]
                if len(self._header_buf) >= 8:
                    (self._pipe_id, self._chunk_offset, self._chunk_crc, self._chunk_length) = struct.unpack('<HHHH', self._header_buf)
                    self._logger.debug("received chunk header: pipe {}, offset {} - {}, crc {:04X}".format(self._pipe_id, self._chunk_offset, self._chunk_offset + self._chunk_length - 1, self._chunk_crc))

                    if (self._pipe_id & 1):
                        pipe_pair = self._remote_node.get_client_pipe_pair(self._pipe_id >> 1)
                    else:
                        pipe_pair = self._remote_node.get_server_pipe_pair(self._pipe_id >> 1)
                    self._input_pipe = pipe_pair[0]
                    self._in_header = False
                    self._header_buf = b''
            else:
                actual_length = min(self._chunk_length, len(buffer))
                self._input_pipe.process_chunk(buffer[:actual_length], self._chunk_offset, self._chunk_crc)
                self._chunk_crc = fibre.calc_crc16(self._chunk_crc, buffer[:actual_length])
                buffer = buffer[actual_length:]
                self._chunk_offset += actual_length
                self._chunk_length -= actual_length

                if not self._chunk_length:
                    self._in_header = True
                    self._header_buf = b''

    def get_min_useful_bytes(self):
        if self._in_header:
            return 8 - len(self._header_buf)
        else:
            return 1
