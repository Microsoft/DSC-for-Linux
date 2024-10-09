/*
   PowerShell Desired State Configuration for Linux

   Copyright (c) Microsoft Corporation

   All rights reserved. 

   MIT License

   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the ""Software""), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED *AS IS*, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "EngineHelper.h"
#include "ModuleHandler.h"
#include "OMI_LocalConfigManagerHelper.h"
#include "LocalConfigManagerTaskStrings.h"
#include "LocalConfigManagerHelper.h"
#include "Resources_LCM.h"
#include <errno.h>
#include "EventWrapper.h"
#include "DSC_Systemcalls.h"
#include "common.h"

#if defined(MSC_VER)
#else
#include <stdio.h>
#include <stdlib.h>
#endif

// some constants, probably defined in EngineHelperInternal.h later
#define CRON_FILE	MI_T("/etc/cron.d/dsc")
#define CRON_FILE_TMP	MI_T("/etc/crontabtmp")
#define UNIT_LINE_SIZE	256

MI_Result UpdateTask(
    _In_z_ MI_Char* taskName,
    _In_z_ MI_Char* taskTime,
    _In_ MI_Uint32 refreshFrequencyInSeconds,
    _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    int retValue = -1;
    ssize_t read = 0;
    size_t readLength = 0, writeLength = 0;
	MI_Char *line = NULL, *lineToWrite = NULL, *taskInCrontab = NULL;
	FILE *cronFile = NULL, *cronFileTmp = NULL;
	MI_Uint32 errorCode = 0;

	if (extendedError == NULL)
	{
		return MI_RESULT_INVALID_PARAMETER;
	}
	*extendedError = NULL;


    // Preparation
    if (refreshFrequencyInSeconds > THIRTY_ONE_DAYS_IN_SECONDS)
    {
		refreshFrequencyInSeconds = THIRTY_ONE_DAYS_IN_SECONDS;
    }
    if (refreshFrequencyInSeconds < REFRESH_MINIMUM_IN_SECONDS)
    {
		refreshFrequencyInSeconds = REFRESH_MINIMUM_IN_SECONDS;
    }

    // PLAN 1 -- process each line of crontab file to a tmp file and then copy tmp file back
    // taskTime string is not used in Linux since we can simply register a task with frequency

	// Allocate memory
	line = (MI_Char *)DSC_malloc(UNIT_LINE_SIZE * sizeof(MI_Char), NitsHere());
	if (line == NULL)
	{
		errorCode = ID_LCMHELPER_MEMORY_ERROR;
		goto ExitWithError;
	}
    lineToWrite = (MI_Char *)DSC_malloc(UNIT_LINE_SIZE * sizeof(MI_Char), NitsHere());
	if (lineToWrite == NULL)
	{
		errorCode = ID_LCMHELPER_MEMORY_ERROR;
		goto ExitWithError;
	}
	taskInCrontab = (MI_Char *)DSC_malloc(UNIT_LINE_SIZE * sizeof(MI_Char), NitsHere());
	if (taskInCrontab == NULL)
	{
		errorCode = ID_LCMHELPER_MEMORY_ERROR;
		goto ExitWithError;
	}

	// Open files
	if (File_ExistT(CRON_FILE) != -1)
	{
		cronFile = File_OpenT(CRON_FILE, MI_T("r"));
		if (cronFile == NULL)
		{
			// Fail to open existing cron file should lead to error exit
			errorCode = ID_ENGINEHELPER_OPENFILE_ERROR;
			goto ExitWithError;
		}
	}
    cronFileTmp = File_OpenT(CRON_FILE_TMP, MI_T("w"));
    if (cronFileTmp == NULL)
	{
		// Fail to create tmp file should lead to error exit
		errorCode = ID_LCMHELPER_CREATEFILE_ERROR;
		goto ExitWithError;
    }

	// Read and process crontab file if it exists and opens appropriately
    while (cronFile != NULL && (read = readline(&line, &readLength, cronFile)) != -1)
    {
        retValue = TcsStrlcpy(lineToWrite, line, Tcslen(line)+1);
		//Assuming size will be not more than 256 which is the value of UNIT_LINE_SIZE
		retValue = sscanf(line, MI_T("%*s %*s %*s %*s %*s %*s %256s"), taskInCrontab);
		if (retValue == 0)
		{
			// Ignore the bad line that does not comply with crontab file format
			continue;
		}
		else
		{
			if (Tcsncasecmp(taskName, taskInCrontab, Tcslen(taskName)) == 0)
			{
				// Ignore entry that duplicates registration of task
				continue;
			}
			else
			{
				// Write the entry to tmp file
				writeLength = fwrite(lineToWrite, 1, Tcslen(lineToWrite), cronFileTmp);
				if (writeLength != read)
				{
					errorCode = ID_LCMHELPER_WRITEFILE_ERROR;
					goto ExitWithError;
				}
			}
		}
    }
    if (readLength == -1)
	{
		// Deal memory failure in readline function
		errorCode = ID_ENGINEHELPER_READFILE_ERROR;
		goto ExitWithError;
	}

    // Add the task entry
    retValue = Stprintf(lineToWrite, UNIT_LINE_SIZE, MI_T("*/%d * * * * root %T\n"), refreshFrequencyInSeconds / 60, taskName);
	if (retValue == -1)
	{
		errorCode = ID_LCMHELPER_PRINTF_ERROR;
		goto ExitWithError;
	}
    writeLength = fwrite(lineToWrite, 1, Tcslen(lineToWrite), cronFileTmp);
	if (writeLength != Tcslen(lineToWrite))
	{
		errorCode = ID_LCMHELPER_WRITEFILE_ERROR;
		goto ExitWithError;
	}

	if (cronFile != NULL)
		File_Close(cronFile);
    File_Close(cronFileTmp);

    File_CopyT(CRON_FILE_TMP, CRON_FILE);
    File_RemoveT(CRON_FILE_TMP);    
    
    DSC_free(line);
    DSC_free(lineToWrite);
	DSC_free(taskInCrontab);

    return MI_RESULT_OK;

ExitWithError:
	// Close files and remove temp file if it exists
	if (cronFile)
		File_Close(cronFile);
	if (cronFileTmp)
		File_Close(cronFileTmp);
    if (File_ExistT(CRON_FILE_TMP) != -1)
        File_RemoveT(CRON_FILE_TMP);
	
	// Free memory allocations
	if (line != NULL)
		DSC_free(line);
	if (lineToWrite != NULL)
		DSC_free(lineToWrite);
	if (taskInCrontab != NULL)
		DSC_free(taskInCrontab);

	// Form rich CIM error
	if (errorCode != 0)
		return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, extendedError, errorCode);
	else
		return MI_RESULT_FAILED;
}

ssize_t readline(MI_Char **lineptr, size_t *n, FILE *stream)
{
	MI_Char ch;
	MI_Char *line = *lineptr;
	size_t allocated = *n;
	size_t length = 0;

	do {
		ch = fgetc(stream);
		if (ch == EOF)
			break;
		if (length + 1 >= allocated)
		{
			allocated += UNIT_LINE_SIZE;
			line = (MI_Char *)DSC_realloc(line, allocated * sizeof(MI_Char), NitsHere());
			if (line == NULL)
			{
				allocated = -1;
				break;
			}
		}
		line[length++] = ch;
	} while (ch != '\n');

	*n = allocated;

	if (length == 0 || allocated == -1)
		return -1;

	line[length] = '\0';
	*lineptr = line;
	return length;
}
