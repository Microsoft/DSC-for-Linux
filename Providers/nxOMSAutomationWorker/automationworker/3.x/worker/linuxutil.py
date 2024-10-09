#!/usr/bin/env python3
# ====================================
#  Copyright (c) Microsoft Corporation. All rights reserved.
# ====================================

from __future__ import print_function
import os
import subprocess
import sys
import re
import codecs
import traceback
from datetime import datetime

# workaround when unexpected environment variables are present
# sets COLUMNS wide enough so that output of ps does not get truncated
if 'COLUMNS' in os.environ:
    os.environ['COLUMNS'] = "3000"

# pwd will not be present on windows
# not using if clause in case some os other than posix has support for pwd
try:
    import pwd
    import grp
except:
    pass

PS_FJH_HEADER = ["UID", "PID", "PPID", "PGID", "SID", "C", "STIME", "TTY", "TIME", "CMD"]
PY_MAJOR_VERSION = 0
PY_MINOR_VERSION = 1
PY_MICRO_VERSION = 2


def posix_only(func):
    """Decorator to prevent linux specific methods to run on other OS."""
    if is_posix_host() is False:
        print(func.__name__ + " isn't supported on " + str(os.name) + " os.")
        return bypass
    else:
        return func


def bypass():
    pass


def format_process_entries_to_list(process_list):
    """Formats a list of raw list of string to process model objects.

    Example input :
            ["UID        PID  PPID  PGID   SID  C STIME TTY          TIME CMD",
             "oaastest 22448 22445 22448 22448  0 Mar08 pts/0    00:00:03 bash",
             "oaastest  2509 22448  2509 22448  0 06:14 pts/0    00:00:00 ps -fjH"]

    Returns:
        A list of ProcessModel objects.

    Note : The header row will be discarded.
    """
    formatted_entries = []
    for entry in process_list:
        sanitized_entry = [_f for _f in entry.split(" ") if _f]
        if len(sanitized_entry) < 1 or sanitized_entry == PS_FJH_HEADER:
            continue
        process = ProcessModel(sanitized_entry)
        formatted_entries.append(process)
    return formatted_entries


def is_posix_host():
    """Returns the True if the host is posix else False.

    Returns:
        bool, True if the host is posix else False.
    """
    return os.name.lower() == "posix"

