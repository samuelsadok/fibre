#!/usr/bin/env python3

import sys, os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.realpath(__file__))) + "/python")
import fibre

with fibre.Domain("tcp-client:address=localhost,port=14220") as domain:
    obj = domain.discover_one()

    try:
        obj.inexistent_prop = 123
        failed = False
    except AttributeError as ex:
        failed = True
    assert(failed) # above line should throw an exception

    assert(obj.prop_uint32 == 135)
    assert(obj.prop_uint32 == 135)
    assert(obj.prop_uint32_rw == 246)
    assert(obj.prop_uint32_rw == 246)
    obj.prop_uint32_rw = 789
    assert(obj.prop_uint32_rw == 789)
    obj.func00()
    assert(obj.func01() == 123)
    assert(obj.subobj.subfunc() == 321)
    assert(obj.func02() == (456, 789))
    obj.func10(1)
    assert(obj.func11(1) == 123)
    assert(obj.func12(1) == (456, 789))
    obj.func20(1, 2)
    assert(obj.func21(1, 2) == 123)
    assert(obj.func22(1, 2) == (456, 789))

    as_str = str(obj)
    print(as_str)
    assert(len(as_str) > 100)

print("done")
