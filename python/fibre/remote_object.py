"""
Provides functions for the discovery of ODrive devices
"""

import sys
import json
import threading
from threading import Lock
import fibre.protocol
import fibre.codecs
from fibre.threading_utils import Semaphore, EventWaitHandle
import time
import struct
import uuid
#from fibre import global_state

class ObjectDefinitionError(Exception):
    pass


class RemoteProperty():
    """
    Used internally by dynamically created objects to translate
    property assignments and fetches into endpoint operations on the
    object's associated channel
    """
    def __init__(self, json_data, parent):
        self._parent = parent
        self.__channel__ = parent.__channel__
        id_str = json_data.get("id", None)
        if id_str is None:
            raise ObjectDefinitionError("unspecified endpoint ID")
        self._id = int(id_str)

        self._name = json_data.get("name", None)
        if self._name is None:
            self._name = "[anonymous]"

        type_str = json_data.get("type", None)
        if type_str is None:
            raise ObjectDefinitionError("unspecified type")

        # Find all codecs that match the type_str and build a dictionary
        # of the form {type1: codec1, type2: codec2}
        eligible_types = {k: v[type_str] for (k,v) in codecs.items() if type_str in v}
        
        if not eligible_types:
            raise ObjectDefinitionError("unsupported codec {}".format(type_str))

        # TODO: better heuristics to select a matching type (i.e. prefer non lossless)
        eligible_types = list(eligible_types.items())
        self._property_type = eligible_types[0][0]
        self._codec = eligible_types[0][1]

        access_mode = json_data.get("access", "r")
        self._can_read = 'r' in access_mode
        self._can_write = 'w' in access_mode

    def get_value(self):
        buffer = self._parent.__channel__.remote_endpoint_operation(self._id, None, True, self._codec.get_length())
        return self._codec.deserialize(buffer)

    def set_value(self, value):
        buffer = self._codec.serialize(value)
        # TODO: Currenly we wait for an ack here. Settle on the default guarantee.
        self._parent.__channel__.remote_endpoint_operation(self._id, buffer, True, 0)

    def dump(self):
        if self._name == "serial_number":
            # special case: serial number should be displayed in hex (TODO: generalize)
            val_str = "{:012X}".format(self.get_value())
        elif self._name == "error":
            # special case: errors should be displayed in hex (TODO: generalize)
            val_str = "0x{:04X}".format(self.get_value())
        else:
            val_str = str(self.get_value())
        return "{} = {} ({})".format(self._name, val_str, self._property_type.__name__)

#class RemoteEndpoint():
#    def __init__(self, outbox, endpoint_id, crc):
#        self.outbox = outbox
#        self.endpoint_id = endpoint_id
#        self.crc = crc

class Chunk():
    def __init__(self, offset, crc_init, data):
        self.offset = offset
        self.crc_init = crc_init
        self.data = data

class OutputPipe(object):
    def __init__(self, pipe_id, exit_func, data_available_event):
        self.pipe_id = pipe_id
        self._exit_func = exit_func
        self._active = False
        self._pending_bytes = b''
        self._pos = 0
        self._crc = 0x1337
        self._data_available_event = data_available_event
    def __enter__(self):
        self._active = True
        return self
    def __exit__(self, exception_type, exception_value, traceback):
        self._exit_func()
        self._active = False
    def send_bytes(self, data):
        fibre.assert_bytes_type(data)
        # TODO: handle zero or multiple endpoints
        self._pending_bytes += data
        #self._pos += len(data)
        #self._crc = fibre.protocol.calc_crc16(self._crc, data)
        self._data_available_event.set()
    def get_chunks(self):
        yield Chunk(self._pos, self._crc, self._pending_bytes)

class Connection(object):
    def __init__(self, remote_node, **kwargs):
        self._output_pipe = remote_node.get_output_pipe(**kwargs)
    def __enter__(self):
        self._output_pipe.__enter__()
        return self
    def __exit__(self, exception_type, exception_value, traceback):
        self._output_pipe.__exit__(exception_type, exception_value, traceback)
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
        while True:
            time.sleep(1)
        pass

