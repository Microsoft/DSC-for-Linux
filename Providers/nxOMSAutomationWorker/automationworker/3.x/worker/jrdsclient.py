#!/usr/bin/env python3
# ====================================
#  Copyright (c) Microsoft Corporation. All rights reserved.
# ====================================

"""JRDSClient class."""

from datetime import datetime
import time
import traceback
import urllib.parse


import configuration3 as configuration
import locallogger
from workerexception import *
import linuxutil
import workercertificaterotation

transient_status_codes = set([408, 429, 500, 502, 503, 504])

DISABLE_CERT_ROTATION = 'False'

class JRDSClient(object):
    def __init__(self, http_client):
        self.httpClient = http_client
        self.protocol_version = "1.0"
        self.base_uri = configuration.get_jrds_base_uri()
        self.account_id = configuration.get_account_id()
        self.HybridWorkerGroupName = configuration.get_hybrid_worker_name()
        self.machine_id = configuration.get_machine_id()

    @staticmethod
    def issue_request(request_function, url):
        """Invokes request_funtion."""
        response = request_function(url)
        tries = 1
        max_tries = 5
        while response.status_code in transient_status_codes:
            if tries > max_tries:
                break
            locallogger.log_info("INFO: http code %d while trying to reach %s, retrying" %(response.status_code, url))
            time_to_wait = 3 * (2 ** tries)
            if time_to_wait > 60:
                time_to_wait = 60
            time.sleep(time_to_wait)
            response = request_function(url)
            tries += 1

        if response.status_code == 401:
            raise JrdsAuthorizationException()
        return response

    def get_sandbox_actions(self):
        """Gets any pending sandbox actions and headers which determine polling frequency and certificate rotation of workers

        Returns:
            A list of sandbox actions.

        Note:
            The output list is of the following format:
            [
                {
                    "SandboxId": "206738c8-b66a-4eee-95a6-ddc23e3833a9",
                    "ConnectionEndpoint": "",
                    "ConnectionPort": 0,
                    "SandboxCookie": "nQwdpZ3MK5s=",
                    "CredentialName": null,
                    "MSISecret": "NullableMSISecret"
                }
            ]
        """
        encoded_hybrid_worker_group_name = urllib.parse.quote(self.HybridWorkerGroupName)
        url = self.base_uri + "/automationAccounts/" + self.account_id + \
              "/Sandboxes/GetSandboxActions?HybridWorkerGroupName=" + encoded_hybrid_worker_group_name + \
              "&api-version=" + self.protocol_version
        response = self.issue_request(lambda u: self.httpClient.get(u), url)

        import tracer

        if response.status_code == 200:
            try:
                if response.deserialized_data is None or "value" not in response.deserialized_data:
                    locallogger.log_info("INFO: Could not deserialize get_sandbox_actions response body: %s" % str(response.deserialized_data))
                    return None
            except TypeError:
                locallogger.log_info("INFO: Could not deserialize get_sandbox_actions response body: %s" % str(response.deserialized_data))
                return None
            
            # whenever worker cert has crossed half of it's lifetime or server is initiating a forced rotation of certificate based on date, header is set on the server side
            # based on the headers client initiates worker certificate rotation
            try:
                if(eval(workercertificaterotation.get_certificate_rotation_header_value())):
                    tracer.log_debug_trace("Initiating certificate Rotation of Hybrid Worker")
                    workercertificaterotation.set_certificate_rotation_header_value(DISABLE_CERT_ROTATION)  
                    self.worker_certificate_rotation()
                    tracer.log_worker_certificate_rotation_successful()
            except Exception as ex:
                tracer.log_debug_trace("[exception=" + str(ex) + "]")
                tracer.log_worker_certificate_rotation_failed(ex)

            return response.deserialized_data["value"]

        raise Exception("Unable to get sandbox actions. [status=" + str(response.status_code) + "]")

    def worker_certificate_rotation(self):
        """ Rotate worker certificate. 
            Steps includes creating new certificate/key and after JRDS returns 200, replace the old certificate/key with newly generated certificate/key.
            Worker.conf is updated with the latest thumbprint.
        """

        import tracer

        try:
            temp_certificate_path, temp_key_path = workercertificaterotation.generate_cert_rotation_self_signed_certificate()
            issuer, subject, thumbprint, not_before, not_after = linuxutil.get_cert_info_with_dates(temp_certificate_path)

            payload = {'Thumbprint': thumbprint,
               'Issuer': issuer,
               'Subject': subject,
               'NotBefore': not_before,
               'NotAfter': not_after}

            headers = {"Content-Type": "application/json"}

            url = self.base_uri + "/automationAccounts/" + self.account_id + \
              "/hybridCertificateRotation?api-version=" + self.protocol_version
            response = self.issue_request(lambda u: self.httpClient.post(u, headers=headers, data=payload), url)

            if response.status_code == 200:
                tracer.log_debug_trace("New worker certificate successfully added in the Database")
                workercertificaterotation.replace_self_signed_certificate_and_key(temp_certificate_path, temp_key_path, thumbprint)
        except Exception as ex:
            tracer.log_debug_trace("[exception=" + str(ex) + "]" + "[stacktrace=" + str(traceback.format_exc()) + "]")
            tracer.log_worker_certificate_rotation_failed(ex)
        finally:
            workercertificaterotation.clean_up_certificate_and_key(temp_certificate_path, temp_key_path)

        return

    def get_job_actions(self, sandbox_id):
        """Gets any pending job action for the given sandbox id.

        Args:
            sandbox_id: string, the sandbox id.

        Returns:
            A list of job actions.

        Note:
            The output list is of the following format:
            [
                {
                    "MessageMetadata": {
                        "PopReceipt": "AgAAAAMAAAAAAAAAPuDINxcZ0gE=",
                        "MessageId": "2e682df3-a6c6-4d23-afc2-40dc01c2a509"
                    },
                    "MessageSource'": "Queue",
                    "LockToken'": "00000000-0000-0000-0000-000000000000",
                    "JobId'": "6cbb98cc-d69a-46f0-a116-3b6094ceba6b"
                }
            ]
        """
        url = self.base_uri + "/automationAccounts/" + self.account_id + "/Sandboxes/" + sandbox_id + \
              "/jobs/getJobActions?api-version=" + self.protocol_version

        try:
            response = self.issue_request(lambda u: self.httpClient.get(u), url)
        except JrdsAuthorizationException:
            raise JrdsSandboxTerminated()

        if response.status_code == 200:
            try:
                if response.deserialized_data is None or "value" not in response.deserialized_data:
                    locallogger.log_info(
                        "INFO: Could not deserialize get_job_actions response body: %s" % str(response.deserialized_data))
                    return []
            except TypeError:
                locallogger.log_info(
                    "INFO: Could not deserialize get_job_actions response body: %s" % str(response.deserialized_data))
                return []
            job_actions = response.deserialized_data["value"]
            if len(job_actions) != 0:
                message_metadatas = [action["MessageMetadata"] for action in job_actions]
                self.acknowledge_job_action(sandbox_id, message_metadatas)
            return job_actions

        raise Exception("Unable to get job actions. [status=" + str(response.status_code) + "]")

    def get_job_data(self, job_id):
        """Get the job details for the given id.

        Args:
            job_id: string, the job id.

        Returns:
            A dictionary containing the job's details.

        Note:
            The output dictionary is of the following format:
            {
                "creationTime": "2016-08-02T22:19:40.39+00:00",
                "endTime": null,
                "startRequestTime": "2016-08-02T22:19:42.597+00:00",
                "jobId": "324e3d65-cae4-4136-b953-7f001f92be13",
                "jobKey": 1341273,
                "jobStatus": 0,
                "triggerScope": 0,
                "jobStatusDetails": 0,
                "lastModifiedTime": "2016-08-02T22:19:40.39+00:00",
                "lastStatusModifiedTime": null,
                "partitionId": 1933161748,
                "parameters": null,
                "invokedRunbookVersions": [],
                "pendingAction": 1,
                "pendingActionData": null,
                "runbookVersionId": "ccb7bc90-20e9-4e5c-bbf0-f265c1de79e6",
                "runbookVersion": "16_2016-06-08T21:27:41.7600000",
                "startTime": null,
                "accountId": "726169cd-9760-46ec-a7a8-ea1c7b4be12b",
                "workflowInstanceId": null,
                "runbookVersionKey": 1198942,
                "runbookKey": 632266,
                "accountKey": 794179,
                "jobException": null,
                "moduleInfo": [
                    {
                        "isGlobal": true,
                        "moduleId": "cb28147b-7933-4fcf-bdf0-bae57571113b",
                        "moduleVersionId": "d5939764-956e-4d99-826a-2e2d24d2ee34",
                        "name": "Azure",
                        "versionKey": 94261
                    },
                    {
                        "isGlobal": true,
                        "moduleId": "b553579f-f545-4295-843b-dfe2e32447e2",
                        "moduleVersionId": "e23c5e08-deaf-4237-b21a-6122a5245ad0",
                        "name": "Microsoft.PowerShell.Core",
                        "versionKey": 94262
                    }
                ],
                "fullWorkflowScript": null,
                "trackingId": "43d2fbbd-bdde-4e9a-840c-eb5a3c634004",
                "subscriptionId": "1d8225ca-97d3-4628-b657-fbb2e0609287",
                "noPersistEvictionsCount": 0,
                "status": null,
                "runOn": "WindowsWorkerGroup",
                "tierName": "Basic",
                "accountName": "PythonTest",
                "triggerSource": 1
            }
        """
        url = self.base_uri + "/automationAccounts/" + self.account_id + "/jobs/" + job_id + "?api-version=" + \
              self.protocol_version
        response = self.issue_request(lambda u: self.httpClient.get(u), url)

        if response.status_code == 200:
            return JobData(response.deserialized_data)

        raise Exception("Unable to get job. [status=" + str(response.status_code) + "]")

    def get_job_updatable_data(self, job_id):
        """Gets the job updatable data for the given id.

        Args:
            job_id: string, the job id.

        Returns:
            A dictionary containing the job's updatable data.

        Note:
            The output dictionary is of the following format:
            {
                "logActivityTrace": 0,
                "triggerSource": 1,
                "jobId": "d894d142-29f2-4f8c-85cd-f2f3c54c5f00",
                "logProgress": false,
                "jobStatus": 3,
                "accountName": "PythonTest",
                "partitionId": 1294079927,
                "logDebug": false,
                "isDraft": false,
                "workflowInstanceId": None,
                "JobKey": 1367376,
                "subscriptionId": "1d8225ca-97d3-4628-b657-fbb2e0609287",
                "noPersistEvictionsCount": 0,
                "AccountKey": 794179,
                "jobStartedBy": "live.com#oaastestdf01@live.com",
                "jobRunDestination": "WindowsWorkerGroup",
                "tierName": "u'Basic'",
                "pendingActionData": None,
                "jobStatusDetails": 0,
                "resourceGroupName": "PythonTest",
                "pendingAction": 0,
                "logVerbose": false,
                "RunbookKey": 632266
            }
        """
        url = self.base_uri + "/automationAccounts/" + self.account_id + "/jobs/" + job_id + \
              "/getUpdatableData?api-version=" + self.protocol_version
        response = self.issue_request(lambda u: self.httpClient.get(u), url)

        if response.status_code == 200:
            return JobUpdatableData(response.deserialized_data)

        raise Exception("Unable to get job. [status=" + str(response.status_code) + "]")

    def get_runbook_data(self, runbook_version_id):
        """Gets the runbook definition of the given runbook_version_id.

        Args:
            runbook_version_id: string, the runbook version id.

        Returns:
            A dictionary containing the runbook's details.

        Note:
            The output list is of the following format:
            {
                "name": "594D5915-57D6-40C3-A8C1-6112C8B25900ForPython",
                "accountId": "726169cd-9760-46ec-a7a8-ea1c7b4be12b",
                "runbookId": "98a55667-7bb9-4feb-acac-ba4664987e9d",
                "definition": "#python\r\nimport platform\r\n\r\nprint \"OS : \" + platform.system(),
                               platform.release()\r\nprint \"Helloworld from Python\"",
                "runbookDefinitionKind": 5,
                "runbookVersionId": "ccb7bc90-20e9-4e5c-bbf0-f265c1de79e6",
                "parameters": null
            }
        """
        url = self.base_uri + "/automationAccounts/" + self.account_id + "/runbooks/" + runbook_version_id + \
              "?api-version=" + self.protocol_version
        response = self.issue_request(lambda u: self.httpClient.get(u), url)

        if response.status_code == 200:
            return RunbookData(response.deserialized_data)

        raise Exception("Unable to get runbook. [status=" + str(response.status_code) + "]")

    def acknowledge_job_action(self, sandbox_id, message_metadatas):
        """Acknowledges the job action.

        Args:
            sandbox_id  : string        , the sandbox id.
            lock_tokens : array(string) , the lock tokens.
        """
        payload = {'MessageMetadatas': message_metadatas}
        headers = {"Content-Type": "application/json"}
        url = self.base_uri + "/automationAccounts/" + self.account_id + "/Sandboxes/" + sandbox_id + \
              "/jobs/AcknowledgeJobActions?api-version=" + self.protocol_version
        response = self.issue_request(lambda u: self.httpClient.post(u, headers=headers, data=payload), url)

        if response.status_code == 200:
            return

        raise Exception("Unable to acknowledge job action. [status=" + str(response.status_code) + "]")

    def set_job_status(self, sandbox_id, job_id, job_status, is_terminal, extended_property=None,exception=None):
        """Set the job status of the given job id (and exception if needed).

        Args:
            sandbox_id  : string    , the sandbox id.
            job_id      : string    , the job id.
            job_status  : string    , the job status.
            is_terminal : boolean   , is the job status terminal value.
            exception   : string    , the exception details (optional).
        """
        payload = {
            'exception': exception,
            'isFinalStatus': is_terminal,
            'jobStatus': job_status,
            'extendedProperty': extended_property
        }
        headers = {"Content-Type": "application/json"}
        url = self.base_uri + "/automationAccounts/" + self.account_id + "/Sandboxes/" + sandbox_id + "/jobs/" + \
              job_id + "/changeStatus?api-version=" + self.protocol_version
        response = self.issue_request(lambda u: self.httpClient.post(u, headers=headers, data=payload), url)

        if response.status_code == 200:
            return

        raise Exception("Unable to set job status. [status=" + str(response.status_code) + "]")

    def set_stream(self, job_id, runbook_version_id, stream_text, stream_type, sequence_number, extended_property):
        """Set the streams for the given job id.

        Args:
            job_id              : string, the job id.
            runbook_version_id  : string, the runbook version id.
            stream_text         : string, the stream text.
            stream_type         : string, the stream type (Progress, Output, Warning, Error, Debug, Verbose, Any).
            sequence_number     : int   , the stream index in sequence.
        """
        payload = {
            'AccountId': self.account_id,
            'JobId': job_id,
            'RecordTime': datetime.now().isoformat(),
            'RunbookVersionId': runbook_version_id,
            'SequenceNumber': sequence_number,
            'StreamRecord': None,
            'StreamRecordText': stream_text,
            'Type': stream_type,
            'extendedProperty': extended_property
        }
        headers = {"Content-Type": "application/json"}
        url = self.base_uri + "/automationAccounts/" + self.account_id + "/jobs/" + job_id + \
              "/postJobStream?api-version=" + self.protocol_version
        response = self.issue_request(lambda u: self.httpClient.post(u, headers=headers, data=payload), url)

        if response.status_code == 200:
            return

        raise Exception("Unable to set streams. [status=" + str(response.status_code) + "]")

    def set_log(self, event_id, activity_id, log_type, args):
        """Set the log (generate a corresponding ETW trace on the JRDS worker).

        Args:
            event_id    : int           , the event id.
            activity_id : string        , the activity id.
            log_type    : string        , the log type (Debug/Informational).
            args        : array(object) , the trace arguments.
        """
        payload = {'activityId': activity_id,
                   'args': args,
                   'eventId': event_id,
                   'logtype': log_type}
        headers = {"Content-Type": "application/json"}
        url = self.base_uri + "/automationAccounts/" + self.account_id + "/logs?api-version=" + self.protocol_version
        response = self.issue_request(lambda u: self.httpClient.post(u, headers=headers, data=payload), url)

        if response.status_code == 200:
            return

        raise Exception("Unable to set log. [status=" + str(response.status_code) + "]")

    def unload_job(self, subscription_id, sandbox_id, job_id, is_test, start_time, execution_time):
        """Unloads the job for the given job id.

        Args:
            sandbox_id      : string    , the sandbox id.
            job_id          : string    , the job id.
            is_test         : boolean   , is test job.
            start_time      : datetime  , the job status.
            execution_time  : int       , the job execution time in seconds.
        """
        payload = {'executionTimeInSeconds': execution_time,
                   'isTest': is_test,
                   'jobId': job_id,
                   'startTime': start_time.isoformat(),
                   'subscriptionId': subscription_id}
        headers = {"Content-Type": "application/json"}
        url = self.base_uri + "/automationAccounts/" + self.account_id + "/Sandboxes/" + sandbox_id + "/jobs/" + \
              job_id + "/unload?api-version=" + self.protocol_version
        response = self.issue_request(lambda u: self.httpClient.post(u, headers=headers, data=payload), url)

        if response.status_code == 200:
            return

        raise Exception("Unable to unload job. [status=" + str(response.status_code) + "]")

    def get_variable_asset(self, name):
        """Gets the requested automation variable asset.

        Args:
            name : Name of the automation variable.

        Note:
            The JRDS response body is of the following format:
            {
                "isEncrypted": false,
                "name": "testvar",
                "value": "Value of variable"
            }
        """
        url = self.base_uri + "/automationAccounts/" + self.account_id + "/variables/" + name + "?api-version=" \
              + self.protocol_version
        response = self.issue_request(lambda u: self.httpClient.get(u), url)

        if response.status_code == 200:
            return response.deserialized_data
        elif response.status_code == 404:
            raise AutomationAssetNotFound()
        elif response.status_code == 503:
            raise Jrds503Exception()

        raise Exception("An unknown error occurred. Unable to get the requested variable asset. status code: "
                        + str(response.status_code))

    def set_variable_asset(self, name, value, isEncrypted):
        """Sets the requested automation variable asset value. The specified variable has to exists for this request
        to succeed.

        Args:
            name        : Name of the automation variable.
            value       : Value of the automation variable to be persisted.
            isEncrypted : specifies if the variable asset value is meant to be encrypted

        Note:
            JRDS can return the following code :
            200 - successfull request
            400 - invalid request (i.e mismatch between the url variable name and the body variable name).
            404 - non-existant variable

        """
        payload = {'name': name,
                   'value': value,
                   'isEncrypted': isEncrypted}
        url = self.base_uri + "/automationAccounts/" + self.account_id + "/variables/" + name + "?api-version=" \
              + self.protocol_version
        response = self.issue_request(lambda u: self.httpClient.post( u, data=payload), url)

        if response.status_code == 200:
            return response.deserialized_data
        elif response.status_code == 404:
            raise AutomationAssetNotFound()
        elif response.status_code == 503:
            raise Jrds503Exception()


        raise Exception("An unknown error occurred. Unable to set the value of the specified variable asset. status code: "
                        + str(response.status_code))

    def get_credential_asset(self, name):
        """Gets the requested automation credential asset.

        Args:
            name : Name of the automation credential.

        Note:
            The JRDS response body is of the following format:
            {
                "userName": "myusername",
                "name": "testcred",
                "value": "mypassword",
                "description": "None"
            }
        """
        url = self.base_uri + "/automationAccounts/" + self.account_id + "/credentials/" + name + "?api-version=" \
              + self.protocol_version
        response = self.issue_request(lambda u: self.httpClient.get(u), url)

        if response.status_code == 200:
            return response.deserialized_data
        elif response.status_code == 404:
            raise AutomationAssetNotFound()

        raise Exception("An unknown error occurred. Unable to get the requested credential asset.")

    def get_certificate_asset(self, name):
        """Gets the requested automation credential asset.

        Args:
            name : Name of the automation certificate.

        Note:
            The JRDS response body is of the following format:
            {
                "name": "",
                "thumbprint": "",
                "isExportable": "",
                "value": "",
                "expiryTime": ""
            }
        """
        url = self.base_uri + "/automationAccounts/" + self.account_id + "/certificates/" + name + "?api-version=" \
              + self.protocol_version
        response = self.issue_request(lambda u: self.httpClient.get(u), url)

        if response.status_code == 200:
            return response.deserialized_data
        elif response.status_code == 404:
            raise AutomationAssetNotFound()

        raise Exception("An unknown error occurred. Unable to get the requested certificate asset.")

    def get_connection_asset(self, name):
        """Gets the requested automation connection asset.

        Args:
            name : Name of the automation connection.

        Note:
            The JRDS response body is of the following format:
            {
                "encryptedFieldNames": [],
                "connectionFields": {"SubscriptionID": "subId", "AutomationCertificateName":"certName"}
            }
        """
        url = self.base_uri + "/automationAccounts/" + self.account_id + "/connections/" + name + "?api-version=" \
              + self.protocol_version
        response = self.issue_request(lambda u: self.httpClient.get(u), url)

        if response.status_code == 200:
            return response.deserialized_data
        elif response.status_code == 404:
            raise AutomationAssetNotFound()

        raise AutomationAssetException("An unknown error occurred. Unable to get the requested connection asset.")


