
## Interface Definition File Structure

This section describes the structure of Fibre interface definition YAML files. Use this as reference if you're writing YAML files for Fibre.

**TODO**


## Template API

This section describes what data is available to a template that is run through the `interface_generator.py`. Use this as reference if you're writing template files.

### Globals

  - `interfaces` (`list<interface>`): List of all interfaces defined in the input file(s) including on-demand generated interfaces (e.g. `fibre.Property`)
  - `enums` (`list<enum>`): List of all enums defined in the input file(s)
  - `exported_functions` (`list<function>`): List of all statically exported functions
  - `exported_interfaces` (`list<interface>`): List of all statically exported interfaces
  - `exported_objects` (`list<object>`): Expanded list of all statically exported objects that should be directly addressable by object ID.
  - `published_objects` (`list<object>`): List of toplevel published objects. This is a subset of `exported_objects`.

### `interface` object
  - `name` (`name_info`): Name of the interface.
  - `id` (`int`): If this interface is statically exported, this is the ID of the interface.
  - `functions` (`dict<string, function>`): Functions implemented by the interface
  - `attributes` (`dict<string, attribute>`): Attributes implemented by the interface

### `enum` object
  - `name` (`name_info`): Name of the enum
  - `is_flags` (`bool`): Indicates if this enum is a bit field or not
  - `nullflag` (`string`): Name of the enumerator that represents the absence of any flags (only present if `is_flags == true`)
  - `values` (`list<enumerator>`): Values of the enum

### `enumerator` object
  - `name` (`string`): Name of the enumerator value
  - `value` (`int`): Value of the enumerator value

### `name_info` object
  - `get_fibre_name() -> string`: Returns a string of the full name in canonical Fibre notation

### `function` object
  - `name` (`name_info`): Full name of the function including the interface.
  - `id` (`int`): If this function is statically exported, this is the ID of the function.
  - `intf` (`interface`): The interface that contains this function.
  - `in` (`dict<string, argument>`): Input arguments
  - `out` (`dict<string, argument>`): Output arguments

### `argument` object
  - `type` (`value_type`): Type of the argument

### `value_type` object
  - `name` (`name_info`): Name of the value type
  - `c_type` (`string`): C name of the data type (only present for basic value types, not for enums)

### `attribute` object
  - `name` (`name_info`): Name of the attribute
  - `type` (`interface`): Interface implemented by the attribute

### `object` object
  - `name` (`name_info`): Full name of the object including its parent objects
