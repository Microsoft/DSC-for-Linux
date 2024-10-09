#!/bin/bash

if [ "$1" = "" ]; then
    yum check-update
    exit ${PIPESTATUS[0]}   
else
    if [ -e "/usr/bin/repoquery" ]; then
	    repoquery --queryformat "Name : %{NAME}\nArch : %{ARCH}\nVersion :  %{EPOCH}:%{VERSION}\nRelease : %{RELEASE}\nRepo : %{REPO}\nBuildtime : %{BUILDTIME}" $*
    else
	    yum info available $*
    fi
fi
