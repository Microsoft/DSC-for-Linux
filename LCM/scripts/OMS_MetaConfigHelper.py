#!/usr/bin/python2
import os
import sys
import imp
import subprocess
import signal
from os.path import basename, dirname, join, realpath, split
from imp                import load_source
from os.path            import dirname, isfile, join, realpath

pathToCurrentScript = realpath(__file__)
pathToCommonScriptsFolder = dirname(pathToCurrentScript)

helperLibPath = join(pathToCommonScriptsFolder, 'helperlib.py')
helperlib = load_source('helperlib', helperLibPath)

conf_path = "/etc/opt/microsoft/omsagent/conf/omsadmin.conf"
metamof_path = "/etc/opt/omi/conf/omsconfig/generated_meta_config.mof"
agentid_path = "/etc/opt/omi/conf/omsconfig/agentid"
omshelper_disable_path = "/etc/opt/omi/conf/omsconfig/omshelper_disable"

# Redirect output to our log file
pathToCurrentScript = realpath(__file__)
pathToCommonScriptsFolder = dirname(pathToCurrentScript)

fullPathDSCLogger = os.path.join(pathToCommonScriptsFolder, 'nxDSCLog.py')
nxDSCLog = imp.load_source('nxDSCLog', fullPathDSCLogger)
logger = nxDSCLog.ConsoleAndFileLogger()
sys.stdout = logger
proc = None

# function to handle ternimation signals received from omsagent.
# Child process are created by with a new group id.
# We issue termination signal together to all childprocesses with group id.
def signal_handler(signalNumber, frame):
    printVerboseMessage("OMS_MetaconfigHelper.py script received SIGTERM signal.")

    if proc is not None:
        printVerboseMessage("Terminating child process by sending SIGTERM signal.")
        os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
    else:
        printVerboseMessage("There is no child process running. It is either completed execution or has not been invoked.")

def exitWithError(message, errorCode = 1):
    errorMessage = "ERROR from OMS_MetaConfigHelper.py: " + message
    print(errorMessage)
    sys.exit(errorCode)

def printVerboseMessage(message):
    verboseMessage = "VERBOSE from OMS_MetaConfigHelper.py: " + message
    print(verboseMessage)

def generate_meta_mof(serverurl):
    return """
instance of MSFT_WebDownloadManager as $MSFT_WebDownloadManager1ref
{
 ResourceID = "[ConfigurationRepositoryWeb]OMSConfigurationManager";
 SourceInfo = "GeneratedLocally";
 ServerURL = "%s";
};


instance of MSFT_WebReportManager as $MSFT_WebReportManager1ref
{
SourceInfo = "GeneratedLocally";
 ServerURL = "%s";
 ResourceID = "[ReportServerWeb]OMSConfigurationManager";
};


instance of MSFT_WebResourceManager as $MSFT_WebResourceManager1ref
{
SourceInfo = "GeneratedLocally";
ServerURL = "%s";
ResourceID = "[ResourceRepositoryWeb]OMSConfigurationManager";
};


instance of MSFT_DSCMetaConfiguration as $MSFT_DSCMetaConfiguration1ref
{
 RefreshMode = "Pull";
 AllowModuleOverwrite = True;
 RefreshFrequencyMins = 5;
 RebootNodeIfNeeded = False;
 ConfigurationModeFrequencyMins = 5;
 ConfigurationMode = "ApplyAndAutoCorrect";

  ResourceModuleManagers = {
   $MSFT_WebResourceManager1ref
  };

  ReportManagers = {
   $MSFT_WebReportManager1ref
  };

  ConfigurationDownloadManagers = {
   $MSFT_WebDownloadManager1ref
  };

};

instance of OMI_ConfigurationDocument
{
 Version="2.0.0";
 MinimumCompatibleVersion = "2.0.0";
};
""" % (serverurl, serverurl, serverurl)



def generate_push_meta_mof():
    return """
instance of MSFT_DSCMetaConfiguration as $MSFT_DSCMetaConfiguration1ref
{
 RefreshMode = "Push";
 AllowModuleOverwrite = True;
 RefreshFrequencyMins = 30;
 RebootNodeIfNeeded = True;
 ConfigurationModeFrequencyMins = 60;
 ConfigurationMode = "ApplyAndAutoCorrect";
};

instance of OMI_ConfigurationDocument
{
 Version="2.0.0";
 MinimumCompatibleVersion = "2.0.0";
};
"""

def source_file(filename):
    retval = dict()
    f = open(filename, "r")
    try:
        contents = f.read()
    finally:
        f.close()
    lines = contents.splitlines()
    for line in lines:
        # Find first '='; everything before is key, everything after is value
        midpoint = line.find("=")
        if (midpoint == 0 or midpoint == -1):
            # Skip over lines without = or lines that begin with =
            continue
        key = line[:midpoint]
        value = line[midpoint+1:]
        retval[key] = value
    return retval


