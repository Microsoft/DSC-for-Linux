#!/usr/bin/env python3
# ====================================
#  Copyright (c) Microsoft Corporation. All rights reserved.
# ====================================

"""Tracer module."""
import inspect

import os
import sys
import threading

import traceback

import configuration3 as configuration
import locallogger
import util
from httpclientfactory import HttpClientFactory
from jrdsclient import JRDSClient

jrds_client = None
jrds_cert_path = None
jrds_key_path = None
jrds_base_uri = None
subscription_id = None
account_id = None
machine_id = None
hybrid_worker_group_name = None
worker_version = None
sandbox_id = None




def safe_trace(func):
    def decorated_func(*args, **kwargs):
        try:
            func(*args, **kwargs)
        except Exception:
            if locallogger.default_logger is None:
                print(traceback.format_exc())
                print("Logger not defined.")
            else:
                locallogger.default_logger.critical(traceback.format_exc())

    return decorated_func


def background_thread(func):
    def decorated_func(*args, **kwargs):
        t = threading.Thread(target=func, args=args, kwargs=kwargs)
        t.daemon = True
        t.start()

    return decorated_func


def init():
    """Initializes all required variable for the tracer."""
    global jrds_client, jrds_cert_path, jrds_key_path, jrds_base_uri, subscription_id, \
        account_id, machine_id, hybrid_worker_group_name, worker_version, sandbox_id

    # Create the http client
    http_client_factory = HttpClientFactory(configuration.get_jrds_cert_path(), configuration.get_jrds_key_path(),
                                            configuration.get_verify_certificates())
    http_client = http_client_factory.create_http_client(sys.version_info)
    jrds_client = JRDSClient(http_client)

    # Populate global configuration values
    jrds_cert_path = configuration.get_jrds_cert_path()
    jrds_key_path = configuration.get_jrds_key_path()
    jrds_base_uri = configuration.get_jrds_base_uri()
    subscription_id = "00000000-0000-0000-0000-000000000000"  # temporary place holder
    account_id = configuration.get_account_id()
    machine_id = configuration.get_machine_id()
    hybrid_worker_group_name = configuration.get_hybrid_worker_name()
    worker_version = configuration.get_worker_version()

    sandbox_id = None
    try:
        sandbox_id = os.environ["sandbox_id"]
    except KeyError:
        pass

    # initialize the loggers for for all components except runbook
    if configuration.get_component() != "runbook":
        locallogger.init_logger()


@background_thread
@safe_trace
def trace_generic_hybrid_worker_event_async(event_id, task_name, message, t_id, keyword, activity_id=None, debug=False):
    """Invokes trace_generic_hybrid_worker_event in an asynchronously.

    Note:
        This method run in a background thread.

    Args:
        event_id    : int   , the event id. This event id doesn't have to map to an ETW event id in the cloud.
        task_name   : string, the task name.
        message     : string, the message.
        t_id        : int   , the thread id.
        keyword     : string, the keyword.
        activity_id : string, the activity id.
        debug       : boolean, the debug flag
    """
    trace_generic_hybrid_worker_event(event_id, task_name, message, t_id, keyword, activity_id=activity_id, debug=debug)


def trace_generic_hybrid_worker_event(event_id, task_name, message, t_id, keyword, activity_id=None, debug=False):
    """Write the trace to STDOUT / log file (on disk) / to MDS (in the cloud).

    Args:
        event_id    : int   , the event id. This event id doesn't have to map to an ETW event id in the cloud.
        task_name   : string, the task name.
        message     : string, the message.
        t_id        : int   , the thread id.
        keyword     : string, the keyword.
        activity_id : string, the activity id.
        debug       : boolean, the debug flag
    """
    # # # # # # # # # # # # # # # # # # # #
    # for tests, disregard tracer
    if os.getenv('test_mode', None) is not None:
        return
    # # # # # # # # # # # # # # # # # # # #

    if jrds_client is None or locallogger.default_logger is None:
        init()

    if debug and not configuration.get_debug_traces():
        return

    if task_name.startswith("log"):
        task_name = "_".join(task_name.split("_")[1:])
    task_name = task_name.title().replace("_", "")

    if isinstance(message, dict):
        message = dict_to_str(message)

    if activity_id is None:
        activity_id = u_activity_id

    # local trace
    # we prioritize cloud traces, do not raise exception if local tracing raises an exception
    locallogger.log_info(message)

    # MDS
    if not debug:
        format_and_issue_generic_hybrid_worker_trace(event_id, task_name, message, t_id, keyword, activity_id)


