#!/bin/bash

yum -y install yum-plugin-security &> /dev/null
yum -q --security check-update
