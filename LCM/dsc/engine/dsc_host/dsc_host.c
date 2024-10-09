/*
   PowerShell Desired State Configuration for Linux

   Copyright (c) Microsoft Corporation

   All rights reserved. 

   MIT License

   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the ""Software""), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED *AS IS*, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <unistd.h>

#define _POSIX_SOURCE
#include <sys/stat.h>
#undef _POSIX_SOURCE

#include "DSC_Systemcalls.h"
#include "Resources_LCM.h"
#include "EngineHelper.h"
#include "EventWrapper.h"
#include "dsc_host.h"
#include "dsc_library.h"
#include "lcm/strings.h"

#if defined(BUILD_OMS)
extern MI_Boolean g_DscHost;
#endif

MI_Result GetDSCHostVersion(_In_z_ MI_Char* version, _In_ size_t length)
{
    if(Stprintf(version, length, MI_T("Version : %d.%d.%04d"), MajorVersion, MinorVersion, BuildVersion) < 0)
        return MI_RESULT_FAILED;
    else
        return MI_RESULT_OK;
}

void PrintVersion()
{
    MI_Char versionInfo[DSCHOST_VERSION_BUFF_SIZE];
    GetDSCHostVersion(versionInfo, DSCHOST_VERSION_BUFF_SIZE);
    Tprintf(versionInfo);
}

void PrintHelp()
{
    Tprintf(MI_T("Usage:\n"));
    Tprintf(MI_T("dsc_host [--help] [--version]\n"));
    Tprintf(MI_T("dsc_host <Output Folder Path> <Operation> [Operation Arguments] \n"));
    Tprintf(MI_T("\n"));
    Tprintf(MI_T("Supported Operation values are:\n"));
    Tprintf(MI_T("  SendConfiguration <MOF Document Path>\n"));
    Tprintf(MI_T("  SendConfigurationApply <MOF Document Path>\n"));
    Tprintf(MI_T("  GetConfiguration [Configuration Document Path]\n"));
    Tprintf(MI_T("  ApplyConfiguration\n"));
    Tprintf(MI_T("  SendMetaConfigurationApply <MOF Document Path> [force]\n"));
    Tprintf(MI_T("  GetMetaConfiguration\n"));
    // Tprintf(MI_T("  RollBack\n")); // Commented out since we do not support this operation. The operation exists as is. No change in behavior.
    Tprintf(MI_T("  TestConfiguration\n"));
    Tprintf(MI_T("  PerformRequiredConfigurationChecks [flags value]\n"));
    Tprintf(MI_T("  StopConfiguration [force]\n"));
    Tprintf(MI_T("  PerformInventory\n"));
    Tprintf(MI_T("  PerformInventoryOOB [MOF Document Path]\n"));
    Tprintf(MI_T("\n"));
    Tprintf(MI_T("Examples:\n"));
    Tprintf(MI_T("  /opt/dsc/bin/dsc_host /opt/dsc/output SendConfiguration ./new_config.mof\n"));
    Tprintf(MI_T("  /opt/dsc/bin/dsc_host /opt/dsc/output SendConfigurationApply ./new_config.mof\n"));
    Tprintf(MI_T("  /opt/dsc/bin/dsc_host /opt/dsc/output GetConfiguration ./GetAuditPolicy.mof\n"));
    Tprintf(MI_T("  /opt/dsc/bin/dsc_host /opt/dsc/output ApplyConfiguration\n"));
    Tprintf(MI_T("  /opt/dsc/bin/dsc_host /opt/dsc/output SendMetaConfigurationApply ./new_metaconfig.mof\n"));
    Tprintf(MI_T("  /opt/dsc/bin/dsc_host /opt/dsc/output GetMetaConfiguration\n"));
    // Tprintf(MI_T("  /opt/dsc/bin/dsc_host /opt/dsc/output RollBack\n"));
    Tprintf(MI_T("  /opt/dsc/bin/dsc_host /opt/dsc/output TestConfiguration\n"));
    Tprintf(MI_T("  /opt/dsc/bin/dsc_host /opt/dsc/output PerformRequiredConfigurationChecks 1\n"));
    Tprintf(MI_T("  /opt/dsc/bin/dsc_host /opt/dsc/output StopConfiguration force\n"));
    Tprintf(MI_T("  /opt/dsc/bin/dsc_host /opt/dsc/output PerformInventory\n"));
    Tprintf(MI_T("  /opt/dsc/bin/dsc_host /opt/dsc/output PerformInventoryOOB ./Inventory.mof\n"));
    Tprintf(MI_T("\n"));
}

void SaveCurrentPID()
{
    int current_pid = getpid();

    FILE * fd = fopen(DSCHOST_PID_FILE_PATH, "w+");
    if (fd == NULL)
    {
        Tprintf(MI_T("Failed to open file '%T' with errno = %d (%T)\n"), DSCHOST_PID_FILE_PATH, errno, strerror(errno));
        DSC_TELEMETRY_ERROR("Failed to open file '%s' with errno = %d (%s)\n", DSCHOST_PID_FILE_PATH, errno, strerror(errno));
    }
    else
    {
        Ftprintf(fd, ZT("%d"), current_pid);
        fclose(fd);
    }
}

int main(int argc, char *argv[])
{
    MI_Instance *extended_error = NULL;
    MI_Result result = MI_RESULT_OK;
    DscSupportedOperation current_operation = DscSupportedOperation_NOP;
    JSON_Value *operation_result_root_value = NULL;
    JSON_Value *operation_error_root_value = NULL;
    char* operation_name;

    // Set umask to 0022
    umask(S_IWGRP | S_IWOTH);

    SaveCurrentPID();

    // Check the user that has invoked the operation: root for DIY and omsagent for OMS
#if defined(BUILD_OMS)
    if (RunningAsRoot() == MI_TRUE)
    {
        Tprintf(MI_T("Unable to run omsconfig configurations as root. Please use omsagent credentials.\n"));
        result = MI_RESULT_FAILED;
        CreateMiInstanceErrorObject(&extended_error, MI_T("Unable to run omsconfig configurations as root. Please use omsagent credentials.\n"));
        JSON_Value *value;
        Convert_MIInstance_JSON(extended_error, &operation_error_root_value);
        goto CleanUp;
    }
#else
    if (RunningAsRoot() == MI_FALSE)
    {
        Tprintf(MI_T("Unable to run dsc configurations as non-root.  Please use root credentials.\n"));
        result = MI_RESULT_FAILED;
        CreateMiInstanceErrorObject(&extended_error, MI_T("Unable to run dsc configurations as non-root. Please use root credentials.\n"));
        JSON_Value *value;
        Convert_MIInstance_JSON(extended_error, &operation_error_root_value);
        goto CleanUp;
    }
#endif

#if defined(BUILD_OMS)
    g_DscHost = MI_TRUE;
#endif

    if(argc < 3)
    {
        if(argc > 1 && 0 == Tcscasecmp(argv[1], MI_T("--version")))
        {
            PrintVersion();
        }
        else
        {
            PrintHelp();
        }
        return result;
    }

    // Checking for operation
    if ( Tcscasecmp(argv[2], DSC_OPERATION_GET_CONFIGURATION_STR) == 0 )
    {
        current_operation = DscSupportedOperation_GetConfiguration;
    }
    else
    if ( Tcscasecmp(argv[2], DSC_OPERATION_TEST_CONFIGURATION_STR) == 0 )
    {
        current_operation = DscSupportedOperation_TestConfiguration;
    }
    else
    if ( Tcscasecmp(argv[2], DSC_OPERATION_PERFORM_INVENTORY_STR) == 0 )
    {
        current_operation = DscSupportedOperation_PerformInventory;
    }
    else
    if ( Tcscasecmp(argv[2], DSC_OPERATION_PERFORM_INVENTORY_OOB_STR) == 0 )
    {
        current_operation = DscSupportedOperation_PerformInventoryOOB;
    }
    else
    if ( Tcscasecmp(argv[2], DSC_OPERATION_SEND_CONFIGURATION_STR) == 0 )
    {
        current_operation = DscSupportedOperation_SendConfiguration;
    }
    else
    if ( Tcscasecmp(argv[2], DSC_OPERATION_SEND_CONFIGURATION_APPLY_STR) == 0 )
    {
        current_operation = DscSupportedOperation_SendConfigurationApply;
    }
    else
    if ( Tcscasecmp(argv[2], DSC_OPERATION_SEND_METACONFIGURATION_APPLY_STR) == 0 )
    {
        current_operation = DscSupportedOperation_SendMetaConfigurationApply;
    }
    else
    if ( Tcscasecmp(argv[2], DSC_OPERATION_GET_METACONFIGURATION_STR) == 0 )
    {
        current_operation = DscSupportedOperation_GetMetaConfiguration;
    }
    else
    if ( Tcscasecmp(argv[2], DSC_OPERATION_APPLY_CONFIGURATION_STR) == 0 )
    {
        current_operation = DscSupportedOperation_ApplyConfiguration;
    }
    else
    if ( Tcscasecmp(argv[2], DSC_OPERATION_ROLLBACK_STR) == 0 )
    {
        current_operation = DscSupportedOperation_RollBack;
    }
    else
    if ( Tcscasecmp(argv[2], DSC_OPERATION_PERFORM_REQUIRED_CONFIGURATION_CHECKS_STR) == 0 )
    {
        current_operation = DscSupportedOperation_PerformRequiredConfigurationChecks;
    }
    else
    if ( Tcscasecmp(argv[2], DSC_OPERATION_STOP_CONFIGURATION_STR) == 0 )
    {
        current_operation = DscSupportedOperation_StopConfiguration;
    }
    else
    {
        Tprintf(MI_T("Operation %T is not supported\n"), argv[2]);
        result = MI_RESULT_FAILED;
        CreateMiInstanceErrorObject(&extended_error, MI_T("Operation %T is not supported\n"), argv[2]);
        JSON_Value *value;
        Convert_MIInstance_JSON(extended_error, &operation_error_root_value);
        goto CleanUp;
    }

    DSC_TELEMETRY_INFO("dsc_host starting operation '%s'", argv[2]);
    DSC_TELEMETRY_INFO("dsc_host configuration '%s'", argv[3]);
    switch(current_operation)
    {
        case DscSupportedOperation_GetConfiguration:
            {
                operation_name = DSC_OPERATION_GET_CONFIGURATION_STR;
                result = DscLib_GetConfiguration (&operation_result_root_value, argv[3], &operation_error_root_value); // CodeQL [cpp/path-injection] Safe Path: Currently only known paths are considered
                break;
            }
        case DscSupportedOperation_TestConfiguration:
            {
                operation_name = DSC_OPERATION_TEST_CONFIGURATION_STR;
                result = DscLib_TestConfiguration (&operation_result_root_value, &operation_error_root_value);
                break;
            }
        case DscSupportedOperation_PerformInventory:
            {
                operation_name = DSC_OPERATION_PERFORM_INVENTORY_STR;
                result = DscLib_PerformInventory (&operation_error_root_value);
                break;
            }
        case DscSupportedOperation_PerformInventoryOOB:
            {
                operation_name = DSC_OPERATION_PERFORM_INVENTORY_OOB_STR;
                result = DscLib_PerformInventoryOOB (argv[3], &operation_error_root_value);
                break;
            }
        case DscSupportedOperation_SendConfiguration:
            {
                operation_name = DSC_OPERATION_SEND_CONFIGURATION_STR;
                MI_Boolean force = (Tcscasecmp(argv[4], MI_T("force")) == 0) ? MI_TRUE : MI_FALSE;
                result = DscLib_SendConfiguration (argv[3], force, &operation_error_root_value); // CodeQL [cpp/path-injection] Safe Path: Currently only known paths are considered
                break;
            }
        case DscSupportedOperation_SendConfigurationApply:
            {
                operation_name = DSC_OPERATION_SEND_CONFIGURATION_APPLY_STR;
                MI_Boolean force = (Tcscasecmp(argv[4], MI_T("force")) == 0) ? MI_TRUE : MI_FALSE;
                result = DscLib_SendConfigurationApply (argv[3], force, &operation_error_root_value); // CodeQL [cpp/path-injection] Safe Path: Currently only known paths are considered
                break;
            }
        case DscSupportedOperation_SendMetaConfigurationApply:
            {
                operation_name = DSC_OPERATION_SEND_METACONFIGURATION_APPLY_STR;
                result = DscLib_SendMetaConfigurationApply (argv[3], &operation_error_root_value); // CodeQL [cpp/path-injection] Safe Path: Currently only known paths are considered
                break;
            }
        case DscSupportedOperation_GetMetaConfiguration:
            {
                operation_name = DSC_OPERATION_GET_METACONFIGURATION_STR;
                result = DscLib_GetMetaConfiguration (&operation_result_root_value, &operation_error_root_value);
                break;
            }
        case DscSupportedOperation_ApplyConfiguration:
            {
                operation_name = DSC_OPERATION_APPLY_CONFIGURATION_STR;
                result = DscLib_ApplyConfiguration (&operation_error_root_value);
                break;
            }
        case DscSupportedOperation_RollBack:
            {
                operation_name = DSC_OPERATION_ROLLBACK_STR;
                result = DscLib_RollBack (&operation_error_root_value);
                break;
            }
        case DscSupportedOperation_PerformRequiredConfigurationChecks:
            {
                operation_name = DSC_OPERATION_PERFORM_REQUIRED_CONFIGURATION_CHECKS_STR;
                MI_Uint32 flags = TASK_REGULAR;
                if (argc >= 3)
                {
                    flags = atoi(argv[3]);
                }
                result = DscLib_PerformRequiredConfigurationChecks (flags, &operation_error_root_value);
                break;
            }
        case DscSupportedOperation_StopConfiguration:
            {
                operation_name = DSC_OPERATION_STOP_CONFIGURATION_STR;
                MI_Boolean force = (Tcscasecmp(argv[4], MI_T("force")) == 0) ? MI_TRUE : MI_FALSE;
                result = DscLib_StopConfiguration (force, &operation_error_root_value);
                break;
            }
        default:
            {
                Tprintf(MI_T("Current operation %d is not supported yet.\n"), current_operation);
                result = MI_RESULT_FAILED;
                CreateMiInstanceErrorObject(&extended_error, MI_T("Operation %T is not supported\n"), argv[2]);
                JSON_Value *value;
                Convert_MIInstance_JSON(extended_error, &operation_error_root_value);
            }
    }

    if(result == MI_RESULT_OK)
    {
        Tprintf(MI_T("Operation %T completed successfully.\n"), operation_name);
        DSC_TELEMETRY_INFO("dsc_host completed operation '%s' successfully.", operation_name);
    }
    else
    {
        Tprintf(MI_T("Error occured during operation %T. r = %d\n"), operation_name, result);
        DSC_TELEMETRY_ERROR("dsc_host operation '%s' failed. r = %d", operation_name, result);
    }

    if (operation_result_root_value)
    {
        Print_JSON_Value(&operation_result_root_value);
        char output_file_path[DSCHOST_STR_BUFFER_SIZE];
        Stprintf(output_file_path, DSCHOST_STR_BUFFER_SIZE, MI_T("%T/dsc.%T.log"), argv[1], operation_name);
        Save_JSON_Value(output_file_path, &operation_result_root_value);
    }

    if (operation_error_root_value)
    {
        Print_JSON_Value(&operation_error_root_value);
        char error_file_path[DSCHOST_STR_BUFFER_SIZE];
        Stprintf(error_file_path, DSCHOST_STR_BUFFER_SIZE, MI_T("%T/dsc.%T.err"), argv[1], operation_name);
        Save_JSON_Value(error_file_path, &operation_error_root_value);
        
        const char *error_message = NULL;
        error_message = json_object_get_string(json_object(operation_error_root_value), "Message");
        DSC_TELEMETRY_ERROR("dsc_host failed with error message '%s'", error_message);
    }

CleanUp:

    if (operation_result_root_value)
    {
        json_value_free(operation_result_root_value);
    }

    if (operation_error_root_value)
    {
        json_value_free(operation_error_root_value);
    }

    if (extended_error)
    {
        MI_Instance_Delete(extended_error);
    }

    if (result == MI_RESULT_OK)
    {
        Tprintf(MI_T("Operation was successful.\n"));
    }
    else
    {
        Tprintf(MI_T("Operation failed.\n"));
    }

    return result;
}
