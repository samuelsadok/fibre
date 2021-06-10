#!/bin/python3

import yaml
import json
import jinja2
import jsonschema
import re
import argparse
import sys
from collections import OrderedDict

# This schema describes what we expect interface definition files to look like
validator = jsonschema.Draft4Validator(yaml.safe_load("""
definitions:
  interface:
    type: object
    properties:
      c_is_class: {type: boolean}
      c_name: {type: string}
      brief: {type: string}
      doc: {type: string}
      implements:
        anyOf:
          - {"$ref": "#/definitions/intf_type_ref"}
          - {type: array, items: {"$ref": "#/definitions/intf_type_ref"}}
      functions:
        type: object
        additionalProperties: {"$ref": "#/definitions/function"}
      attributes:
        type: object
        additionalProperties: {"$ref": "#/definitions/attribute"}
      __line__: {type: object}
      __column__: {type: object}
    required: [c_is_class]
    additionalProperties: false

  valuetype:
    type: object
    properties:
      mode: {type: string} # this shouldn't be here
      c_name: {type: string}
      values: {type: object}
      flags: {type: object}
      nullflag: {type: string}
      __line__: {type: object}
      __column__: {type: object}
    additionalProperties: false

  intf_type_ref:
    anyOf:
      - {"type": "string"}
      - {"$ref": "#/definitions/interface"}

  intf_or_val_type:
    anyOf:
      - {"$ref": "#/definitions/interface"}
      - {"$ref": "#/definitions/valuetype"}
      - {"type": "string"}

  attribute:
    anyOf: # this is probably not being used correctly
      - {"$ref": "#/definitions/intf_or_val_type"}
      - type: object
      - type: object
        properties:
          type: {"$ref": "#/definitions/intf_or_val_type"}
          c_name: {"type": string}
          unit: {"type": string}
          doc: {"type": string}
        additionalProperties: false

  function:
    anyOf:
      - type: 'null'
      - type: object
        properties:
          status: {type: string}
          in: {"$ref": "#/definitions/argList"}
          out: {"$ref": "#/definitions/argList"}
          brief: {type: string}
          doc: {type: string}
          __line__: {type: object}
          __column__: {type: object}
        additionalProperties: false

  argList:
    type: object
    additionalProperties: { "$ref": "#/definitions/arg" }

  arg:
    anyOf:
      - type: object
        properties:
          doc: {"type": string}
          type: {"$ref": "#/definitions/valuetyperef"}
        additionalProperties: false
      - {"$ref": "#/definitions/valuetyperef"}

  valuetyperef:
    type: string

type: object
properties:
  ns: {type: string}
  version: {type: string}
  summary: {type: string}
  dictionary: {type: array, items: {type: string}}
  interfaces:
    type: object
    additionalProperties: { "$ref": "#/definitions/interface" }
  valuetypes:
    type: object
    additionalProperties: { "$ref": "#/definitions/valuetype" }
  exports:
    type: object
    additionalProperties: { "$ref": "#/definitions/intf_type_ref" }
  userdata:
    type: object
  __line__: {type: object}
  __column__: {type: object}
additionalProperties: false
"""))

# TODO: detect duplicate keys in yaml dictionaries

# Source: https://stackoverflow.com/a/53647080/3621512
class SafeLineLoader(yaml.SafeLoader):
    pass
#    def compose_node(self, parent, index):
#        # the line number where the previous token has ended (plus empty lines)
#        line = self.line
#        node = super(SafeLineLoader, self).compose_node(parent, index)
#        node.__line__ = line + 1
#        return node
#
#    def construct_mapping(self, node, deep=False):
#        mapping = super(SafeLineLoader, self).construct_mapping(node, deep=deep)
#        mapping['__line__'] = node.__line__
#        #mapping['__column__'] = node.start_mark.column + 1
#        return mapping

# Ensure that dicts remain ordered, even in Python <3.6
# source: https://stackoverflow.com/a/21912744/3621512
def construct_mapping(loader, node):
    loader.flatten_mapping(node)
    return OrderedDict(loader.construct_pairs(node))
SafeLineLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, construct_mapping)

dictionary = []

def get_words(string):
    """
    Splits a string in PascalCase or MACRO_CASE into a list of lower case words
    """
    if string.isupper():
        return [w.lower() for w in string.split('_')]
    else:
        regex = ''.join((re.escape(w) + '|') for w in dictionary) + '[a-z0-9]+|[A-Z][a-z0-9]*'
        return [(w if w in dictionary else w.lower()) for w in re.findall(regex, string)]

def join_name(*names, delimiter: str = '.'):
    """
    Joins two name components.
    e.g. 'io.helloworld' + 'sayhello' => 'io.helloworld.sayhello'
    """

    #return delimiter.join(y for x in names for y in x.split(delimiter) if y != '')
    return NameInfo(*names)

