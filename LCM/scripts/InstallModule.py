#!/usr/bin/python2
import imp
import os
import stat
import shutil
import subprocess
import sys
import platform
from os.path    import basename, dirname, join, realpath, split

pathToCurrentScript = realpath(__file__)
pathToCommonScriptsFolder = dirname(pathToCurrentScript)

helperLibPath = join(pathToCommonScriptsFolder, 'helperlib.py')
helperlib = imp.load_source('helperlib', helperLibPath)

fullPathDSCLogger = os.path.join(pathToCommonScriptsFolder, 'nxDSCLog.py')
nxDSCLog = imp.load_source('nxDSCLog', fullPathDSCLogger)
logger = nxDSCLog.ConsoleAndFileLogger()
sys.stdout = logger

try:
    zipfileLibPath = join(helperlib.DSC_SCRIPT_PATH, 'zipfile2.6.py')
    zipfile = imp.load_source('zipfile', zipfileLibPath)
except:
    import zipfile

def usage():
    print("Usage:")
    print("  InstallModule.py NAME_VERSION.zip [VERIFY_FLAG]")
    sys.exit(1)

def exitWithError(message, errorCode = 1):
    errorMessage = "ERROR from InstallModule.py: " + message
    print(errorMessage)
    sys.exit(errorCode)

def printVerboseMessage(message):
    verboseMessage = "VERBOSE from InstallModule.py: " + message
    print(verboseMessage)

def getDscRegistrationFileContent(resourceName):
    dscRegistrationFileContent = '''
Instance of MSFT_CimConfigurationProviderRegistration
{
  className = "''' + resourceName + '''";
  DSCEngineCompatVersion = "1.0.0";
  DSCModuleVersion = "1.0.0";
  Namespace = "''' + helperlib.DSC_NAMESPACE + '''";
};
'''
    return dscRegistrationFileContent

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
    regenerateInitFilesResult = subprocess.call("(python2 " + regenerateInitFilesScriptPath + ")", shell=True)
    if regenerateInitFilesResult != 0:
        exitWithError("Failed to regenerate the DSC __init__.py files with the result code", regenerateInitFilesResult)