class RemoteNode(object):
    """
    An outbox multiplexes multiple output pipes onto multiple output channels,
    all of which lead to the same destination node.

    Channels can be added and removed as needed.
    All output channels of a specific outbox must lead to the same destination node.
    Only one outbox must be allocated for a given destination node.
    """

    def __init__(self, uuid, n_pipes):
        self._uuid = uuid
        self._active_pipes = [None] * n_pipes
        self._free_pipes_sem = Semaphore(count=n_pipes)
        self._data_available = EventWaitHandle(auto_reset=True) # spurious signals possible
        self._channel_available = EventWaitHandle(auto_reset=True) # spurious signals possible
        self._cancellation_token = EventWaitHandle()
        self._scheduler_thread = threading.Thread(target=self._scheduler_thread_loop, name='fibre:sched')
        self._lock = Lock()
        self._output_channels = []

    def __enter__(self):
        """Starts the scheduler thread"""
        self._scheduler_thread.start()
        return self
    def __exit__(self, exception_type, exception_value, traceback):
        """Stops the scheduler thread"""
        self._cancellation_token.set()
        self._scheduler_thread.join()

    def get_output_pipe(self, ensure_delivery=True, allow_spurious=False):
        self._free_pipes_sem.acquire() # TODO: allow cancellation
        with self._lock:
            pipe_id = self._active_pipes.index(None)
            pipe = OutputPipe(pipe_id, lambda: self.release_pipe(pipe_id), self._data_available)
            #pipe._data_available_event.subscribe(self._set_data_available)
            self._active_pipes[pipe_id] = pipe
        return pipe

    def release_pipe(self, pipe_id):
        with self._lock:
            pipe = self._active_pipes[pipe_id]
            self._active_pipes[pipe_id] = None
        #    pipe._data_available_event.unsubscribe(self._set_data_available)
        self._free_pipes_sem.release()

    def add_output_channel(self, channel):
        self._output_channels.append(channel)
    
    def remove_output_channel(self, channel):
        self._output_channels.remove(channel)

    def _scheduler_thread_loop(self):
        while not self._cancellation_token.is_set():
            self._data_available.wait(cancellation_token=self._cancellation_token)
            #self._channel_available.wait(cancellation_token=self._cancellation_token)

            for channel in self._output_channels:
                packet = bytes()
                per_packet_overhead = 16 + 2 # 16 bytes source UUID + 2 bytes CRC
                per_chunk_overhead = 8
                free_space = channel.get_min_non_blocking_bytes()
                free_space = None if free_space is None else (free_space - per_packet_overhead)
                assert free_space is not None or free_space >= 0

                for pipe in self._active_pipes:
                    if pipe is None:
                        continue
                    for chunk in pipe.get_chunks():
                        if free_space is not None:
                            chunk.data = chunk.data[:free_space]
                            free_space -= per_chunk_overhead + len(chunk.data)
                        chunk_header = struct.pack('<HHHH', pipe.pipe_id, chunk.offset, chunk.crc_init, len(chunk.data))
                        print(chunk_header)
                        channel.process_bytes(chunk_header, timeout=0)
                        channel.process_bytes(chunk.data, timeout=0)
                        #if pipe.get_available_bytes():
                        #    self._data_available.set()

                # Dispatch packet
                #channel.process_packet_async(packet)


class RemoteFunction(object):
    """
    Represents a callable function that maps to a function call on a remote node
    """
    def __init__(self, remote_node, endpoint_id, endpoint_hash, input_types, output_types):
        self._remote_node = remote_node
        self._endpoint_id = endpoint_id
        self._endpoint_hash = endpoint_hash
        self._input_types = input_types
        self._output_types = output_types
        #self._lock = Lock()