def split_name(name, delimiter: str = '.'):
    def replace_delimiter_in_parentheses():
        parenthesis_depth = 0
        for c in name:
            parenthesis_depth += 1 if c == '<' else -1 if c == '>' else 0
            yield c if (parenthesis_depth == 0) or (c != delimiter) else ':'
    return [part.replace(':', '.') for part in ''.join(replace_delimiter_in_parentheses()).split('.')]

def to_pascal_case(s): return ''.join([(w.title() if not w in dictionary else w) for w in get_words(s)])
def to_camel_case(s): return ''.join([(c.lower() if i == 0 else c) for i, c in enumerate(''.join([w.title() for w in get_words(s)]))])
def to_macro_case(s): return '_'.join(get_words(s)).upper()
def to_snake_case(s): return '_'.join(get_words(s)).lower()
def to_kebab_case(s): return '-'.join(get_words(s)).lower()


class NameInfo():
    def __init__(self, *parts):
        flat_parts = []
        for p in parts:
            if isinstance(p, NameInfo):
                flat_parts += list(p)
            elif isinstance(p, str):
                flat_parts += split_name(p)
            else:
                raise Exception("unexpected type " + str(type(p)))
        self.__parts = flat_parts

    def __getitem__(self, index):
        return self.__parts[index]

    def __len__(self):
        return len(self.__parts)

    def __repr__(self):
        return "Fibre Name \"" + self.get_fibre_name() + "\""

    def get_fibre_name(self):
        return ".".join((n for n in self))


class ArgumentElement():
    def __init__(self, path, name, elem):
        if elem is None:
            elem = {}
        elif isinstance(elem, str):
            elem = {'type': elem}
        self.name = join_name(path, name)
        self.type = ValueTypeRefElement(path, name, elem['type'], [])


class InterfaceRefElement():
    def __init__(self, scope, name, elem, typeargs):
        if isinstance(elem, str):
            self._intf = None
            self._scope = scope
            self._name = elem
        else:
            self._intf = InterfaceElement(scope, name, elem)
            self._scope = None
            self._name = None
        self._typeargs = typeargs

    def resolve(self):
        """
        Resolves this interface reference to an actual InterfaceElement instance.
        The innermost scope is searched first.
        At every scope level, if no matching interface is found, it is checked if a
        matching value type exists. If so, the interface type fibre.Property<value_type>
        is returned.

        This also adds the interface element to the global interface list and
        recursively resolves any on-demand created types.
        """
        if not self._intf is None:
            if self._intf.name.get_fibre_name() in interfaces:
                raise Exception("redefinition of " + str(self._intf.name))
            interfaces[self._intf.name.get_fibre_name()] = self._intf
            deep_resolve(self._intf)
            return self._intf

        typeargs = self._typeargs
        if 'fibre.Property.type' in typeargs and isinstance(typeargs['fibre.Property.type'], ValueTypeRefElement):
            typeargs['fibre.Property.type'] = typeargs['fibre.Property.type'].resolve()

        for probe_scope in [join_name(*self._scope[:(len(self._scope)-i)]) for i in range(len(self._scope)+1)]:
            probe_name = join_name(probe_scope, self._name).get_fibre_name()
            #print('probing ' + probe_name)
            if probe_name in interfaces:
                return interfaces[probe_name]
            elif probe_name in value_types:
                typeargs['fibre.Property.type'] = value_types[probe_name]
                return get_property_type(typeargs)
            elif probe_name in generics:
                return generics[probe_name](typeargs)

        raise Exception('could not resolve type {} in {}. Known interfaces are: {}. Known value types are: {}'.format(self._name, self._scope, list(interfaces.keys()), list(value_types.keys())))


class ValueTypeRefElement():
    def __init__(self, scope, name, elem, typeargs = []):
        if isinstance(elem, str):
            self._val_type = None
            self._scope = scope
            self._name = elem
        else:
            self._val_type = ValueTypeElement(scope, name, elem)
            self._scope = None
            self._name = None
        self._typeargs = typeargs

    def resolve(self):
        """
        Resolves this value type reference to an actual ValueTypeElement instance.
        The innermost scope is searched first.
        """
        if not self._val_type is None:
            value_types[self._val_type.name.get_fibre_name()] = self._intf
            return self._val_type
    
        for probe_scope in [join_name(*self._scope[:(len(self._scope)-i)]) for i in range(len(self._scope)+1)]:
            probe_name = join_name(probe_scope, self._name).get_fibre_name()
            if probe_name in value_types:
                return value_types[probe_name]
            elif probe_name.startswith("fibre.Ref<"):
                return get_ref_type(interfaces[probe_name[10:-1]]) # TODO: this is a bit hacky

        raise Exception('could not resolve type {} in {}. Known value types are: {}'.format(self._name, self._scope, list(value_types.keys())))


