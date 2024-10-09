#!/usr/bin/python2
from datetime   import datetime
from errno      import EINVAL
from imp        import load_source
from json       import dump
from os         import chmod, mkdir, stat
from os.path    import dirname, join, isdir, isfile, realpath
from re         import search
from sys        import exc_info, version_info

if hasattr(datetime, 'strptime'):
    # Only available in this module in Python 2.5+
    strptime = datetime.strptime
else:
    # Only for Python 2.4
    import time
    strptime = lambda date_string, format: datetime(*(time.strptime(date_string, format)[0:6]))

pathToCurrentScript = realpath(__file__)
pathToCommonScriptsFolder = dirname(pathToCurrentScript)

def get_current_timestamp():
    currentDateTime = datetime.now()
    currentDateTimeFormattedString = datetime.strftime(currentDateTime, "%Y/%m/%d %H:%M:%S")
    return str(currentDateTimeFormattedString)

def get_permission_in_oct(threeDigitString):
    # Note: Python 2.4 and 3 recognize different formats for octal
    strMode = "0" + threeDigitString

    octMode = int(strMode, base = 8)
    return octMode

def ensure_file_permissions(filePath, permissionString):
    desiredPermission = get_permission_in_oct(permissionString)

    # Ensure that the file has the desired permissions if it exists
    if (isfile(filePath)):
        filePermission = oct(stat(filePath).st_mode & desiredPermission)
        if (not (filePermission == desiredPermission)):
            chmod(filePath, desiredPermission)
