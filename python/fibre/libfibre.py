#!/bin/python

from ctypes import *
import asyncio
import os
from itertools import count, takewhile
import struct
from types import MethodType
import concurrent
import threading
import time
import platform
from .utils import Logger, Event
import sys

# Enable this for better tracebacks in some cases
#import tracemalloc
#tracemalloc.start(10)

lib_names = {
    ('Linux', 'x86_64'): 'libfibre-linux-amd64.so',
    ('Linux', 'armv7l'): 'libfibre-linux-armhf.so',
    ('Linux', 'aarch64'): 'libfibre-linux-aarch64.so',
    ('Windows', 'AMD64'): 'libfibre-windows-amd64.dll',
    ('Darwin', 'x86_64'): 'libfibre-macos-x86.dylib'
}

system_desc = (platform.system(), platform.machine())

script_dir = os.path.dirname(os.path.realpath(__file__))
fibre_cpp_paths = [
    os.path.join(os.path.dirname(os.path.dirname(script_dir)), "cpp"),
    os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(script_dir)))), "Firmware", "fibre-cpp")
]

def get_first(lst, predicate, default):
    for item in lst:
        if predicate(item):
            return item
    return default

if not system_desc in lib_names:
    raise ModuleNotFoundError(("libfibre is not supported on your platform ({} {}). "
            "Go to https://github.com/samuelsadok/fibre-cpp/tree/devel for "
            "instructions on how to compile libfibre. Once you have compiled it, "
            "add it to this folder.").format(*system_desc))

lib_name = lib_names[system_desc]
search_paths = fibre_cpp_paths + [script_dir]

lib_path = get_first(
    (os.path.join(p, lib_name) for p in search_paths),
    os.path.isfile, None)

if lib_path is None:
    raise ModuleNotFoundError("{} was not found in {}".format(lib_name, search_paths))

if os.path.getsize(lib_path) < 1000:
    raise ModuleNotFoundError("{} is too small. Did you forget to init git lfs? Try this:\n"
        " 1. Install git lfs (https://git-lfs.github.com/)\n"
        " 2. Run `cd {}`\n"
        " 3. Run `git lfs install`\n"
        " 4. Run `git lfs pull`".format(lib_path, os.path.dirname(lib_path)))

if os.name == 'nt':
  dll_dir = os.path.dirname(lib_path)
  try:
    # New way in python 3.8+
    os.add_dll_directory(dll_dir)
  except:
    os.environ['PATH'] = dll_dir + os.pathsep + os.environ['PATH']
  lib = windll.LoadLibrary(lib_path)
else:
  lib = cdll.LoadLibrary(lib_path)

# libfibre definitions --------------------------------------------------------#

PostSignature = CFUNCTYPE(c_int, CFUNCTYPE(None, c_void_p), POINTER(c_int))
RegisterEventSignature = CFUNCTYPE(c_int, c_int, c_uint32, CFUNCTYPE(None, c_void_p, c_int), POINTER(c_int))
DeregisterEventSignature = CFUNCTYPE(c_int, c_int)
OpenTimerSignature = CFUNCTYPE(c_int, POINTER(c_void_p), CFUNCTYPE(None, c_void_p), POINTER(c_int))
SetTimerSignature = CFUNCTYPE(c_int, c_void_p, c_float, c_int)
CloseTimerSignature = CFUNCTYPE(c_int, c_void_p)

LogSignature = CFUNCTYPE(None, c_void_p, c_char_p, c_uint, c_int, c_size_t, c_size_t, c_char_p)

OnFoundObjectSignature = CFUNCTYPE(None, c_void_p, c_void_p, c_void_p, c_char_p, c_size_t)
OnLostObjectSignature = CFUNCTYPE(None, c_void_p, c_void_p)
OnStoppedSignature = CFUNCTYPE(None, c_void_p, c_int)

OnTxCompletedSignature = CFUNCTYPE(None, c_void_p, c_void_p, c_int, c_void_p)
OnRxCompletedSignature = CFUNCTYPE(None, c_void_p, c_void_p, c_int, c_void_p)

kFibreOk = 0
kFibreBusy = 1
kFibreCancelled = 2
kFibreClosed = 3
kFibreInvalidArgument = 4
kFibreInternalError = 5
kFibreProtocolError = 6
kFibreHostUnreachable = 7

kStartCall = 0
kWrite = 1
kWriteDone = 2

class LibFibreVersion(Structure):
    _fields_ = [
        ("major", c_uint16),
        ("minor", c_uint16),
        ("patch", c_uint16),
    ]

    def __repr__(self):
        return "{}.{}.{}".format(self.major, self.minor, self.patch)

class LibFibreEventLoop(Structure):
    _fields_ = [
        ("post", PostSignature),
        ("register_event", RegisterEventSignature),
        ("deregister_event", DeregisterEventSignature),
        ("open_timer", OpenTimerSignature),
        ("set_timer", SetTimerSignature),
        ("close_timer", CloseTimerSignature),
    ]

class LibFibreLogger(Structure):
    _fields_ = [
        ("verbosity", c_int),
        ("log", LogSignature),
        ("ctx", c_void_p),
    ]

class LibFibreFunctionInfo(Structure):
    _fields_ = [
        ("name", c_char_p),
        ("name_length", c_size_t),
        ("input_names", POINTER(c_char_p)),
        ("input_codecs", POINTER(c_char_p)),
        ("output_names", POINTER(c_char_p)),
        ("output_codecs", POINTER(c_char_p)),
    ]

