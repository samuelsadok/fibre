#!/bin/bash
set -o pipefail

THIS_DIR="$(dirname "$0")"

DEFINITIONS_DIR="$THIS_DIR/dbus_interface_definitions/"
DEFINITION_FILES="$(cd "$DEFINITIONS_DIR" && find . -name "*.xml" -type f)"

while read file; do
    python "$THIS_DIR"/dbus_interface_parser.py \
        -d "$DEFINITIONS_DIR/$file" \
        -t "$THIS_DIR"/dbus_interface.c.j2 \
        -o "$THIS_DIR/../cpp/dbus_interfaces/${file%xml}hpp"
done <<< "$DEFINITION_FILES"