class InterfaceElement():
    def __init__(self, path, name, elem):
        if elem is None:
            elem = {}
        assert(isinstance(elem, dict))

        #self.name = split_name(name)[-1]
        self.name = NameInfo(path, name)
        #self.c_name = elem.get('c_name', self.fullname.replace('.', 'Intf::')) + 'Intf'
        
        if not 'implements' in elem:
            elem['implements'] = []
        elif isinstance(elem['implements'], str):
            elem['implements'] = [elem['implements']]
        self.implements = [InterfaceRefElement(self.name, None, elem, {}) for elem in elem['implements']]
        self.functions = OrderedDict((name, regularize_func(self, name, func, {'obj': {'type': 'fibre.Ref<' + self.name.get_fibre_name() + '>'}}))
                                        for name, func in get_dict(elem, 'functions').items())
        if not 'c_is_class' in elem:
            raise Exception('c_is_class missing in ' + str(elem))
        treat_as_class = elem['c_is_class'] # TODO: add command line arg to make this selectively optional
        self.attributes = OrderedDict((name, regularize_attribute(self, name, prop, treat_as_class))
                                        for name, prop in get_dict(elem, 'attributes').items())
        self.interfaces = []
        self.enums = []

    def get_all_attributes(self, stack=[]):
        result = OrderedDict()
        for intf in self.implements:
            assert(not self in stack)
            result.update(intf.get_all_attributes(stack + [self]))
        result.update(self.attributes)
        return result

    def get_all_functions(self, stack=[]):
        result = OrderedDict()
        for intf in self.implements:
            assert(not self in stack)
            result.update(intf.get_all_functions(stack + [self]))
        result.update(self.functions)
        return result


class ValueTypeElement():
    def __init__(self, path, name, elem):
        if elem is None:
            elem = {}
        print(type(elem))
        assert(isinstance(elem, dict))

        self.name = join_name(path, name)
        #elem['c_name'] = elem.get('c_name', elem['fullname'].replace('.', 'Intf::'))
        #value_types[self.name.get_fibre_name()] = self

        if 'flags' in elem: # treat as flags
            bit = 0
            for k, v in elem['flags'].items():
                elem['flags'][k] = elem['flags'][k] or OrderedDict()
                elem['flags'][k]['name'] = k
                current_bit = elem['flags'][k].get('bit', bit)
                elem['flags'][k]['bit'] = current_bit
                elem['flags'][k]['value'] = 0 if current_bit is None else (1 << current_bit)
                bit = bit if current_bit is None else current_bit + 1
            if 'nullflag' in elem:
                elem['flags'] = OrderedDict([(elem['nullflag'], OrderedDict({'name': elem['nullflag'], 'value': 0, 'bit': None})), *elem['flags'].items()])
            self.values = elem['flags']
            self.is_flags = True
            self.is_enum = True
            enums[path] = elem

        elif 'values' in elem: # treat as enum
            val = 0
            self.values = {}
            for k, v in elem['values'].items():
                self.values[k] = elem['values'][k] or OrderedDict()
                self.values[k]['name'] = k
                val = self.values[k].get('value', val)
                self.values[k]['value'] = val
                val += 1
            enums[path] = elem
            self.is_flags = False
            self.is_enum = True

        else:
            print(elem)
            raise Exception("unable to interpret as value type")


class RefType(ValueTypeElement):
    def __init__(self, interface):
        assert(isinstance(interface, InterfaceElement))
        self.name = NameInfo('fibre', 'Ref<' + interface.name.get_fibre_name() + '>')


class BasicValueType(ValueTypeElement):
    def __init__(self, name, info):
        self.name = name
        self.c_name = info['c_name']


class PropertyInterfaceElement(InterfaceElement):
    def __init__(self, name, mode, value_type):
        self.name = name
        #self.c_name = 'Property<' + ('const ' if mode == 'readonly' else '') + value_type['c_name'] + '>'
        self.value_type = value_type # TODO: should be a metaarg
        self.mode = mode # TODO: should be a metaarg
        self.builtin = True
        self.attributes = OrderedDict()
        self.functions = OrderedDict()
        if mode != 'readonly':
            self.functions['exchange'] = {
                'name': join_name(name, 'exchange'),
                'intf': self,
                'in': OrderedDict([('obj', ArgumentElement(name, 'obj', 'fibre.Ref<' + self.name.get_fibre_name() + '>')),
                                   ('value', ArgumentElement(name, 'value', value_type.name.get_fibre_name()))]),
                'out': OrderedDict([('value', ArgumentElement(name, 'value', value_type.name.get_fibre_name()))]),
                #'implementation': 'fibre_property_exchange<' + value_type['c_name'] + '>'
            }
        self.functions['read'] = {
            'name': join_name(name, 'read'),
            'intf': self,
            'in': OrderedDict([('obj', ArgumentElement(name, 'obj', 'fibre.Ref<' + self.name.get_fibre_name() + '>'))]),
            'out': OrderedDict([('value', ArgumentElement(name, 'value', value_type.name.get_fibre_name()))]),
            #'implementation': 'fibre_property_read<' + value_type['c_name'] + '>'
        }


