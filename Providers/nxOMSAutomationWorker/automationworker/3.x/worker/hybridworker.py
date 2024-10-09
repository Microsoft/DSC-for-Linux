#!/usr/bin/env python3
# ====================================
#  Copyright (c) Microsoft Corporation. All rights reserved.
# ====================================

import configparser
import os
import platform
import shutil
import subprocess
import sys
import threading
import time
import traceback
import workerpollingfrequency

# import worker module after linuxutil.daemonize() call

sandboxes_root_folder_name = "sandboxes"


def safe_loop(func):
    def decorated_func(*args, **kwargs):
        while True:
            try:
                # ensure required file / cert exists
                func(*args, **kwargs)
            except (JrdsAuthorizationException):
                tracer.log_worker_safe_loop_terminal_exception(traceback.format_exc())
            except (InvalidFilePermissionException,
                    FileNotFoundException,
                    SystemExit):
                tracer.log_worker_safe_loop_terminal_exception(traceback.format_exc())
                time.sleep(1)  # allow the trace to make it to stdout (since traces are background threads)
                sys.exit(-1)
            except Exception:
                tracer.log_worker_safe_loop_non_terminal_exception(traceback.format_exc())
            time.sleep(workerpollingfrequency.get_jrds_get_sandbox_actions_polling_freq()) #polling frequency as per the value received from headers of GetSandboxActions

    return decorated_func


def background_thread(func):
    def decorated_func(*args, **kwargs):
        t = threading.Thread(target=func, args=args)
        t.daemon = True
        t.start()

    return decorated_func


def exit_on_error(message, exit_code=1):
    crash_log_filename = "automation_worker_crash.log"
    util.exit_on_error(filename=crash_log_filename, message=message, exit_code=exit_code)


def test_file_creation(path):
    try:
        iohelper.write_to_file(path, path)
        os.remove(path)
        return True
    except IOError:
        return False


def validate_and_setup_path():
    # default to user dir for exception logs to be writen to disk
    test_file_name = "test_file"

    # test certificate and key path
    if not os.path.isfile(configuration.get_jrds_cert_path()) or not os.path.isfile(configuration.get_jrds_key_path()):
        exit_on_error("Invalid certificate of key file path (absolute path is required).")

    # test working directory for existence and permissions
    working_directory_path = configuration.get_working_directory_path()
    if not os.path.exists(working_directory_path):
        exit_on_error("Invalid working directory path (absolute path is required).")

    file_creation = test_file_creation(os.path.join(working_directory_path, test_file_name))
    if file_creation is False:
        exit_on_error("Invalid working directory permission (read/write permissions are required).")

    # test state file path
    if configuration.get_state_directory_path() != configuration.DEFAULT_STATE_DIRECTORY_PATH:
        if not os.path.exists(configuration.get_state_directory_path()):
            exit_on_error("Invalid state directory path (absolute path is required).")

        file_creation = test_file_creation(os.path.join(configuration.get_state_directory_path(), test_file_name))
        if file_creation is False:
            exit_on_error("Invalid state directory permission (read/write permissions are required).")

    # OMS integration
    # set the working directory owner to be nxautomation:omiusers
    if os.name.lower() != "nt":
        import pwd
        try:
            nxautomation_uid = int(pwd.getpwnam('nxautomation').pw_uid)
            if os.getuid() == nxautomation_uid:
                retval = subprocess.call(["sudo", "chown", "-R", "nxautomation:omiusers", working_directory_path])
                if retval != 0:
                    exit_on_error("Could not change owner of working directory %s to nxautomation:omiusers"
                                  % (working_directory_path))
        except KeyError:
            # nxautomation user was not found on the system, skip this step
            tracer.log_debug_trace("Ownership change of working directory skipped. nxautomation user not found.")
            pass


