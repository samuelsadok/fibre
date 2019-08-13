
import time
import threading

import fibre



class SendState(object):
    NOT_YET_OPEN = 0
    SENDING = 1
    FINISHED = 2

class ReceiveState(object):
    NOT_YET_OPEN = 0
    RECEIVING = 1
    FINISHED = 2

class OutgoingConnection(object):
    def __init__(self, remote_node):
        self._remote_node = remote_node
        self._input_pipe = None
        self._output_pipe = None
        #self._result_decoder_chain = fibre.StreamChain()
        self._response_handler = None
        self._buffered_input_data = []  # buffers incoming data while response_handler is not yet set
        self._send_state = SendState.NOT_YET_OPEN
        self._receive_state = ReceiveState.NOT_YET_OPEN
        self._receive_lock = threading.Lock()
        self._output_pipe_start_pos = 0
        self._output_pipe_end_pos = 0

    def __enter__(self):
        """
        Makes the connection available for sending and receiving data.
        The sending phase is finished as soon as __exit__ is called. The
        receiving phase is finished once the corresponding input packet is
        received or abort() is called.

        A connection instance cannot be used more than once.
        """
        assert(self._send_state <= SendState.NOT_YET_OPEN)
        self._send_state = SendState.SENDING
        assert(self._receive_state <= ReceiveState.NOT_YET_OPEN)
        self._receive_state = ReceiveState.RECEIVING

        self._input_pipe, self._output_pipe = self._remote_node._client_pipe_pool.acquire()
        self._output_pipe_start_pos = self._output_pipe.get_current_pos()
        self._input_pipe.append_packet_handler(self._receive_bytes)
        return self

    def _stop_sending_phase(self):
        """
        Terminates the sending phase of the connection by releasing the output
        pipe.
        """
        if self._send_state < SendState.FINISHED:
            if self._send_state < SendState.SENDING:
                self._output_pipe_start_pos = self._output_pipe.get_current_pos()
            self._send_state = SendState.FINISHED
            self._output_pipe.send_packet_break()
            self._output_pipe_end_pos = self._output_pipe.get_current_pos()
            self._remote_node._client_pipe_pool.release((self._input_pipe, self._output_pipe))

    def _stop_receiving_phase(self):
        """
        Terminates the receiving phase of the connection by closing the response
        handler. _receive_lock must be owned while calling this function.
        """
        if self._receive_state < ReceiveState.FINISHED:
            self._receive_state = ReceiveState.FINISHED
            if not self._response_handler is None:
                self._response_handler.close()

    def __exit__(self, exception_type, exception_value, traceback):
        """
        Terminates the sending phase of the connection and if the exception is
        not None also the receiving phase.
        """
        self._stop_sending_phase()
        if not exception_value is None:
            self.abort()

    def _receive_bytes(self, data, packet_break=False):
        """
        Handles incoming bytes associated with this connection by forwarding
        them to the response_handler (or buffering them). Once reception is
        finished (either because a packet break was received or abort() was
        called), any further call to this function is ignored.
        """
        with self._receive_lock:
            if self._receive_state == ReceiveState.FINISHED:
                return
            assert(self._receive_state == ReceiveState.RECEIVING)

            if self._response_handler:
                self._response_handler.process_bytes(data)
            else:
                self._buffered_input_data += data

            if packet_break:
                self._stop_receiving_phase()

    def set_response_handler(self, response_handler):
        """
        Sets a stream sink that will receive data associated with this connection.
        The stream is closed if either all data is received or if the 
        """
        with self._receive_lock:
            assert(self._response_handler is None)
            self._response_handler = response_handler
            if self._buffered_input_data:
                self._response_handler.process_bytes(self._buffered_input_data)
                self._buffered_input_data = b''
            if self._receive_state == ReceiveState.FINISHED:
                self._response_handler.close()

    def abort(self):
        """
        Calling this function signifies that the client is no longer interested
        in any result that this connection may have. If the sending phase is still
        active it is finished as well. This does not guarantee
        that the remote operation is aborted. The response handler, if set
        and still open, is closed immediately.
        """
        self._stop_sending_phase()
        self._output_pipe.drop_range(self._output_pipe_start_pos, self._output_pipe_end_pos - self._output_pipe_start_pos)
        with self._receive_lock:
            self._stop_receiving_phase()

    #def flush(self):
    #    """
    #    Blocks until all previously emitted values have reached
    #    the remote endpoint. TODO: support fire-and-forget endpoints
    #    """
    #    pass
    #    #self._output_pipe.send_packet_break()


    def _serialize(self, value_type, value):
        # TODO: the selected codec may depend on the endpoint
        format_name = fibre.codecs.canonical_formats[value_type]
        codec = fibre.codecs.get_codec(format_name, type(value))
        encoder = codec[1]()
        return encoder.serialize(value)

    def emit_value(self, value_type, value):
        data = self._serialize(value_type, value)
        self._output_pipe.send_bytes(data)
        return len(data)

    def receive_value(self, value_type):
        assert isinstance(value_type, str)
        format_name = fibre.codecs.canonical_formats[value_type]
        codec = fibre.codecs.get_codec(format_name, None)
        decoder = codec[0]()
        self._result_decoder_chain.append(decoder)
        return decoder.get_future()