def get_dict(elem, key):
    return elem.get(key, None) or OrderedDict()

def regularize_func(intf, name, elem, prepend_args):
    if elem is None:
        elem = {}
    elem['intf'] = intf
    elem['name'] = join_name(intf.name, name)
    elem['in'] = OrderedDict((n, ArgumentElement(elem['name'], n, arg))
                             for n, arg in (*prepend_args.items(), *get_dict(elem, 'in').items()))
    elem['out'] = OrderedDict((n, ArgumentElement(elem['name'], n, arg))
                              for n, arg in get_dict(elem, 'out').items())
    return elem

def regularize_attribute(parent, name, elem, c_is_class):
    if elem is None:
        elem = {}
    if isinstance(elem, str):
        elem = {'type': elem}
    elif not 'type' in elem:
        elem['type'] = {}
        if 'attributes' in elem: elem['type']['attributes'] = elem.pop('attributes')
        if 'functions' in elem: elem['type']['functions'] = elem.pop('functions')
        if 'implements' in elem: elem['type']['implements'] = elem.pop('implements')
        if 'c_is_class' in elem: elem['type']['c_is_class'] = elem.pop('c_is_class')
        if 'values' in elem: elem['type']['values'] = elem.pop('values')
        if 'flags' in elem: elem['type']['flags'] = elem.pop('flags')
        if 'nullflag' in elem: elem['type']['nullflag'] = elem.pop('nullflag')
    
    #elem['name'] = name
    elem['name'] = join_name(parent.name, name)
    elem['parent'] = parent
    elem['typeargs'] = elem.get('typeargs', {})
    #elem['c_name'] = elem.get('c_name', None) or (elem['name'] + ('_' if c_is_class else ''))
    if ('c_getter' in elem) or ('c_setter' in elem):
        elem['c_getter'] = elem.get('c_getter', elem['c_name'])
        elem['c_setter'] = elem.get('c_setter', elem['c_name'] + ' = ')

    if isinstance(elem['type'], str) and elem['type'].startswith('readonly '):
        elem['typeargs']['fibre.Property.mode'] = 'readonly'
        elem['typeargs']['fibre.Property.type'] = ValueTypeRefElement(parent.name, None, elem['type'][len('readonly '):])
        elem['type'] = InterfaceRefElement(parent.name, None, 'fibre.Property', elem['typeargs'])
        if elem['typeargs']['fibre.Property.mode'] == 'readonly' and 'c_setter' in elem: elem.pop('c_setter')
    elif ('flags' in elem['type']) or ('values' in elem['type']):
        elem['typeargs']['fibre.Property.mode'] = elem['typeargs'].get('fibre.Property.mode', None) or 'readwrite'
        elem['typeargs']['fibre.Property.type'] = ValueTypeRefElement(parent.name, to_pascal_case(name), elem['type'])
        elem['type'] = InterfaceRefElement(parent.name, None, 'fibre.Property', elem['typeargs'])
        if elem['typeargs']['fibre.Property.mode'] == 'readonly' and 'c_setter' in elem: elem.pop('c_setter')
    else:
        elem['type'] = InterfaceRefElement(parent.name, to_pascal_case(name), elem['type'], elem['typeargs'])
    return elem

def get_ref_type(interface):
    name = NameInfo('fibre', 'Ref<' + interface.name.get_fibre_name() + '>')
    ref_type = value_types.get(name.get_fibre_name(), None)
    if ref_type is None:
        value_types[name.get_fibre_name()] = ref_type = RefType(interface)
    return ref_type

def get_property_type(typeargs):
    assert(isinstance(typeargs['fibre.Property.type'], ValueTypeElement))
    value_type = typeargs['fibre.Property.type']
    mode = typeargs.get('fibre.Property.mode', 'readwrite')
    name = NameInfo('fibre', 'Property<' + value_type.name.get_fibre_name() + ', ' + mode + '>')
    prop_type = interfaces.get(name.get_fibre_name(), None)
    if prop_type is None:
        interfaces[name.get_fibre_name()] = prop_type = PropertyInterfaceElement(name, mode, value_type)
        deep_resolve(prop_type)
    return prop_type





def map_to_fibre01_type(t):
    if hasattr(t, 'is_enum') and t.is_enum:
        max_val = max(v['value'] for v in t['values'].values())
        if max_val <= 0xff:
            return 'uint8'
        elif max_val <= 0xffff:
            return 'uint16'
        elif max_val <= 0xffffffff:
            return 'uint32'
        elif max_val <= 0xffffffffffffffff:
            return 'uint64'
        else:
            raise Exception("enum with a maximum value of " + str(max_val) + " not supported")
    elif t.name.get_fibre_name() == 'float32':
        return 'float'
    return t.name.get_fibre_name()

