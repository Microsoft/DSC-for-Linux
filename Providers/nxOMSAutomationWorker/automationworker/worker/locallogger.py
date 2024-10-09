#!/usr/bin/env python2
#
# Copyright (C) Microsoft Corporation, All rights reserved.

"""Local Logger module."""

import configuration

import sys
import os
import logging
import logging.handlers
import time

default_logger = None
sandbox_stdout = None


def init_logger():
    global default_logger, sandbox_stdout

    sandbox_id = None
    try:
        sandbox_id = os.environ["sandbox_id"]
    except KeyError:
        pass

    if sandbox_id is not None:
        log_file_name = configuration.get_component() + sandbox_id
    else:
        log_file_name = configuration.get_component()

    file_name = os.path.join(configuration.get_working_directory_path(), log_file_name + '.log')
    logging.Formatter.converter = time.gmtime

    # Default logger
    default_logger = logging.getLogger("default_logger")
    default_logger.setLevel(logging.INFO)

    # Logger for the sandbox traces coming back to worker
    sandbox_stdout = logging.getLogger("sandbox_stdout_logger")
    sandbox_stdout.setLevel(logging.INFO)

    # Default rotating file handler write traces with the specified format to disk.
    default_rf_handler = logging.handlers.RotatingFileHandler(file_name, maxBytes=10485760, backupCount=5)
    formatter = logging.Formatter('%(asctime)s (' + str(os.getpid()) + ')' + configuration.get_component() +
                                  ' : %(message)s', datefmt="%Y-%m-%d %H:%M:%S")
    default_rf_handler.setFormatter(formatter)
    default_logger.addHandler(default_rf_handler)

    # Stdout handler (Worker traces have to be formatted).
    #log_stream = logging.StreamHandler(sys.stdout)
    #log_stream.setFormatter(formatter)
    #default_logger.addHandler(log_stream)

    # Stdout handler (traces coming from child process are already formatted).
    #sandbox_log_stream = logging.StreamHandler(sys.stdout)
    #sandbox_stdout.addHandler(sandbox_log_stream)

    # Traces coming from sandbox child process and collected by the worker are already formatted, hence no formatter
    # needed.
    worker_sandbox_rf_handler = logging.handlers.RotatingFileHandler(file_name, maxBytes=10485760, backupCount=5)
    sandbox_stdout.addHandler(worker_sandbox_rf_handler)


def log_warning(message):
    if default_logger is None:
        init_logger()
    try:
        default_logger.warning(message)
    except Exception, e:
        print "Exception %s while printing the log %s" %(str(e), message)


def log_info(message):
    if default_logger is None:
        init_logger()
    try:
        default_logger.info(message)
    except Exception, e:
        print "Exception %s while printing the log %s" %(str(e), message)