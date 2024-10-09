#!/usr/bin/python2

# Standard library imports
from subprocess           import Popen, PIPE
from sys                  import argv
from imp                  import load_source
from os.path              import dirname, isfile, join, realpath
from fcntl                import flock, LOCK_EX, LOCK_UN, LOCK_NB
from OmsConfigHostHelpers import write_omsconfig_host_telemetry, write_omsconfig_host_switch_event, write_omsconfig_host_log, stop_old_host_instances
from time                 import sleep
import sys

pathToCurrentScript = realpath(__file__)
pathToCommonScriptsFolder = dirname(pathToCurrentScript)

DSCLogPath = join(pathToCommonScriptsFolder, 'nxDSCLog.py')
nxDSCLog = load_source('nxDSCLog', DSCLogPath)
LG = nxDSCLog.DSCLog

helperLibPath = join(pathToCommonScriptsFolder, 'helperlib.py')
helperlib = load_source('helperlib', helperLibPath)

try:
    # Used by Python 2.7+
    from argparse import ArgumentParser
    useArgParse = True
except:
    # Used by Python 2.4-2.6
    from optparse import OptionParser
    useArgParse = False

def main(argv):
    """StartDscConfiguration"""

    # Define method arguments and description
    description = 'Starts the specified DSC configuration.'

    parameters = {
        'configurationmof' : {
            'shortForm' : 'c',
            'helpText' : 'The path to the configuration mof to start.',
            'required' : True,
            'action' : 'store'
        },
        'force' : {
            'shortForm' : 'f',
            'helpText' : 'Specifies that any current pending configuration should be forcibly removed before starting the new configuration.',
            'required' : False,
            'action' : 'store_true'
        }
    }

    # Parse -configurationmof on its own for backwards compatibility
    configmofArgument = None
    if '-configurationmof' in argv:
        configmofIndex = argv.index('-configurationmof')

        try:
            configmofArgument = argv[configmofIndex + 1]
        except:
            print 'StartDscConfiguration.py: error: Please provide a valid path argument for -configurationmof'
            exit(1)

        # Set the configuration mof parameter to no longer be required so it doesn't error in the arugment parser
        parameters['configurationmof']['required'] = False 

        # Remove -configurationmof and its argument from the list so it doesn't error in the arugment parser
        argv.pop(configmofIndex)
        argv.pop(configmofIndex)

    # Parse arguments
    if (useArgParse):
        # Used by Python 2.7+
        parser = ArgumentParser(description = description)

        for parameter in parameters.keys():
            parameterInfo = parameters[parameter]
            parser.add_argument('-' + parameterInfo['shortForm'], '--' + parameter, required = parameterInfo['required'], help = parameterInfo['helpText'], action = parameterInfo['action'])

        parsedArguments = parser.parse_args(argv)
    else:
        # Used by Python 2.4-2.6
        parser = OptionParser(description = description)

        for parameter in parameters.keys():
            parameterInfo = parameters[parameter]
            parser.add_option('-' + parameterInfo['shortForm'], '--' + parameter, help = parameterInfo['helpText'], action = parameterInfo['action'])

        (parsedArguments, extraArguments) = parser.parse_args(argv)

        for parameter in parameters.keys():
            if parameters[parameter]['required']:
                if not getattr(parsedArguments, parameter):
                    print 'StartDscConfiguration.py: error: argument -' + parameters[parameter]['shortForm'] + '/--' + parameter + ' is required.'
                    exit(1)

    # Check that we don't have two configuration mofs defined
    if configmofArgument and parsedArguments.configurationmof:
        print 'StartDscConfiguration.py: error: Two configuration mof arguments were found. Please provide only one.'
        exit(1)
    
    if configmofArgument:
        parsedArguments.configurationmof = configmofArgument

    # Read the configuration mof
    try:
        configurationFile = open(parsedArguments.configurationmof, 'r')
    except:
        configurationFile = open(parsedArguments.configurationmof, 'r', encoding = 'utf-16')

    try:
        configurationFileContent = configurationFile.read()
    finally:
        if (configurationFile):
            configurationFile.close()

    # Convert the file content to strings of integers representing unicode
    configurationData = []
    for char in configurationFileContent:
        configurationData.append(str(ord(char)))

    # # OMI CLI location
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

    # Assemble parameters to pass to OMI CLI
    host_parameters = []
    if use_omsconfig_host:
        host_parameters.append(dsc_host_path)
        host_parameters.append(dsc_host_output_path)
        host_parameters.append("SendConfigurationApply")
        host_parameters.append(args[2])
        # Insert force if specified
        if parsedArguments.force:
            host_parameters.append("force")
    else:
        host_parameters.append(omicli_path)
        host_parameters.append("iv")
        host_parameters.append("<DSC_NAMESPACE>")
        host_parameters.append("{")
        host_parameters.append("MSFT_DSCLocalConfigurationManager")
        host_parameters.append("}")
        host_parameters.append("SendConfigurationApply")
        host_parameters.append("{")
        host_parameters.append("ConfigurationData")
        host_parameters.append("[")

        # Insert configurationmof data here
        for token in configurationData:
            host_parameters.append(token)
        
        host_parameters.append("]")

        # Insert force if specified
        if parsedArguments.force:
            host_parameters.append("force")
            host_parameters.append("true")

        host_parameters.append("}")

    stdout = ''
    stderr = ''

    if use_omsconfig_host:
        try:
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
                p = subprocess.Popen(host_parameters, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
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
        p = Popen(host_parameters, stdout=PIPE, stderr=PIPE)
        stdout, stderr = p.communicate()

    print(stdout)
    print(stderr)

LG().Log("DEBUG", "Starting Main method for " + argv[0] + " runing with python " + str(sys.version_info))
main(argv[1:])
LG().Log("DEBUG", "End of Main method for " +  argv[0] + " runing with python " + str(sys.version_info))