@background_thread
@safe_trace
def trace_etw_event(event_id, activity_id, log_type, arg_array):
    """Write the trace to STDOUT / log file (on disk) / to MDS (in the cloud).

    Note:
        This method run in a background thread.

    Args:
        event_id    : int   , the event id. This event id doesn't have to map to an ETW event id in the cloud.
        activity_id : string, the activiti id.
        log_type    : int   , the log type.
        arg_array   : string, the argument array.
    """
    # # # # # # # # # # # # # # # # # # # #
    # for tests, disregard tracer
    if os.getenv('test_mode', None) is not None:
        return
    # # # # # # # # # # # # # # # # # # # #

    if jrds_client is None or locallogger.default_logger is None:
        init()

    # MDS
    issue_jrds_trace(event_id, activity_id, log_type, arg_array)


def format_and_issue_generic_hybrid_worker_trace(event_id, task_name, message, t_id, keyword, activity_id):
    """Send the trace to MDS (in the cloud) using the JRDS api.

    Args:
        event_id    : int   , the event id. This event id doesn't have to map to an ETW event id in the cloud.
        task_name   : string, the task name.
        message     : string, the message.
        t_id        : int   , the thread id.
        keyword     : string, the keyword.
    """
    # The local trace is converted into the "GenericHybridWorker" trace in the cloud (eventId: 16000).
    # For this reason, this trace has to follow the JRDS TraceEventSource.cs event's signature contract
    # for eventId 16000.
    cloud_trace_format = [account_id,
                          subscription_id,
                          hybrid_worker_group_name,
                          machine_id,
                          configuration.get_component(),
                          event_id,
                          task_name,
                          keyword,
                          t_id,
                          os.getpid(),
                          activity_id,
                          worker_version,
                          message]
    issue_jrds_trace(16000, activity_id, 0, cloud_trace_format)


def issue_jrds_trace(event_id, activity_id, log_type, arg_array):
    jrds_client.set_log(event_id, activity_id, log_type, list(map(str, arg_array)))


def dict_to_str(input_dict):
    output_str = ""
    for key, value in input_dict.items():
        output_str = output_str + "[" + str(key) + "=" + str(value) + "]"
    return output_str


u_activity_id = util.generate_uuid()

COMPONENT_SANDBOX = "Sandbox"
COMPONENT_WORKER = "Worker"

KEYWORD_DEBUG = "Debug"
KEYWORD_INFO = "Info"
KEYWORD_ERROR = "Error"
KEYWORD_WARNING = "Warning"
KEYWORD_STARTUP = "Startup"
KEYWORD_ROUTINE = "Routine"
KEYWORD_JOB = "Job"
KEYWORD_RUNTIME = "Runtime"
KEYWORD_RUNBOOK = "Runbook"
KEYWORD_CONFIGURATION = "Configuration"
KEYWORD_TELEMETRY = "Telemetry"


# general traces
# traces in this section may be used in all worker components (worker, sandbox, etc)
def log_debug_trace(message):
    message = "[DEBUG] " + message
    trace_generic_hybrid_worker_event_async(1, inspect.stack()[0][3], message, 1, KEYWORD_DEBUG, debug=True)


def log_informational_trace(message):
    trace_generic_hybrid_worker_event_async(2, inspect.stack()[0][3], message, 1, KEYWORD_INFO)


def log_exception_trace(stacktrace):
    message = "Exception encountered. \n" + stacktrace
    trace_generic_hybrid_worker_event_async(3, inspect.stack()[0][3], message, 1, KEYWORD_ERROR)

