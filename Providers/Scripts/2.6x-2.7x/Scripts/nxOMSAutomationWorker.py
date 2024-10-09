#!/usr/bin/env python2
# ====================================
# Copyright (c) Microsoft Corporation. All rights reserved.
# See license.txt for license information.
# ====================================
import ConfigParser
import imp
import logging
import logging.handlers
import os
import re
import signal
import subprocess
import sys
import time
import shutil
import pwd
import traceback

protocol = imp.load_source('protocol', '../protocol.py')
nxDSCLog = imp.load_source('nxDSCLog', '../nxDSCLog.py')
try:
    serializerfactory = imp.load_source('serializerfactory',
                                        '../../modules/nxOMSAutomationWorker/DSCResources/MSFT_nxOMSAutomationWorkerResource/automationworker/worker/serializerfactory.py')
except:
    # this is the path when running tests
    serializerfactory = imp.load_source('serializerfactory',
                                        '../../nxOMSAutomationWorker/automationworker/worker/serializerfactory.py')

try:
    linuxutil = imp.load_source('linuxutil',
                                '../../modules/nxOMSAutomationWorker/DSCResources/MSFT_nxOMSAutomationWorkerResource/automationworker/worker/linuxutil.py')
except:
    linuxutil = imp.load_source('linuxutil',
                                '../../nxOMSAutomationWorker/automationworker/worker/linuxutil.py')
LG = nxDSCLog.DSCLog


