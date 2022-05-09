#!/usr/bin/env python3
"""
The real nbdkit module is only available when the Python interpreter
runs inside the nbdkit binary. To get unit tests to pass, we provide this
"stub" module that provides just the minimum attributes to do
unit testing.
"""

import sys


def parse_size(v):
    return int(v)


def debug(msg):
    print(msg, file=sys.stderr)


def set_error(err):
    pass