def log_warning_trace(message):
    message = "[WARNING] " + message
    trace_generic_hybrid_worker_event_async(4, inspect.stack()[0][3], message, 1, KEYWORD_WARNING)


# worker specific traces
# traces in this section are mainly for the worker component
def log_sandbox_stdout(trace_content):
    locallogger.sandbox_stdout.info(trace_content)


def log_worker_starting(version):
    message = "Worker starting. [workerVersion=" + str(version) + "]"
    trace_generic_hybrid_worker_event_async(5000, inspect.stack()[0][3], message, 1, KEYWORD_STARTUP)


def log_worker_general_telemetry(worker_version, worker_type, username, oms_agent_id):
    message = "Worker general telemetry. [workerVersion=" + str(worker_version) + "][workerType=" + \
              str(worker_type) + "][username=" + str(username) + "][oms_agent_id=" + str(oms_agent_id) + "]"
    trace_generic_hybrid_worker_event_async(5001, inspect.stack()[0][3], message, 1, KEYWORD_TELEMETRY)


def log_worker_python_telemetry(version, build, compiler):
    message = "Worker python telemetry. [version=" + str(version) + "][build=" + str(' '.join(map(str, build))) + \
              "][compiler=" + str(compiler) + "]"
    trace_generic_hybrid_worker_event_async(5002, inspect.stack()[0][3], message, 1, KEYWORD_TELEMETRY)


def log_worker_system_telemetry(system, node, version, machine, processor):
    message = "Worker system telemetry. [system=" + str(system) + "][node=" + str(node) + \
              "][version=" + str(version) + "][machine=" + str(machine) + "][processor=" + str(processor) + "]"
    trace_generic_hybrid_worker_event_async(5003, inspect.stack()[0][3], message, 1, KEYWORD_TELEMETRY)


def log_worker_lsb_release_telemetry(distributor_id, description, release, codename):
    message = "Worker lsb_release telemetry. [distributor_id=" + str(distributor_id) + "][description=" + \
              str(description) + "][release=" + str(release) + "][codename=" + str(codename) + "]"
    trace_generic_hybrid_worker_event_async(5005, inspect.stack()[0][3], message, 1, KEYWORD_TELEMETRY)


def log_worker_sandbox_action_found(sandbox_actions):
    message = "Get sandbox actions found " + str(sandbox_actions) + " new action(s)."
    trace_generic_hybrid_worker_event_async(5100, inspect.stack()[0][3], message, 1, KEYWORD_ROUTINE)


def log_worker_starting_sandbox(sandbox_id):
    message = "Starting sandbox process. [sandboxId=" + str(sandbox_id) + "]"
    trace_generic_hybrid_worker_event_async(5101, inspect.stack()[0][3], message, 1, KEYWORD_ROUTINE)


def log_worker_sandbox_process_started(sandbox_id, pid):
    message = "Sandbox process started. [sandboxId=" + str(sandbox_id) + "][pId=" + str(pid) + "]"
    trace_generic_hybrid_worker_event_async(5102, inspect.stack()[0][3], message, 1, KEYWORD_ROUTINE)


def log_worker_sandbox_process_exited(sandbox_id, pid, exit_code):
    message = "Sandbox process exited. [sandboxId=" + str(sandbox_id) + "][pId=" + str(pid) + "][exitCode=" + \
              str(exit_code) + "]"
    trace_generic_hybrid_worker_event_async(5103, inspect.stack()[0][3], message, 1, KEYWORD_INFO)


def log_worker_sandbox_process_crashed(sandbox_id, pid, exit_code, exception):
    message = "Sandbox process crashed. [sandboxId=" + str(sandbox_id) + "][pId=" + str(pid) + "][exitCode=" + \
              str(exit_code) + "][exception=" + str(exception) + "]"
    trace_generic_hybrid_worker_event_async(5104, inspect.stack()[0][3], message, 1, KEYWORD_ERROR)


