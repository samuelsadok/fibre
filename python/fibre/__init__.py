
from .discovery import find_any, find_all
from .threading_utils import EventWaitHandle
from .utils import Logger
from .protocol import ChannelBrokenException, ChannelDamagedException
from .shell import launch_shell

class GlobalState(object):
    pass

import uuid
global_state = GlobalState()
global_state.own_uuid = uuid.uuid4()

def assert_bytes_type(value):
    if not isinstance(value, (bytes, bytearray)):
        raise TypeError("a bytes-like object is required, not '{}'".format(value.__class__))
