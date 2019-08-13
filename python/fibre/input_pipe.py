
import struct
import threading

import fibre
from fibre import StreamSink

class InputPipe(object):
    def __init__(self, remote_node, pipe_id):
        self._remote_node = remote_node
        self.pipe_id = pipe_id
        self._logger = remote_node.get_logger()
        self._pos = 0
        self._crc = 0x1337
        self._input_handler = None
        self._lock = threading.Lock()
    
    def set_input_handler(self, input_handler):
        self._input_handler = input_handler

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
