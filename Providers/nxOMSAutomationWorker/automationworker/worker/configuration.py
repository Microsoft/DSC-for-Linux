#!/usr/bin/env python2
#
# Copyright (C) Microsoft Corporation, All rights reserved.


import ConfigParser
import os
import sys

import serializerfactory

json = serializerfactory.get_serializer(sys.version_info)

CONFIG_ENV_KEY = "WORKERCONF"
WORKER_REQUIRED_CONFIG_SECTION = "worker-required"
WORKER_OPTIONAL_CONFIG_SECTION = "worker-optional"
METADATA_CONFIG_SECTION = "metadata"

# manually set configuration values
SOURCE_DIRECTORY_PATH = "source_directory_path"
COMPONENT = "component"

# required configuration keys
CERT_PATH = "jrds_cert_path"
KEY_PATH = "jrds_key_path"
BASE_URI = "jrds_base_uri"
ACCOUNT_ID = "account_id"
MACHINE_ID = "machine_id"
HYBRID_WORKER_GROUP_NAME = "hybrid_worker_group_name"
WORKER_VERSION = "worker_version"
WORKING_DIRECTORY_PATH = "working_directory_path"

# optional configuration keys
DEBUG_TRACES = "debug_traces"
BYPASS_CERTIFICATE_VERIFICATION = "bypass_certificate_verification"
ENFORCE_RUNBOOK_SIGNATURE_VALIDATION = "enforce_runbook_signature_validation"
GPG_PUBLIC_KEYRING_PATH = "gpg_public_keyring_path"
STATE_DIRECTORY_PATH = "state_directory_path"
JRDS_POLLING_FREQUENCY = "jrds_polling_frequency"
PROXY_CONFIGURATION_PATH = "proxy_configuration_path"

# optional metadata configuration keys
VM_ID = "vm_id"
IS_AZURE_VM = "is_azure_vm"
WORKER_TYPE = "worker_type"

# optional configuration default values
DEFAULT_EMPTY = ""
DEFAULT_DEBUG_TRACES = "false"
DEFAUTL_BYPASS_CERTIFICATE_VERIFICATION = "false"
DEFAULT_ENFORCE_RUNBOOK_SIGNATURE_VALIDATION = "true"
DEFAULT_GPG_PUBLIC_KEYRING_PATH = DEFAULT_EMPTY
DEFAULT_STATE_DIRECTORY_PATH = DEFAULT_EMPTY
DEFAULT_PROXY_CONFIGURATION_PATH = DEFAULT_EMPTY
DEFAULT_UNKNOWN = "Unknown"
DEFAULT_VM_ID = DEFAULT_UNKNOWN
DEFAULT_WORKER_TYPE = DEFAULT_UNKNOWN
DEFAULT_COMPONENT = DEFAULT_UNKNOWN
DEFAULT_WORKER_VERSION = "1.10.0.0"
DEFAULT_JRDS_POLLING_FREQUENCY = "30"

# state configuration keys
STATE_PID = "pid"
STATE_RESOURCE_VERSION = "resource_version"
STATE_WORKSPACE_ID = "workspace_id"
STATE_WORKER_VERSION = "worker_version"

# other configuration keys (optional and most likely not used by the worker)
AGENT_ID = "agent_id"
WORKSPACE_ID = "workspace_id"
REGISTRATION_ENDPOINT = "registration_endpoint"
CERTIFICATE_THUMBPRINT = "jrds_cert_thumbprint"

worker_configuration_file_path = DEFAULT_EMPTY


