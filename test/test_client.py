#!/usr/bin/env python3

import sys, os
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.realpath(__file__))) + "/python")
import fibre

with fibre.Domain("tcp-client:address=localhost,port=14220") as domain:
    obj = domain.discover_one()
    obj.func00()