class LibFibreAttributeInfo(Structure):
    _fields_ = [
        ("name", c_char_p),
        ("name_length", c_size_t),
        ("intf", c_void_p),
    ]

class LibFibreInterfaceInfo(Structure):
    _fields_ = [
        ("name", c_char_p),
        ("name_length", c_size_t),
        ("attributes", POINTER(LibFibreAttributeInfo)),
        ("n_attributes", c_size_t),
        ("functions", POINTER(c_void_p)),
        ("n_functions", c_size_t),
    ]

class LibFibreChunk(Structure):
    _fields_ = [
        ("layer", c_uint8),
        ("begin", c_void_p),
        ("end", c_void_p),
    ]

class LibFibreStartCallTask(Structure):
    _fields_ = [
        ("func", c_void_p),
        ("domain", c_void_p),
    ]

class LibFibreWriteTask(Structure):
    _fields_ = [
        ("b_begin", c_void_p),
        ("c_begin", POINTER(LibFibreChunk)),
        ("c_end", POINTER(LibFibreChunk)),
        ("elevation", c_int8),
        ("status", c_int),
    ]

class LibFibreWriteDoneTask(Structure):
    _fields_ = [
        ("status", c_int),
        ("c_end", POINTER(LibFibreChunk)),
        ("b_end", c_char_p),
    ]

class LibFibreTaskContent(Union):
    _fields_ = [
        ("start_call", LibFibreStartCallTask),
        ("write", LibFibreWriteTask),
        ("write_done", LibFibreWriteDoneTask),
    ]

class LibFibreTask(Structure):
    _fields_ = [
        ("type", c_int),
        ("handle", c_size_t),
        ("content", LibFibreTaskContent),
    ]

OnRunTasksSignature = CFUNCTYPE(None, c_void_p, POINTER(LibFibreTask), c_size_t, POINTER(POINTER(LibFibreTask)), POINTER(c_size_t))



libfibre_get_version = lib.libfibre_get_version
libfibre_get_version.argtypes = []
libfibre_get_version.restype = POINTER(LibFibreVersion)

version = libfibre_get_version().contents
if (version.major, version.minor) != (0, 3):
    raise Exception("Incompatible libfibre version: {}".format(version))

libfibre_open = lib.libfibre_open
libfibre_open.argtypes = [LibFibreEventLoop, OnRunTasksSignature, LibFibreLogger]
libfibre_open.restype = c_void_p

libfibre_close = lib.libfibre_close
libfibre_close.argtypes = [c_void_p]
libfibre_close.restype = None

libfibre_open_domain = lib.libfibre_open_domain
libfibre_open_domain.argtypes = [c_void_p, c_char_p, c_size_t]
libfibre_open_domain.restype = c_void_p

libfibre_close_domain = lib.libfibre_close_domain
libfibre_close_domain.argtypes = [c_void_p]
libfibre_close_domain.restype = None

libfibre_start_discovery = lib.libfibre_start_discovery
libfibre_start_discovery.argtypes = [c_void_p, c_void_p, OnFoundObjectSignature, OnLostObjectSignature, OnStoppedSignature, c_void_p]
libfibre_start_discovery.restype = None

libfibre_stop_discovery = lib.libfibre_stop_discovery
libfibre_stop_discovery.argtypes = [c_void_p]
libfibre_stop_discovery.restype = None

libfibre_get_function_info = lib.libfibre_get_function_info
libfibre_get_function_info.argtypes = [c_void_p]
libfibre_get_function_info.restype = POINTER(LibFibreFunctionInfo)

libfibre_free_function_info = lib.libfibre_free_function_info
libfibre_free_function_info.argtypes = [POINTER(LibFibreFunctionInfo)]
libfibre_free_function_info.restype = None

libfibre_get_interface_info = lib.libfibre_get_interface_info
libfibre_get_interface_info.argtypes = [c_void_p]
libfibre_get_interface_info.restype = POINTER(LibFibreInterfaceInfo)

libfibre_free_interface_info = lib.libfibre_free_interface_info
libfibre_free_interface_info.argtypes = [POINTER(LibFibreInterfaceInfo)]
libfibre_free_interface_info.restype = None

libfibre_get_attribute = lib.libfibre_get_attribute
libfibre_get_attribute.argtypes = [c_void_p, c_void_p, c_size_t, POINTER(c_void_p)]
libfibre_get_attribute.restype = c_int

libfibre_run_tasks = lib.libfibre_run_tasks
libfibre_run_tasks.argtypes = [c_void_p, POINTER(LibFibreTask), c_size_t, POINTER(POINTER(LibFibreTask)), POINTER(c_size_t)]
libfibre_run_tasks.restype = None

# DEPRECATED
# TODO: remove
#libfibre_start_tx = lib.libfibre_start_tx
#libfibre_start_tx.argtypes = [c_void_p, c_char_p, c_size_t, OnTxCompletedSignature, c_void_p]
#libfibre_start_tx.restype = None
#
#libfibre_cancel_tx = lib.libfibre_cancel_tx
#libfibre_cancel_tx.argtypes = [c_void_p]
#libfibre_cancel_tx.restype = None
#
#libfibre_start_rx = lib.libfibre_start_rx
#libfibre_start_rx.argtypes = [c_void_p, c_char_p, c_size_t, OnRxCompletedSignature, c_void_p]
#libfibre_start_rx.restype = None
#
#libfibre_cancel_rx = lib.libfibre_cancel_rx
#libfibre_cancel_rx.argtypes = [c_void_p]
#libfibre_cancel_rx.restype = None


