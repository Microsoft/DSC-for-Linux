#!/usr/bin/env python
#============================================================================
# Copyright (C) Microsoft Corporation, All rights reserved. 
#============================================================================

import os
import imp
import re
import codecs
import shutil
import string
import filecmp

protocol = imp.load_source('protocol', '../protocol.py')
nxDSCLog = imp.load_source('nxDSCLog', '../nxDSCLog.py')

LG = nxDSCLog.DSCLog

RESOURCE_DIR = '/opt/microsoft/omsconfig/modules/nxOMSSudoCustomLog/DSCResources/MSFT_nxOMSSudoCustomLogResource/'
RESOURCE_PLUGIN_DIR = RESOURCE_DIR + 'CustomLog/Plugin/'
PLUGIN = RESOURCE_PLUGIN_DIR + 'in_sudo_tail.rb'
SCRIPT = RESOURCE_PLUGIN_DIR + 'tailfilereader.rb'
AGENT_PLUGIN_DIR = '/opt/microsoft/omsagent/plugin/'
AGENT_PLUGIN = AGENT_PLUGIN_DIR + 'in_sudo_tail.rb'
AGENT_SCRIPT = AGENT_PLUGIN_DIR + 'tailfilereader.rb'

conf_path = '/etc/opt/microsoft/omsagent/{0}/conf/omsagent.d/customlog.conf'
oms_restart_cmd = 'sudo /opt/microsoft/omsagent/bin/service_control restart {0}'

'''
[ClassVersion("1.0.0")]
class MSFT_nxOMSSudoCustomLogObject
{
    [key] string LogName;
    [write] string FilePath[];
};

[ClassVersion("1.0.0")]
class MSFT_nxOMSSudoCustomLogResource : OMI_BaseResource
{
    [key] string Name;
    [key] string WorkspaceID;
    [write] boolean EnableCustomLogConfiguration;
    [write,ValueMap{"Present", "Absent"},Values{"Present", "Absent"}] string Ensure;
    [write, EmbeddedInstance("MSFT_nxOMSSudoCustomLogObject") : ToSubclass] string CustomLogObjects[];
};
'''

def init_vars(WorkspaceID, Ensure, CustomLogObjects):
    if WorkspaceID is not None:
        WorkspaceID = WorkspaceID.encode('ascii', 'ignore')
    else:
        WorkspaceID = ''

    if 'value' in dir(Ensure):
        Ensure = Ensure.value
    Ensure = Ensure.encode('ascii', 'ignore')
 
    new_customlogs = []
    if CustomLogObjects is not None :
        for customlog in CustomLogObjects:
            if 'value' in dir(customlog['LogName']):
                customlog['LogName'] = customlog['LogName'].value.encode('ascii', 'ignore')
                new_filepaths = []
               
                if 'value' in dir(customlog['FilePath']):    
                    for filepath in customlog['FilePath'].value:
                        if filepath is not None and len(filepath) > 0:
                            new_filepaths.append(filepath.encode('ascii', 'ignore'))

                if len(new_filepaths) > 0:
                    customlog['FilePath'] = new_filepaths
                    new_customlogs.append(customlog)

    CustomLogObjects = new_customlogs

    # The minimum agent version that this module can be applied to is >1.3, so
    # all paths and commands will be workspace-specific
    global conf_path
    global oms_restart_cmd
    conf_path = conf_path.format(WorkspaceID)
    oms_restart_cmd = oms_restart_cmd.format(WorkspaceID)

def Set_Marshall(WorkspaceID, Name, Ensure, EnableCustomLogConfiguration = False, CustomLogObjects = None):
    init_vars(WorkspaceID, Ensure, CustomLogObjects)
    Set(EnableCustomLogConfiguration, Ensure, CustomLogObjects)
    return [0] 

def Test_Marshall(WorkspaceID, Name, Ensure, EnableCustomLogConfiguration = False, CustomLogObjects = None):
    init_vars(WorkspaceID, Ensure, CustomLogObjects)
    return Test(EnableCustomLogConfiguration, Ensure, CustomLogObjects)

def Get_Marshall(WorkspaceID, Name, Ensure, EnableCustomLogConfiguration = False, CustomLogObjects = None):
    arg_names = list(locals().keys()) 
    init_vars(WorkspaceID, Ensure, CustomLogObjects)
    Ensure, CurrentCustomLogObjects = Get(EnableCustomLogConfiguration, Ensure, CustomLogObjects)
    Name = protocol.MI_String(Name)
    WorkspaceID = protocol.MI_String(WorkspaceID)
    EnableCustomLogConfiguration = protocol.MI_Boolean(EnableCustomLogConfiguration)
    Ensure = protocol.MI_String(Ensure)
    for customlog in CurrentCustomLogObjects:
        customlog['LogName'] = protocol.MI_String(customlog['LogName'])
        if customlog['FilePath'] is not None and len(customlog['FilePath']):
            customlog['FilePath'] = protocol.MI_StringA(customlog['FilePath'])
    CustomLogObjects = protocol.MI_InstanceA(CurrentCustomLogObjects)
    retd = {}
    ld = locals()
    for k in arg_names:
        retd[k] = ld[k]
    return 0, retd
            
def Set(EnableCustomLogConfiguration, Ensure, CustomLogObjects):
    if Test(EnableCustomLogConfiguration, Ensure, CustomLogObjects) == [0]:
        return [0]
    if EnableCustomLogConfiguration and CustomLogObjects is not None:
        if os.path.isdir(RESOURCE_PLUGIN_DIR):
            CopyAllFiles(RESOURCE_PLUGIN_DIR, AGENT_PLUGIN_DIR)
    UpdateConf(CustomLogObjects)
    return [0]

