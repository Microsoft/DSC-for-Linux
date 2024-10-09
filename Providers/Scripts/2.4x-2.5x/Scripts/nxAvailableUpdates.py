#!/usr/bin/env python
# ===================================
# Copyright (c) Microsoft Corporation. All rights reserved.
# See license.txt for license information.
# ===================================

import subprocess
import sys
import imp
import re
import copy
import fnmatch
import time
import os
import uuid

protocol = imp.load_source('protocol', '../protocol.py')
nxDSCLog = imp.load_source('nxDSCLog', '../nxDSCLog.py')
helperlib = imp.load_source('helperlib', '../helperlib.py')
LG = nxDSCLog.DSCLog


# [ClassVersion("1.0.0"),FriendlyName("nxAvailableUpdates"),SupportsInventory()]
# class MSFT_nxAvailableUpdatesResource : OMI_BaseResource
# {
#   [Key, InventoryFilter] string Name;
#   [read] string BuildDate;
#   [read] string Repository;
#   [read] string Version;
#   [read] string Architecture;

# };

def Inventory_Marshall(Name):
    if Name is not None and len(Name) > 0:
        Name = Name.encode('ascii', 'ignore')
    retval, pkgs = GetUpdates(Name)
    for p in pkgs:
        p['Name'] = protocol.MI_String(p['Name'])
        p['BuildDate'] = protocol.MI_String(p['BuildDate'])
        p['Repository'] = protocol.MI_String(p['Repository'])
        p['Version'] = protocol.MI_String(p['Version'])
        p['Classification'] = protocol.MI_String(p['Classification'])
        p['Architecture'] = protocol.MI_String(p['Architecture'])
    Inventory = protocol.MI_InstanceA(pkgs)
    retd = {}
    retd["__Inventory"] = Inventory
    return retval, retd


def GetUpdates(Name):
    d = {}
    LG().Log('DEBUG', "Starting to check Available Updates")
    mgr = GetPackageManager()
    if mgr == None:
        LG().Log('ERROR', "Unable to find one of 'apt', 'yum', or 'zypper'.")
        return -1, d
    if mgr == 'apt':
        d = GetAptUpdates(Name)
    elif mgr == 'yum':
        d = GetYumUpdates(Name)
    elif mgr == 'zypper':
        d = GetZypperUpdates(Name)
    LG().Log('DEBUG', "Completed checking Available Updates")
    return 0, d


def GetAptUpdates(Name):
    # Format:
    # Inst python3.5 (3.5.0-3 Ubuntu:15.10/wily [amd64])
    # Inst sosreport [3.2-2ubuntu1] (3.2-2ubuntu1.1 Ubuntu:15.10/wily-updates [amd64])

    updates_list = []
    d = {}
    g_guid = str(uuid.uuid4())
    privileged_path = "/tmp/"

    #Collect Security updates
    security_sources_list = os.path.join(privileged_path, 'msft-v1-assess-security-{0}.list'.format(g_guid))
    prep_security_sources_list_cmd = 'grep security /etc/apt/sources.list > ' + security_sources_list

    #to remove all files, following this pattern
    remove_security_sources_list_cmd = 'rm ' + os.path.join(privileged_path, 'msft-v1-assess-security-*.list')
    dist_upgrade_simulation_cmd_template = 'LANG=en_US.UTF8 apt-get -s dist-upgrade <SOURCES> '
    # Refresh the repo
    if helperlib.CONFIG_SYSCONFDIR_DSC == "omsconfig":
        cmd = 'sudo /opt/microsoft/omsconfig/Scripts/OMSAptUpdates.sh'
    else:
        cmd = 'apt-get -q update'
    code, out = RunGetOutput(cmd, False, False)
    #Get Security Patches list
    security_patch_list = []

    try:
        code, out = RunGetOutput(prep_security_sources_list_cmd,False,False)
        if code != 0:
            LG().Log('ERROR', "Unexpected error in running cmd {0} : {1}".format(prep_security_sources_list_cmd, out))
            raise Exception("Unexpected error in running cmd {0} : {1}".format(prep_security_sources_list_cmd, out))

        security_updates_cmd = dist_upgrade_simulation_cmd_template.replace('<SOURCES>', '-oDir::Etc::Sourcelist=' + security_sources_list)
        code,out = RunGetOutput(security_updates_cmd,False,False)

        srch_txt = r'Inst[ ](.*?)[ ].*?[(](.*?)[ ](.*?)[ ]\[(.*?)\]'
        srch = re.compile(srch_txt, re.M | re.S)
        pkg_list = srch.findall(str(out))
        for package in pkg_list:
            security_patch_list.append(package[0])
        cmd = 'LANG=en_US.UTF8 apt-get -s dist-upgrade | grep "^Inst"'
        LG().Log('DEBUG', "Retrieving update package list using cmd:" + cmd)

        if len(out) < 2:
            return updates_list
        srch_txt = r'Inst[ ](.*?)[ ].*?[(](.*?)[ ](.*?)[ ]\[(.*?)\]'
        srch = re.compile(srch_txt, re.M | re.S)
        pkg_list = srch.findall(out)
        for pkg in pkg_list:
            d['BuildDate'] = ''
            d['Name'] = pkg[0]
            if len(Name) and not fnmatch.fnmatch(d['Name'], Name):
                continue
            d['Architecture'] = pkg[3]
            d['Version'] = pkg[1]
            if d['Name'] in security_patch_list:
                d['Classification'] = "Security Updates"
            else:
                d['Classification'] = "Others"
            d['Repository'] = pkg[2]
            updates_list.append(copy.deepcopy(d))

        LG().Log('DEBUG', "Number of packages being written to the XML: " + str(len(updates_list)))
        return updates_list
    finally:
        rmcode, rmout = RunGetOutput(remove_security_sources_list_cmd,False,False)
        if rmcode != 0:
            LG().Log('DEBUG', "Not able to delete {0}. Error: {1}".format(security_sources_list, rmout))
        else:
            LG().Log('DEBUG', "{0} folder cleared".format(privileged_path))