Variables = dict()
Defines = []

# register the SIGTERM handler
signal.signal(signal.SIGTERM, signal_handler)

# Parse command line arguments
args = []
optlist = []
for arg in sys.argv[1:]:
    if len(arg) < 2:
        # Must be a file
        args.append(arg)
        continue

    if arg[0:2] == "--":
        tokens = arg[2:].split("=",1)
        if len(tokens) == 1:
            # This is a define
            Defines.append(tokens[0])
            Variables[tokens[0]] = ""
        else:
            # This is a variable
            Variables[tokens[0]] = tokens[1]
    else:
        args.append(arg)

enable_flag = False
disable_flag = False

if "enable" in Variables:
    enable_flag = True
if "disable" in Variables:
    disable_flag = True

if enable_flag == True and disable_flag == True:
    exitWithError("Error: Cannot use both disable and enable options at the same time.")

if enable_flag == True:
    printVerboseMessage("Enable flag set to True, setting mof to enabled mode.")
    if os.path.isfile(omshelper_disable_path):
        os.remove(omshelper_disable_path)
if disable_flag == True:
    printVerboseMessage("Disable flag set to True, setting mof to disabled mode.")
    disableFileHandle = open(omshelper_disable_path, "w")
    try:
        disableFileHandle.write("a")
    finally:
        disableFileHandle.close()

    os.system("chown omsagent " + omshelper_disable_path)

if not os.path.isfile(omshelper_disable_path):
    # source the omsadmin conf file and get the key/value pairs
    printVerboseMessage("OMS config path being read: " + os.path.realpath(conf_path))
    keyvals = source_file(conf_path)

    # Looking for DSC_ENDPOINT and AGENT_GUID

    if "DSC_ENDPOINT" not in keyvals or "AGENT_GUID" not in keyvals:
        exitWithError("Error: Unable to find needed key/value pairs in " + conf_path)

    DSC_ENDPOINT = keyvals["DSC_ENDPOINT"].strip()

    # Cut off everything after Nodes in the URL including Nodes, if Nodes is in URL (it should be)
    Nodes_loc = DSC_ENDPOINT.find("/Nodes")
    if Nodes_loc != -1:
        DSC_ENDPOINT = DSC_ENDPOINT[:Nodes_loc]
    AGENT_GUID = keyvals["AGENT_GUID"].strip()

    # Write out agentID
    f = open(agentid_path, "w")
    try:
        f.write(AGENT_GUID)
    finally:
        f.close()

    os.system("chown omsagent " + agentid_path)

    # Write metaconfig
    metaConfig = generate_meta_mof(DSC_ENDPOINT)
else:
    metaConfig = generate_push_meta_mof()

f = open(metamof_path, "w")
try:
    f.write(metaConfig)
finally:
    f.close()

os.system("chown omsagent " + metamof_path)

if os.geteuid() == 0:
    commandToRun = "su - omsagent -c '/opt/microsoft/omsconfig/Scripts/SetDscLocalConfigurationManager.py -configurationmof " + metamof_path + "'"
else:
    commandToRun = "/opt/microsoft/omsconfig/Scripts/SetDscLocalConfigurationManager.py -configurationmof " + metamof_path

# Apply the metaconfig using SetDscLocalConfiguration
proc = subprocess.Popen(commandToRun, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True, close_fds=True, preexec_fn=os.setsid)
exit_code = proc.wait()

stdout, stderr = proc.communicate()
printVerboseMessage("Output from: " + commandToRun + ": " + str(stdout))

set_metaconfig_success_string = ""
if "omsconfig" in helperlib.DSC_SCRIPT_PATH:
    set_metaconfig_success_string = "Operation SendMetaConfigurationApply completed successfully."
else:
    set_metaconfig_success_string = "ReturnValue=0"

# This file is only for python 2
if ((exit_code == 0) and (stderr == '' or (sys.version_info >= (3, 0) and stderr.decode(encoding = 'UTF-8') == '') and (set_metaconfig_success_string in str(stdout)))):
    printVerboseMessage('Successfully configured omsconfig.')
else:
    if exit_code == 0:
        exit_code = 1

    if (stderr != ''):
        exitWithError(("Error on running command: " + commandToRun + " Error Message: " + stderr), exit_code)
    elif ((sys.version_info >= (3, 0) and stderr.decode(encoding = 'UTF-8') != '')):
        exitWithError(("Error on running command: " + commandToRun + " Error Message: " + stderr.decode(encoding = 'UTF-8')), exit_code)
    else:
        exitWithError(("Failed on running command: " + commandToRun), exit_code)