def log_worker_failed_to_create_sandbox_root_folder(sandbox_id, exception):
    message = "Failed to create sandbox root folder. [sandboxId=" + str(sandbox_id) + "][exception=" + \
              str(exception) + "]"
    trace_generic_hybrid_worker_event_async(5105, inspect.stack()[0][3], message, 1, KEYWORD_WARNING)


def log_worker_started_tracking_sandbox(sandbox_id):
    message = "Worker started tracking sandbox. [sandboxId=" + str(sandbox_id) + "]"
    trace_generic_hybrid_worker_event_async(5106, inspect.stack()[0][3], message, 1, KEYWORD_INFO)


def log_worker_stopped_tracking_sandbox(sandbox_id):
    message = "Worker stopped tracking sandbox. [sandboxId=" + str(sandbox_id) + "]"
    trace_generic_hybrid_worker_event_async(5107, inspect.stack()[0][3], message, 1, KEYWORD_INFO)


def log_worker_safe_loop_non_terminal_exception(exception):
    message = "Worker safe loop non terminal exception. [exception=" + str(exception) + "]"
    trace_generic_hybrid_worker_event_async(5108, inspect.stack()[0][3], message, 1, KEYWORD_WARNING)


def log_worker_safe_loop_terminal_exception(exception):
    message = "Worker safe loop terminal exception. [exception=" + str(exception) + "]"
    trace_generic_hybrid_worker_event_async(5109, inspect.stack()[0][3], message, 1, KEYWORD_ERROR)


def log_worker_certificate_rotation_successful():
    message = "Hybrid worker Certificate rotation completed."
    trace_generic_hybrid_worker_event_async(5110, inspect.stack()[0][3], message, 1, KEYWORD_INFO)


def log_worker_certificate_rotation_failed(exception):
    message = "Hybrid worker Certificate rotation failed. [exception=" + \
               str(exception) + "]"
    trace_generic_hybrid_worker_event_async(5111, inspect.stack()[0][3], message, 1, KEYWORD_ERROR)


# sandbox specific traces
# traces in this section are mainly for the sandbox component
#
# ** these trace rely on the sandbox_id env variable to be set **
def log_sandbox_starting(sandbox_id, pid, worker_type):
    message = "Sandbox process starting. [sandboxId=" + str(sandbox_id) + "][pId=" + str(pid) + \
              "][workerType=" + str(worker_type) + "]"
    trace_generic_hybrid_worker_event_async(10000, inspect.stack()[0][3], message, 1, KEYWORD_INFO)


def log_sandbox_get_job_actions(job_actions):
    message = "Get job actions. Found " + str(job_actions) + " new action(s)."
    trace_generic_hybrid_worker_event_async(10001, inspect.stack()[0][3], message, 1, KEYWORD_ROUTINE)


def log_sandbox_stopped_tracking_job(job_id):
    message = "Sandbox stopped tracking job. [sandboxId=" + str(sandbox_id) + "][jobId=" + str(job_id) + "]"
    trace_generic_hybrid_worker_event_async(10002, inspect.stack()[0][3], message, 1, KEYWORD_INFO)


def log_sandbox_started_tracking_job(job_id):
    message = "Sandbox started tracking job. [sandboxId=" + str(sandbox_id) + "][jobId=" + str(job_id) + "]"
    trace_generic_hybrid_worker_event_async(10003, inspect.stack()[0][3], message, 1, KEYWORD_INFO)


def log_sandbox_jrds_closure_request():
    message = "Sandbox closure request received from JRDS. [sandboxId=" + str(sandbox_id) + "]"
    trace_generic_hybrid_worker_event_async(10004, inspect.stack()[0][3], message, 1, KEYWORD_INFO)


def log_sandbox_pending_action_activate_detected(job_id, job_status, pending_action):
    message = "Sandbox pending action activate detected. [sandboxId=" + str(sandbox_id) + "][jobId=" + str(job_id) + \
              "][jobStatus=" + str(job_status) + "][pendingAction=" + str(pending_action) + "]"
    trace_generic_hybrid_worker_event_async(10005, inspect.stack()[0][3], message, 1, KEYWORD_INFO)


