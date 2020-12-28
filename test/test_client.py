#!/usr/bin/env python3

import sys, os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.realpath(__file__))) + "/python")
import fibre

with fibre.Domain("tcp-client:address=localhost,port=14220") as domain:
    obj = domain.discover_one()
    obj.func00()
    obj.func01()
    obj.func02()
    obj.func10(1)
    obj.func11(1)
    obj.func12(1)
    obj.func20(1, 2)
    obj.func21(1, 2)
    obj.func22(1, 2)
