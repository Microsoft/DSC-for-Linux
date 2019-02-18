#include <stdio.h>
#include <stdlib.h>
#include <locale.h>

#include "dsc_sal.h"
//#include <common/linux/sal.h>
#include "DSC_Systemcalls.h"
#include "Resources_LCM.h"
#include "EngineHelper.h"
#include "dsc_host.h"
#include "dsc_operations.h"
#include "dsc_library.h"
#include "lcm/strings.h"

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
    Tprintf(MI_T("  GetConfiguration [Configuration Document Path]\n"));
    Tprintf(MI_T("  TestConfiguration\n"));
    Tprintf(MI_T("  PerformInventory\n"));
    Tprintf(MI_T("  PerformInventoryOOB [MOF Document Path]\n"));
    Tprintf(MI_T("  SendConfiguration [MOF Document Path]\n"));
    Tprintf(MI_T("  SendConfigurationApply [MOF Document Path]\n"));
    Tprintf(MI_T("  SendMetaConfigurationApply [MOF Document Path]\n"));
    Tprintf(MI_T("\n"));
    Tprintf(MI_T("Example:\n"));
    Tprintf(MI_T("dsc_host /tmp/GetAuditPolicyOutput GetConfiguration ./GetAuditPolicy.mof \n"));
    Tprintf(MI_T("dsc_host /tmp/GetAuditPolicyOutput TestConfiguration\n"));
    Tprintf(MI_T("dsc_host /tmp/InventoryOutput PerformInventory\n"));
    Tprintf(MI_T("dsc_host /tmp/InventoryOutput PerformInventoryOOB ./Inventory.mof \n"));
    Tprintf(MI_T("dsc_host /tmp/InventoryOutput SendConfiguration ./new_config.mof \n"));
    Tprintf(MI_T("dsc_host /tmp/InventoryOutput SendConfigurationApply ./new_config.mof \n"));
    Tprintf(MI_T("dsc_host /tmp/InventoryOutput SendMetaConfigurationApply ./new_metaconfig.mof \n"));
    Tprintf(MI_T("\n"));
}

int main(int argc, char *argv[])
{
    MI_Instance *extended_error = NULL;
    MI_Result result = MI_RESULT_OK;
    DscSupportedOperation current_operation = DscSupportedOperation_NOP;
    JSON_Value *operation_result_root_value = NULL;
    JSON_Value *operation_error_root_value = NULL;
    char* operation_name;

    // Check the user that has invoked the operation: root for DIY and omsagent for OMS

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
        // result = GetCimMIError1Param(MI_RESULT_FAILED, &extended_error, ID_DSC_HOST_INVALID_OPERATION, argv[2]);
        result = MI_RESULT_FAILED;
        CreateMiInstanceErrorObject(&extended_error, MI_T("Operation %T is not supported\n"), argv[2]);
        JSON_Value *value;
        Convert_MIInstance_JSON(extended_error, &operation_error_root_value);
        goto CleanUp;
    }

    switch(current_operation)
    {
        case DscSupportedOperation_GetConfiguration:
            {
                operation_name = DSC_OPERATION_GET_CONFIGURATION_STR;
                result = DscLib_GetConfiguration (&operation_result_root_value, argv[3], &operation_error_root_value);
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
                result = DscLib_SendConfiguration (argv[3], force, &operation_error_root_value);
                break;
            }
        case DscSupportedOperation_SendConfigurationApply:
            {
                operation_name = DSC_OPERATION_SEND_CONFIGURATION_APPLY_STR;
                MI_Boolean force = (Tcscasecmp(argv[4], MI_T("force")) == 0) ? MI_TRUE : MI_FALSE;
                result = DscLib_SendConfigurationApply (argv[3], force, &operation_error_root_value);
                break;
            }
        case DscSupportedOperation_SendMetaConfigurationApply:
            {
                operation_name = DSC_OPERATION_SEND_METACONFIGURATION_APPLY_STR;
                result = DscLib_SendMetaConfigurationApply (argv[3], &operation_error_root_value);
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
                // result = GetCimMIError1Param( MI_RESULT_FAILED, &extended_error, ID_DSC_HOST_INVALID_OPERATION, argv[2]);
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
    }
    else
    {
        Tprintf(MI_T("Error occured during operation %T. r = %d\n"), operation_name, result);
    }

    if (operation_result_root_value)
    {
        Print_JSON_Value(&operation_result_root_value);
    }

    if (operation_error_root_value)
    {
        Print_JSON_Value(&operation_error_root_value);
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