def GetYumUpdates(Name):
    # Format:
    # Name        : zsh
    # Arch        : x86_64
    # Version     : 5.0.2
    # Release     : 14.el7
    # Size        : 2.4 M
    # Repo        : base/7/x86_64
    # Summary     : Powerful interactive shell
    # URL         : http://zsh.sourceforge.net/
    # License     : MIT
    # Epoch and Buildtime are not available for 'yum info available'.
    # If 'repoquery is installed Epoch and Buildtime will be used.
    # Epoch is added to the front of the 'Version', and sanitized afterward.

    updates_list = []
    # No need to refresh the repo - 'check-update' will do this.

    if os.path.exists('/usr/bin/repoquery'):
        srch_str = r'Name.*?: (.*?)\n.*?Arch.*?: (.*?)\n.*?Version.*?: (.*?)\n.*?Release.*?: (.*?)\n.*?Repo.*?: (.*?)\n.*?Buildtime.*?: (.*?)\n'
    else:
        srch_str = r'Name.*?: (.*?)\n.*?Arch.*?: (.*?)\n.*?Version.*?: (.*?)\n.*?Release.*?: (.*?)\n.*?Repo.*?: (.*?)\n'

    if helperlib.CONFIG_SYSCONFDIR_DSC == "omsconfig":
        yum_list = 'sudo /opt/microsoft/omsconfig/Scripts/OMSYumUpdates.sh '
        yum_info = yum_list
    else:
        yum_list = 'yum check-update '
        if os.path.exists('/usr/bin/repoquery'):
            yum_info = 'repoquery --queryformat "Name : %{NAME}\nArch : %{ARCH}\nVersion :  %{EPOCH}:%{VERSION}\nRelease : %{RELEASE}\nRepo : %{REPO}\nBuildtime : %{BUILDTIME}" '
        else:
            yum_info = 'yum info available '
    cmd = "LANG=en_US.UTF8 " + yum_list
    start_time = time.time()
    LG().Log('DEBUG', "Retrieving update package list using cmd: " + cmd)
    retcode, pkg_list = RunGetOutput(cmd, False, False)
    LG().Log('DEBUG', "Cmd execution time: " + str((time.time() - start_time) / 60))
    #LG().Log('DEBUG', "Cmd output for update list : " + pkg_list)
        
    # Sample output from OMSYumUpdates.sh
    #   ca-certificates.noarch                                               1:1.1.0-4.el8                                            rhui-rhel-8-for-x86_64-baseos-rhui-rpms
    #   glibc.x86_64                                                         1:1.23.0-4.el8                                           rhui-rhel-8-for-x86_64-baseos-rhui-rpms
    #   glibc-common.x86_64                                                  1:1.45.0-4.el8                                           rhui-rhel-8-for-x86_64-baseos-rhui-rpms
    #   glibc-devel.x86_64                                                   1:1.23.0-4.el8                                           rhui-rhel-8-for-x86_64-baseos-rhui-rpms
    #   glibc-headers.x86_64                                                 1:1.56.0-4.el8                                           rhui-rhel-8-for-x86_64-baseos-rhui-rpms
    if validate_yum_pkg_list_output(pkg_list, retcode):
        # Remove the chatter.  Yum puts a blank line before the list.
        if pkg_list.find('\n\n') > -1:
            pkg_list = pkg_list[pkg_list.find('\n\n') + 2:]
        LG().Log('DEBUG', "Number of packages to be updated: " + str(len(pkg_list.splitlines())))
        srch = re.compile(srch_str, re.M | re.S)

        param_list = ""
        param_list2 = ""
        for pkg in pkg_list.splitlines():
            line = re.split(r'\s+', pkg.strip())

            if len(line) == 0:
                continue
            
            if line[0] == "Obsoleting":
                LG().Log('DEBUG', "List including obsolete packages: " + str(pkg_list))
                break

            if len(line) == 3 and is_yum_package(line[0]):
                pkg_and_arch = str(line[0]).split(".")
                pkg_name = pkg_and_arch[0]
                pkg_arch = pkg_and_arch[1]
                pkg_version = line[1]

                param_list = param_list + " " + str(pkg_name) + "-" + str(pkg_version) + "." + str(pkg_arch)
            else:
                LG().Log('DEBUG', "Package not in correct format: " + str(pkg.strip()))
                param_list2 = param_list2 + " " + line[0]

        if len(param_list) == 0 and len(param_list2) > 0:
            LG().Log('DEBUG', "No valid packages found. Falling back to package_info based on the package name.")

        cmd = "LANG=en_US.UTF8 " + yum_info + param_list
        LG().Log('DEBUG', "Retrieving individual package information from Yum using cmd: " + cmd)
        start_time = time.time()
        code, out = RunGetOutput(cmd, False, False)
        LG().Log('DEBUG', "Cmd execution time: " + str((time.time() - start_time) / 60))
        #LG().Log('DEBUG', "Cmd output is : " + out)
        updates_list = []
        if len(out) < 1 or ':' not in out:
            LG().Log('DEBUG', "Failed retrieving individual package info. Output is too small : " + out)
            return updates_list
        yum_pkg_info_list = srch.finditer(out)
        security_patches = get_yum_security_updates()
        updates_list = get_yum_updates_list(yum_pkg_info_list, security_patches, Name)
    LG().Log('DEBUG', "Number of packages being written to the XML: " + str(len(updates_list)))
    return updates_list
