

SYNC_BYTE = 0xAA
PROTOCOL_VERSION = 1

MAX_PACKET_SIZE = 128

PIPES_PER_REMOTE_NODE = 10

DEFAULT_SCAN_PATHS = [
    'tcp:client:localhost:9910',
    #'tcp:dht'
]

class OperationAbortedError(Exception):
    pass

class DeviceInitException(Exception):
    pass

class ChannelDamagedException(Exception):
    """
    Raised when the channel is temporarily broken and a
    resend of the message might be successful
    """
    pass

class ChannelBrokenException(Exception):
    """
    Raised when the channel is permanently broken
    """
    pass


class GlobalState(object):
    def __init__(self):
        import uuid
        from threading import Lock
        self.own_uuid = uuid.uuid4()
        self.remote_nodes = {}
        self.active_scans = {}
        self.active_scans_lock = Lock()
        self.scan_providers = {}
        self.logger = None # set in init()

global_state = GlobalState()

def assert_bytes_type(value):
    if not isinstance(value, (bytes, bytearray)):
        raise TypeError("a bytes-like object is required, not '{}'".format(value.__class__))


# utils
from .threading_utils import EventWaitHandle, Semaphore, Future
from .utils import Logger
from .crc import calc_crc16

# I/O
from .stream import StreamSink, StreamSource, PacketSink, PacketSource, StreamChain, StreamStatus
from .output import OutputChannel, OutputPipe, SuspendedOutputPipe, OutgoingConnection
from .input import InputPipe, SuspendedInputPipe, InputChannelDecoder

from .remote_node import RemoteNode
#from .remote_object import RemoteFunction
from .discovery import find_any, find_all, init
#from .protocol import ChannelBrokenException, ChannelDamagedException
from .shell import launch_shell


def get_remote_node(uuid):
    # TODO: make thread-safe
    if uuid not in global_state.remote_nodes:
        remote_node = RemoteNode(uuid, global_state.logger, PIPES_PER_REMOTE_NODE)
        remote_node.__enter__()
        global_state.remote_nodes[uuid] = remote_node
    return global_state.remote_nodes[uuid]