def log_sandbox_pending_action_stop_detected(job_id, job_status, pending_action):
    message = "Sandbox pending action stop detected. [sandboxId=" + str(sandbox_id) + "][jobId=" + str(job_id) + \
              "][jobStatus=" + str(job_status) + "][pendingAction=" + str(pending_action) + "]"
    trace_generic_hybrid_worker_event_async(10006, inspect.stack()[0][3], message, 1, KEYWORD_INFO)


def log_sandbox_no_pending_action_detected(job_id, job_status):
    message = "Sandbox no pending action detected. [sandboxId=" + str(sandbox_id) + "][jobId=" + str(job_id) + \
              "][jobStatus=" + str(job_status) + "]"
    trace_generic_hybrid_worker_event_async(10007, inspect.stack()[0][3], message, 1, KEYWORD_INFO)


def log_sandbox_unsupported_pending_action_detected(job_id, job_status, pending_action):
    message = "Sandbox unsupported pending action detected. [sandboxId=" + str(sandbox_id) + "][jobId=" + \
              str(job_id) + "][jobStatus=" + str(job_status) + "][pendingAction=" + str(pending_action) + "]"
    trace_generic_hybrid_worker_event_async(10008, inspect.stack()[0][3], message, 1, KEYWORD_ERROR)


def log_sandbox_job_created(job_id):
    message = "Job created. [sandboxId=" + str(sandbox_id) + "][jobId=" + str(job_id) + "]"
    trace_generic_hybrid_worker_event_async(10009, inspect.stack()[0][3], message, 1, KEYWORD_INFO)


def log_sandbox_job_loaded(job_id):
    message = "Job loaded. [sandboxId=" + str(sandbox_id) + "][jobId=" + str(job_id) + "]"
    trace_generic_hybrid_worker_event_async(10010, inspect.stack()[0][3], message, 1, KEYWORD_INFO)


def log_sandbox_job_started(job_id, runbook_definition_kind, runbook_name, runbook_version_id):
    message = "Job started. [sandboxId=" + str(sandbox_id) + "][jobId=" + str(job_id) + "][runbookDefinitionKind=" + \
              str(runbook_definition_kind) + "][runbookName=" + str(runbook_name) + "][runbookVersionId=" + \
              str(runbook_version_id) + "]"
    trace_generic_hybrid_worker_event_async(10011, inspect.stack()[0][3], message, 1, KEYWORD_INFO)


def log_sandbox_job_pending_action_detected(job_id, pending_action):
    message = "Job pending action detected. [sandboxId=" + str(sandbox_id) + "][jobId=" + str(job_id) + \
              "][pendingAction=" + str(pending_action) + "]"
    trace_generic_hybrid_worker_event_async(10012, inspect.stack()[0][3], message, 1, KEYWORD_INFO)


def log_sandbox_job_unloaded(job_id):
    message = "Job unloaded. [sandboxId=" + str(sandbox_id) + "][jobId=" + str(job_id) + "]"
    trace_generic_hybrid_worker_event_async(10013, inspect.stack()[0][3], message, 1, KEYWORD_INFO)


def log_sandbox_job_unsupported_runbook_type(job_id, exception):
    message = "Unsupported runbook type. [sandboxId=" + str(sandbox_id) + "][jobId=" + str(job_id) + \
              "][exception=" + str(exception) + "]"
    trace_generic_hybrid_worker_event_async(10014, inspect.stack()[0][3], message, 1, KEYWORD_ERROR)


def log_sandbox_job_unhandled_exception(job_id, stacktrace):
    message = "Job unhandled exception. [sandboxId=" + str(sandbox_id) + "][jobId=" + str(job_id) + \
              "][stacktrace=" + str(stacktrace) + "]"
    trace_generic_hybrid_worker_event_async(10016, inspect.stack()[0][3], message, 1, KEYWORD_ERROR)


