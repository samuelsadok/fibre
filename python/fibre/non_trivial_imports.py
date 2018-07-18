
import sys
import time

import abc
if sys.version_info >= (3, 4):
    ABC = abc.ABC
else:
    ABC = abc.ABCMeta('ABC', (), {})

if sys.version_info < (3, 3):
    from monotonic import monotonic
    time.monotonic = monotonic