legacy_sizes = {'uint8': 1, 'int8': 1, 'uint16': 2, 'int16': 2, 'uint32': 4,
                'int32': 4, 'uint64': 8, 'int64': 8, 'float': 1}

def generate_endpoint_for_property(prop, attr_bindto, idx):
    prop_intf = interfaces[prop['type'].fullname]

    endpoint = {
        'id': idx,
        'function': prop_intf.functions['read' if prop['type'].mode == 'readonly' else 'exchange'],
        'in_bindings': OrderedDict([('obj', attr_bindto)]),
        'out_bindings': OrderedDict()
    }
    endpoint_definition = {
        'name': prop['name'],
        'id': idx,
        'type': map_to_fibre01_type(prop['type'].value_type),
        'access': 'r' if prop['type'].mode == 'readonly' else 'rw',
    }
    return endpoint, endpoint_definition

def generate_endpoint_table(intf, bindto, idx):
    """
    Generates a Fibre v0.1 endpoint table for a given interface.
    This will probably be deprecated in the future.
    The object must have no circular property types (i.e. A.b has type B and B.a has type A).
    """
    endpoints = []
    endpoint_definitions = []
    cnt = 0

    for k, prop in intf.get_all_attributes().items():
        property_value_type = re.findall('^fibre\.Property<([^>]*), (readonly|readwrite)>$', prop['type'].name.get_fibre_name())
        #attr_bindto = join_name(bindto, bindings_map.get(join_name(intf['fullname'], k), k + ('_' if len(intf['functions']) or (intf['fullname'] in treat_as_classes) else '')))
        attr_bindto = intf.c_name + '::get_' + prop['name'] + '(' + bindto + ')'
        if len(property_value_type):
            # Special handling for Property<...> attributes: they resolve to one single endpoint
            endpoint, endpoint_definition = generate_endpoint_for_property(prop, attr_bindto, idx + cnt)
            endpoints.append(endpoint)
            endpoint_definitions.append(endpoint_definition)
            cnt += 1
        else:
            inner_endpoints, inner_endpoint_definitions, inner_cnt = generate_endpoint_table(prop['type'], attr_bindto, idx + cnt)
            endpoints += inner_endpoints
            endpoint_definitions.append({
                'name': k,
                'type': 'object',
                'members': inner_endpoint_definitions
            })
            cnt += inner_cnt

    for k, func in intf.get_all_functions().items():
        endpoints.append({
            'id': idx + cnt,
            'function': func,
            'in_bindings': OrderedDict([('obj', bindto), *[(k_arg, '(' + bindto + ')->' + func['name'] + '_in_' + k_arg + '_') for k_arg in list(func['in'].keys())[1:]]]),
            'out_bindings': OrderedDict((k_arg, '&(' + bindto + ')->' + func['name'] + '_out_' + k_arg + '_') for k_arg in func['out'].keys()),
        })
        in_def = []
        out_def = []
        for i, (k_arg, arg) in enumerate(list(func['in'].items())[1:]):
            endpoint, endpoint_definition = generate_endpoint_for_property({
                'name': arg.name,
                'type': get_property_type({'fibre.Property.type': arg.type, 'fibre.Property.mode': 'readwrite'})
            }, intf.c_name + '::get_' + func['name'] + '_in_' + k_arg + '_' + '(' + bindto + ')', idx + cnt + 1 + i)
            endpoints.append(endpoint)
            in_def.append(endpoint_definition)
        for i, (k_arg, arg) in enumerate(func['out'].items()):
            endpoint, endpoint_definition = generate_endpoint_for_property({
                'name': arg.name,
                'type': get_property_type({'fibre.Property.type': arg.type, 'fibre.Property.mode': 'readonly'})
            }, intf.c_name + '::get_' + func['name'] + '_out_' + k_arg + '_' + '(' + bindto + ')', idx + cnt + len(func['in']) + i)
            endpoints.append(endpoint)
            out_def.append(endpoint_definition)

        endpoint_definitions.append({
            'name': k,
            'id': idx + cnt,
            'type': 'function',
            'inputs': in_def,
            'outputs': out_def
        })
        cnt += len(func['in']) + len(func['out'])

    return endpoints, endpoint_definitions, cnt


# Parse arguments

parser = argparse.ArgumentParser(description="Gernerate code from YAML interface definitions")
parser.add_argument("--version", action="store_true",
                    help="print version information")
parser.add_argument("-v", "--verbose", action="store_true",
                    help="print debug information (on stderr)")
parser.add_argument("-d", "--definitions", type=argparse.FileType('r', encoding='utf-8'), nargs='+',
                    help="the YAML interface definition file(s) used to generate the code")