#    def __init__(self, json_data, parent):
#        self._parent = parent
#        id_str = json_data.get("id", None)
#        if id_str is None:
#            raise ObjectDefinitionError("unspecified endpoint ID")
#        self._trigger_id = int(id_str)
#
#        self._name = json_data.get("name", None)
#        if self._name is None:
#            self._name = "[anonymous]"
#
#        self._inputs = []
#        for param_json in json_data.get("arguments", []) + json_data.get("inputs", []): # TODO: deprecate "arguments" keyword
#            param_json["mode"] = "r"
#            self._inputs.append(RemoteProperty(param_json, parent))
#
#        self._outputs = []
#        for param_json in json_data.get("outputs", []): # TODO: deprecate "arguments" keyword
#            param_json["mode"] = "r"
#            self._outputs.append(RemoteProperty(param_json, parent))

    def __call__(self, *args):
        if (len(self._input_types) != len(args)):
            raise TypeError("expected {} arguments but have {}".format(len(self._input_types), len(args)))
        #with self._lock:
        #    call_instance = self._call_instance
        #    self._call_instance += 1
        with Connection(self._remote_node, ensure_delivery=True, allow_spurious=False) as connection:
            connection.emit_value("number", self._endpoint_id)
            for i, input_type in enumerate(self._input_types):
                connection.emit_value(input_type, args[i])
            connection.flush()
            outputs = []
            for i, output_type in enumerate(self._output_types):
                outputs.append(connection.receive_value(output_type))
        return outputs

    def dump(self):
        return "{}({})".format(self._name, ", ".join("{}: {}".format(x._name, x._property_type.__name__) for x in self._inputs))

class RemoteObject(object):
    """
    Object with functions and properties that map to remote endpoints
    """
    def __init__(self, json_data, parent, channel, printer):
        """
        Creates an object that implements the specified JSON type description by
        communicating over the provided channel
        """
        # Directly write to __dict__ to avoid calling __setattr__ too early
        object.__getattribute__(self, "__dict__")["_remote_attributes"] = {}
        object.__getattribute__(self, "__dict__")["__sealed__"] = False
        # Assign once more to make linter happy
        self._remote_attributes = {}
        self.__sealed__ = False

        self.__channel__ = channel
        self.__parent__ = parent

        # Build attribute list from JSON
        for member_json in json_data.get("members", []):
            member_name = member_json.get("name", None)
            if member_name is None:
                printer("ignoring unnamed attribute")
                continue

            try:
                type_str = member_json.get("type", None)
                if type_str == "object":
                    attribute = RemoteObject(member_json, self, channel, printer)
                elif type_str == "function":
                    attribute = RemoteFunction(member_json, self)
                elif type_str != None:
                    attribute = RemoteProperty(member_json, self)
                else:
                    raise ObjectDefinitionError("no type information")
            except ObjectDefinitionError as ex:
                printer("malformed member {}: {}".format(member_name, str(ex)))
                continue

            self._remote_attributes[member_name] = attribute
            self.__dict__[member_name] = attribute

        # Ensure that from here on out assignments to undefined attributes
        # raise an exception
        self.__sealed__ = True
        channel._channel_broken.subscribe(self._tear_down)

    def dump(self, indent, depth):
        if depth <= 0:
            return "..."
        lines = []
        for key, val in self._remote_attributes.items():
            if isinstance(val, RemoteObject):
                val_str = indent + key + (": " if depth == 1 else ":\n") + val.dump(indent + "  ", depth - 1)
            else:
                val_str = indent + val.dump()
            lines.append(val_str)
        return "\n".join(lines)

    def __str__(self):
        return self.dump("", depth=2)

    def __repr__(self):
        return self.__str__()

    def __getattribute__(self, name):
        attr = object.__getattribute__(self, "_remote_attributes").get(name, None)
        if isinstance(attr, RemoteProperty):
            if attr._can_read:
                return attr.get_value()
            else:
                raise Exception("Cannot read from property {}".format(name))
        elif attr != None:
            return attr
        else:
            return object.__getattribute__(self, name)
            #raise AttributeError("Attribute {} not found".format(name))

    def __setattr__(self, name, value):
        attr = object.__getattribute__(self, "_remote_attributes").get(name, None)
        if isinstance(attr, RemoteProperty):
            if attr._can_write:
                attr.set_value(value)
            else:
                raise Exception("Cannot write to property {}".format(name))
        elif not object.__getattribute__(self, "__sealed__") or name in object.__getattribute__(self, "__dict__"):
            object.__getattribute__(self, "__dict__")[name] = value
        else:
            raise AttributeError("Attribute {} not found".format(name))

    def _tear_down(self):
        # Clear all remote members
        for k in self._remote_attributes.keys():
            self.__dict__.pop(k)
        self._remote_attributes = {}
