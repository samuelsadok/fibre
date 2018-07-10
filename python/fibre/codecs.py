"""
Provides several standard codecs for several standard data types.
A codec specifies how a certain semantic piece of information is
encoded in raw bytes.
"""

import struct
import fibre.remote_object

codecs = {}

class StructCodec():
    """
    Generic serializer/deserializer based on struct pack
    """
    def __init__(self, struct_format, target_type):
        self._struct_format = struct_format
        self._target_type = target_type
    def get_length(self):
        return struct.calcsize(self._struct_format)
    def serialize(self, value):
        value = self._target_type(value)
        return struct.pack(self._struct_format, value)
    def deserialize(self, buffer):
        value = struct.unpack(self._struct_format, buffer)
        value = value[0] if len(value) == 1 else value
        return self._target_type(value)

#class EndpointRefCodec():
#    """
#    Serializer/deserializer for an endpoint reference
#    """
#    def get_length(self):
#        return struct.calcsize("<HH")
#    def serialize(self, value):
#        if value is None:
#            (ep_id, ep_crc) = (0, 0)
#        elif isinstance(value, fibre.remote_object.RemoteProperty):
#            (ep_id, ep_crc) = (value._id, value.__channel__._interface_definition_crc)
#        else:
#            raise TypeError("Expected value of type RemoteProperty or None but got '{}'. En example for a RemoteProperty is this expression: odrv0.axis0.controller._remote_attributes['pos_setpoint']".format(type(value).__name__))
#        return struct.pack("<HH", ep_id, ep_crc)
#    def deserialize(self, buffer):
#        return struct.unpack("<HH", buffer)

codecs = {
    'i8le': { int: StructCodec("<b", int) },
    'u8le': { int: StructCodec("<B", int) },
    'i16le': { int: StructCodec("<h", int) },
    'u16le': { int: StructCodec("<H", int) },
    'i32le': { int: StructCodec("<i", int) },
    'u32le': { int: StructCodec("<I", int) },
    'i64le': { int: StructCodec("<q", int) },
    'u64le': { int: StructCodec("<Q", int) },
    'bool': { bool: StructCodec("<?", bool) },
    'float': { float: StructCodec("<f", float) }
}

def get_codec(format_name, python_type):
    """
    Returns a suitable codec for a given (format_name, python_type) pair
    """
    return codecs[format_name][python_type]

def get_python_type(format_name):
    """
    Selects a python type for which a codec is available for the specified format
    """
    return list(codecs[format_name])[0][0]

# Which codecs should be assumed active if nothing else has been negotiated
canonical_formats = {
    "number": "i32le",
    "json": "ascii_string",
}

#codecs[fibre.remote_object.RemoteProperty] = {
#    'endpoint_ref': EndpointRefCodec()
#}