parser.add_argument("-t", "--template", type=argparse.FileType('r', encoding='utf-8'),
                    help="the code template")
group = parser.add_mutually_exclusive_group(required=True)
group.add_argument("-o", "--output", type=argparse.FileType('w', encoding='utf-8'),
                    help="path of the generated output")
group.add_argument("--outputs", type=str,
                    help="path pattern for the generated outputs. One output is generated for each interface. Use # as placeholder for the interface name.")
parser.add_argument("--generate-endpoints", type=str, nargs='?',
                    help="if specified, an endpoint table will be generated and passed to the template for the specified interface")
args = parser.parse_args()

if args.version:
    print("0.1.0")
    sys.exit(0)


definition_files = args.definitions
template_file = args.template


# Load definition files

# Add built-in types
value_types = OrderedDict({
    'bool': BasicValueType(NameInfo('bool'), {'c_name': 'bool', 'py_type': 'bool'}),
    'float32': BasicValueType(NameInfo('float32'), {'c_name': 'float', 'py_type': 'float'}),
    'uint8': BasicValueType(NameInfo('uint8'), {'c_name': 'uint8_t', 'py_type': 'int'}),
    'uint16': BasicValueType(NameInfo('uint16'), {'c_name': 'uint16_t', 'py_type': 'int'}),
    'uint32': BasicValueType(NameInfo('uint32'), {'c_name': 'uint32_t', 'py_type': 'int'}),
    'uint64': BasicValueType(NameInfo('uint64'), {'c_name': 'uint64_t', 'py_type': 'int'}),
    'int8': BasicValueType(NameInfo('int8'), {'c_name': 'int8_t', 'py_type': 'int'}),
    'int16': BasicValueType(NameInfo('int16'), {'c_name': 'int16_t', 'py_type': 'int'}),
    'int32': BasicValueType(NameInfo('int32'), {'c_name': 'int32_t', 'py_type': 'int'}),
    'int64': BasicValueType(NameInfo('int64'), {'c_name': 'int64_t', 'py_type': 'int'}),
    'endpoint_ref': BasicValueType(NameInfo('endpoint_ref'), {'c_name': 'endpoint_ref_t', 'py_type': '[not implemented]'}),
})

enums = OrderedDict()
interfaces = OrderedDict()
userdata = OrderedDict() # Arbitrary data passed from the definition file to the template
exports = OrderedDict()

for definition_file in definition_files:
    try:
        file_content = yaml.load(definition_file, Loader=SafeLineLoader)
    except yaml.scanner.ScannerError as ex:
        print("YAML parsing error: " + str(ex), file=sys.stderr)
        sys.exit(1)
    for err in validator.iter_errors(file_content):
        if '__line__' in err.absolute_path:
            continue
        if '__column__' in err.absolute_path:
            continue
        #instance = err.instance.get(re.findall("([^']*)' (?:was|were) unexpected\)", err.message)[0], err.instance)
        # TODO: print line number
        raise Exception(err.message + '\nat ' + str(list(err.absolute_path)))

    # Regularize everything into a wellknown form
    for k, item in list(get_dict(file_content, 'interfaces').items()):
        interfaces[NameInfo(k).get_fibre_name()] = InterfaceElement(NameInfo(), k, item)
    for k, item in list(get_dict(file_content, 'valuetypes').items()):
        value_types[NameInfo(k).get_fibre_name()] = ValueTypeElement(NameInfo(), k, item)
    for k, item in list(get_dict(file_content, 'exports').items()):
        exports[k] = InterfaceRefElement(NameInfo(), k, item, [])

    userdata.update(get_dict(file_content, 'userdata'))
    dictionary += file_content.get('dictionary', None) or []




if args.verbose:
    print('Known interfaces: ' + ''.join([('\n  ' + k) for k in interfaces.keys()]))
    print('Known value types: ' + ''.join([('\n  ' + k) for k in value_types.keys()]))

clashing_names = list(set(value_types.keys()).intersection(set(interfaces.keys())))
if len(clashing_names):
    print("**Error**: Found both an interface and a value type with the name {}. This is not allowed, interfaces and value types (such as enums) share the same namespace.".format(clashing_names[0]), file=sys.stderr)
    sys.exit(1)

# We init generics this late because they must not be resolved prior to the
# resolve phase.
generics = {
    'fibre.Property': get_property_type # TODO: improve generic support
}

def deep_resolve(intf):
    #print("resolving", intf.name, "")
    assert(isinstance(intf, InterfaceElement))
    for k, attr in intf.attributes.items():
        #print("resolving attr ", k)
        attr['type'] = attr['type'].resolve()
    for _, func in intf.functions.items():
        for _, arg in func['in'].items():
            arg.type = arg.type.resolve()
        for _, arg in func['out'].items():
            arg.type = arg.type.resolve()

