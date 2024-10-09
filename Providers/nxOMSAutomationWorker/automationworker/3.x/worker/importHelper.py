#!/usr/bin/env python3
# ====================================
#  Copyright (c) Microsoft Corporation. All rights reserved.
# ====================================
PY_MAJOR_VERSION = 0
PY_MINOR_VERSION = 2

CONFIGPARSER_KEY = "configparser"
QUEUE_KEY = "queue"
import sys

def install_aliases():
    """
    Imports the relevant modules based on the python runtime version.
    After calling this method the caller can import the latest packages without having to consider about the 
    backward compatibility.

    Currently supported packages:
    1. ConfigParser
    2. Queue

    """
    
    version_info = sys.version
    if int(version_info[PY_MAJOR_VERSION]) == 2 and int(version_info[PY_MINOR_VERSION]) <= 7:
        import ConfigParser
        import Queue
        sys.modules[CONFIGPARSER_KEY] = ConfigParser
        sys.modules[QUEUE_KEY] = Queue
    else:
        return