def main(args):
    '''
    Installs a DSC resource module from a zip file into DSC on Linux.
    All resources in the module will currently be registered as providers in OMI.

    Please note:
        The recommended method of registration with OMI is to use their omireg script.
        The omireg script is not used here because it needs to run as root.
        OMS DSC runs this script as the user omsagent.
    '''

    # Parameter validation
    if len(args) != 1 and len(args) != 2:
        usage()

    moduleZipFilePath = args[0]

    if not os.path.isfile(moduleZipFilePath):
        exitWithError("The provided module zip file path (" + moduleZipFilePath + ") does not point to an existing, accessible file.")

    if len(args) == 2:
        verifyChecksum = args[1] in ['1', 'True']
    else:
        verifyChecksum = False

    moduleZipFileName = os.path.basename(moduleZipFilePath)
    indexOfLastUnderScoreInModuleZipFileName = moduleZipFileName.rfind("_")

    if indexOfLastUnderScoreInModuleZipFileName == -1:
        exitWithError("The provided module zip file name (" + moduleZipFileName + ") is not formatted correctly. Please provide a .zip file with the file name in the format NAME_VERSION.zip. The version must also contain a dot.")

    indexOfZipExtensionInModuleZipFileName = moduleZipFileName.rfind(".zip")

    if indexOfZipExtensionInModuleZipFileName == -1:
        exitWithError("The provided module zip file name (" + moduleZipFileName + ") is not a zip file. Please provide a .zip file with the file name in the format NAME_VERSION.zip. The version must also contain a dot.")

    moduleName = moduleZipFileName[0:indexOfLastUnderScoreInModuleZipFileName]
    moduleVersion = moduleZipFileName[indexOfLastUnderScoreInModuleZipFileName + 1:indexOfZipExtensionInModuleZipFileName]

    if '.' not in moduleVersion:
        exitWithError("The provided module zip file name (" + moduleZipFileName + ") contains an invalid version declaration. Please provide a .zip file with the file name in the format NAME_VERSION.zip. The version must also contain a dot.")

    # Open the zip file
    moduleZipFile = zipfile.ZipFile(moduleZipFilePath)

    try:
        # Check that the zip file members all have valid CRC's and file headers
        firstBadFileName = moduleZipFile.testzip()
        if firstBadFileName is not None:
            exitWithError("The provided module zip file contains at least one invalid file. The first invalid file is named " + firstBadFileName + ". Please ensure that the file has a valid CRC and file header.")

        # Check if the module was zipped to include the module folder or just the module folder's contents.
        zipIncludesModuleFolder = False

        for moduleZipMemberName in moduleZipFile.namelist():
            if moduleZipMemberName.startswith('/') or moduleZipMemberName.startswith('..'):
                exitWithError("The provided module zip file contains at least one file with an invalid name that starts with '/' or '..'. The first invalid file is named " + moduleZipMemberName + ". Please ensure that all files have valid names.")

            if moduleZipMemberName.startswith(moduleName + "/"):
                zipIncludesModuleFolder = True

        if zipIncludesModuleFolder:
            moduleZipExtrationPath = helperlib.DSC_MODULES_PATH
        else:
            moduleZipExtrationPath = join(helperlib.DSC_MODULES_PATH, moduleName)

        # Extract module to destination path
        printVerboseMessage("Extracting module zip file from " + moduleZipFilePath + " to " + moduleZipExtrationPath)
        moduleZipFile.extractall(moduleZipExtrationPath)
    finally:
        moduleZipFile.close()

    # Check that we can find the extracted module folder
    moduleDestinationPath = join(moduleZipExtrationPath, moduleName)
    if not os.path.isdir(moduleDestinationPath):
        exitWithError("Unable to find module directory after attempting to extract module from zip to the path " + moduleDestinationPath)

    # Check that we can find the DSCResources folder under the extracted module folder
    moduleDscResourcesDestinationPath = join(moduleDestinationPath, 'DSCResources')
    if not os.path.isdir(moduleDscResourcesDestinationPath):
        exitWithError("Unable to find the DSCResources directory under the module directory at the path " + moduleDscResourcesDestinationPath + " after extracting module from zip to the path.")

    # Populate commom main DSC path
    dscMainFolderPath = join(helperlib.CONFIG_SYSCONFDIR, helperlib.CONFIG_SYSCONFDIR_DSC)

    # Verify the module checksum if specified
    if verifyChecksum:
        # Check that we can find the keyring file
        keyringFilePath = join(dscMainFolderPath, 'keyring.gpg')
        if not os.path.isfile(keyringFilePath):
            exitWithError("Unable to find DSC keyring file at " + keyringFilePath)

        # Check that we can find the asc file
        ascFileName = moduleName + ".asc"
        ascFilePath = join(moduleDestinationPath, ascFileName)
        if not os.path.isfile(ascFilePath):
            exitWithError("Unable to find module asc file at " + ascFilePath)

        # check that we can find the SHA256 sums file
        sha256SumsFileName = moduleName + ".sha256sums"
        sha256SumsFilePath = join(moduleDestinationPath, sha256SumsFileName)
        if not os.path.isfile(sha256SumsFilePath):
            exitWithError("Unable to find module SHA256 sums file at " + sha256SumsFilePath)

        # Verify the SHA256 sums file with the keyring and asc files
        verifySha256SumsCommand = "HOME=" + dscMainFolderPath + " gpg --no-default-keyring --keyring " + keyringFilePath + " --verify " + ascFilePath  + " " + sha256SumsFilePath
        verifySha256SumsResult = subprocess.call(verifySha256SumsCommand, shell = True)
        if verifySha256SumsResult != 0:
            exitWithError("Failed to verify module SHA256 sums file at " + sha256SumsFilePath + " using signature in module.", 2)

        # Perform SHA256 sums to verify module
        performSha256SumsCommand = "cd " + moduleDestinationPath + "; sha256sum -c " + sha256SumsFileName
        performSha256SumsResult = subprocess.call(performSha256SumsCommand, shell = True)
        if performSha256SumsResult != 0:
            exitWithError("Failed to verify module with the SHA256 sums file at " + sha256SumsFilePath, 2)

    # Write the module version file
    moduleVersionFilePath = join(moduleDestinationPath, 'VERSION')
    moduleVersionFileHandle = open(moduleVersionFilePath, "w+")
    try:
        moduleVersionFileHandle.write(moduleVersion)
    finally:
        moduleVersionFileHandle.close()

    # Populate common DSC paths
    dscConfigurationFolderPath = join (dscMainFolderPath, 'configuration')
    dscConfigurationRegistrationFolderPath = join(dscConfigurationFolderPath, 'registration')
    dscConfigurationSchemaFolderPath = join(dscConfigurationFolderPath, 'schema')

    # Populate common OMI namespace path
    omiNamespaceFolderName = helperlib.DSC_NAMESPACE.replace('/', '-')
    omiRegistrationFolderPath = join(helperlib.CONFIG_SYSCONFDIR, 'omiregister')
    omiNamespaceFolderPath = join(omiRegistrationFolderPath, omiNamespaceFolderName)

    # Populate common platform architecture
    resourceArchitectureFolderName = getPlatformArchitectureFolderName()

    # Install the module's resources
    moduleResources = os.listdir(moduleDscResourcesDestinationPath)

    # Note: Python 2.4 and 3 recognize different formats for octal
    strMode = "0777"

    octMode = int(strMode, base=8)

    for resource in moduleResources:
        resourceFolderPath = join(moduleDscResourcesDestinationPath, resource)

        # Skip anything that is not a directory
        if not os.path.isdir(resourceFolderPath):
            continue

        printVerboseMessage("Installing resource " + resource)

        # Create the resource DSC schema folder if it does not already exist
        resourceDscSchemaFolderPath = join(dscConfigurationSchemaFolderPath, resource)
        if not os.path.isdir(resourceDscSchemaFolderPath):
            os.mkdir(resourceDscSchemaFolderPath)

        # Copy the resource DSC schema to the the resource DSC schema folder
        resourceDscSchemaFileName = resource + ".schema.mof"
        resourceDscSchemaFileSourcePath = join(resourceFolderPath, resourceDscSchemaFileName)
        if os.path.isfile(resourceDscSchemaFileSourcePath):
            shutil.copy(resourceDscSchemaFileSourcePath, resourceDscSchemaFolderPath)
        else:
            exitWithError("Unable to find .schema.mof file for resource " + resource + " at the path " + resourceDscSchemaFileSourcePath)

        # Create the resource DSC registration folder if it does not already exist
        resourceDscRegistrationFolderPath = join(dscConfigurationRegistrationFolderPath, resource)
        if not os.path.isdir(resourceDscRegistrationFolderPath):
            os.mkdir(resourceDscRegistrationFolderPath)

        # Create the DSC registration file in the resource DSC registration folder
        resourceDscRegistrationFileName = resource + ".registration.mof"
        resourceDscRegistrationFileContent = getDscRegistrationFileContent(resource)

        resourceDscRegistrationFileDestinationPath = join(resourceDscRegistrationFolderPath, resourceDscRegistrationFileName)
        resourceDscRegistrationFileDesintationHandle = open(resourceDscRegistrationFileDestinationPath, "w+")
        try:
            resourceDscRegistrationFileDesintationHandle.write(resourceDscRegistrationFileContent)
        finally:
            resourceDscRegistrationFileDesintationHandle.close()

        # Copy the resource Python scripts to the DSC scripts directories
        resourceLibraryFolderPath = join(resourceFolderPath, resourceArchitectureFolderName)
        if not os.path.isdir(resourceLibraryFolderPath):
            exitWithError("Unable to find the resource library folder for the resource " + resource + " and platform architecture " + resourceArchitectureFolderName + " at the path " + resourceLibraryFolderPath + " in the extracted module.")

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
                resourceScriptFilePath = join(resourceScriptsPythonVersionScriptsFolderPath, resourceScriptFileName)
                if (os.path.isfile(resourceScriptFilePath)):
                    shutil.copy(resourceScriptFilePath, dscScriptsPythonVersionScriptsFolderPath)

        # Copy the resource library file to the OMI library folder
        resourceLibraryFileName = "lib" + resource + ".so"
        resourceLibraryFileSourcePath = join(resourceLibraryFolderPath, resourceLibraryFileName)

        if helperlib.DSC_NAMESPACE == "root/Microsoft/DesiredStateConfiguration":
            resourceLibraryFileDestinationName = resourceLibraryFileName
        else:
            resourceLibraryFileDestinationName = resourceLibraryFileName.replace('.so', "_" + omiNamespaceFolderName + ".so")

        resourceLibraryFileDestinationPath = join(helperlib.CONFIG_LIBDIR, resourceLibraryFileDestinationName)

        shutil.copy(resourceLibraryFileSourcePath, resourceLibraryFileDestinationPath)
        os.chmod(resourceLibraryFileDestinationPath , stat.S_IRUSR | stat.S_IWUSR | stat.S_IRGRP | stat.S_IROTH)
        filePermission = oct(os.stat(resourceLibraryFileDestinationPath).st_mode & octMode)
        if filePermission == "0644" or filePermission == "0o644":
            printVerboseMessage("Updated permissions of file: " + resourceLibraryFileDestinationPath + " to " + filePermission)
        else:
            exitWithError("Permissions on file: " + resourceLibraryFileDestinationPath + " set incorrectly: " + filePermission)

        
        if "omsconfig" in helperlib.DSC_SCRIPT_PATH:
            # Copy the resource library file to the OMSCONFIG library folder 
            resourceSharedObjectDestinationPath = join("/opt/dsc/lib", resource)

            if not os.path.isdir(resourceSharedObjectDestinationPath):
                os.mkdir(resourceSharedObjectDestinationPath)

            resourceSharedObjectFileDestinationPath = join(resourceSharedObjectDestinationPath, resourceLibraryFileName)

            shutil.copy(resourceLibraryFileSourcePath, resourceSharedObjectFileDestinationPath)
            os.chmod(resourceSharedObjectFileDestinationPath , stat.S_IRUSR | stat.S_IWUSR | stat.S_IRGRP | stat.S_IROTH)
            filePermission = oct(os.stat(resourceSharedObjectFileDestinationPath).st_mode & octMode)
            if filePermission == "0644" or filePermission == "0o644":
                printVerboseMessage("Updated permissions of file: " + resourceSharedObjectFileDestinationPath + " to " + filePermission)
            else:
                exitWithError("Permissions on file: " + resourceSharedObjectFileDestinationPath + " set incorrectly: " + filePermission)

        # Copy or write the OMI registration file to the OMI registration folder
        resourceOmiRegistrationFileName = resource + ".reg"
        resourceOmiRegistrationFileSourcePath = join(resourceFolderPath, resourceOmiRegistrationFileName)
        resourceOmiRegistrationFileDestinationPath = join(omiNamespaceFolderPath, resourceOmiRegistrationFileName)

        if helperlib.DSC_NAMESPACE == "root/Microsoft/DesiredStateConfiguration":
            shutil.copy(resourceOmiRegistrationFileSourcePath, resourceOmiRegistrationFileDestinationPath)

        else:
            # Read the resource OMI registration file
            resourceOmiRegistrationFileSourceHandle = open(resourceOmiRegistrationFileSourcePath, "r")
            try:
                resourceOmiRegistrationFileContent = resourceOmiRegistrationFileSourceHandle.read()
            finally:
                resourceOmiRegistrationFileSourceHandle.close()

            # Replace the namespace in the resource DSC registration file
            resourceOmiRegistrationFileContentWithOmsLib = resourceOmiRegistrationFileContent.replace("LIBRARY=" + resource, "LIBRARY=" + resource + '_' + omiNamespaceFolderName)

            # Write the content with the replaced namespace to the resource DSC registration file in the resource DSC registration folder
            resourceOmiRegistrationFileDesintationHandle = open(resourceOmiRegistrationFileDestinationPath, "w+")
            try:
                resourceOmiRegistrationFileDesintationHandle.write(resourceOmiRegistrationFileContentWithOmsLib)
            finally:
                resourceOmiRegistrationFileDesintationHandle.close()

        os.chmod(resourceOmiRegistrationFileDestinationPath, stat.S_IRUSR | stat.S_IWUSR | stat.S_IRGRP | stat.S_IROTH)
        filePermission = oct(os.stat(resourceOmiRegistrationFileDestinationPath).st_mode & octMode)
        if filePermission == "0644" or filePermission == "0o644":
            printVerboseMessage("Updated permissions of file: " + resourceOmiRegistrationFileDestinationPath + " to " + filePermission)
        else:
            exitWithError("Permissions on file: " + resourceOmiRegistrationFileDestinationPath + " set incorrectly: " + filePermission)

    # Regenerate the DSC Python scripts init files
    regenerateDscPythonScriptInitFiles()

if __name__ == "__main__":
    main(sys.argv[1:])