class JobData(object):
    """JRDS JobData model."""

    def __init__(self, deserialized_response):
        self.creation_time = deserialized_response["creationTime"]
        self.end_time = deserialized_response["endTime"]
        self.start_request_time = deserialized_response["startRequestTime"]
        self.job_id = deserialized_response["jobId"]
        self.job_key = deserialized_response["jobKey"]
        self.job_status = deserialized_response["jobStatus"]
        self.trigger_scope = deserialized_response["triggerScope"]
        self.job_status_details = deserialized_response["jobStatusDetails"]
        self.last_modified_time = deserialized_response["lastModifiedTime"]
        self.last_status_modified_time = deserialized_response["lastStatusModifiedTime"]
        self.partition_id = deserialized_response["partitionId"]
        self.parameters = deserialized_response["parameters"]
        self.invoked_runbook_versions = deserialized_response["invokedRunbookVersions"]
        self.pending_action = deserialized_response["pendingAction"]
        self.pendinc_action_data = deserialized_response["pendingActionData"]
        self.runbook_version_id = deserialized_response["runbookVersionId"]
        self.runbook_version = deserialized_response["runbookVersion"]
        self.start_time = deserialized_response["startTime"]
        self.account_id = deserialized_response["accountId"]
        self.workflow_instance_id = deserialized_response["workflowInstanceId"]
        self.runbook_version_key = deserialized_response["runbookVersionKey"]
        self.runbook_key = deserialized_response["runbookKey"]
        self.account_key = deserialized_response["accountKey"]
        self.job_exception = deserialized_response["jobException"]
        self.module_info = deserialized_response["moduleInfo"]
        self.full_workflow_script = deserialized_response["fullWorkflowScript"]
        self.tracking_id = deserialized_response["trackingId"]
        self.subscription_id = deserialized_response["subscriptionId"]
        self.no_persis_eviction_count = deserialized_response["noPersistEvictionsCount"]
        self.status = deserialized_response["status"]
        self.run_on = deserialized_response["runOn"]
        self.tier_name = deserialized_response["tierName"]
        self.account_name = deserialized_response["accountName"]
        self.trigger_source = deserialized_response["triggerSource"]
        self.resource_group_name = deserialized_response["resourceGroupName"]
        self.runbook_name = deserialized_response["runbookName"]


