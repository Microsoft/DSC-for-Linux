#!/usr/bin/env python2
#
# Copyright (C) Microsoft Corporation, All rights reserved.

"""Job module. Contains a class representation of an "automation" job."""

import Queue
import sys
import time
import traceback
from datetime import datetime
from threading import Thread

import jrdsclient
import runbook
import runtime
import runtimefactory
import tracer
from automationconstants import pendingactions, jobstatus, jobtriggersource
from streamhandler import StreamHandler
from workerexception import *

EXIT_SUCCESS = 0


class Job(Thread):
    """Job class."""

    def __init__(self, sandbox_id, job_id, msg_queue, jrds_client, job_thread_exception_queue):
        """ Automation job.

        :param sandbox_id                   : The sandbox id owning the specified job
        :param job_id                       : The job id
        :param msg_queue                    : The job specific message queue
        :param jrds_client                  : The JRDS http client
        :param job_thread_exception_queue   : The job exception queue

        :type sandbox_id                : str
        :type job_id                    : str
        :type msg_queue                 : Queue.Queue
        :type jrds_client               : jrdsclient.JRDSClient
        :type job_thread_exception_queue: Queue.Queue
        """
        Thread.__init__(self)
        self.daemon = True

        self.sandbox_id = sandbox_id
        self.job_id = job_id
        self.msg_queue = msg_queue
        self.jrds_client = jrds_client
        self.job_thread_exception_queue = job_thread_exception_queue

        # values populated in load_job()
        self.runtime = None  # type: runtime.Runtime
        self.runbook = None  # type: runbook.Runbook
        self.job_data = None  # type: jrdsclient.JobData
        self.job_updatable_data = None  # type: jrdsclient.JobUpdatableData
        self.runbook_data = None  # type: jrdsclient.RunbookData
        tracer.log_sandbox_job_created(self.job_id)

    def get_job_extended_properties(self):
        return {
            'accountid': str(self.job_data.account_id),
            'accountname': str(self.job_data.account_name),
            'trackingid': str(self.job_data.tracking_id),
            'jobid': str(self.job_data.job_id),
            'resourcegroup': str(self.job_data.resource_group_name),
            'runbookname': str(self.job_data.runbook_name),
            'subscriptionid': str(self.job_data.subscription_id),
            'runon': str(self.job_data.run_on)
        }

    def load_job(self):
        """Load all required artifact for the job to be executed."""
        self.jrds_client.set_job_status(self.sandbox_id, self.job_id, jobstatus.ACTIVATING, False)
        self.job_data = self.jrds_client.get_job_data(self.job_id)
        self.job_updatable_data = self.jrds_client.get_job_updatable_data(self.job_id)
        self.runbook_data = self.jrds_client.get_runbook_data(self.job_data.runbook_version_id)
        tracer.log_sandbox_job_loaded(self.job_id)

    def initialize_runtime(self):
        """Initializes the runtime component for the job. The runtime component is language specific."""
        self.runbook, self.runtime = runtimefactory.create_runtime(self.job_data, self.runbook_data)

    def run(self):
        """Main job execution logic. This methods returns when the job execution is completed.

        Throws:
            WorkerUnsupportedRunbookType  : If the language isn't supported by by the worker.
            OSUnsupportedRunbookType      : If the language isn't supported by by the host.
            Exception                     : Any unhandled exception.
        """
        try:
            self.load_job()
            self.initialize_runtime()
            if (self.runbook.definition_kind_str == "PowerShell" or self.runbook.definition_kind_str == "PowerShell7"):
                self.__process_job_parameters()
            self.execute_runbook()
            self.unload_job()
        except (WorkerUnsupportedRunbookType, OSUnsupportedRunbookType), e:
            tracer.log_sandbox_job_unsupported_runbook_type(self.job_id, e.message)
            self.jrds_client.set_job_status(self.sandbox_id, self.job_id, jobstatus.FAILED, True, self.get_job_extended_properties(), exception=e.message)
            self.unload_job()
        except (InvalidRunbookSignature, GPGKeyringNotConfigured), e:
            self.jrds_client.set_job_status(self.sandbox_id, self.job_id, jobstatus.FAILED, True, self.get_job_extended_properties(), exception=e.message)
            self.unload_job()
        except Exception:
            tracer.log_sandbox_job_unhandled_exception(self.job_id, traceback.format_exc())
            self.job_thread_exception_queue.put(traceback.format_exc())

    def execute_runbook(self):
        """Executes the job runtime and performs runtime operation (stream upload / status change)."""
        # set status to running
        tracer.log_sandbox_job_started(self.job_id, self.runbook.definition_kind_str, self.runbook_data.name,
                                       self.runbook_data.runbook_version_id)
        start_request_time = time.strptime(self.job_data.start_request_time.split("+")[0].split(".")[0],
                                           "%Y-%m-%dT%H:%M:%S")
        time_taken_to_start_td = datetime.utcnow() - datetime.fromtimestamp(time.mktime(start_request_time))
        time_taken_to_start_in_seconds = (time_taken_to_start_td.microseconds + (time_taken_to_start_td.seconds +
                                                                                 time_taken_to_start_td.days * 24 * 3600) * 10 ** 6) / 10 ** 6
        tracer.log_etw_job_status_changed_running(self.job_data.subscription_id, self.job_data.account_id,
                                                  self.job_data.account_name, self.sandbox_id, self.job_data.job_id,
                                                  self.runbook.definition_kind_str, self.runbook_data.name,
                                                  time_taken_to_start_in_seconds)
        tracer.log_etw_user_requested_start_or_resume(self.job_data.account_id, self.sandbox_id, self.job_data.job_id,
                                                      self.runbook_data.name, self.job_data.account_name,
                                                      time_taken_to_start_in_seconds, self.runbook.definition_kind_str)
        self.jrds_client.set_job_status(self.sandbox_id, self.job_id, jobstatus.RUNNING, False, self.get_job_extended_properties())

        # create runbook subprocess
        self.runtime.start_runbook_subprocess()

        # monitor runbook output for streams
        stream_handler = StreamHandler(self.job_data, self.runtime.runbook_subprocess, self.jrds_client)
        stream_handler.daemon = True
        stream_handler.start()

        # wait for runbook execution to complete
        pending_action = None
        while stream_handler.isAlive() or self.runtime.runbook_subprocess.poll() is None:
            try:
                pending_action = self.msg_queue.get(block=False)
                tracer.log_sandbox_job_pending_action_detected(self.job_id, pending_action)
                if pending_action == pendingactions.STOP_ENUM_INDEX:
                    self.jrds_client.set_job_status(self.sandbox_id, self.job_id, jobstatus.STOPPING, False)
                    self.runtime.kill_runbook_subprocess()
                    break
            except Queue.Empty:
                pass
            time.sleep(0.2)

        # handle terminal state changes
        if pending_action == pendingactions.STOP_ENUM_INDEX:
            self.jrds_client.set_job_status(self.sandbox_id, self.job_id, jobstatus.STOPPED, True, self.get_job_extended_properties())
            tracer.log_etw_job_status_changed_stopped(self.job_data.subscription_id, self.job_data.account_id,
                                                      self.job_data.account_name, self.sandbox_id, self.job_data.job_id,
                                                      self.runbook.definition_kind_str, self.runbook_data.name)
        elif self.runtime.runbook_subprocess.poll() is not None and self.runtime.runbook_subprocess.poll() == EXIT_SUCCESS:
            self.jrds_client.set_job_status(self.sandbox_id, self.job_id, jobstatus.COMPLETED, True, self.get_job_extended_properties())
            tracer.log_etw_job_status_changed_completed(self.job_data.subscription_id, self.job_data.account_id,
                                                        self.job_data.account_name, self.sandbox_id,
                                                        self.job_data.job_id, self.runbook.definition_kind_str,
                                                        self.runbook_data.name)
        else:
            full_error_output = self.get_full_stderr_content(self.runtime.runbook_subprocess.stderr)
            self.jrds_client.set_job_status(self.sandbox_id, self.job_id, jobstatus.FAILED, True, self.get_job_extended_properties(), exception=full_error_output)
            tracer.log_etw_job_status_changed_failed(self.job_data.subscription_id, self.job_data.account_id,
                                                     self.job_data.account_name, self.sandbox_id, self.job_id,
                                                     self.runbook.definition_kind_str, self.runbook_data.name,
                                                     self.runbook_data.runbook_version_id, full_error_output)

    def unload_job(self):
        """Unloads the job."""
        self.jrds_client.unload_job(self.job_data.subscription_id, self.sandbox_id, self.job_id,
                                    self.job_updatable_data.is_draft, datetime.now(), 2)
        start_request_time = time.strptime(self.job_data.start_request_time.split("+")[0].split(".")[0],
                                           "%Y-%m-%dT%H:%M:%S")
        duration_td = datetime.utcnow() - datetime.fromtimestamp(time.mktime(start_request_time))
        duration_seconds = (duration_td.microseconds + (
                            duration_td.seconds + duration_td.days * 24 * 3600) * 10 ** 6) / 10 ** 6
        tracer.log_etw_job_duration(self.job_data.subscription_id, self.job_data.account_id, self.sandbox_id,
                                    self.job_id, str(duration_td), duration_seconds, self.job_data.tier_name,
                                    self.job_data.account_name, jobtriggersource.mapping[self.job_data.trigger_source],
                                    "Unknown",  # TODO(dalbe): fix runbook source
                                    self.runbook.definition_kind_str, self.runbook_data.name, self.job_data.run_on)
        tracer.log_sandbox_job_unloaded(self.job_id)

    def __process_job_parameters(self):
        string_parameter_type = 'System.String'
        string_array_parameter_type = 'System.String[]'
        bool_parameter_type = 'System.Boolean'
        object_parameter_type = 'System.Object'


        if self.job_data.parameters is not None and len(self.job_data.parameters) > 0:
            for parameter in self.job_data.parameters:
                parameter_type = self.__get_parameter_type(str(parameter["Name"]))
                if (parameter["Value"] is not None):
                    parameter_value = str(parameter["Value"])
                    if string_parameter_type.lower() == parameter_type.lower():
                        # string with white spaces should be wrapped in single quotes
                        if " " in parameter_value:
                            parameter_value = "'%s'" % parameter_value
                    elif bool_parameter_type.lower() == parameter_type.lower():
                        # Boolean value should start with $, skip for 0 and 1
                        if (parameter_value != "0" and parameter_value != "1"):
                            if not parameter_value.startswith("$"):
                                parameter_value = ("$" + parameter_value)
                    elif string_array_parameter_type.lower() == parameter_type.lower():
                        # split the string by separator and wrap the strings with white spaces in single quotes
                        values_list = (parameter_value.split(","))
                        updated_list = []
                        for value in values_list:
                            if " " in str(value):
                                value = "'%s'" % str(value)
                            updated_list.append(value)
                        parameter_value = ",".join(updated_list)
                        # wrap the final string array in double quotes
                        parameter_value = "\"%s\"" % parameter_value
                    elif object_parameter_type.lower() == parameter_type.lower():
                        # wrap object type in single quotes by default
                        parameter_value = "\'%s\'" % parameter_value
                    parameter["Value"] = parameter_value

    
    def __get_parameter_type(self, parameter_name):
        if self.runbook_data.parameters is not None and len(self.runbook_data.parameters) > 0:
            for parameter in self.runbook_data.parameters:
                if parameter["Name"] == parameter_name:
                    return str(parameter["ParameterType"]["Name"])

    @staticmethod
    def get_full_stderr_content(stderr):
        full_error_output = ""
        while True:
            error_output = stderr.readline()
            if error_output is None or error_output == '':
                break
            full_error_output = "\n".join([full_error_output, error_output])
        return full_error_output