c_size_max = (1 << (8 * sizeof(c_size_t))) - 1

# libfibre wrapper ------------------------------------------------------------#

class ObjectLostError(Exception):
    def __init__(self):
        super(Exception, self).__init__("the object disappeared")

def _get_exception(status):
    if status == kFibreOk:
        return None
    elif status == kFibreCancelled:
        return asyncio.CancelledError()
    elif status == kFibreClosed:
        return EOFError()
    elif status == kFibreInvalidArgument:
        return ArgumentError()
    elif status == kFibreInternalError:
        return Exception("internal libfibre error")
    elif status == kFibreProtocolError:
        return Exception("peer misbehaving")
    elif status == kFibreHostUnreachable:
        return ObjectLostError()
    else:
        return Exception("unknown libfibre error {}".format(status))

class StructCodec():
    """
    Generic serializer/deserializer based on struct pack
    """
    def __init__(self, struct_format, target_type):
        self._struct_format = struct_format
        self._target_type = target_type
    def get_length(self):
        return struct.calcsize(self._struct_format)
    def serialize(self, libfibre, value):
        value = self._target_type(value)
        return struct.pack(self._struct_format, value)
    def deserialize(self, libfibre, buffer):
        value = struct.unpack(self._struct_format, buffer)
        value = value[0] if len(value) == 1 else value
        return self._target_type(value)

class ObjectPtrCodec():
    """
    Serializer/deserializer for an object reference

    libfibre transcodes object references internally from/to something that can
    be sent over the wire and understood by the remote instance.
    """
    def get_length(self):
        return struct.calcsize("P")
    def serialize(self, libfibre, value):
        if value is None:
            return struct.pack("P", 0)
        elif isinstance(value, RemoteObject):
            assert(value._obj_handle) # Cannot serialize reference to a lost object
            return struct.pack("P", value._obj_handle)
        else:
            raise TypeError("Expected value of type RemoteObject or None but got '{}'. An example for a RemoteObject is this expression: odrv0.axis0.controller._input_pos_property".format(type(value).__name__))
    def deserialize(self, libfibre, buffer):
        handle = struct.unpack("P", buffer)[0]
        return None if handle == 0 else libfibre._objects[handle]


codecs = {
    'int8': StructCodec("<b", int),
    'uint8': StructCodec("<B", int),
    'int16': StructCodec("<h", int),
    'uint16': StructCodec("<H", int),
    'int32': StructCodec("<i", int),
    'uint32': StructCodec("<I", int),
    'int64': StructCodec("<q", int),
    'uint64': StructCodec("<Q", int),
    'bool': StructCodec("<?", bool),
    'float': StructCodec("<f", float),
    'object_ref': ObjectPtrCodec()
}

def decode_arg_list(arg_names, codec_names):
    for i in count(0):
        if arg_names[i] is None or codec_names[i] is None:
            break
        arg_name = arg_names[i].decode('utf-8')
        codec_name = codec_names[i].decode('utf-8')
        if not codec_name in codecs:
            raise Exception("unsupported codec {}".format(codec_name))
        yield arg_name, codec_name, codecs[codec_name]

def insert_with_new_id(dictionary, val):
    key = next(x for x in count(1) if x not in set(dictionary.keys()))
    dictionary[key] = val
    return key

# Runs a function on a foreign event loop and blocks until the function is done.
def run_coroutine_threadsafe(loop, func):
    future = concurrent.futures.Future()
    async def func_async():
        try:
            result = func()
            if hasattr(result, '__await__'):
                result = await result
            future.set_result(result)
        except Exception as ex:
            future.set_exception(ex)
    loop.call_soon_threadsafe(asyncio.ensure_future, func_async())
    return future.result()

def customresize(array, new_size):
    # The no-copy resize doesn't work, see
    # https://stackoverflow.com/questions/919369/resize-ctypes-array#comment119544773_919501
    # resize(array, sizeof(array._type_) * new_size)
    # return (array._type_ * new_size).from_address(addressof(array))
    return (array._type_ * new_size)(*array)