def get_product_name(package_name):
        """Splits out product name and architecture - if this is changed, modify in PackageFilter also"""
        return package_name

def get_yum_security_updates():
    d = {}
    if helperlib.CONFIG_SYSCONFDIR_DSC == "omsconfig":
        yum_check_security = 'sudo /opt/microsoft/omsconfig/Scripts/OMSYumSecurityUpdates.sh '
    else:
        yum_check_security = 'yum -q --security check-update '
        install_yum_security_prerequisite()
    code, out = RunGetOutput(yum_check_security,False,False)
    security_packages, security_package_versions = extract_packages_and_versions_including_duplicates(out)
    return security_packages

def install_yum_security_prerequisite():
    yum_check_security_prerequisite = 'sudo yum -y install yum-plugin-security'
    code, out = RunGetOutput(yum_check_security_prerequisite, False, False)

def is_yum_package(chunk):
    package_extensions = ['.x86_64', '.noarch', '.i686']

    # Using a list comprehension to determine if chunk is a package
    return len([p for p in package_extensions if p in chunk]) == 1

def extract_packages_and_versions_including_duplicates(output):
        """Returns packages and versions from given output"""
        packages = []
        versions = []

        print(output)
        lines = output.strip().split('\n')

        for line_index in range(0, len(lines)):
            line = re.split(r'\s+', lines[line_index].strip())
            next_line = []

            if line_index < len(lines) - 1:
                next_line = re.split(r'\s+', lines[line_index + 1].strip())

            # If we run into a length of 3, we'll accept it and continue
            if len(line) == 3 and is_yum_package(line[0]):
                packages.append(get_product_name(line[0]))
                versions.append(line[1])
            # We will handle these two edge cases where the output is on
            # two different lines and treat them as one line
            elif len(line) == 1 and len(next_line) == 2 and is_yum_package(line[0]):
                packages.append(get_product_name(line[0]))
                versions.append(next_line[0])
                line_index += 1
            elif len(line) == 2 and len(next_line) == 1 and is_yum_package(line[0]):
                packages.append(get_product_name(line[0]))
                versions.append(line[1])
                line_index += 1
            else:
                print("Inapplicable line")
                #self.composite_logger.log_debug(" - Inapplicable line (" + str(line_index) + "): " + lines[line_index])

        return packages, versions

