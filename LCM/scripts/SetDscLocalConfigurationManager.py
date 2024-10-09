#!/usr/bin/python2
from imp                  import load_source
from os.path              import dirname, isfile, join, realpath
from subprocess           import PIPE, Popen
from sys                  import argv, exc_info, exit, version_info
from traceback            import format_exc
from fcntl                import flock, LOCK_EX, LOCK_UN, LOCK_NB
from OmsConfigHostHelpers import write_omsconfig_host_telemetry, write_omsconfig_host_switch_event, write_omsconfig_host_log, stop_old_host_instances
from time                 import sleep
import signal
import sys


pathToCurrentScript = realpath(__file__)
pathToCommonScriptsFolder = dirname(pathToCurrentScript)

DSCLogPath = join(pathToCommonScriptsFolder, 'nxDSCLog.py')
nxDSCLog = load_source('nxDSCLog', DSCLogPath)
LG = nxDSCLog.DSCLog

helperLibPath = join(pathToCommonScriptsFolder, 'helperlib.py')
helperlib = load_source('helperlib', helperLibPath)

operationStatusUtilityPath = join(pathToCommonScriptsFolder, 'OperationStatusUtility.py')
operationStatusUtility = load_source('operationStatusUtility', operationStatusUtilityPath)

operation = 'SetLCM'
proc = None

def usage():
    print("Usage:")
    print("    " + argv[0] + " -configurationmof FILE")
    write_omsconfig_host_log('Incorrect parameters to SetDscLocalConfigurationManager.py: ' + str(argv), pathToCurrentScript, 'WARNING')
    exit(1)

def main(args):
    try:
        apply_meta_config(args)
    except SystemExit:
        exit(exc_info()[1])
    except Exception:
        # Python 2.4-2.7 and 2.6-3 recognize different formats for exceptions. This methods works in all versions.
        formattedExceptionMessage = format_exc()
        write_omsconfig_host_log('Python exception raised from SetDscLocalConfigurationManager.py: ' + formattedExceptionMessage, pathToCurrentScript, 'ERROR')
        raise

def apply_meta_config(args):
    if len(args) != 3:
        usage()

    if args[1].lower() != '-configurationmof':
        usage()

    if (not isfile(args[2])):
        errorMessage = 'The provided configurationmof file does not exist: ' + str(args[2])
        print(errorMessage)
        write_omsconfig_host_log('Incorrect parameters to SetDscLocalConfigurationManager.py: ' + errorMessage, pathToCurrentScript, 'ERROR')
        exit(1)

    fileHandle = open(args[2], 'r')
    global proc
    try:
        fileContent = fileHandle.read()
        outtokens = []
        for char in fileContent:
            outtokens.append(str(ord(char)))

        omicli_path = join(helperlib.CONFIG_BINDIR, 'omicli')
        dsc_host_base_path = helperlib.DSC_HOST_BASE_PATH
        dsc_host_path = join(dsc_host_base_path, 'bin/dsc_host')
        dsc_host_output_path = join(dsc_host_base_path, 'output')
        dsc_host_lock_path = join(dsc_host_base_path, 'dsc_host_lock')
        dsc_host_switch_path = join(dsc_host_base_path, 'dsc_host_ready')

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
            parameters.append("SendMetaConfigurationApply")
            parameters.append(args[2])
        else:
            parameters.append(omicli_path)
            parameters.append("iv")
            parameters.append(helperlib.DSC_NAMESPACE)
            parameters.append("{")
            parameters.append("MSFT_DSCLocalConfigurationManager")
            parameters.append("}")
            parameters.append("SendMetaConfigurationApply")
            parameters.append("{")
            parameters.append("ConfigurationData")
            parameters.append("[")
            # Insert configurationmof data here
            for token in outtokens:
                parameters.append(token)
            parameters.append("]")
            parameters.append("}")

        exit_code = 0
        stdout = ''
        stderr = ''

        # Apply the metaconfig
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
                        write_omsconfig_host_log('dsc_host lock file is acquired by : SendMetaConfigurationApply', pathToCurrentScript)
                        dschostlock_acquired = True
                        break
                    except IOError:
                        write_omsconfig_host_log('dsc_host lock file not acquired. retry (#' + str(retry) + ') after 60 seconds...', pathToCurrentScript)
                        sleep(60)

                if dschostlock_acquired:
                    proc = Popen(parameters, stdout=PIPE, stderr=PIPE)
                    exit_code = proc.wait()
                    stdout, stderr = proc.communicate()
                    print(stdout)
                else:
                    print("dsc host lock already acuired by a different process")
            finally:
                if dschostlock_filehandle:
                    # Release dsc host file lock
                    flock(dschostlock_filehandle, LOCK_UN)

                    # Close dsc host lock file handle
                    dschostlock_filehandle.close()
        else:
            proc = Popen(parameters, stdout=PIPE, stderr=PIPE)
            exit_code = proc.wait()
            stdout, stderr = proc.communicate()

        print(stdout)

        if ((exit_code != 0) or (stderr)):
            exit(1)

    finally:
        fileHandle.close()

# function to handle ternimation signals received from omsagent.
# we kill the child dsc_host process if it is invoked and exit current script.
def signal_handler(signalNumber, frame):
    global proc
    write_omsconfig_host_log("SIGTERM signal received. Trying to kill any child process...", pathToCurrentScript)

    if proc is not None:
        write_omsconfig_host_log("Terminating child process (ID = " + str(proc.pid) + ") by sending SIGTERM signal.", pathToCurrentScript)
        proc.terminate()
    else:
        write_omsconfig_host_log("There is no child process running. It has either completed execution or has not been started.", pathToCurrentScript)
    
    write_omsconfig_host_log("Script exiting...", pathToCurrentScript)
    exit(1)

if __name__ == "__main__":
    # register the SIGTERM handler
    signal.signal(signal.SIGTERM, signal_handler)
    LG().Log("DEBUG", "Starting Main method for " + argv[0] + " runing with python " + str(sys.version_info))
    main(argv)
    LG().Log("DEBUG", "End of Main method for " +  argv[0] + " runing with python " + str(sys.version_info))