class TxSocket():
    def __init__(self, call, handle):
        self._call = call
        self._handle = handle
        self._future = None
        self._buf = None
        self._chunks = None
        self._b_begin = 0
        self._c_begin = 0

    def _c_end(self):
        return cast(addressof(self._chunks) + sizeof(LibFibreChunk) * len(self._chunks), POINTER(LibFibreChunk))

    def write_all(self, buf, status):
        assert(self._future is None)
        self._future = self._call._libfibre.loop.create_future()

        # Retain a reference to the buffers to prevent them from being garbage collected
        self._buf = buf
        self._chunks = (LibFibreChunk * len(buf))()
        self._c_begin = cast(addressof(self._chunks), POINTER(LibFibreChunk))
        self._status = status

        # Convert python Chunk list to LibFibreChunk array
        for i, chunk in enumerate(buf):
            if chunk.is_frame_boundary():
                self._chunks[i] = LibFibreChunk(chunk.layer, 0, c_size_max)
            else:
                ptr = cast(c_char_p(chunk.buffer), c_void_p).value
                self._chunks[i] = LibFibreChunk(chunk.layer, ptr, ptr + len(chunk.buffer))

        self._b_begin = self._chunks[0].begin if len(self._chunks) else 0

        task = LibFibreTask(kWrite, self._handle)
        task.content.write = LibFibreWriteTask(self._b_begin, self._c_begin, self._c_end(), 0, self._status)
        self._call._libfibre.enqueue_task(task)

        return self._future

    def _on_write_done(self, result):
        self._b_begin = result.c_end
        self._c_begin = result.c_end

        if self._c_begin != self._c_end() and result.status == self._status:
            self._future.set_result(True)
        elif result.status != kFibreOk:
            self._future.set_exception(_get_exception(result.status))
        else:
            # If there are more chunks enqueued, continue right away
            task = LibFibreTask(kWrite, self._handle)
            task.content.write = LibFibreWriteTask(self._b_begin, self._c_begin, self._c_end(), 0, self._status)
            self._call._libfibre.enqueue_task(task)

        if result.status != kFibreOk:
            self._buf = None
            self._chunks = None
            self._call._maybe_close(1)

class RxSocket():
    def __init__(self, call, handle):
        self._call = call
        self._handle = handle
        self._future = None

    def read_all(self):
        self._buf = []
        self._future = self._call._libfibre.loop.create_future()
        return self._future

    def _on_write(self, args):
        # TODO: implement blocking operation while no Python client is listening
        c_begin = cast(args.c_begin, c_void_p).value or 0
        c_end = cast(args.c_end, c_void_p).value or 0
        chunks = (LibFibreChunk * int((c_end - c_begin) / sizeof(LibFibreChunk))).from_address(c_begin)
        assert(not self._future is None)

        # Convert LibFibreChunk array to python Chunk list
        buf = [0] * len(chunks)
        for i, c in enumerate(chunks):
            length = (cast(c.end, c_void_p).value or 0) - (cast(c.begin, c_void_p).value or 0)
            if length == c_size_max:
                buf[i] = Chunk(c.layer + args.elevation, None)
            else:
                buf[i] = Chunk(c.layer + args.elevation, bytes((c_uint8 * length).from_address(cast(c.begin, c_void_p).value)))

        self._buf += buf

        # If there are more chunks enqueued, continue right away
        task = LibFibreTask(kWriteDone, self._handle)
        task.content.write_done = LibFibreWriteDoneTask(args.status, args.c_end, 0)
        self._call._libfibre.enqueue_task(task)

        if args.status != kFibreOk:
            self._call._maybe_close(0)

        if args.status == kFibreClosed:
            self._future.set_result(self._buf)
        elif args.status != kFibreOk:
            self._future.set_exception(_get_exception(args.status))

class Call(object):
    def __init__(self, libfibre):
        self._libfibre = libfibre
        self._tx_socket = TxSocket(self, id(self))
        self._rx_socket = RxSocket(self, id(self))

    def _maybe_close(self, idx):
        if idx == 0:
            self._rx_socket = None
        elif idx == 1:
            self._tx_socket = None
        if self._rx_socket is None and self._tx_socket is None:
            self._libfibre._calls.pop(id(self))

class Chunk(object):
    def __init__(self, layer, buffer):
        self.layer = layer
        self.buffer = buffer

    def is_frame_boundary(self):
        return self.buffer is None

class RemoteFunction(object):
    """
    Represents a callable function that maps to a function call on a remote object.
    """
    def __init__(self, libfibre, name, func_handle, inputs, outputs):
        self._libfibre = libfibre
        self._name = name
        self._func_handle = func_handle
        self._inputs = inputs
        self._outputs = outputs
        self._rx_size = sum(codec.get_length() for _, _, codec in self._outputs)

    def start(self):
        call = Call(self._libfibre)
        self._libfibre._calls[id(call)] = call
        task = LibFibreTask(kStartCall, id(call))
        task.content.start_call = LibFibreStartCallTask(self._func_handle, 0) # TODO: set domain handle
        self._libfibre.enqueue_task(task)
        return call._tx_socket, call._rx_socket

    async def async_call(self, args, cancellation_token):
        #print("making call on " + hex(args[0]._obj_handle))
        tx_chunks = [0] * (2 * len(self._inputs))
        for i, arg in enumerate(self._inputs):
            tx_chunks[2 * i] = Chunk(0, arg[2].serialize(self._libfibre, args[i]))
            tx_chunks[2 * i + 1] = Chunk(0, None)

        rx_buf = bytes()

        tx_socket, rx_socket = self.start()

        rx_future = rx_socket.read_all()
        await tx_socket.write_all(tx_chunks, kFibreClosed)
        rx_chunks = await rx_future

        # Convert chunks list to list of raw arg buffers
        outputs = []
        i = 0
        last_arg = bytes()
        for c in rx_chunks:
            if i >= len(self._outputs):
                raise Exception("received unexpected extra data")

            if c.layer == 0 and not c.is_frame_boundary():
                last_arg += c.buffer
            elif c.layer == 0:
                outputs.append(self._outputs[i][2].deserialize(self._libfibre, last_arg))
                last_arg = bytes()
                i += 1
            else:
                raise Exception("expected layer 0 chunk but got layer " + str(c.layer) + " chunk")

        if len(outputs) == 0:
            return
        elif len(outputs) == 1:
            return outputs[0]
        else:
            return tuple(outputs)

    def __call__(self, *args, cancellation_token = None):
        """
        Starts invoking the remote function. The first argument is usually a
        remote object.
        If this function is called from the Fibre thread then it is nonblocking
        and returns an asyncio.Future. If it is called from another thread then
        it blocks until the function completes and returns the result(s) of the 
        invokation.
        """

        if threading.current_thread() != libfibre_thread:
            return run_coroutine_threadsafe(self._libfibre.loop, lambda: self.__call__(*args))

        if (len(self._inputs) != len(args)):
            raise TypeError("expected {} arguments but have {}".format(len(self._inputs), len(args)))

        coro = self.async_call(args, cancellation_token)
        return asyncio.ensure_future(coro, loop=self._libfibre.loop)

    def __get__(self, instance, owner):
        return MethodType(self, instance) if instance else self

    def _dump(self, name):
        print_arglist = lambda arglist: ", ".join("{}: {}".format(arg_name, codec_name) for arg_name, codec_name, codec in arglist)
        return "{}({}){}".format(name,
            print_arglist(self._inputs),
            "" if len(self._outputs) == 0 else
            " -> " + print_arglist(self._outputs) if len(self._outputs) == 1 else
            " -> (" + print_arglist(self._outputs) + ")")

