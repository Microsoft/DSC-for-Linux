#!/usr/bin/env python3
# ====================================
#  Copyright (c) Microsoft Corporation. All rights reserved.
# ====================================

""" Contains functions to rotate worker certificate """

import configparser
import os
import subprocess
import linuxutil
import diydirs
import configuration3

DIY_STATE_PATH = diydirs.DIY_STATE_PATH

SHOULD_WORKER_CERT_ROTATE = ['False']

def generate_cert_rotation_self_signed_certificate():
    """Creates a self-signed x509 certificate and key.

    Returns:
        temp_certificate_path    : string, the path of the new certificate
        temp_key_path            : string, the path of the new key
    """
    
    import tracer

    tracer.log_debug_trace("Creating Certificate/Key")
    temp_certificate_path = os.path.join(DIY_STATE_PATH, "worker_diy_temp.crt")
    temp_key_path = os.path.join(DIY_STATE_PATH, "worker_diy_temp.key")
    cmd = ["openssl", "req", "-subj",
           "/C=US/ST=Washington/L=Redmond/O=Microsoft Corporation/OU=Azure Automation/CN=Hybrid Runbook Worker",
           "-new", "-newkey", "rsa:2048", "-days", "365", "-nodes", "-x509", "-keyout", temp_key_path, "-out",
           temp_certificate_path]
    process, certificate_creation_output, error = linuxutil.popen_communicate(cmd)
    error = error.decode() if isinstance(error, bytes) else error
    if process.returncode != 0:
        raise Exception("Unable to create certificate/key. " + str(error))
    import tracer
    tracer.log_debug_trace("Certificate/Key created for initiating certificate rotation")

    return temp_certificate_path, temp_key_path


def clean_up_certificate_and_key(temp_certificate_path, temp_key_path):
    """ Delete the temporary certificate/key.

    Args:
        temp_certificate_path    : string, the path of the certificate
        temp_key_path            : string, the path of the key
    """ 
    import tracer

    tracer.log_debug_trace("Cleaning up the certificate/key generated for certificate rotation")
    subprocess.call(["sudo", "rm", temp_certificate_path])
    subprocess.call(["sudo", "rm", temp_key_path])
    tracer.log_debug_trace("Clean up of certificate/key generated for certificate rotation completed")


def replace_self_signed_certificate_and_key(temp_certificate_path, temp_key_path, thumbprint):
    """ Replace the old certificate/key with new certificate/key and update worker.conf with latest thumbprint.

    Args:
        temp_certificate_path    : string, the path of the certificate
        temp_key_path            : string, the path of the key
    """ 
    import tracer

    tracer.log_debug_trace("Replacing the old certificate/key with newly generated certificate/key")
    old_certificate_path = os.path.join(DIY_STATE_PATH, "worker_diy.crt")
    old_key_path = os.path.join(DIY_STATE_PATH, "worker_diy.key")
    subprocess.call(["sudo", "mv", "-f", temp_certificate_path, old_certificate_path])
    subprocess.call(["sudo", "mv", "-f", temp_key_path, old_key_path])
    tracer.log_debug_trace("Worker certificate/key is updated with the latest one.")

    tracer.log_debug_trace("Updating worker.conf with latest thumbprint.")
    worker_conf_path = os.path.join(DIY_STATE_PATH, "worker.conf")

    config = configparser.ConfigParser()
    if os.path.isfile(worker_conf_path):
        config.read(worker_conf_path)
    conf_file = open(worker_conf_path, 'w')

    registration_metadata_section = "registration-metadata"
    if not config.has_section(registration_metadata_section):
        config.add_section(registration_metadata_section)
    config.set(registration_metadata_section, configuration3.CERTIFICATE_THUMBPRINT, thumbprint)

    config.write(conf_file)
    conf_file.close()

    tracer.log_debug_trace("Worker.conf is updated with newest thumbprint")


def get_certificate_rotation_header_value():
    return SHOULD_WORKER_CERT_ROTATE[0]


def set_certificate_rotation_header_value(shouldworkercertificaterotate):
    SHOULD_WORKER_CERT_ROTATE[0] = shouldworkercertificaterotate