def get_yum_updates_list(yum_pkg_info_list, security_updates, Name):
    d = {}
    updates_list = []
    for yum_pkg_info in yum_pkg_info_list:
        d['Name'] = yum_pkg_info.group(1)
        if len(Name) and not fnmatch.fnmatch(d['Name'], Name):
            continue
        d['Architecture'] = yum_pkg_info.group(2)
        d['Version'] = yum_pkg_info.group(3) + '-' + yum_pkg_info.group(4)
        if ':' not in d['Version']:  # Add a '0:' for epoch.
            d['Version'] = '0:' + d['Version']
        d['Version'] = d['Version'].replace('(none)', '0')  # Handle the Epoch '(none)'.
        if str(d['Name']).strip() + "." + str(d['Architecture']).strip() in security_updates:
           d['Classification'] = "Security Updates"
        else:
           d['Classification'] = "Others"
        d['Repository'] = yum_pkg_info.group(5)
        d['BuildDate'] = ''
        updates_list.append(copy.deepcopy(d))
    return updates_list


def validate_yum_pkg_list_output(pkg_list, retcode):
    repo_urls_unreachable_error = "Could not contact any CDS load balancers"
    repo_urls_unconfigured_error = "Cannot find a valid baseurl for repo"
    yum_packageupdates_available_exitcode = 100
    yum_no_applicable_packages_exitcode = 0

    if retcode == yum_packageupdates_available_exitcode:
        if len(pkg_list) < 2:
            LG().Log('INFO', "Nothing to send info on. No packages")
        else:
            return True
    elif retcode == yum_no_applicable_packages_exitcode:
        LG().Log('DEBUG', "No packages are available for update")
    else:
        LG().Log('DEBUG', "Error return code when retrieving update package list: " + str(retcode))
        LG().Log('DEBUG', "Output when retrieving update package list: " + str(pkg_list))
        if repo_urls_unreachable_error in str(pkg_list):
            LG().Log('DEBUG', "Unable to contact YUM repos" + str(pkg_list))
        if repo_urls_unconfigured_error in str(pkg_list):
            LG().Log('DEBUG', "YUM Repo urls are not configured" + str(pkg_list))
    return False


def GetZypperUpdates(Name):
    # Format:
    # Repository         | Name                       | Version | Category    | Status
    # -------------------+----------------------------+---------+-------------+-------
    # SLES11-SP3-Updates | slessp3-WALinuxAgent       | 10531   | recommended | needed
    # SLES11-SP3-Updates | slessp3-WALinuxAgent-12085 | 1       | recommended | needed

    updates_list = []
    d = {}
    # For omsagent the repo is refreshed in OMSZypperUpdates.sh.
    if helperlib.CONFIG_SYSCONFDIR_DSC == "omsconfig":
        zypper = 'sudo /opt/microsoft/omsconfig/Scripts/OMSZypperUpdates.sh'
        zypper_install_security_patches_simulate = 'sudo /opt/microsoft/omsconfig/Scripts/OMSZypperSecurityUpdates.sh'               
    else:
        zypper = 'zypper -q lu'
        zypper_install_security_patches_simulate = 'zypper --non-interactive patch --category security --dry-run'      
        # Refresh the repo.
        cmd = 'zypper -qn refresh'
        LG().Log('DEBUG', "Executing cmd: " + cmd)
        code, out = RunGetOutput(cmd, False, False)
    cmd = 'LANG=en_US.UTF8 ' + zypper + ' | grep "|" | grep -vE "Status|Current Version"'
    LG().Log('DEBUG', "Retrieving update package list using cmd:" + cmd)
    code, out = RunGetOutput(cmd, False, False)
    pkg_list = out
    # Get all security packages
    code,out = RunGetOutput(zypper_install_security_patches_simulate,False,False)
    packages_from_patch_data = extract_packages_from_patch_data(out)
    if len(pkg_list) < 2:
        return updates_list
    for pkg in pkg_list.splitlines():
        pkg = pkg.split('|')
        d['BuildDate'] = ''
        d['Name'] = pkg[2].strip()
        if len(Name) and not fnmatch.fnmatch(d['Name'], Name):
            continue
        d['Architecture'] = pkg[5].strip()
        d['Version'] = "0:" + pkg[4].strip()
        if d['Name'] in packages_from_patch_data:
           d['Classification'] = "Security Updates"
        else:
           d['Classification'] = "Others"
        d['Repository'] = pkg[1].strip()
        updates_list.append(copy.deepcopy(d))
    LG().Log('DEBUG', "Number of packages being written to the XML: " + str(len(updates_list)))
    return updates_list


