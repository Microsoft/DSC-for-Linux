#!/usr/bin/python2
import sys
import os
import os.path
import tempfile
import shutil
from sys                import argv
from imp                import load_source
from os.path import dirname, join, realpath

def usage():
   print("""Usage: Register.py [OPTIONS]
OPTIONS (case insensitive):
 --RegistrationKey KEY
 --ServerURL URL
 --ConfigurationName NAME
 --RefreshFrequencyMins NUM                                          default=30
 --ConfigurationModeFrequencyMins NUM                                default=15
 --ConfigurationMode (ApplyAndMonitor, ApplyAndAutoCorrect, ApplyOnly, MonitorOnly) default=ApplyAndMonitor
 --RefreshMode (Pull|Push)                                           default=Pull
 --Help
""")


pathToCurrentScript = realpath(__file__)
pathToCommonScriptsFolder = dirname(pathToCurrentScript)

DSCLogPath = join(pathToCommonScriptsFolder, 'nxDSCLog.py')
nxDSCLog = load_source('nxDSCLog', DSCLogPath)
LG = nxDSCLog.DSCLog

LG().Log("DEBUG", "Starting script logic for " + argv[0]+ " runing with python " + str(sys.version_info))

# Apply a DSC meta configuration based on a template
Variables = dict()

# Parse command line arguments
args = []
optlist = []

command_line_length = len(sys.argv)
i = 0
inArgument = False
currentArgument = ""
arg = ""
while i < command_line_length:
   arg = sys.argv[i]
   if i == 0:
      # skip the program name
      i += 1
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
         args = sys.argv[i:]
         break
   i += 1
   
if inArgument:
   Variables[currentArgument] = arg

AcceptableOptions = ["registrationkey", "serverurl", "configurationname", "refreshfrequencymins", "configurationmodefrequencymins", "configurationmode", "refreshmode", "help", "regeneratecert"]

if "help" in Variables:
   usage()
   sys.exit(0)

optionsValid = True
for arg in Variables.keys():
   if arg.lower() not in AcceptableOptions:
      optionsValid = False
      print("Error: %s is not a valid option" % arg)

if len(Variables.keys()) == 0:
   if len(args) == 2:
      # Assume first parameter is RegistrationKey and second parameter is ServerURL in this case
      Variables["registrationkey"] = args[0]
      Variables["serverurl"] = args[1]
   else:
      print("Error: Unexpected (" + str(len(args)) + ") number of non-option arguments.  Without options specified, we expect arguments to be RegistrationKey followed by ServerURL.")
      optionsValid = False
   
if optionsValid == False:
   usage()
   sys.exit(1)

ServerURL = ""
RegistrationKey = ""
ConfigurationName = ""

# If RefreshMode == Pull (which is default), then RegistrationKey and ServerURL are required.
RefreshMode = "Pull"
if "refreshmode" in Variables:
   RefreshMode = Variables["refreshmode"]

if RefreshMode == "Pull":
   if "registrationkey" not in Variables:
      print("Error: RegistrationKey must be specified for Pull mode")
      usage()
      sys.exit(1)
   if "serverurl" not in Variables:
      print("Error: ServerURL must be specified for Pull mode")
      usage()
      sys.exit(1)
   ServerURL = Variables["serverurl"]
   RegistrationKey = Variables["registrationkey"]
   if "configurationname" in Variables:
      ConfigurationName = Variables["configurationname"]

ConfigurationMode = "ApplyAndMonitor"
if "configurationmode" in Variables:
   ConfigurationMode = Variables["configurationmode"]

RefreshFrequencyMins = "30"
if "refreshfrequencymins" in Variables:
   RefreshFrequencyMins = Variables["refreshfrequencymins"]
   
ConfigurationModeFrequencyMins = "15"
if "configurationmodefrequencymins" in Variables:
   ConfigurationModeFrequencyMins = Variables["configurationmodefrequencymins"]

RegenerateCert = False
if "regeneratecert" in Variables:
   RegenerateCert = True

metaConfig = ""
if RefreshMode == "Push":
   metaConfig = """
     instance of MSFT_DSCMetaConfiguration as $MSFT_DSCMetaConfiguration1ref
     {
     DownloadManagerName = "WebDownloadManager";
     RefreshMode = "<REFRESHMODE>";
     ConfigurationMode = "<CONFIGURATIONMODE>";
     
     };
     
     instance of OMI_ConfigurationDocument
     {
     Version="1.0.0";
     };
     """