def log_sandbox_job_streamhandler_unhandled_exception(job_id, exception):
    message = "Job stream handler unhandled exception. [sandboxId=" + str(sandbox_id) + "][jobId=" + str(job_id) + \
              "][stacktrace=" + str(exception) + "]"
    trace_generic_hybrid_worker_event_async(10017, inspect.stack()[0][3], message, 1, KEYWORD_ERROR)


def log_sandbox_job_streamhandler_processing_complete(job_id):
    message = "Job stream handler processing complete. [sandboxId=" + str(sandbox_id) + "][jobId=" + str(job_id) + "]"
    trace_generic_hybrid_worker_event_async(10018, inspect.stack()[0][3], message, 1, KEYWORD_INFO)


def log_sandbox_job_runbook_signature_validation_failed(keyring_path, exception):
    message = "Runbook signature validation failed. [sandboxId=" + str(sandbox_id) + "][gpgException=" + \
              str(exception) + "]" + "[keyring_path=" + str(keyring_path) + "]"
    trace_generic_hybrid_worker_event_async(10019, inspect.stack()[0][3], message, 1, KEYWORD_WARNING)


def log_sandbox_job_runbook_signature_validation_succeeded(keyring_path):
    message = "Runbook signature validation succeeded. [sandboxId=" + str(sandbox_id) + "]" + \
              "[keyring_path=" + str(keyring_path) + "]"
    trace_generic_hybrid_worker_event_async(10020, inspect.stack()[0][3], message, 1, KEYWORD_WARNING)


def log_sandbox_job_runbook_signature_invalid():
    message = "Runbook signature validation is invalid. [sandboxId=" + str(sandbox_id) + "]"
    trace_generic_hybrid_worker_event_async(10021, inspect.stack()[0][3], message, 1, KEYWORD_WARNING)


def log_sandbox_configuration(sandbox_id, enforce_runbook_signature_validation, gpg_public_keyring_paths,
                              working_directory):
    message = "Sandbox configuration. [sandboxId=" + str(sandbox_id) + "][enforceRunbookSignatureValidation=" + \
              str(enforce_runbook_signature_validation) + "][gpgPublicKeyringPaths=" + str(gpg_public_keyring_paths) + \
              "][workingDirectory=" + str(working_directory) + "]"
    trace_generic_hybrid_worker_event_async(10022, inspect.stack()[0][3], message, 1, KEYWORD_INFO)


def log_sandbox_safe_loop_terminal_exception(exception):
    message = "Sandbox safe loop terminal exception. [sandboxId=" + str(sandbox_id) + "][exception=" + \
              str(exception) + "]"
    trace_generic_hybrid_worker_event_async(10023, inspect.stack()[0][3], message, 1, KEYWORD_ERROR)


def log_runbook_runtime_trace(trace, activity_id):
    message = "Runbook runtime trace: " + str(trace)
    trace_generic_hybrid_worker_event(89000, inspect.stack()[0][3], message, 1, KEYWORD_RUNBOOK,
                                      activity_id=activity_id)

def log_get_sandbox_action_returned_null_data():
    message = "Get sandbox action returned null data."
    trace_generic_hybrid_worker_event_async(10024, inspect.stack()[0][3], message, 1, KEYWORD_WARNING)


# GenericHybridWorker event for ETW traces
def log_job_status_changed_completed(message):
    trace_generic_hybrid_worker_event_async(90000, inspect.stack()[0][3], message, 1, KEYWORD_JOB)


def log_job_status_changed_failed(message):
    trace_generic_hybrid_worker_event_async(90001, inspect.stack()[0][3], message, 1, KEYWORD_JOB)


def log_job_status_changed_running(message):
    trace_generic_hybrid_worker_event_async(90002, inspect.stack()[0][3], message, 1, KEYWORD_JOB)


def log_job_status_changed_stopped(message):
    trace_generic_hybrid_worker_event_async(90003, inspect.stack()[0][3], message, 1, KEYWORD_JOB)


def log_job_status_changed_suspended_by_user(message):
    trace_generic_hybrid_worker_event_async(90004, inspect.stack()[0][3], message, 1, KEYWORD_JOB)