# Resolve all types to references
for _, item in list(interfaces.items()):
    item.implements = [ref.resolve() for ref in item.implements]
    deep_resolve(item)

# Attach interfaces to their parents
toplevel_interfaces = []
for k, item in list(interfaces.items()):
    k = split_name(k)
    item.parent = None
    if len(k) == 1:
        toplevel_interfaces.append(item)
    else:
        if k[:-1] != ['fibre']: # TODO: remove special handling
            parent = interfaces[join_name(*k[:-1]).get_fibre_name()]
            parent.interfaces.append(item)
            item.parent = parent
toplevel_enums = []
for k, item in list(enums.items()):
    k = split_name(k)
    if len(k) == 1:
        toplevel_enums.append(item)
    else:
        if k[:-1] != ['fibre']: # TODO: remove special handling
            parent = interfaces[join_name(*k[:-1]).get_fibre_name()]
            parent.enums.append(item)
            item['parent'] = parent


exported_functions = []
exported_interfaces = []
exported_objects = {}
published_objects = []
endpoint_table = [{}] # LEGACY

def exported_func(func):
    if func in exported_functions:
        return
    func['id'] = len(exported_functions)
    exported_functions.append(func)

def export_intf(intf):
    if intf in exported_interfaces:
        return
    intf.id = len(exported_interfaces)
    exported_interfaces.append(intf)
    for k, attr in intf.attributes.items():
        export_intf(attr['type'])
    for _, func in intf.functions.items():
        exported_func(func)

def export_obj(obj, intf_stack):
    if obj['type'] in intf_stack:
        raise Exception("circular attribute tree not yet supported")

    obj['id'] = len(exported_objects)
    intf = obj['type']
    exported_objects[obj['name'].get_fibre_name()] = obj


    # LEGACY SUPPORT
    property_value_type = re.findall('^fibre\.Property<([^>]*), (readonly|readwrite)>$', intf.name.get_fibre_name())
    if len(property_value_type):
        # Special handling for Property<...> attributes: they resolve to one single endpoint
        endpoint_descr = {
            'name': obj['name'][-1],
            'id': len(endpoint_table),
            'type': map_to_fibre01_type(intf.value_type),
            'access': 'r' if intf.mode == 'readonly' else 'rw',
        }
        endpoint_table.append(
            '{.type = EndpointType::kRoProperty, .ro_property = {.read_function_id = ' + str(intf.functions['read']['id']) + ', .object_id = ' + str(obj['id']) + '}}'
            if intf.mode == 'readonly' else
            '{.type = EndpointType::kRwProperty, .rw_property = {.read_function_id = ' + str(intf.functions['read']['id']) + ', .exchange_function_id = ' + str(intf.functions['exchange']['id']) + ', .object_id = ' + str(obj['id']) + '}}'
        )
    else:
        endpoint_descr = {
            'name': obj['name'][-1],
            'type': 'object',
            'members': []
        }
        for k, func in intf.get_all_functions().items():
            fn_desc = {
                'name': k,
                'id': len(endpoint_table),
                'type': 'function',
                'inputs': [],
                'outputs': []
            }
            endpoint_table.append(
                '{.type = EndpointType::kFunctionTrigger, .function_trigger = {.function_id = ' + str(func['id']) + ', .object_id = ' + str(obj['id']) + '}}'
            )
            for i, (k_arg, arg) in enumerate(list(func['in'].items())[1:]):
                fn_desc['inputs'].append({
                    'name': k_arg,
                    'id': len(endpoint_table),
                    'type': map_to_fibre01_type(arg.type),
                    'access': 'rw',
                })
                endpoint_table.append(
                    '{.type = EndpointType::kFunctionInput, .function_input = {.size = ' + str(legacy_sizes[map_to_fibre01_type(arg.type)]) + '}}'
                )
            for i, (k_arg, arg) in enumerate(func['out'].items()):
                fn_desc['outputs'].append({
                    'name': k_arg,
                    'id': len(endpoint_table),
                    'type': map_to_fibre01_type(arg.type),
                    'access': 'r',
                })
                endpoint_table.append(
                    '{.type = EndpointType::kFunctionOutput, .function_output = {.size = ' + str(legacy_sizes[map_to_fibre01_type(arg.type)]) + '}}'
                )
            endpoint_descr['members'].append(fn_desc)

    for k, attr in intf.attributes.items():
        endpoint_descr['members'].append(export_obj({'name': NameInfo(obj['name'], k), 'type': attr['type']}, intf_stack + [obj['type']]))
    
    return endpoint_descr



endpoint_descr = [{'name': '', 'id': 0, 'type': 'json', 'access': 'r'}] # LEGACY



for name, export in exports.items():
    intf = export.resolve()
    export_intf(intf)
    obj = {'name': NameInfo(name), 'type': intf}
    endpoint_descr += export_obj(obj, [])['members']
    published_objects.append(obj)

    