class JobUpdatableData(object):
    """JRDS JobUpdatableData model."""

    def __init__(self, deserialized_response):
        self.log_activity_trace = deserialized_response["logActivityTrace"]
        self.trigger_source = deserialized_response["triggerSource"]
        self.job_id = deserialized_response["jobId"]
        self.log_progress = deserialized_response["logProgress"]
        self.job_status = deserialized_response["jobStatus"]
        self.account_name = deserialized_response["accountName"]
        self.partition_id = deserialized_response["partitionId"]
        self.log_debug = deserialized_response["logDebug"]
        self.is_draft = deserialized_response["isDraft"]
        self.workflow_instance_id = deserialized_response["workflowInstanceId"]
        self.job_key = deserialized_response["JobKey"]
        self.subscription_id = deserialized_response["subscriptionId"]
        self.noPersist_evictions_count = deserialized_response["noPersistEvictionsCount"]
        self.account_key = deserialized_response["AccountKey"]
        self.job_started_by = deserialized_response["jobStartedBy"]
        self.job_run_destination = deserialized_response["jobRunDestination"]
        self.tier_name = deserialized_response["tierName"]
        self.pending_action_data = deserialized_response["pendingActionData"]
        self.job_status_details = deserialized_response["jobStatusDetails"]
        self.resource_group_name = deserialized_response["resourceGroupName"]
        self.pending_action = deserialized_response["pendingAction"]
        self.log_verbose = deserialized_response["logVerbose"]
        self.runbook_key = deserialized_response["RunbookKey"]


class RunbookData(object):
    """JRDS RunbookData model."""

    def __init__(self, deserialized_response):
        self.name = deserialized_response["name"]
        self.account_id = deserialized_response["accountId"]
        self.runbook_id = deserialized_response["runbookId"]
        self.definition = deserialized_response["definition"]
        self.runbook_definition_kind = deserialized_response["runbookDefinitionKind"]
        self.runbook_version_id = deserialized_response["runbookVersionId"]
        self.parameters = deserialized_response["parameters"]