class EmptyInterface():
    def __str__(self):
        return "[lost object]"
    def __repr__(self):
        return self.__str__()

class RemoteObject(object):
    """
    Base class for interfaces of remote objects.
    """
    __sealed__ = False

    def __init__(self, libfibre, obj_handle, path):
        self._refcount = 0
        self._children = set()

        self._libfibre = libfibre
        self._obj_handle = obj_handle
        self._path = path
        self._on_lost = concurrent.futures.Future() # TODO: maybe we can do this with conc

        # Ensure that assignments to undefined attributes raise an exception
        self.__sealed__ = True

    def __setattr__(self, key, value):
        obj = self._get_without_magic(key)
        if not obj is None and obj.__class__._magic_setter:
            obj.__class__._functions[obj.__class__._magic_setter](obj, value)
            return

        if self.__sealed__ and not hasattr(self, key):
            raise AttributeError("Attribute {} not found".format(key))
        object.__setattr__(self, key, value)

    def _get_without_magic(self, key):
        idx, intf = self._attributes.get(key, (None, None))
        if intf is None:
            return None

        assert(not self._obj_handle is None)

        obj_handle = c_void_p(0)
        status = libfibre_get_attribute(self.__class__._handle, self._obj_handle, idx, byref(obj_handle))
        if status != kFibreOk:
            raise _get_exception(status)
        
        assert(obj_handle.value)
        obj = self._libfibre._load_py_obj(obj_handle.value, intf._handle, "UNKNOWN PATH") # TODO: fetch path from libfibre
        if obj in self._children:
            self._libfibre._release_py_obj(obj_handle.value)
        else:
            # the object will be released when the parent is released
            self._children.add(obj)

        return obj

    def __getattr__(self, key):
        obj = self._get_without_magic(key)
        if not obj is None:
            if obj.__class__._magic_getter:
                if threading.current_thread() == libfibre_thread:
                    # read() behaves asynchronously when run on the fibre thread
                    # which means it returns an awaitable which _must_ be awaited
                    # (otherwise it's a bug). However hasattr(...) internally calls
                    # __get__ and does not await the result. Thus the safest thing
                    # is to just disallow __get__ from run as an async method.
                    raise Exception("Cannot use magic getter on Fibre thread. Use _[prop_name]_propery.read() instead.")
                return obj.__class__._functions[obj.__class__._magic_getter](obj)
            else:
                return obj

        if key.startswith('_') and key.endswith('_property'):
            obj = self._get_without_magic(key[1:-9])
            if not obj is None:
                return obj

        func = self._functions.get(key, None)
        if not func is None:
            return func.__get__(self, None)

        return object.__getattribute__(self, key)

    def __dir__(self):
        props = ["_" + k + "_property" for k, (idx, intf) in self._attributes.items() if intf._name.startswith("fibre.Property<") and intf._name.endswith(">")]
        return sorted(set(super(object, self).__dir__()
                        + list(self._functions.keys())
                        + list(self._attributes.keys())
                        + props))

    #def __del__(self):
    #    print("unref")
    #    libfibre_unref_obj(self._obj_handle)

    def _dump(self, indent, depth):
        if self._obj_handle is None:
            return "[object lost]"

        try:
            if depth <= 0:
                return "..."
            lines = []
            for key, func in self.__class__._functions.items():
                lines.append(indent + func._dump(key))
            for key, (idx, intf) in self.__class__._attributes.items():
                val = getattr(self, key)
                if isinstance(val, RemoteObject) and not intf._magic_getter:
                    lines.append(indent + key + (": " if depth == 1 else ":\n") + val._dump(indent + "  ", depth - 1))
                else:
                    if isinstance(val, RemoteObject) and intf._magic_getter:
                        val_str = get_user_name(val)
                    else:
                        val_str = str(val)
                    property_type = str(intf._functions[intf._magic_getter]._outputs[0][1])
                    lines.append(indent + key + ": " + val_str + " (" + property_type + ")")
        except:
            return "[failed to dump object]"

        return "\n".join(lines)

    def __str__(self):
        return self._dump("", depth=2)

    def __repr__(self):
        return self.__str__()

    def _destroy(self):
        libfibre = self._libfibre
        on_lost = self._on_lost
        children = self._children

        self._libfibre = None
        self._obj_handle = None
        self._on_lost = None
        self._children = set()

        for child in children:
            libfibre._release_py_obj(child._obj_handle)

        self.__class__ = EmptyInterface # ensure that this object has no more attributes
        on_lost.set_result(True)


