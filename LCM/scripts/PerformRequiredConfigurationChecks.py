#!/usr/bin/python2
import sys
from imp        import load_source
from os.path    import dirname, join, realpath, isfile
from subprocess import PIPE, Popen
from sys        import exc_info, exit, version_info, argv
from traceback  import format_exc
from fcntl      import flock, LOCK_EX, LOCK_UN, LOCK_NB
from OmsConfigHostHelpers import write_omsconfig_host_telemetry, write_omsconfig_host_switch_event, write_omsconfig_host_log, stop_old_host_instances
from time       import sleep

pathToCurrentScript = realpath(__file__)
pathToCommonScriptsFolder = dirname(pathToCurrentScript)

DSCLogPath = join(pathToCommonScriptsFolder, 'nxDSCLog.py')
nxDSCLog = load_source('nxDSCLog', DSCLogPath)
LG = nxDSCLog.DSCLog

helperLibPath = join(pathToCommonScriptsFolder, 'helperlib.py')
helperlib = load_source('helperlib', helperLibPath)

operationStatusUtilityPath = join(pathToCommonScriptsFolder, 'OperationStatusUtility.py')
operationStatusUtility = load_source('operationStatusUtility', operationStatusUtilityPath)

operation = 'PerformConsistency'

def main():
    try:
        run_perform_required_configuration_checks()
    except SystemExit:
        exit(exc_info()[1])
    except Exception:
        # Python 2.4-2.7 and 2.6-3 recognize different formats for exceptions. This methods works in all versions.
        formattedExceptionMessage = format_exc()
        write_omsconfig_host_log('Python exception raised from PerformRequiredConfigurationChecks.py: ' + formattedExceptionMessage, pathToCurrentScript, 'ERROR')
        raise

def run_perform_required_configuration_checks():

    dsc_sysconfdir = join(helperlib.CONFIG_SYSCONFDIR, helperlib.CONFIG_SYSCONFDIR_DSC)
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
        parameters.append("PerformRequiredConfigurationChecks")
        parameters.append("1")
    else:
        parameters.append(omicli_path)
        parameters.append("iv")
        parameters.append(helperlib.DSC_NAMESPACE)
        parameters.append("{")
        parameters.append("MSFT_DSCLocalConfigurationManager")
        parameters.append("}")
        parameters.append("PerformRequiredConfigurationChecks")
        parameters.append("{")
        parameters.append("Flags")
        parameters.append("1")
        parameters.append("}")

    stdout = ''
    stderr = ''

    if use_omsconfig_host:
        try:
            # Open the dsc host lock file. This also creates a file if it does not exist
            dschostlock_filehandle = None
            dschostlock_filehandle = open(dsc_host_lock_path, 'w')
            print("Opened the dsc host lock file at the path '" + dsc_host_lock_path + "'")
            
            dschostlock_acquired = False

            # Acquire dsc host file lock
            for retry in range(60):
                try:
                    flock(dschostlock_filehandle, LOCK_EX | LOCK_NB)
                    write_omsconfig_host_log('dsc_host lock file is acquired by : PerformRequiredConfigurationChecks', pathToCurrentScript)
                    dschostlock_acquired = True
                    break
                except IOError:
                    write_omsconfig_host_log('dsc_host lock file not acquired. retry (#' + str(retry) + ') after 60 seconds...', pathToCurrentScript)
                    sleep(15)
                    stop_old_host_instances(dsc_host_lock_path)
                
            if dschostlock_acquired:
                p = Popen(parameters, stdout=PIPE, stderr=PIPE)
                stdout, stderr = p.communicate()
                print(stdout)
            else:
                print("dsc host lock already acquired by a different process")
        finally:
            if (dschostlock_filehandle):
                # Release dsc host file lock
                flock(dschostlock_filehandle, LOCK_UN)

                # Close dsc host lock file handle
                dschostlock_filehandle.close()
    else:
        p = Popen(parameters, stdout=PIPE, stderr=PIPE)
        stdout, stderr = p.communicate()

    print(stdout)

if __name__ == "__main__":
    LG().Log("DEBUG", "Starting Main method for " + argv[0] + " runing with python " + str(sys.version_info))
    main()
    LG().Log("DEBUG", "End of Main method for " +  argv[0] + " runing with python " + str(sys.version_info))
