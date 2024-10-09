#!/usr/bin/python2
import sys
import os
import subprocess
import shutil
import platform
import imp
from os.path import dirname, join, realpath
from sys                  import argv
from imp        import load_source

pathToCurrentScript = realpath(__file__)
pathToCommonScriptsFolder = dirname(pathToCurrentScript)

DSCLogPath = join(pathToCommonScriptsFolder, 'nxDSCLog.py')
nxDSCLog = load_source('nxDSCLog', DSCLogPath)
LG = nxDSCLog.DSCLog

helperLibPath = join(pathToCommonScriptsFolder, 'helperlib.py')
helperlib = imp.load_source('helperlib', helperLibPath)
fullPathDSCLogger = os.path.join(pathToCommonScriptsFolder, 'nxDSCLog.py')
nxDSCLog = imp.load_source('nxDSCLog', fullPathDSCLogger)
logger = nxDSCLog.ConsoleAndFileLogger()
sys.stdout = logger

def usage():
    print("Usage:")
    print("  RemoveModule.py NAME")
    sys.exit(1)

def exitWithError(message, errorCode = 1):
    errorMessage = "ERROR from RemoveModule.py: " + message
    print(errorMessage)
    sys.exit(errorCode)

def printVerboseMessage(message):
    verboseMessage = "VERBOSE from RemoveModule.py: " + message
    print(verboseMessage)

def getPlatformArchitectureFolderName():
    platformArchitecture = platform.architecture()
    if len(platformArchitecture) != 2:
        exitWithError("The python function platform.architecture() failed to return a valid tuple. Cannot detect if this system has an x64 or an x86 architecture.")

    if (platformArchitecture[0] == "64bit"):
        platformArchitectureFolderName = "x64"
    else:
        platformArchitectureFolderName = "x86"

    return platformArchitectureFolderName

def regenerateDscPythonScriptInitFiles():
    regenerateInitFilesScriptPath = join(helperlib.DSC_SCRIPT_PATH, 'RegenerateInitFiles.py')
    regenerateInitFilesResult = subprocess.call(regenerateInitFilesScriptPath)
    if regenerateInitFilesResult != 0:
        exitWithError("Failed to regenerate the DSC __init__.py files with the result code " + regenerateInitFilesResult)