@posix_only
def invoke_dmidecode():
    """Gets the dmidecode output from the host."""
    proc = subprocess.Popen(["sudo", "dmidecode"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    dmidecode, error = proc.communicate()
    if proc.poll() != 0:
        raise Exception("Unable to get dmidecode output : " + str(error))
    return dmidecode


def get_azure_vm_asset_tag():
    """Return the azure vm asset tag."""
    return "7783-7084-3265-9085-8269-3286-77"


def is_azure_vm(dmidecode_output):
    """Detects azure vm from dmidecode output.

    Note : is an asset tag "7783-7084-3265-9085-8269-3286-77" is present then this is an azure vm.

    Returns:
        bool, true if the host is an azure vm.
    """
    #print("linux util dmidecode : "+ str(dmidecode_output))
    asset_tags = re.findall(get_azure_vm_asset_tag(), dmidecode_output)

    for tag in asset_tags:
        if get_azure_vm_asset_tag() in tag:
            return True

    return False


def get_vm_unique_id_from_dmidecode(byteorder, dmidecode_output):
    """Extract the host UUID from dmidecode output.

    Returns:
        string, the host UUID.
    """
    uuid_prefix = "UUID: "
    uuids = re.findall(uuid_prefix + "[A-Z0-9]{8}-[A-Z0-9]{4}-[A-Z0-9]{4}-[A-Z0-9]{4}-[A-Z0-9]{12}",
                       dmidecode_output.upper())
    if len(uuids) < 1:
        raise Exception("No host UUID found.")

    # if multiple UUIDs are found take the first one
    uuid = uuids[0].split(uuid_prefix)[1].strip()

    # azure uuids are big endian
    if byteorder == "big":
        return uuid

    uuid_part = uuid.split("-")
    big_endian_uuid = "-".join([convert_to_big_endian(uuid_part[0]),
                                convert_to_big_endian(uuid_part[1]),
                                convert_to_big_endian(uuid_part[2]),
                                uuid_part[3],
                                uuid_part[4]]).upper()
    return big_endian_uuid


def convert_to_big_endian(little_endian_value):
    """Converts the little endian representation of the value into a big endian representation of the value"""
    """
    Little and big endian are two ways of storing multibyte data-types ( int, float, etc). 
    In little endian machines, last byte of binary representation of the multibyte data-type is stored first. 
    On the other hand, in big endian machines, first byte of binary representation of the multibyte data-type is stored first.
    """
    codecs_decoded =  codecs.decode(little_endian_value, "hex")
    codecs_reordered = codecs_decoded[::-1]
    return codecs.encode(codecs_reordered, "hex").decode()


@posix_only
def generate_uuid():
    """ UUID module isn't available in python 2.4. Since activity id are only required for tracing this is enough.

    Returns: string, an activity id which has a GUID format
    """
    proc = subprocess.Popen(["cat", "/proc/sys/kernel/random/uuid"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    uuid, error = proc.communicate()
    uuid = uuid.decode() if isinstance(uuid, bytes) else uuid
    error = error.decode() if isinstance(error, bytes) else error
    if proc.poll() != 0:
        raise Exception("Unable to get uuid from /proc/sys/kernel/random/uuid : " + str(error))
    return uuid.strip()


@posix_only
def get_current_username():
    """Returns the username owning the current process.

    Returns:
        string, representing the username (i.e myusername)
    """
    user_id = os.getuid()
    return pwd.getpwuid(user_id).pw_name


@posix_only
def is_existing_group(group_name):
    """Asserts the group exists on the host.

    Returns:
        bool, True if group exists on the box, False otherwise
    """
    try:
        grp.getgrnam(group_name)
        return True
    except KeyError:
        return False


@posix_only
def is_existing_user(username):
    """Asserts the user exists on the host.

    Returns:
        bool, True if user exists on the box, False otherwise
    """
    try:
        pwd.getpwnam(username)
        return True
    except KeyError:
        return False


@posix_only
def get_current_user_processes():
    """Gets the list of process of the current user.

    Returns:
        A list of ProcessModel objects.
    """
    current_username = get_current_username()
    proc = subprocess.Popen(["ps", "-fjH", "-u", current_username], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    output, error = proc.communicate()
    output = output.decode() if isinstance(output, bytes) else output
    error = error.decode() if isinstance(error, bytes) else error
    if proc.poll() != 0:
        raise Exception("Unable to get processes : " + str(error))
    formatted_entries = format_process_entries_to_list(output.split("\n"))
    return formatted_entries


@posix_only
def get_lsb_release():
    """Gets the os info through lsb_release.

    Returns:
        (distributor_id, description, release, codename)
    """
    proc = subprocess.Popen(["lsb_release", "-i", "-d", "-r", "-c"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    output, error = proc.communicate()
    output = output.decode()
    if proc.poll() != 0:
        raise Exception("Unable to get lsb_release info. Error : " + str(error))

    formatted_output = [entry.split("\t") for entry in output.strip().split("\n")]
    distributor_id = formatted_output[0][1]
    description = formatted_output[1][1]
    release = formatted_output[2][1]
    codename = formatted_output[3][1]
    return distributor_id, description, release, codename


@posix_only
def get_oms_agent_id():
    """Gets the oms agent id.

    Returns:
        string, the agent id
        None,   if no agent id can be fouud
    """
    omsadmin_filepath = "/etc/opt/microsoft/omsagent/conf/omsadmin.conf"
    agentid_filepath = "/etc/opt/microsoft/omsagent/agentid"
    agent_guid_delimiter = "AGENT_GUID="
    agent_id = None
    if os.path.isfile(agentid_filepath):
        try:
            file = open(agentid_filepath, "r")
            agent_id = file.read().strip()
            file.close()
        except:
            pass
    else:
        try:
            file = open(omsadmin_filepath, "r")
            for line in file.readlines():
                if agent_guid_delimiter in line:
                    agent_id = line.split(agent_guid_delimiter)[1].strip()
            file.close()
        except:
            pass
    return agent_id


@posix_only
def kill_current_user_process(pid):
    """Kills the process specified by the pid argument.

    Note:
        The specified pid has to be own by the same user owning the current process.
    """
    subprocess.call(["kill", "-9", str(pid)])


@posix_only
def get_cert_info(certificate_path):
    """Gets certificate information by invoking OpenSSL (OMS agent dependency).

    Returns:
        A tuple containing the certificate's issuer, subject, thumbprint.
    """
    issuer, subject, thumbprint, not_before, not_after = get_cert_info_with_dates(certificate_path)

    return issuer, subject, thumbprint


@posix_only
def get_cert_info_with_dates(certificate_path):
    """Gets certificate information by invoking OpenSSL (OMS agent dependency).

    Returns:
        A tuple containing the certificate's issuer, subject, thumbprint, start date and end date.
    """

    p = subprocess.Popen(["openssl", "x509", "-noout", "-in", certificate_path, "-fingerprint", "-sha1"],
                         stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE)
    raw_fingerprint, e = p.communicate()

    if p.poll() != 0:
        raise Exception("Unable to get certificate thumbprint.")

    p = subprocess.Popen(["openssl", "x509", "-noout", "-in", certificate_path, "-issuer"],
                         stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE)
    raw_issuer, e = p.communicate()

    if p.poll() != 0:
        raise Exception("Unable to get certificate issuer.")

    p = subprocess.Popen(["openssl", "x509", "-noout", "-in", certificate_path, "-subject"],
                         stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE)
    raw_subject, e = p.communicate()

    if p.poll() != 0:
        raise Exception("Unable to get certificate subject.")

    p = subprocess.Popen(["openssl", "x509", "-noout", "-in", certificate_path, "-startdate"],
                         stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE)
    raw_not_before, e = p.communicate()

    if p.poll() != 0:
        raise Exception("Unable to get certificate start date.")

    p = subprocess.Popen(["openssl", "x509", "-noout", "-in", certificate_path, "-enddate"],
                         stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE)
    raw_not_after, e = p.communicate()

    if p.poll() != 0:
        raise Exception("Unable to get certificate end date.")

    return parse_issuer_from_openssl_output(raw_issuer.decode()), \
           parse_subject_from_openssl_output(raw_subject.decode()), \
           parse_thumbprint_from_openssl_output(raw_fingerprint.decode()), \
           parse_not_before_from_openssl_output(raw_not_before.decode()), \
           parse_not_after_from_openssl_output(raw_not_after.decode())


def parse_thumbprint_from_openssl_output(raw_fingerprint):
    """Parses the thumbprint value from the raw OpenSSL output.

    Example output from openSSL:
    SHA1 Fingerprint=3B:C3:70:46:00:0C:B2:0B:F9:86:98:CF:9D:11:DF:EB:22:B7:41:F5

    Returns:
        string : The certificate thumbprint.
    """
    return raw_fingerprint.split("SHA1 Fingerprint=")[1].replace(":", "").strip()


def parse_issuer_from_openssl_output(raw_issuer):
    """Parses the issuer value from the raw OpenSSL output.

    For reference, openssl cmd has different format between openssl versions;

    OpenSSL 1.1.x formatting:
    issuer=C = US, ST = Some-State, O = Internet Widgits Pty Ltd
    issuer=C = CT, ST = "STA,STB", L = "LocalityA, LocalityB", O = Internet Widgits Pty Ltd

    OpenSSL 1.0.x  formatting:
    issuer= /C=US/ST=Some-State/O=Internet Widgits Pty Ltd
    issuer= /C=US/ST=STA,STB/L=LocalityA, LocatlityB/O=Internet Widgits Pty Ltd

    Returns:
        string : The certificate issuer.
    """
    return raw_issuer.split("issuer=")[1].strip()


def parse_subject_from_openssl_output(raw_subject):
    """Parses the subject value from the raw OpenSSL output.

    For reference, openssl cmd has different format between openssl versions;

    OpenSSL 1.1.x formatting:
    subject=C = CT, ST = "ST,Cs", L = "Locality, Locality", O = Internet Widgits Pty Ltd

    OpenSSL 1.0.x  formatting:
    subject= /C=US/ST=WA/L=Locality, Locality/O=Internet Widgits Pty Ltd

    Returns:
        string : The certificate subject.
    """
    return raw_subject.split("subject=")[1].strip()


def parse_not_before_from_openssl_output(raw_not_before):
    """Parses the not before value from the raw OpenSSL output.

    Example output from openSSL:
    notBefore=Jun 28 15:25:08 2022 GMT

    Returns:
        datetime : The certificate not before date.
    """
    not_before_date = raw_not_before.split("notBefore=")[1].replace("GMT", "").strip()
    datetime_object = datetime.strptime(not_before_date, '%b %d %H:%M:%S %Y')
    date_iso_format = datetime_object.isoformat()
    return date_iso_format


def parse_not_after_from_openssl_output(raw_not_after):
    """Parses the not after value from the raw OpenSSL output.

    Example output from openSSL:
    notAfter=Jun 30 15:25:08 2022 GMT

    Returns:
        datetime : The certificate not after date.
    """

    not_after_date = raw_not_after.split("notAfter=")[1].replace("GMT", "").strip()

    datetime_object = datetime.strptime(not_after_date, '%b %d %H:%M:%S %Y')
    date_iso_format = datetime_object.isoformat()
    return date_iso_format


@posix_only
def fork_and_exit_parent():
    """Forks and kills the parent process."""
    try:
        pid = os.fork()
        if pid > 0:
            print("parent process " + str(os.getpid()) + " exiting")
            sys.exit(0)
    except OSError as e:
        print("fork failed. " + str(e.message))
        sys.exit(1)


@posix_only
def daemonize():
    """Daemonize the current process by double forking and closing all file descriptors

    Note:
        One of the fork fails, the process will exit
    """
    # fork first child and exist parent
    fork_and_exit_parent()

    # decouple from parent environment
    os.setsid()

    process_std_fd = [sys.stdin.fileno(), sys.stdout.fileno(), sys.stderr.fileno()]
    for fd in process_std_fd:
        try:
            os.close(fd)
        except OSError:
            # fd is already closed
            pass

    fd_devnull = os.open(os.devnull, os.O_RDWR)
    for fd in process_std_fd:
        try:
            os.dup2(fd_devnull, fd)
        except OSError:
            pass

    # fork second child and exit parent
    fork_and_exit_parent()

@posix_only
def popen_communicate(command, shell=False):
    """Issues a process open followed by a communicate call.

    Args:
        command : array, the process command
        shell :   shell, if true the specified command will be executed through the shell

    Returns:
        The process, output and error tuple
    """
    process = subprocess.Popen(command, shell=shell, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    output, error = process.communicate()
    return process, output, error


def set_permission_recursive(permission, path):
    """Sets the permission for a specific path and it's child items recursively.

    Args:
        permission  : string, linux permission (i.e 770).
        path        : string, the target path.
    """
    cmd = ["sudo", "chmod", "-R", permission, path]
    process, output, error = popen_communicate(cmd)
    error = error.decode() if isinstance(error, bytes) else error
    if process.returncode != 0:
        raise Exception(
            "Unable to change permission of " + str(path) + " to " + str(permission) + ". Error : " + str(error))
    print("Permission changed to " + str(permission) + " for " + str(path))


def set_user_and_group_recursive(owning_username, owning_group_name, path):
    """Sets the owner for a specific path and it's child items recursively.

    Args:
        owning_username     : string, the owning user
        owning_group_name   : string, the owning group
        path                : string, the target path.
    """
    owners = owning_username + ":" + owning_group_name
    cmd = ["sudo", "chown", "-R", owners, path]
    process, output, error = popen_communicate(cmd)
    error = error.decode() if isinstance(error, bytes) else error
    if process.returncode != 0:
        raise Exception("Unable to change owner of " + str(path) + " to " + str(owners) + ". Error : " + str(error))
    print("Owner changed to " + str(owners) + " for " + str(path))


class ProcessModel(object):
    def __init__(self, process_info):
        """FORMAT : ['UID', 'PID', 'PPID', 'PGID', 'SID', 'C', 'STIME', 'TTY', 'TIME', 'CMD']"""
        self.uid = process_info[0]
        self.pid = int(process_info[1])
        self.ppid = int(process_info[2])
        self.pgid = int(process_info[3])
        self.sid = int(process_info[4])
        self.c = process_info[5]
        self.stime = process_info[6]
        self.tty = process_info[7]
        self.time = process_info[8]
        self.cmd = " ".join(process_info[9:])

    def __str__(self):
        return " ".join([self.uid,
                         self.pid,
                         self.ppid,
                         self.pgid,
                         self.sid,
                         self.c,
                         self.stime,
                         self.tty,
                         self.time,
                         self.cmd])