class LibFibre():
    def __init__(self):
        self.loop = asyncio.get_event_loop()

        # We must keep a reference to these function objects so they don't get
        # garbage collected.
        self.c_log = LogSignature(self._log)
        self.c_post = PostSignature(self._post)
        self.c_register_event = RegisterEventSignature(self._register_event)
        self.c_deregister_event = DeregisterEventSignature(self._deregister_event)
        self.c_open_timer = OpenTimerSignature(self._open_timer)
        self.c_set_timer = SetTimerSignature(self._set_timer)
        self.c_close_timer = CloseTimerSignature(self._close_timer)
        self.c_on_run_tasks = OnRunTasksSignature(self._on_run_tasks)
        self.c_on_found_object = OnFoundObjectSignature(self._on_found_object)
        self.c_on_lost_object = OnLostObjectSignature(self._on_lost_object)
        self.c_on_discovery_stopped = OnStoppedSignature(self._on_discovery_stopped)
        
        self.timer_map = {}
        self.eventfd_map = {}
        self.interfaces = {} # key: libfibre handle, value: python class
        self.discovery_processes = {} # key: ID, value: python dict
        self._objects = {} # key: libfibre handle, value: python class
        self._functions = {} # key: libfibre handle, value: python class
        self._calls = {} # key: libfibre handle, value: Call object

        self.clear_tasks()
        self._autostart_dispatcher = True
        self._in_dispatcher = False

        event_loop = LibFibreEventLoop()
        event_loop.post = self.c_post
        event_loop.register_event = self.c_register_event
        event_loop.deregister_event = self.c_deregister_event
        event_loop.open_timer = self.c_open_timer
        event_loop.set_timer = self.c_set_timer
        event_loop.close_timer = self.c_close_timer

        logger = LibFibreLogger()
        logger.verbosity = int(os.environ.get('FIBRE_LOG', '2'))
        logger.log = self.c_log
        logger.ctx = None

        self.ctx = c_void_p(libfibre_open(event_loop, self.c_on_run_tasks, logger))
        assert(self.ctx)

    def _log(self, ctx, file, line, level, info0, info1, text):
        file = string_at(file).decode('utf-8')
        text = string_at(text).decode('utf-8')
        color = "\x1b[91;1m" if level <= 2 else ""
        print(color + "[" + file.rpartition('/')[-1] + ":" + str(line) + "]: " + text + "\x1b[0m", file=sys.stderr)

    def _post(self, callback, ctx):
        self.loop.call_soon_threadsafe(callback, ctx)
        return 0

    def _register_event(self, event_fd, events, callback, ctx):
        self.eventfd_map[event_fd] = events
        if (events & 1):
            self.loop.add_reader(event_fd, lambda x: callback(x, 1), ctx)
        if (events & 4):
            self.loop.add_writer(event_fd, lambda x: callback(x, 4), ctx)
        if (events & 0xfffffffa):
            raise Exception("unsupported event mask " + str(events))
        return 0

    def _deregister_event(self, event_fd):
        events = self.eventfd_map.pop(event_fd)
        if (events & 1):
            self.loop.remove_reader(event_fd)
        if (events & 4):
            self.loop.remove_writer(event_fd)
        return 0

    def _open_timer(self, p_timer_id, callback, ctx):
        timer = {
            'callback': callback,
            'ctx': ctx,
            'interval': 0.0,
            'mode': 0,
            'tim': None,
        }
        p_timer_id[0] = insert_with_new_id(self.timer_map, timer)
        return 0

    def _set_timer(self, timer_id, interval, mode):
        assert(timer_id in self.timer_map)
        timer = self.timer_map[timer_id]
        if not timer['tim'] is None:
            timer['tim'].cancel()
        timer['interval'] = interval
        timer['mode'] = mode
        if timer['mode'] != 0:
            timer['tim'] = self.loop.call_later(timer['interval'], self._on_timer, timer)
        return 0

    def _on_timer(self, timer):
        if timer['mode'] != 0:
            timer['tim'] = self.loop.call_later(timer['interval'], self._on_timer, timer)
        timer['callback'](timer['ctx'])

    def _close_timer(self, timer_id):
        assert(timer_id in self.timer_map)
        timer = self.timer_map.pop(timer_id)
        if not timer['tim'] is None:
            timer['tim'].cancel()
        return 0

    def _load_py_func(self, func_handle):
        if not func_handle in self._functions:
            info_ptr = libfibre_get_function_info(func_handle)
            info = info_ptr.contents
            
            try:
                name = string_at(info.name, info.name_length).decode('utf-8')
                inputs = list(decode_arg_list(info.input_names, info.input_codecs))
                outputs = list(decode_arg_list(info.output_names, info.output_codecs))
                py_func = RemoteFunction(self, name, func_handle, inputs, outputs)
            finally:
                libfibre_free_function_info(info_ptr)

            self._functions[func_handle] = py_func
        else:
            py_func = self._functions[func_handle]

        return py_func


    def _load_py_intf(self, intf_handle):
        """
        Creates a new python type for the specified libfibre interface handle or
        returns the existing python type if one was already create before.

        Behind the scenes the python type will react to future events coming
        from libfibre, such as functions/attributes being added/removed.
        """
        if intf_handle in self.interfaces:
            py_intf = self.interfaces[intf_handle]

        else:
            info_ptr = libfibre_get_interface_info(intf_handle)
            info = info_ptr.contents

            try:
                name = string_at(info.name, info.name_length).decode('utf-8')

                attributes = {}
                for i in range(info.n_attributes):
                    attr = info.attributes[i]
                    attributes[string_at(attr.name, attr.name_length).decode('utf-8')] = (i, self._load_py_intf(attr.intf))

                functions = {}
                for i in range(info.n_functions):
                    func = self._load_py_func(info.functions[i])
                    functions[func._name] = func

            finally:
                libfibre_free_interface_info(info_ptr)

            py_intf = self.interfaces[intf_handle] = type(name, (RemoteObject,), {
                '_name': name,
                '_handle': intf_handle,
                '_attributes': attributes,
                '_functions': functions,
                '_magic_getter': 'read' if (name.startswith('fibre.Property<') and name.endswith('>')) else None,
                '_magic_setter': 'exchange' if (name.startswith('fibre.Property<readwrite ') and name.endswith('>')) else None,
                '_refcount': 0
            })

        py_intf._refcount += 1
        return py_intf

    def _release_py_intf(self, intf_handle):
        py_intf = self.interfaces[intf_handle]
        py_intf._refcount -= 1
        if py_intf._refcount <= 0:
            for idx, intf in py_intf._attributes.values():
                self._release_py_intf(intf._handle)
            self.interfaces.pop(intf_handle)

    def _load_py_obj(self, obj_handle, intf_handle, path):
        if not obj_handle in self._objects:
            py_intf = self._load_py_intf(intf_handle)
            py_obj = py_intf(self, obj_handle, path)
            self._objects[obj_handle] = py_obj
        else:
            py_obj = self._objects[obj_handle]

        # Note: this refcount does not count the python references to the object
        # but rather mirrors the libfibre-internal refcount of the object. This
        # is so that we can destroy the Python object when libfibre releases it.
        py_obj._refcount += 1
        return py_obj

    def _release_py_obj(self, obj_handle):
        py_obj = self._objects[obj_handle]
        py_obj._refcount -= 1
        if py_obj._refcount <= 0:
            self._objects.pop(obj_handle)
            intf_handle = py_obj.__class__._handle
            py_obj._destroy()
            self._release_py_intf(intf_handle)

    def enqueue_task(self, task):
        # Enlarge array by factor 5 if necessary
        if self._n_pending_tasks >= len(self._tasks):
            self._tasks = customresize(self._tasks, 5 * len(self._tasks))

        self._tasks[self._n_pending_tasks] = task
        self._n_pending_tasks += 1

        # Dispatch all tasks at next opportunity
        if self._autostart_dispatcher:
            self._autostart_dispatcher = False
            self.loop.call_soon(self.dispatch_tasks_to_lib)

    def clear_tasks(self):
        self._tasks = (LibFibreTask * 10)()
        self._n_pending_tasks = 0

    def dispatch_tasks_to_lib(self):
        self._in_dispatcher = True

        while self._n_pending_tasks:
            out_tasks = POINTER(LibFibreTask)()
            n_out_tasks = c_size_t()
            libfibre_run_tasks(self.ctx, self._tasks, self._n_pending_tasks, byref(out_tasks), byref(n_out_tasks))
            
            self.clear_tasks()

            self.handle_tasks(out_tasks, n_out_tasks.value)

        self._autostart_dispatcher = True
        self._in_dispatcher = False

    def handle_tasks(self, tasks, n_tasks):
        tasks = (LibFibreTask * n_tasks).from_address(cast(tasks, c_void_p).value or 0)
        for task in tasks:
            if task.type == kStartCall:
                print("function server not implemented")
            elif task.type == kWrite:
                self._calls[task.handle]._rx_socket._on_write(task.content.write)
            elif task.type == kWriteDone:
                self._calls[task.handle]._tx_socket._on_write_done(task.content.write_done)
            else:
                print("unknown task type: ", task.type)

    def _on_run_tasks(self, ctx, tasks, n_tasks, out_tasks, n_out_tasks):
        assert(not self._in_dispatcher)
        self.handle_tasks(tasks, n_tasks)

        # Move new tasks to the shadow task queue so they remain valid until the
        # next invokation of _on_run_tasks.
        self._shadow_tasks = self._tasks
        out_tasks[0] = self._shadow_tasks
        n_out_tasks[0] = c_ulong(self._n_pending_tasks)
        self.clear_tasks()

    def _on_found_object(self, ctx, obj, intf, path, path_length):
        py_obj = self._load_py_obj(obj, intf, string_at(path, path_length).decode('utf-8'))
        discovery = self.discovery_processes[ctx]
        discovery._unannounced.append(py_obj)
        old_future = discovery._future
        discovery._future = self.loop.create_future()
        old_future.set_result(None)
    
    def _on_lost_object(self, ctx, obj):
        assert(obj)
        self._release_py_obj(obj)
    
    def _on_discovery_stopped(self, ctx, result):
        print("discovery stopped")

    def _on_call_completed(self, ctx, status, tx_end, rx_end, tx_buf, tx_len, rx_buf, rx_len):
        call = self._calls.pop(ctx)

        call.ag_await.set_result((status, tx_end, rx_end))

        return kFibreBusy

