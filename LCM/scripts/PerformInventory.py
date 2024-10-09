#!/usr/bin/python2
from fcntl                import flock, LOCK_EX, LOCK_UN, LOCK_NB
from imp                  import load_source
from os                   import listdir, system
from os.path              import dirname, isfile, join, realpath
from shutil               import move
from subprocess           import Popen, PIPE
from sys                  import argv, exc_info, exit, stdout, version_info
from traceback            import format_exc
from xml.dom.minidom      import parse
from OmsConfigHostHelpers import write_omsconfig_host_telemetry, write_omsconfig_host_switch_event, write_omsconfig_host_log, stop_old_host_instances
from time                 import sleep

pathToCurrentScript = realpath(__file__)
pathToCommonScriptsFolder = dirname(pathToCurrentScript)

helperLibPath = join(pathToCommonScriptsFolder, 'helperlib.py')
helperlib = load_source('helperlib', helperLibPath)

operationStatusUtilityPath = join(pathToCommonScriptsFolder, 'OperationStatusUtility.py')
operationStatusUtility = load_source('operationStatusUtility', operationStatusUtilityPath)

operation = 'PerformInventory'

# Redirect output to our log file
fullPathDSCLogger = join(pathToCommonScriptsFolder, 'nxDSCLog.py')
nxDSCLog = load_source('nxDSCLog', fullPathDSCLogger)
logger = nxDSCLog.ConsoleAndFileLogger()
stdout = logger

def usage():
    write_omsconfig_host_log('Incorrect parameters to PerformInventory.py: ' + str(argv), pathToCurrentScript, 'WARNING')
    print("""Usage: PerformInventory.py [OPTIONS]
OPTIONS (case insensitive):
 --InMOF PATH_TO_INVENTORY.MOF
 --OutXML PATH_TO_OUTPUT_REPORT.XML
 --help
""")

def exitWithError(message, errorCode = 1):
    timestamp = operationStatusUtility.get_current_timestamp()
    errorMessage = timestamp + ": ERROR from PerformInventory.py: " + message
    print(errorMessage)
    exit(errorCode)

def printVerboseMessage(message):
    timestamp = operationStatusUtility.get_current_timestamp()
    verboseMessage = str(timestamp) + ": VERBOSE from PerformInventory.py: " + str(message)
    print(verboseMessage)

def main(args):
    try:
        perform_inventory(args)
    except SystemExit:
        exit(exc_info()[1])
    except Exception:
        # Python 2.4-2.7 and 2.6-3 recognize different formats for exceptions. This methods works in all versions.
        formattedExceptionMessage = format_exc()
        write_omsconfig_host_log('Python exception raised from PerformInventory.py: ' + formattedExceptionMessage, pathToCurrentScript, 'ERROR')
        raise