def read_and_set_configuration(configuration_file_path):
    """Reads the worker configuration from the file at config_path and sets the read configuration to
    the env variable.

    The configuration is read of the path, put into a dictionary which will be serialized and set in the env
    variable.

    Notes:
        The WORKER_VERSION has to be set manually for now.
        The COMPONENT has to be set manually at the entry point of each component (worker/sandbox).

    Args:
        configuration_file_path: string, the configuration file path.
    """
    global worker_configuration_file_path
    worker_configuration_file_path = configuration_file_path

    clear_config()

    # init and set default values for optional configuration keys
    config = ConfigParser.SafeConfigParser({DEBUG_TRACES: DEFAULT_DEBUG_TRACES,
                                            BYPASS_CERTIFICATE_VERIFICATION: DEFAUTL_BYPASS_CERTIFICATE_VERIFICATION,
                                            ENFORCE_RUNBOOK_SIGNATURE_VALIDATION: DEFAULT_ENFORCE_RUNBOOK_SIGNATURE_VALIDATION,
                                            GPG_PUBLIC_KEYRING_PATH: DEFAULT_GPG_PUBLIC_KEYRING_PATH,
                                            STATE_DIRECTORY_PATH: DEFAULT_STATE_DIRECTORY_PATH,
                                            JRDS_POLLING_FREQUENCY: DEFAULT_JRDS_POLLING_FREQUENCY,
                                            PROXY_CONFIGURATION_PATH: DEFAULT_PROXY_CONFIGURATION_PATH,
                                            WORKER_TYPE: DEFAULT_WORKER_TYPE,
                                            VM_ID: DEFAULT_VM_ID,
                                            IS_AZURE_VM: "False"})

    # load the worker configuration file
    config.read(configuration_file_path)

    # create the configuration dictionary
    # read required configuration values
    configuration = {CERT_PATH: os.path.abspath(config.get(WORKER_REQUIRED_CONFIG_SECTION, CERT_PATH)),
                     KEY_PATH: os.path.abspath(config.get(WORKER_REQUIRED_CONFIG_SECTION, KEY_PATH)),
                     BASE_URI: config.get(WORKER_REQUIRED_CONFIG_SECTION, BASE_URI),
                     ACCOUNT_ID: config.get(WORKER_REQUIRED_CONFIG_SECTION, ACCOUNT_ID),
                     MACHINE_ID: config.get(WORKER_REQUIRED_CONFIG_SECTION, MACHINE_ID),
                     HYBRID_WORKER_GROUP_NAME: config.get(WORKER_REQUIRED_CONFIG_SECTION, HYBRID_WORKER_GROUP_NAME),
                     WORKING_DIRECTORY_PATH: os.path.abspath(
                         config.get(WORKER_REQUIRED_CONFIG_SECTION, WORKING_DIRECTORY_PATH)),
                     SOURCE_DIRECTORY_PATH: os.path.dirname(os.path.realpath(__file__)),
                     WORKER_VERSION: DEFAULT_WORKER_VERSION,
                     COMPONENT: DEFAULT_COMPONENT}

    # read optional configuration section
    configuration.update({DEBUG_TRACES: config.getboolean(WORKER_OPTIONAL_CONFIG_SECTION, DEBUG_TRACES),
                          BYPASS_CERTIFICATE_VERIFICATION: config.getboolean(WORKER_OPTIONAL_CONFIG_SECTION,
                                                                             BYPASS_CERTIFICATE_VERIFICATION),
                          ENFORCE_RUNBOOK_SIGNATURE_VALIDATION: config.getboolean(WORKER_OPTIONAL_CONFIG_SECTION,
                                                                                  ENFORCE_RUNBOOK_SIGNATURE_VALIDATION),
                          GPG_PUBLIC_KEYRING_PATH: config.get(WORKER_OPTIONAL_CONFIG_SECTION, GPG_PUBLIC_KEYRING_PATH),
                          STATE_DIRECTORY_PATH: config.get(WORKER_OPTIONAL_CONFIG_SECTION, STATE_DIRECTORY_PATH),
                          JRDS_POLLING_FREQUENCY: config.getint(WORKER_OPTIONAL_CONFIG_SECTION, JRDS_POLLING_FREQUENCY),
                          PROXY_CONFIGURATION_PATH: config.get(WORKER_OPTIONAL_CONFIG_SECTION,
                                                               PROXY_CONFIGURATION_PATH),
                          WORKER_TYPE: config.get(METADATA_CONFIG_SECTION, WORKER_TYPE),
                          VM_ID: config.get(METADATA_CONFIG_SECTION, VM_ID),
                          IS_AZURE_VM: config.getboolean(METADATA_CONFIG_SECTION, IS_AZURE_VM)})

    # set the worker conf to env var
    set_config(configuration)


def set_config(configuration):
    """Sets the worker configuration to the env variable.

    This method will merge the provided dictionary to any existent value in the environment variable.

    Args:
        configuration: dictionary(string), the configuration key value pairs.
    """
    try:
        env_config = os.environ[CONFIG_ENV_KEY]
        config = json.loads(env_config)
        config.update(configuration)
        configuration = config
    except KeyError:
        pass

    os.environ[CONFIG_ENV_KEY] = json.dumps(configuration)


def clear_config():
    try:
        del os.environ[CONFIG_ENV_KEY]
    except Exception:
        pass


def get_value(key):
    """Gets a specific value from the configuration value in the environment variable.

    This method will merge the provided dictionary to any existent value in the environment variable.

    Args:
        key: string, the configuration key value.

    Returns:
        The configuration value.
    """
    try:
        return json.loads(os.environ[CONFIG_ENV_KEY])[key]
    except KeyError:
        raise KeyError("Configuration environment variable not found. [key=" + key + "].")


def get_jrds_get_job_actions_polling_freq():
    return get_value(JRDS_POLLING_FREQUENCY)


def get_component():
    return get_value(COMPONENT)


def get_jrds_cert_path():
    return get_value(CERT_PATH)


def get_jrds_key_path():
    return get_value(KEY_PATH)


def get_jrds_base_uri():
    return get_value(BASE_URI)


def get_account_id():
    return get_value(ACCOUNT_ID)


def get_machine_id():
    return get_value(MACHINE_ID)


def get_hybrid_worker_name():
    return get_value(HYBRID_WORKER_GROUP_NAME)


def get_worker_version():
    return get_value(WORKER_VERSION)


def get_worker_configuration_file_path():
    return worker_configuration_file_path


def get_working_directory_path():
    return get_value(WORKING_DIRECTORY_PATH)


def get_debug_traces():
    return get_value(DEBUG_TRACES)


def get_verify_certificates():
    return get_value(BYPASS_CERTIFICATE_VERIFICATION)


def get_source_directory_path():
    return get_value(SOURCE_DIRECTORY_PATH)


def get_enforce_runbook_signature_validation():
    return get_value(ENFORCE_RUNBOOK_SIGNATURE_VALIDATION)


def get_gpg_public_keyrings_path():
    """Return a list of string representing keyring path."""
    keyring_list = str(get_value(GPG_PUBLIC_KEYRING_PATH)).split(",")
    sanitized_list = list(map(str.strip, keyring_list))
    return sanitized_list


def get_state_directory_path():
    return get_value(STATE_DIRECTORY_PATH)


def get_temporary_request_directory_path():
    return os.path.join(get_working_directory_path(), "req_tmp")


def get_proxy_configuration_path():
    return get_value(PROXY_CONFIGURATION_PATH)


def get_worker_type():
    return get_value(WORKER_TYPE)