else:
   metaConfig="""
     instance of MSFT_WebDownloadManager as $MSFT_WebDownloadManager1ref
     {
     ResourceID = "[ConfigurationRepositoryWeb]AzureAutomationDSC";
     SourceInfo = "C:\\\\OaaS-RegistrationMetaConfig2.ps1::20::9::ConfigurationRepositoryWeb";
     RegistrationKey = "<REGKEY>"; 
     ServerURL = "<SERVERURL>";
     ConfigurationNames = {"<CONFIGURATIONNAME>"};
     };
     
     instance of MSFT_WebResourceManager as $MSFT_WebResourceManager1ref
     {
     SourceInfo = "C:\\\\OaaS-RegistrationMetaConfig2.ps1::27::9::ResourceRepositoryWeb";
     ResourceID = "[ResourceRepositoryWeb]AzureAutomationDSC";
     RegistrationKey = "<REGKEY>"; 
     ServerURL = "<SERVERURL>";
     };
     
     instance of MSFT_WebReportManager as $MSFT_WebReportManager1ref
     {
     SourceInfo = "C:\\\\OaaS-RegistrationMetaConfig2.ps1::34::9::ReportServerWeb";
     ResourceID = "[ReportServerWeb]AzureAutomationDSC";
     RegistrationKey = "<REGKEY>"; 
     ServerURL = "<SERVERURL>";
     };
     
     instance of MSFT_DSCMetaConfiguration as $MSFT_DSCMetaConfiguration1ref
     {
     RefreshMode = "<REFRESHMODE>";
     AllowModuleOverwrite = False;
     RefreshFrequencyMins = <REFRESHFREQUENCYMINS>;
     RebootNodeIfNeeded = False;
     ConfigurationModeFrequencyMins = <CONFIGURATIONMODEFREQUENCYMINS>;
     ConfigurationMode = "<CONFIGURATIONMODE>";
     
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
     CompatibleVersionAdditionalProperties= { "MSFT_DSCMetaConfiguration:StatusRetentionTimeInDays" };
     Author="azureautomation";
     Name="RegistrationMetaConfig";
     };
"""
metaConfig = metaConfig.replace("<REFRESHMODE>", RefreshMode)
metaConfig = metaConfig.replace("<REFRESHFREQUENCYMINS>", RefreshFrequencyMins)
metaConfig = metaConfig.replace("<CONFIGURATIONMODEFREQUENCYMINS>", ConfigurationModeFrequencyMins)
metaConfig = metaConfig.replace("<CONFIGURATIONMODE>", ConfigurationMode)
metaConfig = metaConfig.replace("<SERVERURL>", ServerURL)
metaConfig = metaConfig.replace("<REGKEY>", RegistrationKey)
metaConfig = metaConfig.replace("<CONFIGURATIONNAME>", ConfigurationName)

# Write to file and run SendMetaConfigurationApply.py
tempdir = tempfile.mkdtemp()
meta_path = tempdir + "/metaconf.mof"
f = open(meta_path, "w")
f.write(metaConfig)
f.close()

# Generate new cert if specified
if RegenerateCert == True:
   OAAS_CERTPATH="<OAAS_CERTPATH>"
   OAAS_KEYPATH="<OAAS_KEYPATH>"
   OAAS_THUMBPRINT="<OAAS_THUMBPRINT>"
   os.system("touch " + OAAS_KEYPATH + "; chmod 0600 " + OAAS_KEYPATH);
   os.system("touch " + OAAS_KEYPATH + "_old; chmod 0600 " + OAAS_KEYPATH + "_old");
   os.system("openssl req -subj '/CN=DSC-OaaS' -new -newkey rsa:2048 -days 365 -nodes -x509 -keyout " + OAAS_KEYPATH + "_old -out " + OAAS_CERTPATH + " && openssl rsa -in " +  OAAS_KEYPATH + "_old -out " + OAAS_KEYPATH + " && rm -f " +  OAAS_KEYPATH + "_old");
   os.system("openssl x509 -noout -in " + OAAS_CERTPATH + " -fingerprint | sed 's/^.*=//' > " + OAAS_THUMBPRINT);

os.system("<DSC_SCRIPT_PATH>/SetDscLocalConfigurationManager.py -configurationmof " + meta_path)

shutil.rmtree(tempdir)

LG().Log("DEBUG", "End of script logic for " +  argv[0] + " runing with python " + str(sys.version_info))