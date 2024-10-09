#!/usr/bin/env python2
#
# Copyright (C) Microsoft Corporation, All rights reserved.

""" Worker manager module.

    Note:
        This module is meant to be ran on posix systems and OMS integrated scenarios only.

        When testing this module make sure to add MSFT_nxOMSAutomationWorkerResource as an argument to started
        worker processes.
"""

import ConfigParser
import os
import subprocess
import sys
import time
import traceback

# import worker module after linuxutil.daemonize() call

NXAUTOMATION_USERNAME = "nxautomation"
RESOURCE_VERSION_ARG_PREFIX = "rversion:"
OMSUTIL_FILE_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "omsutil.py")


def loop(func):
    def decorated_func(*args, **kwargs):
        while True:
            try:
                func(*args, **kwargs)
            except Exception:
                exception_trace = str(traceback.format_exc())
                print exception_trace
                exit_on_error(exception_trace)
            time.sleep(30)

    return decorated_func


def exit_on_error(message, exit_code=1):
    crash_log_filename = "automation_workermanager_crash.log"
    util.exit_on_error(filename=crash_log_filename, message=message, exit_code=exit_code)


class WorkerManager:
    def __init__(self, oms_configuration_path):
        self.oms_configuration = OMSConfiguration(oms_configuration_path)

    @staticmethod
    def get_worker_and_sandbox_processes(process_list):
        """Gets a list of automation hybrid worker and sandbox process.

        Args:
            process_list : list, a list of process models used for the search.

        Note:
            For worker and sandbox process to be found, 3 conditions have to be met :
                1. The cmd used to start these process has to contain the python interpreter keyword (python)
                1. The cmd used to start these process has to refer to the respective python source (hybridworker.py/sandbox.py)
                2. The resource folder name has to be present in the path to the python source (nxOMSAutomationWorkerResource)

        Returns:
            A list of process model meeting the 3 conditions above
        """
        worker_and_sandbox_processes = []
        for process in process_list:
            if ("python" in process.cmd or "python2" in process.cmd) and ("hybridworker.py" in process.cmd or "sandbox.py" in process.cmd) \
                    and "MSFT_nxOMSAutomationWorkerResource" in process.cmd:
                worker_and_sandbox_processes.append(process)
        return worker_and_sandbox_processes

    @staticmethod
    def get_worker_processes(process_list):
        """Gets a list of automation hybrid worker.

        Args:
            process_list : list, a list of process models.

        Note:
            For worker process to be found, 3 conditions have to be met :
                1. The cmd used to start these process has to contain the python interpreter keyword (python)
                1. The cmd used to start these process has to refer to the respective python source (hybridworker.py)
                2. The resource folder name has to be present in the path to the python source (nxOMSAutomationWorkerResource)

        Returns:
            A list of process model meeting the 3 conditions above
        """
        worker_processes = []
        for process in process_list:
            if ("python" in process.cmd or "python2" in process.cmd) and "hybridworker.py" in process.cmd \
                    and "MSFT_nxOMSAutomationWorkerResource" in process.cmd:
                worker_processes.append(process)
        return worker_processes

    @staticmethod
    def get_process_containing_arg(process_list, arg_to_match):
        """Gets a list of process containing a specific argument.

        Args:
            process_list : list  , a list of process models.
            arg_to_match : string, a specific keyword to match

        Returns:
            A list of process model containing the specific argument
        """
        valid_worker_processes = []
        for process in process_list:
            if arg_to_match in process.cmd:
                valid_worker_processes.append(process)
        return valid_worker_processes

    @staticmethod
    def kill_process_list(process_list):
        """Kills a list of process.

        Args:
            process_list : list , a list of process models.

        Note:
            The list of process has to be owned by the current user
        """
        for process in process_list:
            print "killing " + str(process.pid)
            linuxutil.kill_current_user_process(process.pid)
            print "killed " + str(process.pid)

    @staticmethod
    def get_configuration_path_to_be_started(worker_process_list, valid_configuration_paths):
        """Returns a list of configuration path that has to be started. Configuration paths that are already being
        executed by a worker process will not be returned.

        Args:
            worker_process_list : list , a list of worker process models.
            valid_configuration_paths : list, a list of configuration path

        Returns:
            A list of configuration paths to be started
        """
        configuration_path_to_be_started = []
        for path in valid_configuration_paths:
            path_already_in_use = False
            for worker in worker_process_list:
                if path in worker.cmd:
                    path_already_in_use = True

            if path_already_in_use is False:
                configuration_path_to_be_started.append(path)
        return configuration_path_to_be_started

    def get_process_running_valid_configuration(self, process_list, valid_configuration_paths):
        """Returns a list of process running valid configuration path.

        Args:
            process_list : list , a list of process models.
            valid_configuration_paths : list, a list of configuration path

        Returns:
            A list of process running valid configuration paths
        """
        process_running_valid_configuration = []
        for valid_configuration_path in valid_configuration_paths:
            process_running_valid_configuration += self.get_process_containing_arg(process_list,
                                                                                   arg_to_match=valid_configuration_path)
        return process_running_valid_configuration

    def get_worker_and_sandbox_process_running_invalid_configuration(self, process_list, valid_configuration_paths):
        """Returns a list of process running invalid configuration path.

        Args:
            process_list : list , a list of process models.
            valid_configuration_paths : list, a list of configuration path

        Returns:
           A list of process running invalid configuration paths
        """
        worker_and_sandbox_processes = self.get_worker_and_sandbox_processes(process_list)
        process_running_valid_config = self.get_process_running_valid_configuration(worker_and_sandbox_processes,
                                                                                    valid_configuration_paths=
                                                                                    valid_configuration_paths)
        return set(worker_and_sandbox_processes).difference(process_running_valid_config)

    def get_process_running_up_to_date_resource_version(self, process_running_valid_config, current_resource_version):
        """Returns a list of process running source code that IS up to date based on the DSC resource version.

        Args:
            process_running_valid_config : list , a list of process models.
            current_resource_version : int, the most up to date dsc resource version

        Returns:
            A list of process running up to date source code
        """
        version_arg = RESOURCE_VERSION_ARG_PREFIX + current_resource_version
        return self.get_process_containing_arg(process_running_valid_config, arg_to_match=version_arg)

    def get_worker_process_running_outdated_resource_version(self, process_list, current_resource_version):
        """Returns a list of worker process running source code that IS NOT up to date based on the DSC resource
        version.

        Args:
            process_list : list , a list of process models.
            current_resource_version : int, the most up to date dsc resource version

        Returns:
            A list of process running outdated source code
        """
        worker_processes = self.get_worker_processes(process_list)
        worker_process_running_up_to_date_version = self.get_process_running_up_to_date_resource_version(
            worker_processes, current_resource_version=current_resource_version)
        return set(worker_processes).difference(worker_process_running_up_to_date_version)

    @loop
    def routine(self):
        """Main OMS worker manager routine.

        All the steps below are in the context of the nxautomation user
        1. Remove WORKER and SANDBOX processes that are not running "unknown" configuration path
        2. Remove WORKER processes running outdated resource version (we do not care about sandbox processes)
        3. Start worker processes for configuration path that are not yet running
        """
        print "in routine"
        valid_configuration_paths = self.oms_configuration.get_active_worker_configuration_paths()
        current_resource_version = self.oms_configuration.get_current_resource_version()

        # kill workers running rogue config
        process_running_invalid_configuration = self.get_worker_and_sandbox_process_running_invalid_configuration(
            linuxutil.get_current_user_processes(), valid_configuration_paths=valid_configuration_paths)
        print "removing " + str(len(process_running_invalid_configuration)) + " process running rogue config"
        self.kill_process_list(process_running_invalid_configuration)

        # kill outdated worker
        worker_process_running_outdated_version = self.get_worker_process_running_outdated_resource_version(
            linuxutil.get_current_user_processes(), current_resource_version=current_resource_version)
        print "removing " + str(
            len(worker_process_running_outdated_version)) + " process running outdated resource version"
        self.kill_process_list(worker_process_running_outdated_version)

        configuration_path_to_be_started = self.get_configuration_path_to_be_started(
            self.get_worker_processes(linuxutil.get_current_user_processes()),
            valid_configuration_paths=valid_configuration_paths)

        print "worker to be started " + str(len(configuration_path_to_be_started))

        if len(configuration_path_to_be_started) > 0 and linuxutil.get_current_username() == NXAUTOMATION_USERNAME:
            proc = subprocess.Popen(["sudo", "-u", NXAUTOMATION_USERNAME, "python2", OMSUTIL_FILE_PATH, "--initialize"],
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            output, error = proc.communicate()
            if proc.returncode != 0:
                raise Exception("Initialization failed (omsutil.py --initialize). Error : " + str(error))

        for configuration_path in configuration_path_to_be_started:
            start_worker_for_path = True
            running_workers = self.get_worker_processes(linuxutil.get_current_user_processes())

            # check if there's an existing worker running the desired configuration
            for worker in running_workers:
                if configuration_path in worker.cmd:
                    start_worker_for_path = False

            # create a new worker if needed
            if os.path.isfile(configuration_path) and start_worker_for_path is True and \
                            self.oms_configuration.get_disable_worker_creation() is False:
                cmd = ["python2", self.oms_configuration.get_hybrid_worker_source_path(), configuration_path, "managed"]

                # workspace id arg is used by the oms uninstall script
                workspace_id = self.oms_configuration.get_workspace_id()
                if workspace_id != self.oms_configuration.DEFAULT_EMPTY_VALUE:
                    cmd.append("rworkspace:" + str(workspace_id))

                # resource version is used by the oms resource when resource update are required
                resource_version_arg = "rversion:" + str(current_resource_version)
                cmd.append(resource_version_arg)

                print "starting worker process " + str(configuration_path)
                subprocess.Popen(cmd)
            else:
                print "configuration path doesn't exist or worker already running configuration"


class OMSConfiguration:
    SECTION_WORKER_CONF = "oms-worker-conf"
    KEY_AUTO_REGISTERED_WORKER_CONF_PATH = "auto_registered_worker_conf_path"
    KEY_MANUALLY_AUTO_REGISTERED_WORKER_CONF_PATH = "manually_registered_worker_conf_path"

    SECTION_OMS_GLOBAL = "oms-global"
    KEY_RESOURCE_VERSION = "resource_version"
    KEY_HYBRID_WORKER_PATH = "hybrid_worker_path"
    KEY_DISABLE_WORKER_CREATION = "disable_worker_creation"
    KEY_WORKSPACE_ID = "workspace_id"

    DEFAULT_EMPTY_VALUE = ""
    DEFAULT_HYBRID_WORKER_PATH = "/opt/microsoft/omsconfig/modules/nxOMSAutomationWorker/DSCResources/MSFT_nxOMSAutomationWorkerResource/automationworker/worker/hybridworker.py"

    def __init__(self, oms_configuration_path):
        self.oms_configuration_path = oms_configuration_path

    def get_config_reader(self):
        config = ConfigParser.SafeConfigParser({self.KEY_AUTO_REGISTERED_WORKER_CONF_PATH: self.DEFAULT_EMPTY_VALUE,
                                                self.KEY_MANUALLY_AUTO_REGISTERED_WORKER_CONF_PATH:
                                                    self.DEFAULT_EMPTY_VALUE,
                                                self.KEY_HYBRID_WORKER_PATH: self.DEFAULT_HYBRID_WORKER_PATH,
                                                self.KEY_DISABLE_WORKER_CREATION: "False",
                                                self.KEY_WORKSPACE_ID: self.DEFAULT_EMPTY_VALUE})

        config.read(self.oms_configuration_path)
        return config

    def get_active_worker_configuration_paths(self):
        if not os.path.isfile(self.oms_configuration_path):
            return []

        worker_configuration_paths = []
        config = self.get_config_reader()

        try:
            worker_configuration_paths.append(config.get(self.SECTION_WORKER_CONF,
                                                         self.KEY_AUTO_REGISTERED_WORKER_CONF_PATH))
        except (ConfigParser.NoOptionError, ConfigParser.NoSectionError):
            pass

        try:
            worker_configuration_paths.append(config.get(self.SECTION_WORKER_CONF,
                                                         self.KEY_MANUALLY_AUTO_REGISTERED_WORKER_CONF_PATH))
        except (ConfigParser.NoOptionError, ConfigParser.NoSectionError):
            pass

        print worker_configuration_paths
        valid_worker_configuration_paths = []
        for path in worker_configuration_paths:
            if path != self.DEFAULT_EMPTY_VALUE and os.path.isfile(path):
                valid_worker_configuration_paths.append(path)
        return valid_worker_configuration_paths

    def get_current_resource_version(self):
        config = self.get_config_reader()
        return config.get(self.SECTION_OMS_GLOBAL, self.KEY_RESOURCE_VERSION)

    def get_hybrid_worker_source_path(self):
        config = self.get_config_reader()
        return config.get(self.SECTION_OMS_GLOBAL, self.KEY_HYBRID_WORKER_PATH)

    def get_disable_worker_creation(self):
        config = self.get_config_reader()
        return config.getboolean(self.SECTION_OMS_GLOBAL, self.KEY_DISABLE_WORKER_CREATION)

    def get_workspace_id(self):
        config = self.get_config_reader()
        return config.get(self.SECTION_OMS_GLOBAL, self.KEY_WORKSPACE_ID)


def main():
    if len(sys.argv) < 2:
        exit_on_error("Missing oms configuration file path.")
    oms_configuration_path = str(sys.argv[1])

    if not os.path.isfile(oms_configuration_path):
        exit_on_error("Invalid oms configuration file path or empty configuration file (absolute path is required).")

    manager = WorkerManager(oms_configuration_path)
    manager.routine()


if __name__ == "__main__":
    # daemonize before loading the logging library to prevent deadlock in 2.4 (see: http://bugs.python.org/issue6721)
    import linuxutil

    linuxutil.daemonize()

    try:
        import util

        main()
    except:
        exit_on_error(traceback.format_exc())