## Legacy support


#def generate_endpoints(intf):
#    table = []
#
#    for k, prop in intf.get_all_attributes().items():
#    return table


#for name, export in exports.items():
#    intf = export.resolve()
#    endpoint_descr += generate_endpoints(intf)

#if args.generate_endpoints:
#    endpoints, embedded_endpoint_definitions, _ = generate_endpoint_table(interfaces[args.generate_endpoints])
#    embedded_endpoint_definitions = 
#        
#    
#     + embedded_endpoint_definitions
#    endpoints = [{'id': 0, 'function': {'fullname': 'endpoint0_handler', 'in': {}, 'out': {}}, 'bindings': {}}] + endpoints
#else:
#    embedded_endpoint_definitions = None
#    endpoints = None


# Render template

env = jinja2.Environment(
    comment_start_string='[#', comment_end_string='#]',
    block_start_string='[%', block_end_string='%]',
    variable_start_string='[[', variable_end_string=']]'
)

def tokenize(text, interface, interface_transform, value_type_transform, attribute_transform):
    """
    Looks for referencable tokens (interface names, value type names or
    attribute names) in a documentation text and runs them through the provided
    processing functions.
    Tokens are detected by enclosing back-ticks (`).

    interface: The interface type object that defines the scope in which the
               tokens should be detected.
    interface_transform: A function that takes an interface object as an argument
                         and returns a string.
    value_type_transform: A function that takes a value type object as an argument
                          and returns a string.
    attribute_transform: A function that takes the token string, an interface object
                         and an attribute object as arguments and returns a string.
    """
    if text is None or isinstance(text, jinja2.runtime.Undefined):
        return text

    def token_transform(token):
        token = token.groups()[0]

        if ':' in token:
            intf_name, _, attr_name = token.partition(':')
            intf = InterfaceRefElement(interface.fullname, None, intf_name, []).resolve()
            token = attr_name
        else:
            intf = interface

        token_list = split_name(token)

        # Check if this is an attribute reference
        scope = intf
        attr = None

        for name in token_list:
            if scope.fullname == 'ODrive': # TODO: this is a temporary hack
                scope = interfaces['ODrive3']
            if (not name.endswith('()')) and name in scope.get_all_attributes():
                attr = scope.get_all_attributes()[name]
                scope = attr['type']
            elif name.endswith('()') and name[:-2] in scope.get_all_functions():
                attr = scope.get_all_functions()[name[:-2]]
                scope = None # TODO
            else:
                print('Warning: cannot resolve "{}" in {}'.format(token, intf.fullname))
                return "`" + token + "`"
        
        return attribute_transform(token, interface, intf, attr)


    return re.sub(r'`([A-Za-z0-9\.:_]+)`', token_transform, text)

def html_escape(text):
    import html
    return html.escape(str(text))

env.filters['to_pascal_case'] = to_pascal_case
env.filters['to_camel_case'] = to_camel_case
env.filters['to_macro_case'] = to_macro_case
env.filters['to_snake_case'] = to_snake_case
env.filters['to_kebab_case'] = to_kebab_case
env.filters['first'] = lambda x: next(iter(x))
env.filters['skip_first'] = lambda x: list(x)[1:]
env.filters['to_c_string'] = lambda x: '\n'.join(('"' + line.replace('"', '\\"') + '"') for line in json.dumps(x, separators=(',', ':')).replace('{"name"', '\n{"name"').split('\n'))
env.filters['tokenize'] = tokenize
env.filters['html_escape'] = html_escape
env.filters['diagonalize'] = lambda lst: [lst[:i + 1] for i in range(len(lst))]
env.filters['debug'] = lambda x: print(x)

template = env.from_string(template_file.read())

template_args = {
    'interfaces': interfaces,
    'value_types': value_types,
    'toplevel_interfaces': toplevel_interfaces,
    'exported_functions': exported_functions,
    'exported_interfaces': exported_interfaces,
    'exported_objects': exported_objects,
    'published_objects': published_objects,
    'userdata': userdata,
    'endpoint_table': endpoint_table, # deprecated
    'endpoint_descr': endpoint_descr, # deprecated
}

if not args.output is None:
    output = template.render(**template_args)
    args.output.write(output)
else:
    assert('#' in args.outputs)

    for k, intf in interfaces.items():
        if split_name(k)[0] == 'fibre':
            continue # TODO: remove special case
        output = template.render(interface = intf, **template_args)
        with open(args.outputs.replace('#', k.lower()), 'w', encoding='utf-8') as output_file:
            output_file.write(output)

    for k, enum in value_types.items():
        if enum.get('builtin', False) or not enum.get('is_enum', False):
            continue
        output = template.render(enum = enum, **template_args)
        with open(args.outputs.replace('#', k.lower()), 'w', encoding='utf-8') as output_file:
            output_file.write(output)
