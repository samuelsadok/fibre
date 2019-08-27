#!/bin/python

import argparse
import jinja2
import sys
import xml.etree.ElementTree as ET
import os

# dbus-send --system --print-reply --reply-timeout=2000 --type=method_call --dest=org.bluez /org/bluez org.freedesktop.DBus.Introspectable.Introspect | tail +2

# interface definitions can be obtained from DBus like this:
# xmllint --format <(dbus-send --system --print-reply --reply-timeout=2000 --type=method_call --dest=org.bluez / org.freedesktop.DBus.Introspectable.Introspect | tail +2 | head -c -2 | tail -c +12)
# This inquires object "/" of service "org.bluez". The returned XML can contain multiple interfaces.

# Returns a list that contains a dict for every interface definition that is
# found in the given input XML file.
def get_interfaces(definition_file):
    tree = ET.parse(definition_file)
    interfaces = []
    for interface_xml in tree.getroot():
        methods = []
        properties = []
        signals = []
        for member_xml in interface_xml:
            if member_xml.tag == "method":
                in_args = []
                out_args = []
                for arg_xml in member_xml:
                    arg = {
                        'name': arg_xml.attrib["name"],
                        'type': arg_xml.attrib["type"]
                    }
                    if arg_xml.attrib["direction"] == "in":
                        in_args.append(arg)
                    elif arg_xml.attrib["direction"] == "out":
                        out_args.append(arg)
                    else:
                        raise Exception("unsupported direction \"{}\"".format(arg_xml.attrib["direction"]))
                methods.append({
                    'name': member_xml.attrib["name"],
                    'inputs': in_args,
                    'outputs': out_args
                })
            elif member_xml.tag == "property":
                # TODO: make use of "access"
                properties.append({
                    'name': member_xml.attrib["name"],
                    'type': member_xml.attrib["type"]
                })
            elif member_xml.tag == "signal":
                args = []
                for arg_xml in member_xml:
                    args.append({
                        'name': arg_xml.attrib["name"],
                        'type': arg_xml.attrib["type"]
                    })
                signals.append({
                    'name': member_xml.attrib["name"],
                    'args': args
                })
            else:
                raise Exception("unsupported member type \"{}\"".format(member_xml.tag))
        interfaces.append({
            'name': interface_xml.attrib["name"],
            'methods': methods,
            'properties': properties,
            'signals': signals
        })
    return interfaces


def pop_type_list(string):
    assert(string[0] == "(")
    string = string[1:]
    type_list = []
    while string[0] != ")":
        type_str, string = pop_single_type(string)
        type_list.append(type_str)
    return type_list, string[1:]

def pop_single_type(string):
    simple_types = {
        'y': 'uint8_t',
        'b': 'bool',
        'n': 'int16_t',
        'q': 'uint16_t',
        'i': 'int32_t',
        'u': 'uint32_t',
        'x': 'int64_t',
        't': 'uint64_t',
        'd': 'double',
        'h': 'handle',
        's': 'std::string',
        'o': 'DBusObject',
        'g': 'signature'
    }
    simple_type_str = simple_types.get(string[0], None)
    if simple_type_str:
        return simple_type_str, string[1:]
    elif string[0] == "v":
        return "fibre::dbus_variant", string[1:]
    elif string[0] == "a":
        if string[1] == "{":
            key_type_str, string = pop_single_type(string[2:])
            val_type_str, string = pop_single_type(string)
            assert(string[0] == "}")
            return ("std::unordered_map<" + key_type_str + ", " + val_type_str + ">"), string[1:]
        else:
            type_str, string = pop_single_type(string[1:])
            return ("std::vector<" + type_str + ">"), string
    elif string[0] == "(":
        type_list, string = pop_type_list(string)
        type_str = "std::tuple<" + ", ".join(type_list) + ">"
        return type_str, string
    else:
        raise Exception("error")

def dbus_type_to_cpp_type(string):
    type_str, string = pop_single_type(string)
    assert(string == "")
    return type_str



# Parse arguments

parser = argparse.ArgumentParser(description="Gernerate code from XML DBus interface definitions")
parser.add_argument("--version", action="store_true",
                    help="print version information")
parser.add_argument("-v", "--verbose", action="store_true",
                    help="print debug information (on stderr)")
parser.add_argument("-d", "--definition", type=argparse.FileType('r'), nargs='+',
                    help="the XML interface definition file(s) used to generate the code")
parser.add_argument("-t", "--template", type=argparse.FileType('r'),
                    help="the code template")
parser.add_argument("-o", "--output", type=argparse.FileType('w'), default='-',
                    help="path of the generated output")
args = parser.parse_args()

if args.version:
    print("0.0.1")
    sys.exit(0)


definition_files = args.definition
template_file = args.template
output_file = args.output

all_interfaces = []
for file in definition_files:
    all_interfaces += get_interfaces(file)


env = jinja2.Environment()
env.filters["dbus_type_to_cpp_type"] = dbus_type_to_cpp_type

template = env.from_string(template_file.read())
output = template.render(
    #namespaces=type_loader.global_namespaces,
    #all_builtin=[item for namespace in type_loader.global_namespaces.values()
    #             for item in namespace.all_builtin()],
    #all_properties=[item for namespace in type_loader.global_namespaces.values()
    #                for item in namespace.all_properties()],
    #all_functions=[item for namespace in type_loader.global_namespaces.values()
    #               for item in namespace.all_functions()],
    #all_enums=[item for namespace in type_loader.global_namespaces.values()
    #           for item in namespace.all_enums()],
    #all_interfaces=[item for namespace in type_loader.global_namespaces.values()
    #                for item in namespace.all_interfaces()],
    all_interfaces = all_interfaces,
    output_name = os.path.basename(output_file.name)
)

# Output
output_file.write(output)