def generate_state_file():
    # skip state file if the worker is managed by the worker manager
    if len(sys.argv) >= 3 and str(sys.argv[2]) == "managed":
        return

    state_file_name = "state.conf"
    if configuration.get_state_directory_path() == configuration.DEFAULT_STATE_DIRECTORY_PATH:
        state_file_path = os.path.join(configuration.get_working_directory_path(), state_file_name)
    else:
        state_file_path = os.path.join(configuration.get_state_directory_path(), state_file_name)

    tracer.log_debug_trace("State file path : " + str(state_file_path))

    if os.path.isfile(state_file_path):
        os.remove(state_file_path)

    section = "state"
    conf_file = open(state_file_path, 'w')
    config = configparser.ConfigParser()
    config.add_section(section)
    config.set(section, configuration.STATE_PID, str(os.getpid()))
    config.set(section, configuration.WORKER_VERSION, str(configuration.get_worker_version()))

    # for OMS scenarios, optional for DIY
    if len(sys.argv) >= 3:
        config.set(section, configuration.STATE_WORKSPACE_ID, str(sys.argv[2]))

    if len(sys.argv) >= 4:
        config.set(section, configuration.STATE_RESOURCE_VERSION, str(sys.argv[3]))

    config.write(conf_file)
    conf_file.close()

    # OMS integration
    # set the ownership of the state file to nxautomation:omiusers
    # set the permission of the state file to 660
    if os.name.lower() != "nt":
        import pwd
        try:
            nxautomation_uid = int(pwd.getpwnam('nxautomation').pw_uid)
            if os.getuid() == nxautomation_uid:
                retval = subprocess.call(["sudo", "chown", "nxautomation:omiusers", state_file_path])
                if retval != 0:
                    exit_on_error(
                        "Could not change owner of state file %s to nxautomation:omiusers" % (state_file_path))

                retval = subprocess.call(["sudo", "chmod", "660", state_file_path])
                if retval != 0:
                    exit_on_error("Could not change permission of state file %s " % (state_file_path))
        except KeyError:
            # nxautomation user was not found on the system, skip this step
            tracer.log_debug_trace("State file permission change skipped. nxautomation user not found.")
            pass