def Set_Marshall(ResourceSettings):
    try:
        settings = read_settings_from_mof_json(ResourceSettings)
        if not is_oms_primary_workspace(settings.workspace_id):
            # not primary workspace
            # return unconditional [0] for a NOOP on non-primary workspace
            log(DEBUG, "Set_Marshall skipped: non primary workspace. Set marshall returned [0]")
            return [0]

        if not nxautomation_user_exists():
            log(ERROR, "Set_Marshall skipped: please update omsagent to the latest version")
            return [0]

        # compatibility from 1.4 remove state.conf file
        if os.path.isfile(STATE_CONF_FILE_PATH):
            os.remove(STATE_CONF_FILE_PATH)

        # if an update is required from 1.4
        # major changes were made in 1.5.0.0 that are incompatible with the 1.4 way of doing things
        if is_any_1_4_process_running(get_nxautomation_ps_output(), settings.workspace_id):
            log(DEBUG, "Hybrid worker 1.4 detected, attempting to kill")
            kill_process_by_pattern_string(settings.workspace_id)

        try:
            kill_any_worker_running_as_omsagent(
                worker_pgrep_pattern="/opt/microsoft/omsconfig/modules/nxOMSAutomationWorker/DSCResources/MSFT_nxOMSAutomationWorkerResource/automationworker/worker/main.py")
        except:
            log(INFO, "Unable to kill old omsagent worker")
            pass

            # Kill worker managers that might already be running
        log(DEBUG, "Killing the instance of worker manager already running")
        kill_worker_manager(settings.workspace_id)

        # Kill all stray processes
        for ws_id in get_stray_worker_and_manager_wsids(get_nxautomation_ps_output(), settings.workspace_id):
            log(DEBUG, "Workspace id %s has worker and manager processes running in improper context. Terminating."
                % ws_id)
            kill_process_by_pattern_string(WORKSPACE_ID_PREFIX + ws_id)

        # Set up conf and working directories if it doesn't exit
        if not os.path.isdir(WORKER_STATE_DIR):
            os.makedirs(WORKER_STATE_DIR, PERMISSION_LEVEL_0770)
        if not os.path.isdir(WORKING_DIRECTORY_PATH):
            os.makedirs(WORKING_DIRECTORY_PATH, PERMISSION_LEVEL_0770)

        # if the directory does not have permision level 770, reset the permission level
        if os.stat(WORKER_STATE_DIR).st_mode & PERMISSION_LEVEL_0777 != PERMISSION_LEVEL_0770:
            # bitwise AND with PERMISSION_LEVEL_0777 will give true permission level
            os.chmod(WORKER_STATE_DIR, PERMISSION_LEVEL_0770)

        # set cert permissions
        proc = subprocess.Popen(["sudo", "-u", AUTOMATION_USER, "python2", OMS_UTIL_FILE_PATH, "--initialize"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        stdout, stderr = proc.communicate()
        if proc.returncode != 0:
            raise Exception("call to omsutil.py --initialize failed. %s, %s" % (stdout, stderr))

    except Exception:
        log(ERROR, "Set_Marshall returned [-1] with following error: %s" % traceback.format_exc())
        return [-1]

    try:
        # Create the configuration object
        write_omsconf_file(settings.workspace_id, settings.auto_register_enabled, settings.diy_enabled)
        os.chmod(OMS_CONF_FILE_PATH, PERMISSION_LEVEL_0770)
        log(DEBUG, "oms.conf file was written")
    except Exception:
        log(ERROR, "Set_Marshall returned [-1] with following error: %s" % traceback.format_exc())
        return [-1]

    try:
        # register the auto worker if required
        if settings.auto_register_enabled:
            # Write worker.conf file
            oms_workspace_id, agent_id = get_workspaceid_agentid_from_oms_config()
            # If both proxy files exist use the new one
            # If neither exist use the new path, path will have no file in it, but no file means no proxy set up
            # If one of them exists, use that
            proxy_conf_path = PROXY_CONF_PATH_NEW
            if not os.path.isfile(PROXY_CONF_PATH_NEW) and os.path.isfile(PROXY_CONF_PATH_LEGACY):
                proxy_conf_path = PROXY_CONF_PATH_LEGACY

            args = ["python2", REGISTRATION_FILE_PATH, "--register", "-w", settings.workspace_id, "-a", agent_id,
                    "-c", OMS_CERTIFICATE_PATH, "-k", OMS_CERT_KEY_PATH, "-f", WORKING_DIRECTORY_PATH, "-s",
                    WORKER_STATE_DIR, "-e", settings.azure_dns_agent_svc_zone, "-p", proxy_conf_path, "-g",
                    KEYRING_PATH]

            diy_account_id = get_diy_account_id()
            if diy_account_id:
                args.append("-y")
                args.append(diy_account_id)

            asset_tag, is_azure_vm, vm_id = get_optional_metadata()
            args.append("-i")
            args.append(vm_id)

            if is_azure_vm:
                args.append("-z")

            azure_resource_id = get_azure_resource_id_from_oms_config()
            if azure_resource_id:
                args.append("-v")
                args.append(azure_resource_id)

            proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            stdout, stderr = proc.communicate()
            # log(DEBUG, "Trying to register Linux hybrid worker with args: %s" % str(args))
            if proc.returncode == -5:
                log(ERROR, "Linux Hybrid Worker registration failed: DIY and auto-register account ids do not match")
                log(INFO, "Worker manager with be started without auto registered worker")
            elif proc.returncode != 0:
                raise Exception("Linux Hybrid Worker registration failed: Return code %s :" % str(proc.returncode)
                                + stderr + "\n" + stdout)

            elif not os.path.isfile(AUTO_REGISTERED_WORKER_CONF_PATH):
                raise Exception("Linux Hybrid Worker registration file could not be created")
            else:
                os.chmod(AUTO_REGISTERED_WORKER_CONF_PATH, PERMISSION_LEVEL_0770)

        if settings.diy_enabled:
            move_diy_settings_to_new_location()

    except Exception:
        log(ERROR, "Set_Marshall returned [-1] with following error: %s" % traceback.format_exc())
        return [-1]

    try:
        # start the worker manager proc
        if (settings.auto_register_enabled or settings.diy_enabled) and start_worker_manager_process(
                settings.workspace_id) < 0:
            log(ERROR, "Worker manager process could not be started. Set_Marshall returned [-1]")
            return [-1]
        elif not settings.auto_register_enabled and not settings.diy_enabled:
            log(DEBUG,
                "No solutions requiring linux hybrid worker are enabled. Terminating the hybrid worker processes")
            # Kill all workers and managers
            kill_process_by_pattern_string(WORKSPACE_ID_PREFIX + settings.workspace_id)
            if is_hybrid_worker_or_manager_running(settings.workspace_id):
                raise Exception("Could not kill worker and manager processes")
        log(INFO, "Set_Marshall returned [0]. Exited successfully")
        return [0]

    except Exception:
        log(ERROR, "Set_Marshall returned [-1] with following error: %s" % traceback.format_exc())
        return [-1]


def Test_Marshall(ResourceSettings):
    """
    Test method for the DSC resoruce
    If it returns [0] no further action is taken
    If it returns [-1] Set_Marshall is called
    It tests for whether oms.conf exists and the worker manager is running and the latest version
    :param ResourceSettings:
    :return: [0] if all tests pass [-1] otherwise
    """
    try:
        settings = read_settings_from_mof_json(ResourceSettings)
        log(DEBUG, "UpdatesEnabled is %s; AutomationWorkerEnabled is %s" % (settings.auto_register_enabled, settings.diy_enabled))
        if not is_oms_primary_workspace(settings.workspace_id):
            # not primary workspace
            # return unconditional [0] for a NOOP on non-primary workspace
            log(DEBUG, "Test_Marshall skipped: non primary workspace. Test_Marshall returned [0]")
            return [0]

        if not nxautomation_user_exists():
            log(ERROR, "Test_Marshall skipped: please update omsagent to the latest version")
            return [0]

        if get_stray_worker_and_manager_wsids(get_nxautomation_ps_output(), settings.workspace_id):
            log(INFO, "Test_Marshall returned [-1]: process started by other workspaces detected")
            return [-1]
        if not os.path.isfile(OMS_CONF_FILE_PATH):
            log(INFO, "Test_Marshall returned [-1]: oms.conf file not found")
            return [-1]
        if is_any_1_4_process_running(get_nxautomation_ps_output(), settings.workspace_id):
            log(INFO, "Test_Marshall returned [-1]: an older version of Hybrid Worker was found")
            return [-1]
        if (settings.auto_register_enabled or settings.diy_enabled) and not is_worker_manager_running_latest_version(
                settings.workspace_id):
            # Either the worker manager is not running, or its not latest
            log(INFO, "Test_Marshall returned [-1]: worker manager isn't running or is not latest")
            return [-1]
        if not settings.auto_register_enabled and not settings.diy_enabled and is_hybrid_worker_or_manager_running(
                settings.workspace_id):
            log(INFO, "Test_Marshall returned [-1]: worker or manager is running when no solution is enabled")
            return [-1]
        if not is_oms_config_consistent_with_mof(settings.auto_register_enabled, settings.diy_enabled):
            # Current oms.conf is inconsistent with the mof
            log(INFO, "Test_Marshall returned [-1]: oms.conf differs from configuration mof")
            return [-1]
        if settings.auto_register_enabled:
            if not os.path.isfile(AUTO_REGISTERED_WORKER_CONF_PATH):
                log(INFO, "Test_Marshall returned [-1]: auto register is enabled but registration file is absent")
                return [-1]
            elif not is_certificate_valid(AUTO_REGISTERED_WORKER_CONF_PATH, OMS_CERTIFICATE_PATH):
                # worker.conf file is present, check if the certificates are most recent
                log(INFO, "Test_Marshall returned [-1]: certificate mismatch for auto registered worker")
                return [-1]

    except Exception:
        log(INFO, "Test_Marshall returned [-1]: %s" % traceback.format_exc())
        return [-1]
    # All went well
    log(DEBUG, "Test_Marshall returned [0]")
    return [0]


def Get_Marshall(ResourceSettings):
    settings = read_settings_from_mof_json(ResourceSettings)
    retval = 0
    retd = dict()
    retd['WorkspaceId'] = protocol.MI_String(settings.workspace_id)
    retd['AzureDnsAgentSvcZone'] = protocol.MI_String(settings.azure_dns_agent_svc_zone)
    retd['UpdatesEnabled'] = protocol.MI_Boolean(settings.auto_register_enabled)
    retd['DiyEnabled'] = protocol.MI_Boolean(settings.diy_enabled)
    return retval, retd


# ###########################################################
# Begin user defined DSC functions
# ###########################################################
WORKSPACE_ID_PREFIX = "rworkspace:"

ERROR = logging.ERROR
DEBUG = logging.DEBUG
INFO = logging.INFO

OPTION_OMS_WORKSPACE_ID = "WORKSPACE_ID"
OPTION_AGENT_ID = "AGENT_GUID"
OPTION_AZURE_RESOURCE_ID = "AZURE_RESOURCE_ID"

SECTION_OMS_GLOBAL = "oms-global"
OPTION_AUTO_REGISTERED_WORKER_CONF_PATH = "auto_registered_worker_conf_path"
OPTION_MANUALLY_REGISTERED_WORKER_CONF_PATH = "manually_registered_worker_conf_path"
OPTION_WORKSPACE_ID = "workspace_id"

SECTION_OMS_WORKER_CONF = "oms-worker-conf"
OPTION_RESOURCE_VERSION = "resource_version"
OPTION_HYBRID_WORKER_PATH = "hybrid_worker_path"
OPTION_DISABLE_WORKER_CREATION = "disable_worker_creation"

SECTION_OMS_METADATA = "oms-metadata"
OPTION_JRDS_CERT_THUMBPRINT = "jrds_cert_thumbprint"

SECTION_WORKER_REQUIRED = "worker-required"
OPTION_ACCOUNT_ID = "account_id"
OPTION_JRDS_CERT_PATH = "jrds_cert_path"
OPTION_JRDS_KEY_PATH = "jrds_key_path"
OPTION_WORKING_DIRECTORY_PATH = "working_directory_path"

SECTION_WORKER_OPTIONAL = "worker-optional"
OPTION_STATE_DIRECTORY_PATH = "state_directory_path"

WORKER_STATE_DIR = "/var/opt/microsoft/omsagent/state/automationworker"
DIY_WORKER_STATE_DIR = "/home/nxautomation/state"
DIY_WORKER_STATE_DIR_OLD = os.path.join(WORKER_STATE_DIR, "diy")
OMS_CONF_FILE_PATH = os.path.join(WORKER_STATE_DIR, "oms.conf")
AUTO_REGISTERED_WORKER_CONF_PATH = os.path.join(WORKER_STATE_DIR, "worker.conf")
DIY_WORKER_CONF_PATH = os.path.join(DIY_WORKER_STATE_DIR, "worker.conf")
DIY_WORKER_CONF_PATH_OLD = os.path.join(DIY_WORKER_STATE_DIR_OLD, "worker.conf")
STATE_CONF_FILE_PATH = os.path.join(WORKER_STATE_DIR, "state.conf")

OMS_PRIMARY_WORKSPACE_CONF_DIR = "/etc/opt/microsoft/omsagent/conf"
DSC_RESOURCE_VERSION_FILE = "/opt/microsoft/omsconfig/modules/nxOMSAutomationWorker/VERSION"
OMS_ADMIN_CONFIG_FILE = os.path.join(OMS_PRIMARY_WORKSPACE_CONF_DIR, "omsadmin.conf")
OMS_AGENTID_FILE= "/etc/opt/microsoft/omsagent/agentid"
WORKING_DIRECTORY_PATH = "/var/opt/microsoft/omsagent/run/automationworker"
DIY_WORKING_DIRECTORY_PATH = "/home/nxautomation/run"
WORKER_MANAGER_START_PATH = "/opt/microsoft/omsconfig/modules/nxOMSAutomationWorker/DSCResources/MSFT_nxOMSAutomationWorkerResource/automationworker/worker/main.py"
HYBRID_WORKER_START_PATH = "/opt/microsoft/omsconfig/modules/nxOMSAutomationWorker/DSCResources/MSFT_nxOMSAutomationWorkerResource/automationworker/worker/hybridworker.py"
PROXY_CONF_PATH_LEGACY = os.path.join(OMS_PRIMARY_WORKSPACE_CONF_DIR, "proxy.conf")
PROXY_CONF_PATH_NEW = "/etc/opt/microsoft/omsagent/proxy.conf"
REGISTRATION_FILE_PATH = "/opt/microsoft/omsconfig/modules/nxOMSAutomationWorker/DSCResources/MSFT_nxOMSAutomationWorkerResource/automationworker/scripts/register_oms.py"
OMS_CERTIFICATE_PATH = "/etc/opt/microsoft/omsagent/certs/oms.crt"
OMS_CERT_KEY_PATH = "/etc/opt/microsoft/omsagent/certs/oms.key"
KEYRING_PATH = "/etc/opt/omi/conf/omsconfig/keyring.gpg"
OMS_UTIL_FILE_PATH = "/opt/microsoft/omsconfig/modules/nxOMSAutomationWorker/DSCResources/MSFT_nxOMSAutomationWorkerResource/automationworker/worker/omsutil.py"

# permission level rwx rwx ---
# leading zero is necessary because this is an octal number
# Note: for python 3.x use 0o770 instead of 0770
PERMISSION_LEVEL_0770 = 0770
PERMISSION_LEVEL_0777 = 0777

AUTOMATION_USER = "nxautomation"
OMSAGENT_USER = "omsagent"

LOCAL_LOG_LOCATION = "/var/opt/microsoft/omsagent/log/nxOMSAutomationWorker.log"
LOG_LOCALLY = False


def get_diy_account_id():
    """
    Gets the account id from diy conf file
    :return: The account id if the configuration file exists, otherwise None
    """
    try:
        diy_config = ConfigParser.ConfigParser()
        diy_config.read(DIY_WORKER_CONF_PATH)
        return diy_config.get(SECTION_WORKER_REQUIRED, OPTION_ACCOUNT_ID)
    except:
        return None


def get_optional_metadata():
    unknown = "Unknown"
    asset_tag = unknown
    vm_id = unknown
    is_azure_vm = False
    try:
        proc = subprocess.Popen(["sudo", "-u", AUTOMATION_USER, "python2", OMS_UTIL_FILE_PATH, "--dmidecode"],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        dmidecode, error = proc.communicate()
        if proc.returncode != 0 or not dmidecode:
            raise Exception("Unable to invoke omsutil.py --dmidecode: %s" % error)
        is_azure_vm = linuxutil.is_azure_vm(dmidecode)
        if is_azure_vm:
            asset_tag = linuxutil.get_azure_vm_asset_tag()
        vm_id = linuxutil.get_vm_unique_id_from_dmidecode(sys.byteorder, dmidecode)
    except Exception, e:
        log(INFO, "unable to get_optional_metadata: %s" % str(e))

    return asset_tag, is_azure_vm, vm_id



def is_certificate_valid(worker_conf_path, certificate_path):
    """
    certificate is vaild when thumbprints match
    worker.conf stores the certificate thumbprint that was used to register linux hybrid worker for updates and take
    action solution (auto registered)
    if the thumbprint doesn't match with the certificate in the certificate_path, then certificates have been rotated
    and re-registration is required
    :param worker_conf_path:
    :param certificate_path:
    :return: True if thumbprints match, false otherwise.
    """
    try:
        worker_conf = ConfigParser.ConfigParser()
        worker_conf.read(worker_conf_path)
        worker_certificate_thumbprint = worker_conf.get(SECTION_OMS_METADATA, OPTION_JRDS_CERT_THUMBPRINT)

        issuer, subject, omsagent_certificate_thumbprint = linuxutil.get_cert_info(certificate_path)

        if worker_certificate_thumbprint == omsagent_certificate_thumbprint:
            return True
    except:
        pass
    return False


class Settings:
    workspace_id = ""
    azure_dns_agent_svc_zone = ""
    auto_register_enabled = ""
    diy_enabled = ""

    def __init__(self, workpsace_id, azure_dns_agent_svc_zone, updates_enabled, diy_enabled):
        self.workspace_id = workpsace_id
        self.azure_dns_agent_svc_zone = azure_dns_agent_svc_zone
        self.auto_register_enabled = updates_enabled
        self.diy_enabled = diy_enabled


def read_settings_from_mof_json(json_serialized_string):
    """
    Deserializes a JSON serialized string
    :param json_serialized_string: the serialized JOSN string
    :return: Settings object
    """
    try:
        json_serializer = serializerfactory.get_serializer(sys.version_info)
        settings = json_serializer.loads(json_serialized_string)
        workspace_id = settings[0]["WorkspaceId"].encode("ascii", "ignore")
        azure_dns_agent_svc_zone = settings[0]["AzureDnsAgentSvcZone"].encode("ascii", "ignore")
        updates_enabled = settings[0]["Solutions"]["Updates"]["Enabled"]
        diy_enabled = settings[0]["Solutions"]["AzureAutomation"]["Enabled"]
        return Settings(workspace_id, azure_dns_agent_svc_zone, updates_enabled, diy_enabled)
    except Exception, e:
        log(ERROR, "Json parameters deserialization Error: %s" % str(e))
        raise e


def is_hybrid_worker_or_manager_running(workspace_id):
    search_expression = WORKSPACE_ID_PREFIX + workspace_id
    result, retcode = run_pgrep_command(search_expression)
    if result and retcode == 0:
        log(DEBUG, "Hybrid worker and manager processes detected: %s" % result)
        return True
    else:
        log(DEBUG, "No hybrid worker or manager processes found")
        return False


def is_oms_config_consistent_with_mof(updates_enabled, diy_enabled, oms_conf_file_path=OMS_CONF_FILE_PATH):
    if not os.path.isfile(oms_conf_file_path):
        return False

    oms_config = ConfigParser.ConfigParser()
    oms_config.read(oms_conf_file_path)
    if not oms_config.has_section(SECTION_OMS_WORKER_CONF):
        return False
    updates_present = oms_config.has_option(SECTION_OMS_WORKER_CONF, OPTION_AUTO_REGISTERED_WORKER_CONF_PATH)
    diy_present = oms_config.has_option(SECTION_OMS_WORKER_CONF, OPTION_MANUALLY_REGISTERED_WORKER_CONF_PATH)

    return updates_present == updates_enabled and diy_present == diy_enabled


def write_omsconf_file(workspace_id, updates_enabled, diy_enabled):
    oms_config = ConfigParser.ConfigParser()
    if os.path.isfile(OMS_CONF_FILE_PATH):
        oms_config.read(OMS_CONF_FILE_PATH)

    # oms.conf region [oms-worker-conf]
    if not oms_config.has_section(SECTION_OMS_WORKER_CONF):
        oms_config.add_section(SECTION_OMS_WORKER_CONF)
    if updates_enabled:
        oms_config.set(SECTION_OMS_WORKER_CONF, OPTION_AUTO_REGISTERED_WORKER_CONF_PATH,
                       AUTO_REGISTERED_WORKER_CONF_PATH)
    else:
        oms_config.remove_option(SECTION_OMS_WORKER_CONF, OPTION_AUTO_REGISTERED_WORKER_CONF_PATH)
    if diy_enabled:
        oms_config.set(SECTION_OMS_WORKER_CONF, OPTION_MANUALLY_REGISTERED_WORKER_CONF_PATH,
                       DIY_WORKER_CONF_PATH)
    else:
        oms_config.remove_option(SECTION_OMS_WORKER_CONF, OPTION_MANUALLY_REGISTERED_WORKER_CONF_PATH)

    # oms.conf region [oms-global]
    if not oms_config.has_section(SECTION_OMS_GLOBAL):
        oms_config.add_section(SECTION_OMS_GLOBAL)
    oms_config.set(SECTION_OMS_GLOBAL, OPTION_RESOURCE_VERSION, get_module_version())
    oms_config.set(SECTION_OMS_GLOBAL, OPTION_HYBRID_WORKER_PATH, HYBRID_WORKER_START_PATH)
    oms_config.set(SECTION_OMS_GLOBAL, OPTION_WORKSPACE_ID, workspace_id)
    if not oms_config.has_option(SECTION_OMS_GLOBAL, OPTION_DISABLE_WORKER_CREATION):
        oms_config.set(SECTION_OMS_GLOBAL, OPTION_DISABLE_WORKER_CREATION, "False")
    oms_config_fp = open(OMS_CONF_FILE_PATH, 'wb')
    oms_config.write(oms_config_fp)
    oms_config_fp.close()

def move_diy_settings_to_new_location():
    if os.path.isdir(DIY_WORKER_STATE_DIR_OLD):
        # Destination directory is already created by omsutil --initialize

        proc = subprocess.Popen(["cp", "-r", "-f", DIY_WORKER_STATE_DIR_OLD, DIY_WORKER_STATE_DIR],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        stdout, stderr = proc.communicate()
        if proc.returncode != 0:
            raise OSError(
                "Copying old diy directory to new diy directory failed\nstdout: " + stdout + "\nstderr: " + stderr)

        if os.path.isfile(DIY_WORKER_CONF_PATH_OLD):
            worker_config = ConfigParser.ConfigParser()
            worker_config.read(DIY_WORKER_CONF_PATH_OLD)
            certificate_path = worker_config.get(SECTION_WORKER_REQUIRED, OPTION_JRDS_CERT_PATH)
            key_path = worker_config.get(SECTION_WORKER_REQUIRED, OPTION_JRDS_KEY_PATH)

            new_certificate_path = os.path.join(DIY_WORKER_STATE_DIR, os.path.basename(certificate_path))
            new_key_path = os.path.join(DIY_WORKER_STATE_DIR, os.path.basename(key_path))

            # explicitly copy certificate and key files if not already copied
            if not os.path.isfile(new_certificate_path):
                shutil.copyfile(certificate_path, new_certificate_path)
            if not os.path.isfile(new_key_path):
                shutil.copyfile(key_path, new_key_path)


            worker_config.set(SECTION_WORKER_REQUIRED, OPTION_JRDS_CERT_PATH, new_certificate_path)
            worker_config.set(SECTION_WORKER_REQUIRED, OPTION_JRDS_KEY_PATH, new_key_path)
            worker_config.set(SECTION_WORKER_REQUIRED, OPTION_WORKING_DIRECTORY_PATH, DIY_WORKING_DIRECTORY_PATH)
            worker_config.set(SECTION_WORKER_OPTIONAL, OPTION_STATE_DIRECTORY_PATH, DIY_WORKER_STATE_DIR)

            if os.path.isfile(DIY_WORKER_CONF_PATH):
                os.remove(DIY_WORKER_CONF_PATH)
            with open(DIY_WORKER_CONF_PATH, 'wb') as fp:
                worker_config.write(fp)

        # remove old config file when copy is complete
        shutil.rmtree(DIY_WORKER_STATE_DIR_OLD)
        log(INFO, 'Migrated DIY worker settings and working directory under /home/nxautomation')


def is_oms_primary_workspace(workspace_id):
    """
    Detect if the passed workspace id is primary workspace on multi-homing enabled OMS agent
    Mulit-homing for OMS is 2 tiered, one primary and multiple secondary workspaces are allowed
    Currently, the Automation Worker should only run on primary workspace
    A primary OMS workspace is pointed to by the symbolic links of old style (single homing) paths like
    /etc/opt/microsoft/omsagent/ the actual location of which is /etc/opt/microsoft/omsagent/<workspace id>.
    A sufficient test for checking whether a given workspace id belongs to a primary is to compare it against the
    workspace id found in the oms config file in the old style path
    :return: True, if the given workspace id belongs to the primary OMS workspace, False otherwise
    """
    if not os.path.exists(OMS_PRIMARY_WORKSPACE_CONF_DIR):
        log(INFO, "Primary workspace conf directory not found")
        return False
    oms_workspace_id, agent_id = get_workspaceid_agentid_from_oms_config()
    if oms_workspace_id == workspace_id:
        return True
    else:
        return False


def read_omsconfig_file():
    if os.path.isfile(OMS_ADMIN_CONFIG_FILE):
        # the above path always points to the oms configuration file of the primary workspace
        keyvals = config_file_to_kv_pair(OMS_ADMIN_CONFIG_FILE)
        if os.path.isfile(OMS_AGENTID_FILE):
            # OMS_AGENTID_FILE is a new addition to the omsagent. If the file is not present, the agentid is supposed to be present in the OMS_ADMIN_CONFIG_FILE
            agentid_file = open(OMS_AGENTID_FILE, "r")
            agent_id = agentid_file.read().strip()
            agentid_file.close()
            keyvals[OPTION_AGENT_ID] = agent_id
        return keyvals
    else:
        error_string = "could not find file " + OMS_ADMIN_CONFIG_FILE
        log(DEBUG, error_string)
        raise ConfigParser.Error(error_string)


def get_azure_resource_id_from_oms_config():
    keyvals = read_omsconfig_file()
    try:
        return keyvals[OPTION_AZURE_RESOURCE_ID].strip()
    except KeyError:
        log(DEBUG, "Azure resource id was not specified in omsadmin config file")
        return ""


def get_workspaceid_agentid_from_oms_config():
    # Reads the oms config file
    # Returns: AgentID config value

    keyvals = read_omsconfig_file()
    try:
        return keyvals[OPTION_OMS_WORKSPACE_ID].strip(), keyvals[OPTION_AGENT_ID].strip()
    except KeyError, exception:
        log(DEBUG, str(exception))
        raise ConfigParser.Error(str(exception))


def config_file_to_kv_pair(filename):
    # gets key value pairs from files with similar format to omsadmin.conf
    retval = dict()
    f = open(filename, "r")
    contents = f.read()
    f.close()
    lines = contents.splitlines()
    for line in lines:
        # Find first '='; everything before is key, everything after is value
        midpoint = line.find("=")
        if (midpoint == 0 or midpoint == -1):
            # Skip over lines without = or lines that begin with =
            continue
        key = line[:midpoint]
        value = line[midpoint + 1:]
        retval[key] = value
    return retval


def start_worker_manager_process(workspace_id):
    """
    Start the worker_manager_process
    :param workspace_id:
    :return: the pid of the worker manager process
    """
    proc = subprocess.Popen(["sudo", "-u", AUTOMATION_USER, "python2", WORKER_MANAGER_START_PATH, OMS_CONF_FILE_PATH,
                             WORKSPACE_ID_PREFIX + workspace_id, get_module_version()])
    for i in range(0, 5):
        time.sleep(3)
        pid = get_worker_manager_pid_and_version(workspace_id, throw_error_on_multiple_found=False)[0]
        if pid > 0:
            # if the pid is greater than 0
            return pid

    # Failure path
    os.kill(proc.pid, signal.SIGTERM)
    return -1


def is_any_1_4_process_running(processes, workspace_id):
    for ps in processes:
        if ps:
            version = ps.split(" ")[-1]
            if WORKER_MANAGER_START_PATH in ps and workspace_id in ps and version == "1.4":
                return True
    return False

def get_worker_manager_pid_and_version(workspace_id, throw_error_on_multiple_found=True):
    """
    Returns the PID of the worker manager
    :return: pid of the worker manager, -1 if it isn't running
    """
    processes = get_nxautomation_ps_output()
    manager_processes_found = 0
    pid = -1
    version = "0.0"
    for process_line in processes:
        if process_line:
            process_line = str(process_line)
            # make sure process_line is not null or empty
            split_line = process_line.split(" ")
            args = " ".join(split_line[1:])
            if WORKER_MANAGER_START_PATH in args and workspace_id in args:
                pid = int(split_line[0])
                version = split_line[-1]
                manager_processes_found += 1
                if throw_error_on_multiple_found and manager_processes_found > 1:
                    raise AssertionError("More than one manager processes found")
    if pid == -1:
        log(INFO, "Failed to detect instance of worker manager")
    return pid, version


class Filter:
    workpsace_id = ""

    def __init__(self, workspace_id):
        self.workpsace_id = workspace_id

    def detect_stray_workspace(self, ps_string):
        uuid_pattern = re.compile("[a-z0-9]{8}-[a-z0-9]{4}-[a-z0-9]{4}-[a-z0-9]{4}-[a-z0-9]{12}")
        return uuid_pattern.findall(ps_string.lower()) and self.workpsace_id not in ps_string


def get_nxautomation_ps_output():
    if 'COLUMNS' in os.environ:
        os.environ['COLUMNS'] = "3000"

    proc = subprocess.Popen(["ps", "-u", AUTOMATION_USER, "-o", "pid=", "-o", "args="], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    command, error = proc.communicate()

    if proc.returncode != 0 or error:
        log(INFO, "Failed to read nxautomation user processes")
        return []

    command = command.strip()
    if command:
        processes = [x.strip() for x in command.split('\n')]
    else:
        processes = []
    return processes


def is_worker_manager_running_latest_version(workspace_id):
    try:
        pid, running_version = get_worker_manager_pid_and_version(workspace_id)
    except AssertionError:
        # more than one manager processes were found
        return False
    available_version = get_module_version()
    log(DEBUG, "running version is: " + running_version)
    log(DEBUG, "latest available version is: " + available_version)
    return pid > 0 and running_version == available_version


def kill_stray_processes(workspace_id):
    processes = get_nxautomation_ps_output()
    for wrkspc_id in get_stray_worker_and_manager_wsids(processes, workspace_id):
        kill_process_by_pattern_string(wrkspc_id)


def get_stray_worker_and_manager_wsids(processes, workspace_id):
    """
    Gets the pids of the workers and that are running in the context of another user
    :param workspace_id:
    :return: list of pids not running in context of workspace_id
    """
    uuid_pattern = re.compile("rworkspace:([a-z0-9]{8}-[a-z0-9]{4}-[a-z0-9]{4}-[a-z0-9]{4}-[a-z0-9]{12})")
    all_workspaces = [uuid_pattern.search(x).group(1) for x in processes if uuid_pattern.findall(x.lower())]
    return set(all_workspaces).difference([workspace_id])


def kill_worker_manager(workspace_id):
    """ Worker manger process if it exists
    Exceptions:
        throws exception if process was running and could not be killed
    """
    pattern_match_string = "python\s.*main\.py.*%s%s\s" % (WORKSPACE_ID_PREFIX, workspace_id)

    retval = kill_process_by_pattern_string(pattern_match_string)
    # can't depend on the return value to ensure that the process was killed since it pattern matches
    pid, version = get_worker_manager_pid_and_version(workspace_id)
    if pid > 0:
        # worker was not killed
        raise OSError("Could not kill worker manager process")
    if retval == 0:
        log(DEBUG, "Processes for worker manager were terminated successfully")
    else:
        log(DEBUG, "No worker manager processes to be killed")


def kill_process_by_pattern_string(pattern_match_string):
    result, retcode = run_pgrep_command(pattern_match_string)
    if retcode == 0:
        log(DEBUG, "The following worker processes will be terminated: %s" % result)
    else:
        log(DEBUG, "No process to terminate")
    # the above code is for logging only, we don't use its output to determine which process to kill
    return subprocess.call(["sudo", "pkill", "-u", AUTOMATION_USER, "-f", pattern_match_string])


def kill_any_worker_running_as_omsagent(worker_pgrep_pattern):
    proc = subprocess.Popen(["pgrep", "-u", OMSAGENT_USER, "-f", worker_pgrep_pattern], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    result, error = proc.communicate()
    result = str(result)
    result = result.replace('\n', ' ')
    if proc.returncode == 0:
        log(DEBUG, "The following old worker processes will be terminated: %s" % result)
    else:
        log(DEBUG, "No old worker process to terminate")
    # the above code is for logging only, we don't use its output to determine which process to kill
    subprocess.call(["pkill", "-u", OMSAGENT_USER,"-f", worker_pgrep_pattern])


def run_pgrep_command(pattern_match_string):
    proc = subprocess.Popen(["pgrep", "-u", AUTOMATION_USER, "-f", pattern_match_string], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    result, error = proc.communicate()
    result = str(result)
    result = result.replace('\n', ' ')
    return result, proc.returncode


def get_module_version():
    """
    Gets the version of the installed nxOMSAutomationWorker module
    :return: str: module version number
    """
    version_file_handle = open(DSC_RESOURCE_VERSION_FILE, 'r')
    version = version_file_handle.read().strip()
    version_file_handle.close()
    return version


def nxautomation_user_exists():
    """
    Tests if the user nxautomation exists on the machine
    Newer OMS agent installs will have that user
    :return: True if user "nxautomation" exists on the system, False otherwise
    """
    try:
        pwd.getpwnam(AUTOMATION_USER)
    except KeyError:
        # if the user was not found
        log(INFO, "%s was NOT found on the system" % (AUTOMATION_USER))
        return False
    log(INFO, "%s was found on the system" % (AUTOMATION_USER))
    return True


def config_file_to_kv_pair(filename):
    # gets key value pairs from files with similar format to omsadmin.conf
    retval = dict()
    f = open(filename, "r")
    contents = f.read()
    f.close()
    lines = contents.splitlines()
    for line in lines:
        # Find first '='; everything before is key, everything after is value
        midpoint = line.find("=")
        if (midpoint == 0 or midpoint == -1):
            # Skip over lines without = or lines that begin with =
            continue
        key = line[:midpoint]
        value = line[midpoint + 1:]
        retval[key] = value
    return retval


def log(level, message):
    try:
        LG().Log(logging._levelNames[level], message)
    except:
        pass
    if LOG_LOCALLY:
        try:
            log_local(level, message)
        except:
            pass


def log_local(level, message):
    log_fh = open(LOCAL_LOG_LOCATION, 'ab')
    log_fh.write("%s: %s" % (logging._levelNames[level], message))
    log_fh.close()
