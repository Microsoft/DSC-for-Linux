#!/usr/bin/env python2
#
# Copyright (C) Microsoft Corporation, All rights reserved.

"""Utility for DIY hybrid worker directories"""


import linuxutil
import os

NXAUTOMATION_HOME_DIR = "/home/nxautomation"
DIY_STATE_PATH = os.path.join(NXAUTOMATION_HOME_DIR, "state")
DIY_WORKING_DIR = os.path.join(NXAUTOMATION_HOME_DIR , "run")


def create_persistent_diy_dirs():
    if not os.path.exists(DIY_STATE_PATH):
        process, output, error = linuxutil.popen_communicate(["sudo", "mkdir", "-p", "-m", "770", DIY_STATE_PATH])
        if process.returncode != 0:
            raise Exception("Unable to create dir " + DIY_STATE_PATH + ". Error: " + str(error))

    if not os.path.exists(DIY_WORKING_DIR):
        process, output, error = linuxutil.popen_communicate(["sudo", "mkdir", "-p", "-m", "770", DIY_WORKING_DIR])
        if process.returncode != 0:
            raise Exception("Unable to create dir " + DIY_WORKING_DIR + ". Error: " + str(error))


    linuxutil.set_user_and_group_recursive(owning_username="omsagent", owning_group_name="omiusers",
                                           path=NXAUTOMATION_HOME_DIR)
    linuxutil.set_permission_recursive(permission="770", path=NXAUTOMATION_HOME_DIR)

    linuxutil.set_user_and_group_recursive(owning_username="nxautomation", owning_group_name="omiusers",
                                           path=DIY_WORKING_DIR)
    linuxutil.set_permission_recursive(permission="770", path=DIY_WORKING_DIR)