def perform_inventory(args):
    Variables = dict()

    # Parse command line arguments
    optlist = []

    command_line_length = len(args)
    argIndex = 0
    inArgument = False
    currentArgument = ""
    arg = ""

    while argIndex < command_line_length:
        arg = args[argIndex]
        if argIndex == 0:
            # skip the program name
            argIndex += 1
            continue

        if inArgument:
            Variables[currentArgument] = arg
            inArgument = False
        else:
            if arg[0:2] == "--":
                inArgument = True
                currentArgument = arg[2:].lower()
            else:
                # The rest are not options
                args = args[argIndex:]
                break
        argIndex += 1

    if inArgument:
        Variables[currentArgument] = arg

    AcceptableOptions = ["inmof", "outxml", "help"]

    if "help" in Variables:
        usage()
        exit(0)

    optionsValid = True
    for arg in Variables.keys():
        if arg.lower() not in AcceptableOptions:
            optionsValid = False
            exitWithError("Error: %s is not a valid option" % arg)

    if optionsValid == False:
        usage()
        exit(1)

    dsc_sysconfdir = join(helperlib.CONFIG_SYSCONFDIR, helperlib.CONFIG_SYSCONFDIR_DSC)
    dsc_reportdir = join(dsc_sysconfdir, 'InventoryReports')
    omicli_path = join(helperlib.CONFIG_BINDIR, 'omicli')
    dsc_host_base_path = helperlib.DSC_HOST_BASE_PATH
    dsc_host_path = join(dsc_host_base_path, 'bin/dsc_host')
    dsc_host_output_path = join(dsc_host_base_path, 'output')
    dsc_host_lock_path = join(dsc_host_base_path, 'dsc_host_lock')
    dsc_host_switch_path = join(dsc_host_base_path, 'dsc_host_ready')
    dsc_configuration_path = join(dsc_sysconfdir, 'configuration')
    temp_report_path = join(dsc_configuration_path, 'Inventory.xml.temp')
    report_path = join(dsc_configuration_path, 'Inventory.xml')
    inventorylock_path = join(dsc_sysconfdir, 'inventory_lock')

    if ("omsconfig" in helperlib.DSC_SCRIPT_PATH):
        write_omsconfig_host_switch_event(pathToCurrentScript, isfile(dsc_host_switch_path))

    if ("omsconfig" in helperlib.DSC_SCRIPT_PATH) and (isfile(dsc_host_switch_path)):
        use_omsconfig_host = True
    else:
        use_omsconfig_host = False

    if "outxml" in Variables:
        report_path = Variables["outxml"]

    parameters = []

    if use_omsconfig_host:
        parameters.append(dsc_host_path)
        parameters.append(dsc_host_output_path)

        if "inmof" in Variables:
            parameters.append("PerformInventoryOOB")
            parameters.append(Variables["inmof"])
        else:
            parameters.append("PerformInventory")
    else:
        parameters.append(omicli_path)
        parameters.append("iv")
        parameters.append(helperlib.DSC_NAMESPACE)
        parameters.append("{")
        parameters.append("MSFT_DSCLocalConfigurationManager")
        parameters.append("}")

        if "inmof" in Variables:
            parameters.append("PerformInventoryOOB")
            parameters.append("{")
            parameters.append("InventoryMOFPath")
            parameters.append(Variables["inmof"])
            parameters.append("}")
        else:
            parameters.append("PerformInventory")

    # Ensure inventory lock file permission is set correctly before opening
    operationStatusUtility.ensure_file_permissions(inventorylock_path, '644')

    # Open the inventory lock file. This also creates a file if it does not exist.
    inventorylock_filehandle = open(inventorylock_path, 'w')
    printVerboseMessage("Opened the inventory lock file at the path '" + inventorylock_path + "'")
    retval = 0
    inventorylock_acquired = True
    dschostlock_filehandle = None
    inmof_file = ''
    if "inmof" in Variables:
        inmof_file = Variables["inmof"]

    try:
        # Acquire inventory file lock
        try:
            flock(inventorylock_filehandle, LOCK_EX | LOCK_NB)
            write_omsconfig_host_log('Inventory lock is acquired by : ' + inmof_file, pathToCurrentScript)
        except IOError:
            inventorylock_acquired = False
            write_omsconfig_host_log('Failed to acquire inventory lock.', pathToCurrentScript, 'WARNING')

        if inventorylock_acquired:
            dschostlock_acquired = False
            
            if use_omsconfig_host:
                if isfile(dsc_host_lock_path):
                    stop_old_host_instances(dsc_host_lock_path)
                    # Open the dsc host lock file. This also creates a file if it does not exist.
                    dschostlock_filehandle = open(dsc_host_lock_path, 'w')
                    printVerboseMessage("Opened the dsc host lock file at the path '" + dsc_host_lock_path + "'")

                    # Acquire dsc host file lock
                    for retry in range(10):
                        try:
                            flock(dschostlock_filehandle, LOCK_EX | LOCK_NB)
                            dschostlock_acquired = True
                            write_omsconfig_host_log('dsc_host lock file is acquired by : ' + inmof_file, pathToCurrentScript)
                            break
                        except IOError:
                            write_omsconfig_host_log('dsc_host lock file not acquired. retry (#' + str(retry) + ') after 60 seconds...', pathToCurrentScript)
                            sleep(60)
                else:
                    write_omsconfig_host_log('dsc_host lock file does not exist. Skipping this operation until next consistency hits.', pathToCurrentScript, 'WARNING')

            if dschostlock_acquired or (not use_omsconfig_host):
                try:
                    system("rm -f " + dsc_reportdir + "/*")

                    process = Popen(parameters, stdout = PIPE, stderr = PIPE)
                    stdout, stderr = process.communicate()
                    retval = process.returncode

                    printVerboseMessage(stdout)

                    if (retval > 0):
                        write_omsconfig_host_log('dsc_host failed with code = ' + str(retval), pathToCurrentScript)
                        exit(retval)

                    # Combine reports together
                    reportFiles = listdir(dsc_reportdir)

                    final_xml_report = '<INSTANCE CLASSNAME="Inventory"><PROPERTY.ARRAY NAME="Instances" TYPE="string" EmbeddedObject="object"><VALUE.ARRAY>'
                    values = []
                    for reportFileName in reportFiles:
                        reportFilePath = join(dsc_reportdir, reportFileName)

                        if not isfile(reportFilePath):
                            continue
                        report = parse(reportFilePath)
                        for valueNode in report.getElementsByTagName('VALUE'):
                            values.append(valueNode.toxml())

                    final_xml_report = final_xml_report + "".join(values) + "</VALUE.ARRAY></PROPERTY.ARRAY></INSTANCE>"

                    # Ensure temporary inventory report file permission is set correctly before opening
                    operationStatusUtility.ensure_file_permissions(temp_report_path, '644')

                    tempReportFileHandle = open(temp_report_path, 'w')
                    try:
                        tempReportFileHandle.write(final_xml_report)
                    finally:
                        if (tempReportFileHandle):
                            tempReportFileHandle.close()

                    # Ensure temporary inventory report file permission is set correctly after opening
                    operationStatusUtility.ensure_file_permissions(temp_report_path, '644')

                    system("rm -f " + dsc_reportdir + "/*")
                    move(temp_report_path, report_path)

                    # Ensure inventory report file permission is set correctly
                    operationStatusUtility.ensure_file_permissions(report_path, '644')
                finally:
                    if (dschostlock_filehandle):
                        # Release inventory file lock
                        flock(inventorylock_filehandle, LOCK_UN)

                        # Release dsc host file lock
                        if isfile(dsc_host_lock_path) and use_omsconfig_host:
                            try:
                                flock(dschostlock_filehandle, LOCK_UN)
                            except:
                                pass
    finally:
        if (inventorylock_filehandle):
            # Close inventory lock file handle
            inventorylock_filehandle.close()
        
        if (dschostlock_filehandle):
            # Close dsc host lock file handle
            if use_omsconfig_host:
                try:
                    dschostlock_filehandle.close()
                except:
                    pass

    # Ensure inventory lock file permission is set correctly after opening
    operationStatusUtility.ensure_file_permissions(inventorylock_path, '644')

    # Ensure dsc host lock file permission is set correctly after opening
    if use_omsconfig_host:
        operationStatusUtility.ensure_file_permissions(dsc_host_lock_path, '644')

    exit(retval)

if __name__ == "__main__":
    main(argv)