class Discovery():
    """
    All public members of this class are thread-safe.
    """

    def __init__(self, domain):
        self._domain = domain
        self._id = 0
        self._discovery_handle = c_void_p(0)
        self._unannounced = []
        self._future = domain._libfibre.loop.create_future()

    async def _next(self):
        if len(self._unannounced) == 0:
            await self._future
        return self._unannounced.pop(0)

    def _stop(self):
        self._domain._libfibre.discovery_processes.pop(self._id)
        libfibre_stop_discovery(self._discovery_handle)
        self._future.set_exception(asyncio.CancelledError())

    def stop(self):
        if threading.current_thread() == libfibre_thread:
            self._stop()
        else:
            run_coroutine_threadsafe(self._domain._libfibre.loop, self._stop)

class _Domain():
    """
    All public members of this class are thread-safe.
    """

    def __init__(self, libfibre, handle):
        self._libfibre = libfibre
        self._domain_handle = handle

    def _close(self):
        libfibre_close_domain(self._domain_handle)
        self._domain_handle = None
        #decrement_lib_refcount()

    def _start_discovery(self):
        discovery = Discovery(self)
        discovery._id = insert_with_new_id(self._libfibre.discovery_processes, discovery)
        libfibre_start_discovery(self._domain_handle, byref(discovery._discovery_handle), self._libfibre.c_on_found_object, self._libfibre.c_on_lost_object, self._libfibre.c_on_discovery_stopped, discovery._id)
        return discovery

    async def _discover_one(self):
        discovery = self._start_discovery()
        obj = await discovery._next()
        # TODO: this would tear down all discovered objects. Need to think about
        # how to implement this properly (object ref count?).
        #discovery._stop()
        return obj

    def discover_one(self):
        """
        Blocks until exactly one object is discovered.
        """
        return run_coroutine_threadsafe(self._libfibre.loop, self._discover_one)

    def run_discovery(self, callback):
        """
        Invokes `callback` for every object that is discovered. The callback is
        invoked on the libfibre thread and can be an asynchronous function.
        Returns a `Discovery` object on which `stop()` can be called to
        terminate the discovery.
        """
        discovery = run_coroutine_threadsafe(self._libfibre.loop, self._start_discovery)
        async def loop():
            while True:
                obj = await discovery._next()
                await callback(obj)
        self._libfibre.loop.call_soon_threadsafe(lambda: asyncio.ensure_future(loop()))
        return discovery


