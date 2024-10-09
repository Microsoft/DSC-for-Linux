#!/usr/bin/python2
import fileinput
import sys
import subprocess
import json
import time
import datetime
import os
import os.path
from OmsConfigHostHelpers import write_omsconfig_host_telemetry, write_omsconfig_host_switch_event, write_omsconfig_host_log, stop_old_host_instances
from imp            import load_source
from os.path        import dirname, isfile, join, realpath
from fcntl          import flock, LOCK_EX, LOCK_UN, LOCK_NB
from sys            import argv


pathToCurrentScript = realpath(__file__)
pathToCommonScriptsFolder = dirname(pathToCurrentScript)

DSCLogPath = join(pathToCommonScriptsFolder, 'nxDSCLog.py')
nxDSCLog = load_source('nxDSCLog', DSCLogPath)
LG = nxDSCLog.DSCLog

helperLibPath = join(pathToCommonScriptsFolder, 'helperlib.py')
helperlib = load_source('helperlib', helperLibPath)

omicli_path = join(helperlib.CONFIG_BINDIR, 'omicli')
dsc_host_base_path = helperlib.DSC_HOST_BASE_PATH
dsc_host_path = join(dsc_host_base_path, 'bin/dsc_host')
dsc_host_output_path = join(dsc_host_base_path, 'output')
dsc_host_lock_path = join(dsc_host_base_path, 'dsc_host_lock')
dsc_host_switch_path = join(dsc_host_base_path, 'dsc_host_ready')

LG().Log("DEBUG", "Starting script logic for " + argv[0]+ " runing with python " + str(sys.version_info))

if ("omsconfig" in helperlib.DSC_SCRIPT_PATH):
    write_omsconfig_host_switch_event(pathToCurrentScript, isfile(dsc_host_switch_path))

if ("omsconfig" in helperlib.DSC_SCRIPT_PATH) and (isfile(dsc_host_switch_path)):
    use_omsconfig_host = True
else:
    use_omsconfig_host = False

parameters = []

if use_omsconfig_host:
    parameters.append(dsc_host_path)
    parameters.append(dsc_host_output_path)
    parameters.append("GetConfiguration")
else:
    parameters.append(omicli_path)
    parameters.append("iv")
    parameters.append("<DSC_NAMESPACE>")
    parameters.append("{")
    parameters.append("MSFT_DSCLocalConfigurationManager")
    parameters.append("}")
    parameters.append("GetConfiguration")

stdout = ''
stderr = ''

if use_omsconfig_host:
    try:
        dschostlock_filehandle = None
        stop_old_host_instances(dsc_host_lock_path)

        # Open the dsc host lock file. This also creates a file if it does not exist
        dschostlock_filehandle = open(dsc_host_lock_path, 'w')
        print("Opened the dsc host lock file at the path '" + dsc_host_lock_path + "'")
        
        dschostlock_acquired = False

        # Acquire dsc host file lock
        for retry in range(10):
            try:
                flock(dschostlock_filehandle, LOCK_EX | LOCK_NB)
                dschostlock_acquired = True
                break
            except IOError:
                write_omsconfig_host_log('dsc_host lock file not acquired. retry (#' + str(retry) + ') after 60 seconds...', pathToCurrentScript)
                sleep(60)

        if dschostlock_acquired:
            p = subprocess.Popen(parameters, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            stdout, stderr = p.communicate()
            print(stdout)
        else:
            print("dsc host lock already acuired by a different process")
    finally:
        if (dschostlock_filehandle):
            # Release dsc host file lock
            flock(dschostlock_filehandle, LOCK_UN)

            # Close dsc host lock file handle
            dschostlock_filehandle.close()
else:
    p = subprocess.Popen(parameters, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout, stderr = p.communicate()

print(stdout)
print(stderr)

LG().Log("DEBUG", "End of script logic for " +  argv[0] + " runing with python " + str(sys.version_info))

