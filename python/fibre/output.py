
import time

import fibre

class OutputChannel(fibre.StreamSink):
    def __init__(self, resend_interval):
        self.resend_interval = resend_interval
    #@abc.abstractmethod
    #def get_properties(self, parameter_list):
    #    pass


class Chunk():
    def __init__(self, offset, crc_init, data):
        self.offset = offset
        self.crc_init = crc_init
        self.data = data

class OutputPipe(object):
    def __init__(self, remote_node, pipe_id, ensure_delivery):
        self._remote_node = remote_node
        self.pipe_id = pipe_id
        self._pending_bytes = b''
        self._pos = 0
        self._crc = 0x1337
        self._next_due_time = 0

        self._ensure_delivery = ensure_delivery

    def send_bytes(self, data):
        fibre.assert_bytes_type(data)
        # TODO: handle zero or multiple endpoints
        self._pending_bytes += data
        #self._pos += len(data)
        #self._crc = fibre.protocol.calc_crc16(self._crc, data)
        self._remote_node.notify_output_pipe_ready()

    def get_pending_chunks(self):
        if time.monotonic() >= self._next_due_time:
            yield Chunk(self._pos, self._crc, self._pending_bytes)
    
    def drop_chunk(self, offset, length):
        if (offset > self._pos):
            return
        if (offset + length <= self._pos):
            return
        if (offset < self._pos):
            offset += self._pos - offset
            length -= self._pos - offset
        if (length > len(self._pending_bytes)):
            return
        
        self._crc = fibre.calc_crc16(self._crc, self._pending_bytes[:length])
        self._pending_bytes = self._pending_bytes[length:]
        self._pos += length
    
    def get_due_time(self):
        return self._next_due_time

    def set_due_time(self, offset, length, next_due_time):
        self._next_due_time = next_due_time

class OutgoingConnection(object):
    def __init__(self, remote_node, **kwargs):
        self._kwargs = kwargs
        self._remote_node = remote_node
        self._input_pipe = None
        self._output_pipe = None
        self._result_decoder_chain = fibre.StreamChain()
    def __enter__(self):
        self._input_pipe, self._output_pipe = self._remote_node.get_client_pipe_pair(**self._kwargs)
        self._input_pipe.set_input_handler(self._result_decoder_chain)
        return self
    def __exit__(self, exception_type, exception_value, traceback):
        #self._output_pipe.__exit__(exception_type, exception_value, traceback)
        self._remote_node.release_client_pipe_pair(self._output_pipe.pipe_id)
    def _serialize(self, value_type, value):
        # TODO: the selected codec may depend on the endpoint
        format_name = fibre.codecs.canonical_formats[value_type]
        codec = fibre.codecs.get_codec(format_name, type(value))
        return codec.serialize(value)
    def flush(self):
        """
        Blocks until all previously emitted values have reached
        the remote endpoint. TODO: support fire-and-forget endpoints
        """
        pass
        #self._output_pipe.send_packet_break()
    def emit_value(self, value_type, value):
        self._output_pipe.send_bytes(self._serialize(value_type, value))
    def receive_value(self, value_type):
        format_name = fibre.codecs.canonical_formats[value_type]
        codec = fibre.codecs.get_codec(format_name, None)
        decoder = codec[0]()
        self._result_decoder_chain.append(decoder)
        return decoder.get_future()