def log_job_status_changed_suspended_by_exception(message):
    trace_generic_hybrid_worker_event_async(90005, inspect.stack()[0][3], message, 1, KEYWORD_JOB)


def log_user_requested_start_or_resume(message):
    trace_generic_hybrid_worker_event_async(90006, inspect.stack()[0][3], message, 1, KEYWORD_JOB)


def log_job_duration(message):
    trace_generic_hybrid_worker_event_async(90007, inspect.stack()[0][3], message, 1, KEYWORD_JOB)


# etw specific traces
# traces in the following block
def log_etw_job_status_changed_completed(subscription_id, account_id, account_name, sandbox_id, job_id,
                                         runbook_definition_kind, runbook_name):
    cloud_trace_format = [subscription_id,
                          account_id,
                          account_name,
                          sandbox_id,
                          job_id,
                          runbook_definition_kind,
                          runbook_name]
    local_message = "Job state changed to completed. [subscriptionId=" + str(subscription_id) + \
                    "][accountId=" + str(account_id) + "][accountName=" + str(account_name) + "][sandboxId=" + \
                    str(sandbox_id) + "][jobId=" + str(job_id) + "][runbookDefinitionKind=" + \
                    str(runbook_definition_kind) + "][runbookName=" + str(runbook_name) + "]"
    trace_etw_event(3181, u_activity_id, 0, cloud_trace_format)
    log_job_status_changed_completed(local_message)


def log_etw_job_status_changed_failed(subscription_id, account_id, account_name, sandbox_id, job_id,
                                      runbook_definition_kind, runbook_name, runbook_version_id, exception):
    cloud_trace_format = [subscription_id,
                          account_id,
                          account_name,
                          sandbox_id,
                          job_id,
                          runbook_definition_kind,
                          runbook_name,
                          runbook_version_id,
                          exception]
    local_message = "Job state changed to failed. [subscriptionId=" + str(subscription_id) + "][accountId=" + \
                    str(account_id) + "][accountName=" + str(account_name) + "][sandboxId=" + str(sandbox_id) + \
                    "][jobId=" + str(job_id) + "][runbookDefinitionKind=" + str(runbook_definition_kind) + \
                    "][runbookName=" + str(runbook_name) + "][runbookVersionId=" + str(runbook_version_id) + \
                    "][exception=" + str(exception) + "]"
    trace_etw_event(3182, u_activity_id, 0, cloud_trace_format)
    log_job_status_changed_failed(local_message)


def log_etw_job_status_changed_running(subscription_id, account_id, account_name, sandbox_id, job_id,
                                       runbook_definition_kind, runbook_name, time_taken_to_start_running_in_seconds):
    cloud_trace_format = [subscription_id,
                          account_id,
                          account_name,
                          sandbox_id,
                          job_id,
                          runbook_definition_kind,
                          runbook_name,
                          time_taken_to_start_running_in_seconds]
    local_message = "Job state changed to running. [subscriptionId=" + str(subscription_id) + "][accountId=" + \
                    str(account_id) + "][accountName=" + str(account_name) + "][sandboxId=" + str(sandbox_id) + \
                    "][jobId=" + str(job_id) + "][runbookDefinitionKind=" + str(runbook_definition_kind) + \
                    "][runbookName=" + str(runbook_name) + "][timeTakenToStartRunningInSeconds=" + \
                    str(time_taken_to_start_running_in_seconds) + "]"
    trace_etw_event(3183, u_activity_id, 0, cloud_trace_format)
    log_job_status_changed_running(local_message)


def log_etw_job_status_changed_stopped(subscription_id, account_id, account_name, sandbox_id, job_id,
                                       runbook_definition_kind, runbook_name):
    cloud_trace_format = [subscription_id,
                          account_id,
                          account_name,
                          sandbox_id,
                          job_id,
                          runbook_definition_kind,
                          runbook_name]
    local_message = "Job state changed to stopped. [subscriptionId=" + str(subscription_id) + "][accountId=" + \
                    str(account_id) + "][accountName=" + str(account_name) + "][sandboxId=" + str(sandbox_id) + \
                    "][jobId=" + str(job_id) + "][runbookDefinitionKind=" + str(runbook_definition_kind) + \
                    "][runbookName=" + str(runbook_name) + "]"
    trace_etw_event(3184, u_activity_id, 0, cloud_trace_format)
    log_job_status_changed_stopped(local_message)


