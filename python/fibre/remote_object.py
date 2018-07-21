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
from fibre import OutgoingConnection

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
        #with OutgoingConnection(self._remote_node, ensure_delivery=True, allow_spurious=False) as connection:
        with OutgoingConnection(self._remote_node, ensure_delivery=True) as connection:
            output_chunk_start = connection._output_pipe._pos
            output_chunk_length = 0

            output_futures = []
            for i, output_type in enumerate(self._output_types):
                output_futures.append(connection.receive_value(output_type))

            output_chunk_length += connection.emit_value("number", self._endpoint_id)
            for i, input_type in enumerate(self._input_types):
                output_chunk_length += connection.emit_value(input_type, args[i])
            connection._output_pipe.send_packet_break()

            outputs = []
            for output in output_futures:
                outputs.append(output.get_value())

            if not outputs:
                raise Exception("zero outputs not yet supported, we use the results as implicit ACKs")
            connection._output_pipe.drop_chunk(output_chunk_start, output_chunk_length)

        if len(outputs) > 1:
            return tuple(outputs)
        elif len(outputs) == 1:
            return outputs[0]
        else:
            return None

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