def main(args):
    '''
    Removes all the resources in the named module from DSC and OMI.
    '''

    # Parameter validation
    if len(args) != 1:
        usage()

    moduleName = args[0]
    modulePath = join(helperlib.DSC_MODULES_PATH, moduleName)
    
    if os.path.isdir(modulePath):
        printVerboseMessage("Found installed module " + moduleName + " at the path " + modulePath + ". Removing module.")
    else:
        exitWithError("Unable to find installed module " + moduleName + " at the path " + modulePath)

    # TODO: Move this module-specifc section out of this generic script.
    # Special section for nxOMSAutomationWorker module.
    # The Linux Hybrid worker and manager needs to be killed before the module is removed.
    # Also a good idea to remove the state and working directories.
    if moduleName == "nxOMSAutomationWorker":
        # Invoke deregister when it becomes available.
        # Kill all processes running under the nxOMSAutomationWorker user.
        # In some cases, there might be no processes to kill, and the return code of the following command might be non-zero.
        # We make a best attempt to terminate the processes and don't care about its return code.
        nxOMSAutomationWorkerUserName = "nxautomation"
        subprocess.call(["sudo", "pkill", "-u", nxOMSAutomationWorkerUserName, ".*"])

        # Remove the state directory for nxOMSAutomationWorker
        nxOMSAutomationWorkerStateFolderPath = "/var/opt/microsoft/omsagent/state/automationworker"
        shutil.rmtree(nxOMSAutomationWorkerStateFolderPath, ignore_errors=True)

        # Remove the working directory for nxOMSAutomationWorker
        nxOMSAutomationWorkerWorkingFolderPath = "/var/opt/microsoft/omsagent/run/automationworker"
        shutil.rmtree(nxOMSAutomationWorkerWorkingFolderPath, ignore_errors=True)

    # Populate common DSC paths
    dscMainFolderPath = join(helperlib.CONFIG_SYSCONFDIR, helperlib.CONFIG_SYSCONFDIR_DSC)
    dscConfigurationFolderPath = join (dscMainFolderPath, 'configuration')
    dscConfigurationRegistrationFolderPath = join(dscConfigurationFolderPath, 'registration')
    dscConfigurationSchemaFolderPath = join(dscConfigurationFolderPath, 'schema')

    # Populate common OMI namespace path
    omiNamespaceFolderName = helperlib.DSC_NAMESPACE.replace('/', '-')
    omiRegistrationFolderPath = join(helperlib.CONFIG_SYSCONFDIR, 'omiregister')
    omiNamespaceFolderPath = join(omiRegistrationFolderPath, omiNamespaceFolderName)

    # Populate common platform architecture
    resourceArchitectureFolderName = getPlatformArchitectureFolderName()

    # Remove all the resources in the module from DSC and OMI
    moduleResourcePath = join(modulePath, 'DSCResources')
    moduleResources = os.listdir(moduleResourcePath)

    for resource in moduleResources:
        resourceFolderPath = join(moduleResourcePath, resource)

        # Skip anything that is not a directory
        if not os.path.isdir(resourceFolderPath):
            continue

        printVerboseMessage("Removing resource " + resource)

        # Remove DSC schema for the resource
        dscConfigurationSchemaFolderPath = join(dscConfigurationFolderPath, 'schema')
        resourceDscSchemaFolderPath = join(dscConfigurationSchemaFolderPath, resource)

        if os.path.isdir(resourceDscSchemaFolderPath):
            shutil.rmtree(resourceDscSchemaFolderPath)
        else:
            printVerboseMessage("Unable to find DSC schema folder for resource " + resource + " at the path " + resourceDscSchemaFolderPath + ". Continuing with resource removal.")

        # Remove DSC registration for the resource
        resourceDscRegistrationFolderPath = join(dscConfigurationRegistrationFolderPath, resource)

        if os.path.isdir(resourceDscRegistrationFolderPath):
            shutil.rmtree(resourceDscRegistrationFolderPath)
        else:
            printVerboseMessage("Unable to find DSC registration folder for resource " + resource + " at the path " + resourceDscRegistrationFolderPath + ". Continuing with resource removal.")

        # Remove the resource Python scripts from the DSC scripts directories
        resourceLibraryFolderPath = join(resourceFolderPath, resourceArchitectureFolderName)
        if not os.path.isdir(resourceLibraryFolderPath):
            exitWithError("Unable to find the resource library folder for the " + resourceArchitectureFolderName + " architecture in the module at " + resourceLibraryFolderPath)

        resourceScriptsFolderPath = join(resourceLibraryFolderPath, 'Scripts')
        if not os.path.isdir(resourceLibraryFolderPath):
            exitWithError("Unable to find the resource library scripts folder for the resource " + resource + " and platform architecture " + resourceArchitectureFolderName + " at the path " + resourceScriptsFolderPath + " in the extracted module.")


        pythonVersionFileNames = ['2.4x-2.5x', '2.6x-2.7x', '3.x']

        for pythonVersionFileName in pythonVersionFileNames:
            resourceScriptsPythonVersionFolderPath = join(resourceScriptsFolderPath, pythonVersionFileName)
            if not os.path.isdir(resourceScriptsPythonVersionFolderPath):
                exitWithError("Unable to find the version-specific Python folder under the resource library scripts folder for the resource " + resource + " and platform architecture " + resourceArchitectureFolderName + " at the path " + resourceScriptsPythonVersionFolderPath + " in the extracted module.")

            resourceScriptsPythonVersionScriptsFolderPath = join(resourceScriptsPythonVersionFolderPath, 'Scripts')
            if not os.path.isdir(resourceScriptsPythonVersionScriptsFolderPath):
                exitWithError("Unable to find the version-specific Python scripts folder under the resource library scripts folder for the resource " + resource + " and platform architecture " + resourceArchitectureFolderName + " at the path " + resourceScriptsPythonVersionScriptsFolderPath + " in the extracted module.")

            resourceScriptFileNames = os.listdir(resourceScriptsPythonVersionScriptsFolderPath)

            dscScriptsPythonVersionFolderPath = join(helperlib.DSC_SCRIPT_PATH, pythonVersionFileName)
            dscScriptsPythonVersionScriptsFolderPath = join(dscScriptsPythonVersionFolderPath, 'Scripts')

            for resourceScriptFileName in resourceScriptFileNames:
                resourceScriptFilePath = join(dscScriptsPythonVersionScriptsFolderPath, resourceScriptFileName)
                if os.path.isfile(resourceScriptFilePath):
                    os.remove(resourceScriptFilePath)
                else:
                    printVerboseMessage("Unable to find resource script " + resourceScriptFileName + " under the DSC scripts folder at the path " + resourceScriptFilePath + ". Continuing with resource removal.")

        # Remove OMI library file for the resource
        resourceLibraryOriginalName = "lib" + resource + ".so"
        
        if helperlib.DSC_NAMESPACE == "root/Microsoft/DesiredStateConfiguration":
            resourceLibraryFileName = resourceLibraryOriginalName
        else:
            resourceLibraryFileName = resourceLibraryOriginalName.replace('.so', "_" + omiNamespaceFolderName + ".so")

        resourceLibraryFilePath = join(helperlib.CONFIG_LIBDIR, resourceLibraryFileName)

        if os.path.isfile(resourceLibraryFilePath):
            os.remove(resourceLibraryFilePath)
        else:
            printVerboseMessage("Unable to find OMI library file for resource " + resource + " at the path " + resourceLibraryFilePath + ". Continuing with resource removal.")
        
        # Remove OMSCONFIG library file for the resource
        if "omsconfig" in helperlib.DSC_SCRIPT_PATH:
            resourceSharedObjectDestinationPath = join("/opt/dsc/lib", resource)

            if os.path.isdir(resourceSharedObjectDestinationPath):
                shutil.rmtree(resourceSharedObjectDestinationPath)

        # Remove OMI registration for the resource
        resourceOmiRegistrationFileName = resource + ".reg"
        resourceOmiRegistrationFilePath = join(omiNamespaceFolderPath, resourceOmiRegistrationFileName)
        os.remove(resourceOmiRegistrationFilePath)

    # Regenerate the DSC Python scripts init files
    regenerateDscPythonScriptInitFiles()

    # Remove the extracted module directory and everything in it   
    shutil.rmtree(modulePath)

if __name__ == "__main__":
    LG().Log("DEBUG", "Starting Main method for " + argv[0] + " runing with python " + str(sys.version_info))
    main(sys.argv[1:])
    LG().Log("DEBUG", "End of Main method for " +  argv[0] + " runing with python " + str(sys.version_info))