class Worker(object):
    def __init__(self):
        tracer.log_worker_starting(configuration.get_worker_version())
        http_client_factory = HttpClientFactory(configuration.get_jrds_cert_path(), configuration.get_jrds_key_path(),
                                                configuration.get_verify_certificates())
        http_client = http_client_factory.create_http_client(sys.version_info)
        self.jrds_client = JRDSClient(http_client)
        self.running_sandboxes = {}

    @staticmethod
    def assert_environment_prerequisite():
        jrds_cert_path = configuration.get_jrds_cert_path()
        if util.assert_file_read_permission(jrds_cert_path) is False:
            raise InvalidFilePermissionException(jrds_cert_path)

        jrds_key_path = configuration.get_jrds_key_path()
        if util.assert_file_read_permission(jrds_key_path) is False:
            raise InvalidFilePermissionException(jrds_key_path)

        worker_conf_path = configuration.get_worker_configuration_file_path()
        if util.assert_file_read_permission(worker_conf_path) is False:
            raise InvalidFilePermissionException(worker_conf_path)

        proxy_conf_path = configuration.get_proxy_configuration_path()
        if proxy_conf_path != configuration.DEFAULT_PROXY_CONFIGURATION_PATH and os.path.isfile(proxy_conf_path):
            if util.assert_file_read_permission(proxy_conf_path) is False:
                raise InvalidFilePermissionException(proxy_conf_path)
    

    @staticmethod
    def construct_jrds_msi_endpoint(sandbox_id):
        url = configuration.get_jrds_base_uri() + "/automationAccounts/" + configuration.get_account_id() + \
              "/sandboxes/" + sandbox_id + "/metadata/identity/oauth2/token"
        return url

    @safe_loop
    def routine(self):
        self.assert_environment_prerequisite()
        self.stop_tracking_terminated_sandbox()

        sandbox_actions = self.jrds_client.get_sandbox_actions()
        if sandbox_actions is None:
            tracer.log_get_sandbox_action_returned_null_data()
            return

        tracer.log_debug_trace("Get sandbox action. Found " + str(len(sandbox_actions)) + " action(s).")

        for action in sandbox_actions:
            tracer.log_worker_sandbox_action_found(len(sandbox_actions))
            sandbox_id = str(action["SandboxId"])

            # prevent duplicate sandbox from running
            if sandbox_id in self.running_sandboxes:
                continue

            # create sandboxes folder if needed
            sandbox_working_dir = os.path.join(configuration.get_working_directory_path(), sandboxes_root_folder_name,
                                               sandbox_id)

            try:
                iohelper.assert_or_create_path(sandbox_working_dir)
            except OSError as exception:
                tracer.log_worker_failed_to_create_sandbox_root_folder(sandbox_id, exception)
                raise SystemExit("Sandbox folder creation failed.")

            # copy current process env variable (contains configuration) and add the sanbox_id key
            process_env_variables = os.environ.copy()
            process_env_variables["sandbox_id"] = sandbox_id

            msi_secret = str(action["MSISecret"])

            if (msi_secret and msi_secret != "None"):
                process_env_variables["MSI_SECRET"] = msi_secret
                process_env_variables["MSI_ENDPOINT"] = self.construct_jrds_msi_endpoint(sandbox_id)
                process_env_variables["IDENTITY_HEADER"] = msi_secret
                process_env_variables["IDENTITY_ENDPOINT"] = self.construct_jrds_msi_endpoint(sandbox_id)


            python_to_be_used = util.get_python_to_be_used()

            print("Using %s" %(python_to_be_used))
            cmd = [python_to_be_used, os.path.join(configuration.get_source_directory_path(), "sandbox.py"),
                   configuration.get_worker_configuration_file_path()]
            tracer.log_worker_starting_sandbox(sandbox_id)
            sandbox_process = subprocessfactory.create_subprocess(cmd=cmd,
                                                                  env=process_env_variables,
                                                                  stdout=subprocess.PIPE,
                                                                  stderr=subprocess.PIPE,
                                                                  cwd=sandbox_working_dir)
            self.running_sandboxes[sandbox_id] = sandbox_process
            tracer.log_worker_started_tracking_sandbox(sandbox_id)

            self.monitor_sandbox_process_outputs(sandbox_id, sandbox_process)
            tracer.log_worker_sandbox_process_started(sandbox_id, str(sandbox_process.pid))

    @background_thread
    def monitor_sandbox_process_outputs(self, sandbox_id, process):
        while process.poll() is None:
            output = process.stdout.readline().decode().replace("\n", "")
            if output == '':
                continue
            if output != '':
                tracer.log_sandbox_stdout(output)

        if process.poll() != 0:
            full_error_output = ""
            while True:
                error_output = process.stderr.readline().decode()
                if error_output is None or error_output == '':
                    break
                full_error_output += error_output
            tracer.log_worker_sandbox_process_crashed(sandbox_id, process.pid, process.poll(), full_error_output)

        tracer.log_worker_sandbox_process_exited(sandbox_id, str(process.pid), process.poll())

        # cleanup sandbox directory
        sandbox_working_dir = os.path.join(configuration.get_working_directory_path(), sandboxes_root_folder_name,
                                           sandbox_id)
        shutil.rmtree(sandbox_working_dir, ignore_errors=True)

    @background_thread
    def telemetry_routine(self):
        while True:
            tracer.log_worker_general_telemetry(configuration.get_worker_version(), configuration.get_worker_type(),
                                                linuxutil.get_current_username(), linuxutil.get_oms_agent_id())
            tracer.log_worker_python_telemetry(platform.python_version(), platform.python_build(),
                                               platform.python_compiler())
            tracer.log_worker_system_telemetry(platform.system(), platform.node(), platform.version(),
                                               platform.machine(), platform.processor())

            try:
                distributor_id, description, release, codename = linuxutil.get_lsb_release()
                tracer.log_worker_lsb_release_telemetry(distributor_id, description, release, codename)
            except:
                pass

            # sleep for 6 hours, this allows us to gather daily telemetry
            time.sleep(60 * 60 * 6)

    def stop_tracking_terminated_sandbox(self):
        terminated_sandbox_ids = []

        # detect terminated sandboxes
        for sandbox_id, sandbox_process in list(self.running_sandboxes.items()):
            if sandbox_process.poll() is not None:
                terminated_sandbox_ids.append(sandbox_id)

        # clean-up terminated sandboxes
        for sandbox_id in terminated_sandbox_ids:
            removal = self.running_sandboxes.pop(sandbox_id, None)
            if removal is not None:
                tracer.log_worker_stopped_tracking_sandbox(sandbox_id)


def main():
    if len(sys.argv) < 2:
        exit_on_error("Missing configuration file path.")
    configuration_path = str(sys.argv[1])

    if not os.path.isfile(configuration_path):
        exit_on_error("Invalid configuration file path or empty configuration file (absolute path is required).")

    # configuration has to be read first thing
    try:
        # remove the test_mode env_var value (mainly for Windows)
        # this value is set in test
        del os.environ["test_mode"]
    except KeyError:
        pass

    configuration.read_and_set_configuration(configuration_path)
    configuration.set_config({configuration.COMPONENT: "worker"})
    validate_and_setup_path()
    # do not trace anything before this point

    generate_state_file()
    worker = Worker()
    worker.telemetry_routine()
    worker.routine()


if __name__ == "__main__":
    try:
        import configuration3 as configuration
        import iohelper
        import subprocessfactory
        import tracer
        from httpclientfactory import HttpClientFactory
        from jrdsclient import JRDSClient
        from workerexception import *    
        import util
        main()
    except:
        exit_on_error(traceback.format_exc())