def extract_packages_from_patch_data(output):
        """Returns packages (sometimes with version information embedded) from patch data"""
        packages = []
        parser_seeing_packages_flag = False

        lines = output.strip().split('\n')
        for line in lines:
            if not parser_seeing_packages_flag:
                if 'package is going to be installed' in line or 'package is going to be upgraded' in line or \
                        'packages are going to be installed:' in line or 'packages are going to be upgraded:' in line:
                    parser_seeing_packages_flag = True  # Start -- Next line contains information we need
                else:
                    print(" - Inapplicable line: " + line)
                continue

            if not line or line.isspace():
                parser_seeing_packages_flag = False
                continue  # End -- We're past a package information block

            line_parts = line.strip().split(' ')
            for line_part in line_parts:
                packages.append(line_part)

        return packages
 
def RunGetOutput(cmd, no_output, chk_err=True):
    """
    Wrapper for subprocess.check_output.
    Execute 'cmd'.  Returns return code and STDOUT,
    trapping expected exceptions.
    Reports exceptions to Error if chk_err parameter is True
    """

    def check_output(no_output, *popenargs, **kwargs):
        """
        Backport from subprocess module from python 2.7
        """
        if 'stdout' in kwargs:
            raise ValueError(
                'stdout argument not allowed, it will be overridden.')
        if no_output:
            out_file = None
        else:
            out_file = subprocess.PIPE
        process = subprocess.Popen(stdout=out_file, *popenargs, **kwargs)
        output, unused_err = process.communicate()
        retcode = process.poll()
        #LG().Log('DEBUG', "return code is  " + str(retcode))
        if retcode:
            cmd = kwargs.get("args")
            if cmd is None:
                cmd = popenargs[0]
            raise CalledProcessError(retcode, cmd, output=output)
        return output

    # Exception classes used by this module.
    class CalledProcessError(Exception):

        def __init__(self, returncode, cmd, output=None):
            self.returncode = returncode
            self.cmd = cmd
            self.output = output

        def __str__(self):
            return "Command '%s' returned non-zero exit status %d" \
                   % (self.cmd, self.returncode)

    try:
        output = check_output(no_output, cmd, stderr=subprocess.STDOUT, shell=True)
    except CalledProcessError, e:
        if chk_err:
            LG().Log('DEBUG', "CalledProcessError.  Error Code is " + str(e.returncode), file=sys.stdout)
            LG().Log('DEBUG', "CalledProcessError.  Command string was " + e.cmd, file=sys.stdout)
            LG().Log('DEBUG',
                     "CalledProcessError.  Command result was " + (e.output[:-1]).decode('utf8', 'ignore').encode(
                         "ascii",
                         "ignore"),
                     file=sys.stdout)
        if no_output:
            return e.returncode, None
        else:
            return e.returncode, e.output.decode('utf8', 'ignore').encode('ascii', 'ignore')
    if no_output:
        return 0, None
    else:
        return 0, output.decode('utf8', 'ignore').encode('ascii', 'ignore')


def GetPackageManager():
    ret = None
    # choose default - almost surely one will match.
    for b in ('apt-get', 'zypper', 'yum'):
        code, out = RunGetOutput('which ' + b, False, False)
        if code is 0:
            ret = b
            if ret == 'apt-get':
                ret = 'apt'
            break
    return ret