class Domain():
    def __init__(self, path):
        increment_lib_refcount()
        self._opened_domain = run_coroutine_threadsafe(libfibre.loop, lambda: Domain._open(path))
        
    def _open(path):
        assert(libfibre_thread == threading.current_thread())
        buf = path.encode('ascii')
        domain_handle = libfibre_open_domain(libfibre.ctx, buf, len(buf))
        return _Domain(libfibre, domain_handle)

    def __enter__(self):
        return self._opened_domain

    def __exit__(self, type, value, traceback):
        run_coroutine_threadsafe(self._opened_domain._libfibre.loop, self._opened_domain._close)
        self._opened_domain = None
        decrement_lib_refcount()

libfibre = None

def _run_event_loop():
    global libfibre
    global terminate_libfibre

    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)

    terminate_libfibre = loop.create_future()
    libfibre = LibFibre()

    libfibre.loop.run_until_complete(terminate_libfibre)

    libfibre_close(libfibre.ctx)

    # Detach all objects that still exist
    # TODO: the proper way would be either of these
    #  - provide a libfibre function to destroy an object on-demand which we'd
    #    call before libfibre_close().
    #  - have libfibre_close() report the destruction of all objects

    while len(libfibre._objects):
        libfibre._release_py_obj(list(libfibre._objects.keys())[0])
    assert(len(libfibre.interfaces) == 0)

    libfibre = None


lock = threading.Lock()
libfibre_refcount = 0
libfibre_thread = None

def increment_lib_refcount():
    global libfibre_refcount
    global libfibre_thread

    with lock:
        libfibre_refcount += 1
        #print("inc refcount to {}".format(libfibre_refcount))

        if libfibre_refcount == 1:
            libfibre_thread = threading.Thread(target = _run_event_loop)
            libfibre_thread.start()

        while libfibre is None:
            time.sleep(0.1)

def decrement_lib_refcount():
    global libfibre_refcount
    global libfibre_thread

    with lock:
        #print("dec refcount from {}".format(libfibre_refcount))
        libfibre_refcount -= 1

        if libfibre_refcount == 0:
            libfibre.loop.call_soon_threadsafe(lambda: terminate_libfibre.set_result(True))

            # It's unlikely that releasing fibre from a fibre callback is ok. If
            # there is a valid scenario for this then we can remove the assert.
            assert(libfibre_thread != threading.current_thread())

            libfibre_thread.join()
            libfibre_thread = None

def get_user_name(obj):
    """
    Can be overridden by the application to return the user-facing name of an
    object.
    """
    return "[anonymous object]"
