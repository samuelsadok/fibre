"""
Provides functions for the discovery of Fibre nodes
"""

import sys
import json
import time
import threading
import traceback

from fibre import Logger, EventWaitHandle, ChannelBrokenException, RemoteNode, calc_crc16
import fibre.remote_object
from fibre import global_state, DEFAULT_SCAN_PATHS

# Load all installed transport layers

#scan_providers = {}

try:
    import fibre.usbbulk_transport
    global_state.scan_providers['usb'] = fibre.usbbulk_transport.discover_channels
except ModuleNotFoundError:
    pass

try:
    import fibre.serial_transport
    global_state.scan_providers['serial'] = fibre.serial_transport.discover_channels
except ModuleNotFoundError:
    pass

try:
    import fibre.tcp_transport
    global_state.scan_providers['tcp:client'] = fibre.tcp_transport.discover_channels
except ModuleNotFoundError:
    pass

try:
    import fibre.udp_transport
    global_state.scan_providers['udp'] = fibre.udp_transport.discover_channels
except ModuleNotFoundError:
    pass

class Scan():
    def __init__(self, cancellation_token):
        self.ref_count = 0
        self.cancellation_token = cancellation_token

def get_scan_provider(path):
    """
    Returns the scan provider with the longest matching name.
    I.e. if there is are scan providers with the name "tcp" and
    "tcp:dht", the path spec "tcp:dht:NODE_ID" would match the
    latter.
    """
    path_elements = path.split(':')
    while path_elements:
        scanner_name = ':'.join(path_elements)
        if scanner_name in global_state.scan_providers:
            return global_state.scan_providers[scanner_name], path[len(scanner_name):].lstrip(':')
        path_elements = path_elements[:-1]
    raise Exception("no scan provider installed for path spec {}".format(path))

def start_scan(path):
    with global_state.active_scans_lock:
        if path in global_state.active_scans:
            scan = global_state.active_scans[path]
        else:
            scan_provider, remaining_path = get_scan_provider(path)
            scan_cancellation_token = EventWaitHandle(name='fibre:scan-cancellation-token:'+str(path))
            threading.Thread(
                target=scan_provider,
                name='fibre:scanner:'+str(path),
                args=(remaining_path, scan_cancellation_token, global_state.logger)).start()
            scan = Scan(scan_cancellation_token)
            global_state.active_scans[path] = scan
        scan.ref_count += 1

def stop_scan(path):
    with global_state.active_scans_lock:
        if not path in global_state.active_scans:
            raise Exception("attempt to stop scanning on {} but there is no active scan with such a path".format(path))
        else:
            kv = global_state.active_scans.pop(path)
            kv.ref_count -= 1
            if not kv.ref_count:
                kv.cancellation_token.set()

def init(logger):
    if logger is None:
        logger = Logger(verbose=False)

    global_state.logger = logger
    for path in DEFAULT_SCAN_PATHS:
        start_scan(path)

def exit():
    for path in DEFAULT_SCAN_PATHS:
        stop_scan(path)

def noprint(text):
    pass

def find_all(path, serial_number,
         did_discover_object_callback,
         search_cancellation_token,
         channel_termination_token,
         logger):
    """
    Starts scanning for Fibre nodes that match the specified path spec and calls
    the callback for each Fibre node that is found.
    This function is non-blocking.
    """

    def did_discover_channel(channel):
        """
        Inits an object from a given channel and then calls did_discover_object_callback
        with the created object
        This queries the endpoint 0 on that channel to gain information
        about the interface, which is then used to init the corresponding object.
        """
        try:
            logger.debug("Connecting to device on " + channel._name)

            uuid = bytes([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0])
            node = fibre.RemoteNode(uuid, 5)
            node.add_output_channel(channel)
            with node:
                get_function_json = fibre.remote_object.RemoteFunction(node, 0, 0, ["number"], ["json"])
                json_string = get_function_json(5)
            
            try:
                json_bytes = channel.remote_endpoint_read_buffer(0)
            except (TimeoutError, ChannelBrokenException):
                logger.debug("no response - probably incompatible")
                return
            json_crc16 = fibre.calc_crc16(fibre.PROTOCOL_VERSION, json_bytes)
            channel._interface_definition_crc = json_crc16
            try:
                json_string = json_bytes.decode("ascii")
            except UnicodeDecodeError:
                logger.debug("device responded on endpoint 0 with something that is not ASCII")
                return
            logger.debug("JSON: " + json_string.replace('{"name"', '\n{"name"'))
            logger.debug("JSON checksum: 0x{:02X} 0x{:02X}".format(json_crc16 & 0xff, (json_crc16 >> 8) & 0xff))
            try:
                json_data = json.loads(json_string)
            except json.decoder.JSONDecodeError as error:
                logger.debug("device responded on endpoint 0 with something that is not JSON: " + str(error))
                return
            json_data = {"name": "fibre_node", "members": json_data}
            obj = fibre.remote_object.RemoteObject(json_data, None, channel, logger)

            obj.__dict__['_json_data'] = json_data['members']
            obj.__dict__['_json_crc'] = json_crc16

            device_serial_number = fibre.utils.get_serial_number_str(obj)
            if serial_number != None and device_serial_number != serial_number:
                logger.debug("Ignoring device with serial number {}".format(device_serial_number))
                return
            did_discover_object_callback(obj)
        except Exception:
            logger.debug("Unexpected exception after discovering channel: " + traceback.format_exc())

    # For each connection type, kick off an appropriate discovery loop
    for search_spec in path.split(','):
        prefix = search_spec.split(':')[0]
        the_rest = ':'.join(search_spec.split(':')[1:])
        if prefix in channel_types:
            logger.debug("start discovery on {}".format(prefix))
            threading.Thread(target=channel_types[prefix],
                             args=(the_rest, serial_number, did_discover_channel, search_cancellation_token, channel_termination_token, logger)).start()
        else:
            raise Exception("Invalid path spec \"{}\"".format(search_spec))


def find_any(path="usb", serial_number=None,
        search_cancellation_token=None, channel_termination_token=None,
        timeout=None, logger=Logger(verbose=False)):
    """
    Blocks until the first matching Fibre node is connected and then returns that node
    """
    result = [ None ]
    done_signal = EventWaitHandle(search_cancellation_token)
    def did_discover_object(obj):
        result[0] = obj
        done_signal.set("search succeeded")
    find_all(path, serial_number, did_discover_object, done_signal, channel_termination_token, logger)
    try:
        done_signal.wait(timeout=timeout)
    finally:
        done_signal.set("search finished") # terminate find_all
    return result[0]