def log_etw_job_status_changed_suspended_by_user(account_id, sandbox_id, job_id, runbook_name, runbook_definition_kind,
                                                 account_name):
    cloud_trace_format = [account_id,
                          sandbox_id,
                          job_id,
                          runbook_name,
                          runbook_definition_kind,
                          account_name]
    local_message = "Job state changed to suspended. [accountId=" + str(account_id) + "][sandboxId=" + \
                    str(sandbox_id) + "][jobId=" + str(job_id) + "][runbookName=" + str(runbook_name) + \
                    "][runbookDefinitionKind=" + str(runbook_definition_kind) + "][accountName=" + str(account_name) + \
                    "]"
    trace_etw_event(3185, u_activity_id, 0, cloud_trace_format)
    log_job_status_changed_suspended_by_user(local_message)


def log_etw_job_status_changed_suspended_by_exception(account_id, sandbox_id, job_id, runbook_name, exception,
                                                      account_name):
    cloud_trace_format = [account_id,
                          sandbox_id,
                          job_id,
                          runbook_name,
                          exception,
                          account_name]
    local_message = "Job state changed to suspended after an exception was encountered. [accountId=" + \
                    str(account_id) + "][sandboxId=" + str(sandbox_id) + "][jobId=" + str(job_id) + "][runbookName=" + \
                    str(runbook_name) + "][exception=" + str(exception) + "]"
    trace_etw_event(3186, u_activity_id, 0, cloud_trace_format)
    log_job_status_changed_suspended_by_exception(local_message)


def log_etw_user_requested_start_or_resume(account_id, sandbox_id, job_id, runbook_name, account_name,
                                           time_taken_to_start_running_in_seconds, runbook_definition_kind):
    cloud_trace_format = [account_id,
                          sandbox_id,
                          job_id,
                          runbook_name,
                          account_name,
                          time_taken_to_start_running_in_seconds,
                          runbook_definition_kind]
    local_message = "The user requested job state has changed to running. [accountId=" + str(account_id) + \
                    "][sandboxId=" + str(sandbox_id) + "][jobId=" + str(job_id) + "][runbookName=" + \
                    str(runbook_name) + "][timeTakenToStartRunningInSeconds=" + \
                    str(time_taken_to_start_running_in_seconds) + "][runbookType=" + str(runbook_definition_kind) + "]"
    trace_etw_event(3732, u_activity_id, 0, cloud_trace_format)
    log_user_requested_start_or_resume(local_message)


def log_etw_job_duration(subscription_id, account_id, sandbox_id, job_id, duration, duration_in_seconds, tier_name,
                         account_name, job_trigger_source, runbook_source, runbook_type, runbook_name,
                         job_run_destination):
    cloud_trace_format = [subscription_id,
                          account_id,
                          sandbox_id,
                          job_id,
                          duration,
                          duration_in_seconds,
                          tier_name,
                          account_name,
                          job_trigger_source,
                          runbook_source,
                          runbook_type,
                          runbook_name,
                          job_run_destination]
    local_message = "Job execution time recorded. [subscriptionId=" + str(subscription_id) + "][accountId=" + \
                    str(account_id) + "][sandboxId=" + str(sandbox_id) + "][jobId=" + str(job_id) + \
                    "][jobTriggerSource=" + str(job_trigger_source) + "][runbookSource=" + str(runbook_source) + \
                    "][runbookType=" + str(runbook_type) + "][runbookName=" + str(runbook_name) + \
                    "][jobRunDestination=" + str(job_run_destination) + "]"
    trace_etw_event(3453, u_activity_id, 0, cloud_trace_format)
    log_job_duration(local_message)
