
import struct
import itertools
import time
from threading import Thread, Lock
from fibre import Semaphore, EventWaitHandle, InputPipe, SuspendedInputPipe, OutputPipe, SuspendedOutputPipe
import fibre.remote_object

class IndexPool(object):
    """
    Represents a pool of limited resources.
    All public member functions of this class are mutually thread-safe.
    """
    def __init__(self, max_instances):
        self._lock = Lock()
        self._max_instances = max_instances
        self._slots = [None] * max_instances
        self._free_slots = Semaphore(count=max_instances)

    def acquire(self, index, factory):
        """
        Acquires one instance of the resource. If no index is specified, the
        function may block until a slot becomes free.
        In any case, if a new item is created, the factory callback is invoked
        with the item's index as a parameter.

        Parameters:
        index -- the desired index of the resource
        """
        if index is None:
            self._free_slots.acquire() # TODO: allow cancellation
        with self._lock:
            if index is None:
                index = self._slots.index(None)
            item = self._slots[index]
            if item is None:
                item = factory(index)
                self._slots[index] = item
            return item
    
    def release(self, index):
        """Releases the resource at the specified index"""
        with self._lock:
            self._slots[index] = None
        self._free_slots.release()

    def get_active_items(self):
        """
        Returns a generator that returns all active items in the pool.
        If an item is added or removed while the generator is active, it may or
        may not be included in the result.
        """
        for idx in range(len(self._slots)):
            with self._lock:
                item = self._slots[idx]
                if item is None:
                    continue
            yield item