def Test(EnableCustomLogConfiguration, Ensure, CustomLogObjects):
    if EnableCustomLogConfiguration:
        CurrentCustomLogObjects = ReadConf()
        if CurrentCustomLogObjects is None and CustomLogObjects is None:
            return [0]
        elif CurrentCustomLogObjects is None or CustomLogObjects is None:
            return [-1]

        CustomLogObjects = sorted(CustomLogObjects, key = lambda d : d['LogName'])
        for customlog in CustomLogObjects:
            customlog['FilePath'].sort()

        CurrentCustomLogObjects = sorted(CurrentCustomLogObjects, key = lambda d : d['LogName'])
        for customlog in CurrentCustomLogObjects:
            customlog['FilePath'].sort()

        if CustomLogObjects != CurrentCustomLogObjects:
            return [-1]

        if Ensure == "Absent":
            return [-1]

    return [0]

def Get(EnableCustomLogConfiguration, Ensure, CustomLogObjects):
    CurrentCustomLogObjects = ReadConf()
    return Ensure, CurrentCustomLogObjects 

'''
# this file is configured by the OMS service

<source>
  type tail
  path /tmp/cl_syslog
  pos_file /var/opt/microsoft/omsagent/state/CUSTOM_LOG_BLOB.dummysyslog_CL_817f36cf-991d-4b97-afc0-cb70ec349371.pos
  read_from_head true
  tag oms.blob.CustomLog.CUSTOM_LOG_BLOB.dummysyslog_CL_817f36cf-991d-4b97-afc0-cb70ec349371.*
  format none
</source>

<source>
  type tail
  path /tmp/customlog/*.log,/tmp/none.log
  pos_file /var/opt/microsoft/omsagent/state/CUSTOM_LOG_BLOB.WildCardFilePath_CL_817f36cf-991d-4b97-afc0-cb70ec349371.pos
  read_from_head true
  tag oms.blob.CustomLog.CUSTOM_LOG_BLOB.WildCardFilePath_CL_817f36cf-991d-4b97-afc0-cb70ec349371.*
  format none
</source>
'''

def ReadConf():
    if not os.path.isfile(conf_path):
        return [];
    txt = codecs.open(conf_path, 'r', 'utf8').read().encode('ascii','ignore')
    customlog_src_srch_str = r'\n<source>\n  type sudo_tail.*?path (.*?)\n.*?pos_file /var/opt/microsoft/omsagent/state/(.*?)\.pos\n.*?run_interval ([0-9]+[a-z]*).*?tag oms\.blob\.CustomLog\.(.*?)\.\*.*?format none.*?</source>\n'
    customlog_src_srch = re.compile(customlog_src_srch_str, re.M|re.S)
    new_customlogs = []
    sources = customlog_src_srch.findall(txt)
    for source in sources:
        s_filepaths = []
        if len(source[0]):
            s_filepaths = source[0].split(',')
        logname = source[1]
        new_customlogs.append({'FilePath':s_filepaths,'LogName':logname})
    return new_customlogs

'''
sources = [('/tmp/cl_syslog', 'CUSTOM_LOG_BLOB.dummysyslog_CL_817f36cf-991d-4b97-afc0-cb70ec349371', 'CUSTOM_LOG_BLOB.dummysyslog_CL_817f36cf-991d-4b97-afc0-cb70ec349371'), ('/tmp/customlog/*.log,/tmp/none.log', 'CUSTOM_LOG_BLOB.WildCardFilePath_CL_817f36cf-991d-4b97-afc0-cb70ec349371', 'CUSTOM_LOG_BLOB.WildCardFilePath_CL_817f36cf-991d-4b97-afc0-cb70ec349371')]

new_customlogs = [{'LogName': 'CUSTOM_LOG_BLOB.dummysyslog_CL_817f36cf-991d-4b97-afc0-cb70ec349371', 'FilePath': ['/tmp/cl_syslog']}, {'LogName': 'CUSTOM_LOG_BLOB.WildCardFilePath_CL_817f36cf-991d-4b97-afc0-cb70ec349371', 'FilePath': ['/tmp/customlog/*.log', '/tmp/none.log']}]
'''

def UpdateConf(CustomLogObjects):
    header = '# This file is configured by the OMS service\n'
    new_source = ''
    if CustomLogObjects is not None:
        for customlog in CustomLogObjects:
            logname = customlog['LogName']
            filepaths = ','.join(customlog['FilePath'])
            new_source += '\n<source>\n  type sudo_tail\n  path ' + filepaths + '\n  pos_file /var/opt/microsoft/omsagent/state/' + logname + '.pos\n  read_from_head false\n  run_interval 60\n  tag oms.blob.CustomLog.' + logname + '.*\n  format none\n</source>\n'
    txt = header + new_source
    if os.path.isfile(conf_path):
        shutil.copy2(conf_path, conf_path + '.bak')
    codecs.open(conf_path, 'w', 'utf8').write(txt)
    os.system(oms_restart_cmd)

def CopyAllFiles(src, dest):
    try:
        src_files = os.listdir(src)
        for file_name in src_files:
            full_file_name = os.path.join(src, file_name)
            if (os.path.isfile(full_file_name)):
                shutil.copy(full_file_name, dest)
    except:
        LG().Log('ERROR', 'CopyAllFiles failed for src: ' + src + ' dest: ' + dest)
        return False
            
def CompareFiles(file1, file2):
    return filecmp.cmp(file1, file2) 
