#!/bin/bash

if [ "$#" -ne 0 ]
then
	# Checking which python version is available 
	# python3
	if [! -z $(python3 -V 2>&1 | grep -Po '(?<=Python )(.+)') ]
	then
		#echo "python3 exists"; 
		pythonVer="python3"
	elif [! -z $(python2 -V 2>&1 | grep -Po '(?<=Python )(.+)') ]
	then
		#echo "python2 exists"; 
		pythonVer="python2"
	else 
		echo "'python2' or 'python3' not found on this machine. Please install python."
		exit 1
	fi

	# Detemine file location
	echo "You provided the following argument(s): $@"
	if [ "$pythonVer" == "python2" ] 
	then
		filePath="/opt/microsoft/omsconfig/Scripts/"
	else
		filePath="/opt/microsoft/omsconfig/Scripts/python3/"
	fi

	# Construct command from arguments
	for args in "$@"; do
		filePath+=$args
		filePath+=" "
	done
	echo "The path provided is: "
	echo $(ls $filePath)
	
	# Construct command
	echo "sudo su omsagent -c '$pythonVer $filePath'"
	sudo su omsagent -c "$pythonVer $filePath"
else
	echo "No parameters were passed."
fi