class RemoteNode(object):
    """
    An outbox multiplexes multiple output pipes onto multiple output channels,
    all of which lead to the same destination node.

    Channels can be added and removed as needed.
    All output channels of a specific outbox must lead to the same destination node.
    Only one outbox must be allocated for a given destination node.
    """

    def __init__(self, uuid, logger, n_pipes):
        self._uuid = uuid
        self._logger = logger
        self._server_pipe_pool = IndexPool(n_pipes)
        self._client_pipe_pool = IndexPool(n_pipes)
        self._server_pipe_offsets = [(SuspendedInputPipe(),SuspendedOutputPipe())] * n_pipes # only valid for suspended pipes
        self._client_pipe_offsets = [(SuspendedInputPipe(),SuspendedOutputPipe())] * n_pipes # only valid for suspended pipes
        self._output_pipe_ready = EventWaitHandle(auto_reset=True) # spurious signals possible
        self._output_channel_ready = EventWaitHandle(auto_reset=True) # spurious signals possible
        self._cancellation_token = EventWaitHandle() # triggered in __exit__
        self._scheduler_thread = Thread(target=self._scheduler_thread_loop, name='fibre:sched')
        self._interrogate_thread = Thread(target=self._interrogate, name='fibre:interrogate')
        self._lock = Lock()
        self._output_channels = []

    def __enter__(self):
        """Starts the scheduler thread"""
        self._scheduler_thread.start()
        self._interrogate_thread.start()
        return self
    def __exit__(self, exception_type, exception_value, traceback):
        """Stops the scheduler thread"""
        self._cancellation_token.set()
        self._scheduler_thread.join()
        self._interrogate_thread.join()

    #def create_server_pipe_pair(self, ensure_delivery=True, allow_spurious=False):
    #    return self._server_pipe_pool.acquire(None, lambda idx: (InputPipe(self, idx), OutputPipe(self, idx)))

    def get_logger(self):
        return self._logger

    def get_server_pipe_pair(self, index, **kwargs):
        """
        Returns a pipe pair from the server pipe pool. If the index is not
        specified or points to an inactive slot, a new pipe pair is created.
        The function blocks until the timeout (TODO) is reached.
        """
        return self._server_pipe_pool.acquire(index, lambda idx: (InputPipe(self, idx, self._server_pipe_offsets[idx][0]), OutputPipe(self, idx, self._server_pipe_offsets[idx][1], **kwargs)))

    def get_client_pipe_pair(self, index=None, **kwargs):
        """
        Returns a pipe pair from the client pipe pool. If the index is not
        specified or points to an inactive slot, a new pipe pair is created.
        The function blocks until the timeout (TODO) is reached.
        """
        return self._client_pipe_pool.acquire(index, lambda idx: (InputPipe(self, idx, self._client_pipe_offsets[idx][0]), OutputPipe(self, idx, self._client_pipe_offsets[idx][1], **kwargs)))

    def release_server_pipe_pair(self, index):
        self._server_pipe_pool.release(index)

    def release_client_pipe_pair(self, index):
        pipe_pair = self.get_client_pipe_pair(index)
        # the calls to close block until the pipes are fully flushed
        self._client_pipe_offsets[index] = (pipe_pair[0].close(), pipe_pair[1].close())
        self._client_pipe_pool.release(index)

    #def release_pipe(self, pipe_id):
    #    with self._lock:
    #        pipe = self._active_pipes[pipe_id]
    #        self._active_pipes[pipe_id] = None
    #    #    pipe._data_available_event.unsubscribe(self._set_data_available)
    #    self._free_pipes_sem.release()

    def add_output_channel(self, channel):
        self._output_channels.append(channel)
        if channel.get_min_non_blocking_bytes() != 0:
            self.notify_output_channel_ready()
    
    def remove_output_channel(self, channel):
        # TODO: should we tear down a node if it has no more output channels?
        self._output_channels.remove(channel)

    def notify_output_pipe_ready(self):
        self._logger.debug("output pipe ready")
        self._output_pipe_ready.set()

    def notify_output_channel_ready(self):
        self._output_channel_ready.set()

    def _interrogate(self):
        get_function_json = fibre.remote_object.RemoteFunction(self, 0, 0, ["number"], ["json"])
        json_string = get_function_json(0)
        self._logger.debug("JSON: {}".format(json_string))
        json_string = get_function_json(1)
        self._logger.debug("JSON: {}".format(json_string))

    def _scheduler_thread_loop(self):
        next_pipe_ready_time = None

        while not self._cancellation_token.is_set():
            timeout = None if next_pipe_ready_time is None else (next_pipe_ready_time - time.monotonic() + 0.001)
            self._logger.debug("Will trigger again in {}s".format(timeout))
            try:
                self._output_pipe_ready.wait(timeout=timeout, cancellation_token=self._cancellation_token)
            except TimeoutError:
                pass
            #self._output_channel_ready.wait(cancellation_token=self._cancellation_token)

            next_pipe_ready_time = None

            self._logger.debug(str(len(self._output_channels)))
            for channel in self._output_channels:
                self._logger.debug("handling channel {}".format(channel))
                #packet = bytes()
                per_packet_overhead = 16 + 2 # 16 bytes source UUID + 2 bytes CRC
                per_chunk_overhead = 8
                free_space = channel.get_min_non_blocking_bytes()
                free_space = None if free_space is None else (free_space - per_packet_overhead)
                assert free_space is not None or free_space >= 0

                for pipe_pair in itertools.chain(self._server_pipe_pool.get_active_items(), self._client_pipe_pool.get_active_items()):
                    output_pipe = pipe_pair[1]
                    for chunk in output_pipe.get_pending_chunks():
                        if free_space is not None:
                            chunk.data = chunk.data[:free_space]
                            free_space -= per_chunk_overhead + len(chunk.data)
                        chunk_header = struct.pack('<HHHH', output_pipe.pipe_id, chunk.offset, chunk.crc_init, (len(chunk.data) << 1) | chunk.packet_break)
                        self._logger.debug("pipe {}: emitting chunk {} - {} (crc 0x{:04X})".format(output_pipe.pipe_id, chunk.offset, chunk.offset + len(chunk.data) - 1, chunk.crc_init))
                        channel.process_bytes(chunk_header, timeout=0)
                        channel.process_bytes(chunk.data, timeout=0)

                        if not output_pipe._ensure_delivery:
                            output_pipe.drop_chunk(chunk.offset, len(chunk.data))
                        else:
                            next_due_time = max(output_pipe.get_due_time() + channel.resend_interval, time.monotonic())
                            output_pipe.set_due_time(chunk.offset, len(chunk.data), next_due_time)

                    next_pipe_ready_time = min(next_pipe_ready_time or output_pipe.get_due_time(), output_pipe.get_due_time())
                    #if output_pipe.get_pending_chunks():
                    #    self._output_pipe_ready.set()

                # Dispatch packet
                #channel.process_packet_async(packet)
