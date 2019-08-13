
import time
import threading
import bisect

import fibre
import interval_list

class OutputChannel(fibre.StreamSink):
    def __init__(self, resend_interval):
        self.resend_interval = resend_interval
    #@abc.abstractmethod
    #def get_properties(self, parameter_list):
    #    pass


class Chunk():
    def __init__(self, offset, data, packet_break):
        self.offset = offset
        self.data = data
        self.packet_break = packet_break

class SuspendedOutputPipe(object):
    def __init__(self, offset=0, crc=fibre.crc.CRC16_INIT):
        self.offset = offset
        self.crc = crc

class OutputDataState(object):
    TO_BE_SENT = 0          # the range shall be sent and was never sent before
    WAIT_RESEND = 1         # the range was sent at least once and a resend may become necessary after a timeout
    SENT = 2                # no further resend of the data is necessary (because it was sent on a lossless channel or explicitly acknowledged by the receiver)
    TO_BE_DROPPED = 3       # the client wants to drop the range but the receiver has not yet been informed
    WAIT_REDROP = 4         # the client wants to drop the range and the receiver was informed, but a resend of the drop request may become necessary after a timeout
    DROPPED = 5             # no further resend of the drop request is necessary (because it was sent on a lossless channel)
    RESPONSE_RECEIVED = 6   # A response from the receiver (or a drop confirmation) was received for this range

class OutputPipe(object):
    def __init__(self, remote_node, pipe_id):
        self._remote_node = remote_node
        self.pipe_id = pipe_id
        self._buffer = b''
        self._buffer_state = interval_list.IntervalList()
        self._pos = 0
        self._sent_until = 0 # indicates how many bytes may have been emitted
        self._packet_breaks = [] # list of integers
        self._lock = threading.Lock()
        #self._crc = 0x1337
        self._next_due_time = 0
        self._packet_break = False

    def send_bytes(self, data, append_packet_break=True):
        fibre.assert_bytes_type(data)
        with self._lock:
            self._buffer_state.set_value(self._pos + len(self._buffer), len(data), OutputDataState.NOT_YET_SENT)
            self._buffer += data
            if append_packet_break:
                self._packet_breaks = self._pos + len(self._buffer)
                self._buffer_state.set_value(self._pos + len(self._buffer), 1, OutputDataState.NOT_YET_SENT)
                self._buffer.append(0)
            self._remote_node.notify_output_pipe_ready()

    def send_packet_break(self):
        """
        Sends a packet break on the pipe. For every packet break on an output
        pipe, the sender shall expect a packet break on the corresponding input
        pipe or a "skip" token that skips the range containing the packet break.

        A packet break increments the pipe position by one.
        """
        self.send_bytes(b'', True)

    def get_pending_chunks(self):
        """
        Returns a generator for all pending data chunks or drop requests that
        are awaiting a resend. If a chunk has data set to "None" it should be
        considered a dropped range.
        """
        packet_break_idx = 0
        while packet_break_idx < len(self._packet_breaks) and self._packet_breaks[packet_break_idx] < self._pos:
            packet_break_idx += 1

        should_resend = time.monotonic() >= self._next_due_time

        for offset, length, state in self._buffer_state:
            if state == OutputDataState.RESPONSE_RECEIVED:
                continue
            assert(offset >= self._pos)

            if state == OutputDataState.NOT_YET_SENT:
                yield_chunk
            elif state == SENT and should_resend:
                yield_chunk
            elif state == ACKD:
                dont_yield_chunk
            
            
            #    yield Chunk(pos, self._buffer[(pos - self._pos):(packet_break - self._pos)], True)
            #elif state == OutputDataState.SENT:
            #    if 

        if time.monotonic() >= self._next_due_time:
            with self._lock:
                pos = self._pos
                for packet_break in self._packet_breaks:
                    yield Chunk(pos, self._buffer[(pos - self._pos):(packet_break - self._pos)], True)
                    pos = packet_break + 1
                if pos - self._pos < len(self._buffer):
                    yield Chunk(pos, self._buffer[(pos - self._pos):], False)
    
    def get_current_pos(self):
        return self._pos + len(self._buffer)

    def drop_range(self, offset, length):
        """
        Requests dropping of the specified range. If the range was already sent,
        this may lead to a drop request being emitted.
        TODO: don't emit drop request if a range was not yet sent.
        """
        subintervals = self._buffer_state.get_intervals(offset, length)
        for i_offset, i_length, i_state in subintervals:
            if i_state in [OutputDataState.TO_BE_SENT, OutputDataState.WAIT_RESEND, OutputDataState.SENT]:
                self._buffer_state.set_value(i_offset, i_length, OutputDataState.TO_BE_DROPPED)

    def did_emit(self, offset, length, was_sent_reliably=False, next_due_time=None):
        """
        Registers the emission of a range of data (of the corresponding drop
        request).
        """
        subintervals = self._buffer_state.get_intervals(offset, length)
        for i_offset, i_length, i_state in subintervals:
            if i_state == OutputDataState.TO_BE_SENT or i_state == OutputDataState.WAIT_RESEND:
                if was_sent_reliably:
                    new_state = OutputDataState.SENT
                else:
                    self._next_due_time = next_due_time
                    new_state = OutputDataState.WAIT_RESEND
            elif i_state == OutputDataState.TO_BE_DROPPED or i_state == OutputDataState.WAIT_REDROP:
                if was_sent_reliably:
                    new_state = OutputDataState.DROPPED
                else:
                    self._next_due_time = next_due_time
                    new_state = OutputDataState.WAIT_REDROP
            else:
                # Ignore send event for all other states
                continue
            
            self._buffer_state.set_value(i_offset, i_length, new_state)

    def did_receive_response(self, offset, length):
        """
        Registers the reception of a response (or drop confirmation) for the specified range.
        """
        self._buffer_state.set_value(offset, length, OutputDataState.RESPONSE_RECEIVED)

    #def did_receive_response_packet(self):
    #    """
    #    Registers the reception of a response packet.
    #    """
    #    self._buffer_state.set_value(offset, length, OutputDataState.RESPONSE_RECEIVED)
    
    def get_due_time(self):
        return self._next_due_time

    def set_due_time(self, offset, length, next_due_time):
        self._next_due_time = next_due_time
