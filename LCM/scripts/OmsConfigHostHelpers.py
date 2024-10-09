#!/usr/bin/python2
import json
import time
import datetime
import os
import math
import signal

def write_omsconfig_host_telemetry(message, pathToCurrentScript='', level = 'INFO'):
    omsagent_telemetry_path = '/var/opt/microsoft/omsconfig/status'
    dsc_host_telemetry_path = os.path.join(omsagent_telemetry_path, 'omsconfighost')

    if not os.path.exists(omsagent_telemetry_path):
        os.makedirs(omsagent_telemetry_path)

    if os.path.isfile(dsc_host_telemetry_path):
        with open(dsc_host_telemetry_path) as host_telemetry_file:
            try:
                host_telemetry_json = json.load(host_telemetry_file)
            except:
                host_telemetry_json = {}
                host_telemetry_json['operation'] = 'omsconfighost'
                host_telemetry_json['message'] = ''
                host_telemetry_json['success'] = 1
    else:
        os.mknod(dsc_host_telemetry_path)
        host_telemetry_json = {}
        host_telemetry_json['operation'] = 'omsconfighost'
        host_telemetry_json['message'] = ''
        host_telemetry_json['success'] = 1

    msg_template = '<OMSCONFIGLOG>[%s] [%d] [%s] [%d] [%s:%d] %s</OMSCONFIGLOG>'
    timestamp = datetime.datetime.fromtimestamp(time.time()).strftime('%Y/%m/%d %H:%M:%S')
    msg_buffer = msg_template % (timestamp, os.getpid(), level, 0, pathToCurrentScript, 0, json.dumps(message))

    host_telemetry_json['message'] += msg_buffer

    with open(dsc_host_telemetry_path, 'w+') as host_telemetry_file:
        json.dump(host_telemetry_json, host_telemetry_file)

def get_agent_id():
    agentid_path = "/etc/opt/omi/conf/omsconfig/agentid"
    if os.path.isfile(agentid_path):
        with open(agentid_path, "r") as agentid_file:
            agent_id = agentid_file.read(36) # read 36 characters from the file
            return agent_id
    return "00000000-0000-0000-0000-000000000000"

def write_omsconfig_host_switch_event(pathToCurrentScript, dsc_host_switch_exists):
    if dsc_host_switch_exists:
        message = 'Using dsc_host'
    else:
        message = 'Falling back to OMI'
    telemetry_message = '[%s] %s' % (get_agent_id(), message)
    write_omsconfig_host_telemetry(telemetry_message, pathToCurrentScript)

def write_omsconfig_host_log(message, pathToCurrentScript, level = 'INFO'):
    log_entry_template = '[%s] [%d] [%s] [%d] [%s:%d] %s\n'
    timestamp = datetime.datetime.fromtimestamp(time.time()).strftime('%Y/%m/%d %H:%M:%S')
    log_entry = log_entry_template % (timestamp, os.getpid(), level, 0, pathToCurrentScript, 0, message)

    omsconfig_log_folder = '/var/opt/microsoft/omsconfig'
    if not os.path.exists(omsconfig_log_folder):
        os.makedirs(omsconfig_log_folder)

    print(log_entry)

    omsconfig_log_path = os.path.join(omsconfig_log_folder, 'omsconfig.log')
    with open(omsconfig_log_path, 'a+') as omsconfig_log_file:
        omsconfig_log_file.write(log_entry)

    omsconfig_detailed_log_path = os.path.join(omsconfig_log_folder, 'omsconfigdetailed.log')
    with open(omsconfig_detailed_log_path, 'a+') as omsconfig_detailed_log_file:
        omsconfig_detailed_log_file.write(log_entry)
    
    if (level == 'ERROR') or (level == 'WARNING') or (level == 'FATAL'):
        write_omsconfig_host_telemetry(message, pathToCurrentScript, level)

def stop_old_host_instances(dsc_host_lock_path):
    dsc_host_pid_path = '/opt/dsc/bin/dsc_host.pid'

    last_host_pid = 0

    if os.path.isfile(dsc_host_pid_path):
        with open(dsc_host_pid_path) as dsc_host_pid_file:
            try:
                last_host_pid = dsc_host_pid_file.read()
            except:
                pass
    
    if last_host_pid == 0:
        return
    
    # Timestamps are measured in seconds since epoch
    host_pid_last_modified_time = os.path.getmtime(dsc_host_pid_path)
    current_time = math.floor(time.time())
    timestamp_diff = current_time - host_pid_last_modified_time

    if (timestamp_diff < 0):
        return
    
    # If file was last modified more than 3 hours ago, we will kill the process
    if (timestamp_diff > 3600 * 3):
        try:
            write_omsconfig_host_log('Killing dsc_host with pid = ' + str(last_host_pid) + ' since it is older than 3 hours.', 'stop_old_host_instances', 'WARNING')
            os.kill(last_host_pid, signal.SIGTERM)
            if (os.path.exists(dsc_host_lock_path)):
                os.remove(dsc_host_lock_path)
            write_omsconfig_host_log('Killed dsc_host with pid = ' + str(last_host_pid) + ' since it was taking longer than 3 hours.', 'stop_old_host_instances', 'WARNING')
        except:
            pass
