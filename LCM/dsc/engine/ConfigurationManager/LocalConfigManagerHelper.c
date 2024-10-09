/*
   PowerShell Desired State Configuration for Linux

   Copyright (c) Microsoft Corporation

   All rights reserved. 

   MIT License

   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the ""Software""), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED *AS IS*, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/


#include <EngineHelper.h>
#include "LocalConfigManagerHelper.h"
#include "LocalConfigManagerTaskStrings.h"
#include <ModuleHandler.h>
#include <ModuleHandlerInternal.h>
#include <ModuleValidator.h>
#include "LocalConfigManagerHelperForCA.h"
#include "CAEngine.h"
#include "DSC_Systemcalls.h"
#include "EngineHelper.h"
#include "EventWrapper.h"
#include "Resources_LCM.h"
#include "common.h"
#include "MSFT_WebDownloadManager.h"
#include "RegistrationManager.h"
#include "OMI_LocalConfigManagerHelper.h"

#if defined(BUILD_OMS)
extern MI_Boolean g_DscHost;
#endif

#define NOT_INITIALIZED         0
#define INITIALIZED             1
#define RUNNING_INITIALIZATION  2

// Constants used only for API tests
#define LCM_STATUSCODE_HISTORY_STR_SIZE 3   // 1 for code, 1 for comma seperator, 1 for tailing null
#define MAX_LCM_STATUSCODE_HISTORY_SIZE 50

MI_Char* overWroteUserSpecifiedRefreshFreqMins = NULL;
MI_Char* overWroteUserSpecifiedConfModeFreqMins = NULL;
MI_Boolean MetaMofCorrupted = MI_FALSE;
volatile MI_Char* g_inializingOperationMethodName = NULL;
volatile ptrdiff_t g_InitializationState = NOT_INITIALIZED;
void * g_registrationManager = NULL;
MI_Boolean g_bNotFirstTimeReport = MI_FALSE;
LCMProviderContext g_baselcmContext = {0};
LCMProviderContext * g_lcmContext = &g_baselcmContext;

MI_Result ReportStatusToServer(
        _In_ LCMProviderContext *lcmContext,
        _In_opt_z_ const MI_Char * errorMessage,
        _In_opt_z_ const MI_Char *errorSource,
        _In_opt_z_ const MI_Char *resourceId,
        _In_ MI_Uint32 errorCode,
        _In_ MI_Boolean bLastReport,
        _In_ MI_Uint32 isStatusReport,
        _In_opt_ MI_Instance* instanceMIError);

ExpandedSystemPath g_ExpandedSystemPath[] =
{
    {CONFIGURATION_LOCATION_PENDING, NULL},
    {CONFIGURATION_LOCATION_PENDINGTMP, NULL},
    {CONFIGURATION_LOCATION_CURRENT, NULL},
    {CONFIGURATION_LOCATION_GET, NULL},
    {CONFIGURATION_LOCATION_METACONFIG, NULL},
    {CONFIGURATION_LOCATION_METACONFIG_TMP, NULL},
    {CONFIGURATION_LOCATION_CURRENTCHECKSUM, NULL},
    {CONFIGURATION_LOCATION_PREVIOUS, NULL},
    {CONFIGURATION_LOCATION_PULLRUNLOGFILE, NULL},
    { CONFIGURATION_LOCATION_PARTIALBASEDOCUMENT, NULL },
    { CONFIGURATION_LOCATION_PARTIALBASEDOCUMENTTMP, NULL },
    { CONFIGURATION_LOCATION_PARTIALCONFIGURATIONS_STORE, NULL },
    { CONFIGURATION_LOCATION_INVENTORY, NULL },
    { CONFIGURATION_LOCATION_INVENTORY_REPORT, NULL },
    {NULL, NULL}
};
MI_Result RetryDeleteFile(
    _In_z_ const MI_Char* filePath)
{
    MI_Uint32 xCount = 0 ;   
    DSC_EventWriteMessageDeletingFile(filePath);

    while (xCount++ < RETRY_LOOP_COUNT)
    {
        BOOL fResult = File_RemoveT(filePath);
        if (fResult)
        {
            DSC_EventWriteDeleteFileFailed(xCount,filePath, -1, NULL);

            Sleep_Milliseconds(RETRY_LOOP_SLEEP);
            continue;
        }

        return MI_RESULT_OK;
    }

    LCM_WriteMessage1ParamWithoutContext(DSC_RESOURCENAME, ID_LCMHELPER_DEL_FAILED, filePath, MI_WRITEMESSAGE_CHANNEL_WARNING);
    return MI_RESULT_FAILED;
}

MI_Result RegisterStandardTasks(_Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Instance *currentMetaConfigInstance = NULL;

    MI_Result result = MI_RESULT_OK;

    if (cimErrorDetails == NULL)
    {        
        return MI_RESULT_INVALID_PARAMETER; 
    }
    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once. 

    result = GetMetaConfig((MSFT_DSCMetaConfiguration **)&currentMetaConfigInstance);
    if (result != MI_RESULT_OK)
    {
        return result;
    }

        result = RegisterConsistencyTask(currentMetaConfigInstance, cimErrorDetails);

        MI_Instance_Delete(currentMetaConfigInstance);

    return result;
}

MI_Result DoPushDependencyCheck(
        _In_z_ MI_Char *mofFileName,
        _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
        MI_Result result = MI_RESULT_OK;

        result = MI_RESULT_NOT_SUPPORTED;

        return result;
}

void SetLCMProviderContext(_Inout_ LCMProviderContext *lcmContext, MI_Uint32 executionMode, _In_ void *context)
{
        lcmContext->executionMode = executionMode;
        lcmContext->context = context;
}

MI_Result GetNextRefreshTimeHelper(_Inout_updates_z_(MAX_PATH) MI_Char* timeString)
{
    MI_Result r = MI_RESULT_OK;
    PAL_Datetime time ;
    size_t result;

    result = CPU_GetLocalTimestamp(&time);
    if (result == -1)
    {
        return MI_RESULT_FAILED;
    }
    result = Stprintf(timeString, MAX_PATH, MI_T("%04d-%02d-%02dT%02d:%02d:%02d"), time.u.timestamp.year,  time.u.timestamp.month, time.u.timestamp.day, time.u.timestamp.hour, time.u.timestamp.minute, time.u.timestamp.second);

    if (result == -1)
    {
        return MI_RESULT_FAILED;
    }

    return r;
}

MI_Result InitHandler(
    _In_z_ const MI_Char* methodName,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Uint32 error;
    MI_Result result = MI_RESULT_OK;
    int initState;

    if (cimErrorDetails == NULL)
    {        
        return MI_RESULT_INVALID_PARAMETER; 
    }
    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once. 

    initState = (int)Atomic_CompareAndSwap(&g_InitializationState, (ptrdiff_t)RUNNING_INITIALIZATION, (ptrdiff_t)NOT_INITIALIZED);
    if (initState == INITIALIZED)
    {
        // already initialized.
        return MI_RESULT_OK;
    }
    else if (initState == RUNNING_INITIALIZATION)
    {
        // currently going on.
        return GetCimMIError3Params(MI_RESULT_FAILED, cimErrorDetails, ID_LCM_MULTIPLE_METHOD_REQUEST, methodName, (const MI_Char*)g_inializingOperationMethodName, methodName);
    }

    InitLocTable();

    g_inializingOperationMethodName = (MI_Char*)methodName;

    error = DSC_EventRegister();
    if (error != 0)
    {
        Atomic_Swap(&g_InitializationState, NOT_INITIALIZED);
        g_inializingOperationMethodName = NULL;
        return GetCimWin32Error(error, cimErrorDetails, ID_LCMHELPER_ETWREGISTREATION_FAILED);
    }

    result = InitCAHandler(cimErrorDetails);
    if (result != MI_RESULT_OK)
    {
        DSC_EventUnRegister();
        Atomic_Swap(&g_InitializationState, NOT_INITIALIZED);
        g_inializingOperationMethodName = NULL;
        return result;
    }

    g_PendingConfigFileName = NULL;
    g_PendingConfigTmpFileName = NULL;
    g_CurrentConfigFileName = NULL;
    g_PreviousConfigFileName = NULL;
    g_GetConfigFileName = NULL;
    g_InventoryFileName = NULL;
    g_InventoryReportFileName = NULL;
    g_MetaConfigFileName = NULL;
    g_MetaConfigTmpFileName = NULL;
    g_ConfigChecksumFileName = NULL;
    g_PullRunLogFileName = NULL;
    g_LCMStatusCodeHistory = NULL;
    g_PartialConfigDataStoreName = NULL;
    g_PartialConfigBaseDocumentInstanceFileName = NULL;
    g_PartialConfigBaseDocumentInstanceTmpFileName = NULL;

    result = InitPath(cimErrorDetails);
    if (result != MI_RESULT_OK)
    {
        DSC_EventUnRegister();
        DSC_free(g_PendingConfigFileName);
        DSC_free(g_CurrentConfigFileName);
        DSC_free(g_PreviousConfigFileName);
        DSC_free(g_GetConfigFileName);
        DSC_free(g_InventoryFileName);
        DSC_free(g_InventoryReportFileName);
        DSC_free(g_MetaConfigFileName);
        DSC_free(g_MetaConfigTmpFileName);
        DSC_free(g_ConfigChecksumFileName);
        DSC_free(g_PullRunLogFileName);
        DSC_free(g_LCMStatusCodeHistory);

        g_PendingConfigFileName = NULL;
        g_CurrentConfigFileName = NULL;
        g_PreviousConfigFileName = NULL;
        g_GetConfigFileName = NULL;
        g_InventoryFileName = NULL;
        g_InventoryReportFileName = NULL;
        g_MetaConfigFileName = NULL;
        g_MetaConfigTmpFileName = NULL;
        g_ConfigChecksumFileName = NULL;
        g_PullRunLogFileName = NULL;
        g_LCMStatusCodeHistory = NULL;

        DSC_free(g_PendingConfigTmpFileName);
        DSC_free(g_PartialConfigDataStoreName);
        DSC_free(g_PartialConfigBaseDocumentInstanceFileName);
        DSC_free(g_PartialConfigBaseDocumentInstanceTmpFileName);
        g_PendingConfigTmpFileName = NULL;
        g_PartialConfigDataStoreName = NULL;
        g_PartialConfigBaseDocumentInstanceFileName = NULL;
        g_PartialConfigBaseDocumentInstanceTmpFileName = NULL;

        RecursiveLock_Release(&g_cs_CurrentWmiv2Operation);
        Sem_Destroy(&g_h_ConfigurationStoppedEvent);
        Atomic_Swap(&g_InitializationState, NOT_INITIALIZED);
        g_inializingOperationMethodName = NULL;
        return result;
    } 

    g_DSCInternalCache = NULL;
    result = InitCacheAndMetaConfig(&g_metaConfig, &g_DSCInternalCache, cimErrorDetails);
    if (result != MI_RESULT_OK)
    {
        DSC_EventUnRegister();
        DSC_free(g_PendingConfigFileName);
        DSC_free(g_CurrentConfigFileName);
        DSC_free(g_PreviousConfigFileName);
        DSC_free(g_GetConfigFileName);
        DSC_free(g_InventoryFileName);
        DSC_free(g_InventoryReportFileName);
        DSC_free(g_MetaConfigFileName);
        DSC_free(g_MetaConfigTmpFileName);
        DSC_free(g_ConfigChecksumFileName);
        DSC_free(g_PullRunLogFileName);
        DSC_free(g_LCMStatusCodeHistory);

        g_PendingConfigFileName = NULL;
        g_CurrentConfigFileName = NULL;
        g_PreviousConfigFileName = NULL;
        g_GetConfigFileName = NULL;
        g_InventoryFileName = NULL;
        g_InventoryReportFileName = NULL;
        g_MetaConfigFileName = NULL;
        g_MetaConfigTmpFileName = NULL;
        g_ConfigChecksumFileName = NULL;
        g_PullRunLogFileName = NULL;
        g_LCMStatusCodeHistory = NULL;

        DSC_free(g_PendingConfigTmpFileName);
        DSC_free(g_PartialConfigDataStoreName);
        DSC_free(g_PartialConfigBaseDocumentInstanceFileName);
        DSC_free(g_PartialConfigBaseDocumentInstanceTmpFileName);
        g_PendingConfigTmpFileName = NULL;
        g_PartialConfigDataStoreName = NULL;
        g_PartialConfigBaseDocumentInstanceFileName = NULL;
        g_PartialConfigBaseDocumentInstanceTmpFileName = NULL;

        RecursiveLock_Release(&g_cs_CurrentWmiv2Operation);
        Sem_Destroy(&g_h_ConfigurationStoppedEvent);
        Atomic_Swap(&g_InitializationState, NOT_INITIALIZED);
        g_inializingOperationMethodName = NULL;
        return result;
    }
    SetJobDeviceName();

    RegistrationManager_New((RegistrationManager**)&(g_registrationManager), cimErrorDetails);

    Atomic_Swap(&g_InitializationState, INITIALIZED);
    g_inializingOperationMethodName = NULL;
    return MI_RESULT_OK;
}

MI_Result InitPath(
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result result = MI_RESULT_OK;
    MI_Uint32 count;
    MI_Char *fileFullFrom;
    MI_Uint32 initCount = 0;

    if (cimErrorDetails == NULL)
    {        
        return MI_RESULT_INVALID_PARAMETER; 
    }
    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once. 

    for (count = 0; g_ExpandedSystemPath[count].dscSystemFile != NULL; count++)
    {
        if (Tcscasecmp(g_ExpandedSystemPath[count].dscSystemFile, CONFIGURATION_LOCATION_PENDING) == 0)
        {
            g_ExpandedSystemPath[count].expandedPath = &g_PendingConfigFileName;
            initCount++;
        }
        else if (Tcscasecmp(g_ExpandedSystemPath[count].dscSystemFile, CONFIGURATION_LOCATION_PENDINGTMP) == 0)
        {
            g_ExpandedSystemPath[count].expandedPath = &g_PendingConfigTmpFileName;
            initCount++;
        }
        else if (Tcscasecmp(g_ExpandedSystemPath[count].dscSystemFile, CONFIGURATION_LOCATION_CURRENT) == 0)
        {
            g_ExpandedSystemPath[count].expandedPath = &g_CurrentConfigFileName;
            initCount++;
        }
        else if (Tcscasecmp(g_ExpandedSystemPath[count].dscSystemFile, CONFIGURATION_LOCATION_GET) == 0)
        {
            g_ExpandedSystemPath[count].expandedPath = &g_GetConfigFileName;
            initCount++;
        }
        else if (Tcscasecmp(g_ExpandedSystemPath[count].dscSystemFile, CONFIGURATION_LOCATION_INVENTORY) == 0)
        {
            g_ExpandedSystemPath[count].expandedPath = &g_InventoryFileName;
            initCount++;
        }
        else if (Tcscasecmp(g_ExpandedSystemPath[count].dscSystemFile, CONFIGURATION_LOCATION_INVENTORY_REPORT) == 0)
        {
            g_ExpandedSystemPath[count].expandedPath = &g_InventoryReportFileName;
            initCount++;
        }
        else if (Tcscasecmp(g_ExpandedSystemPath[count].dscSystemFile, CONFIGURATION_LOCATION_METACONFIG) == 0)
        {
            g_ExpandedSystemPath[count].expandedPath = &g_MetaConfigFileName;
            initCount++;
        }
        else if (Tcscasecmp(g_ExpandedSystemPath[count].dscSystemFile, CONFIGURATION_LOCATION_METACONFIG_TMP) == 0)
        {
            g_ExpandedSystemPath[count].expandedPath = &g_MetaConfigTmpFileName;
            initCount++;
        }
        else if (Tcscasecmp(g_ExpandedSystemPath[count].dscSystemFile, CONFIGURATION_LOCATION_CURRENTCHECKSUM) == 0)
        {
            g_ExpandedSystemPath[count].expandedPath = &g_ConfigChecksumFileName;
            initCount++;
            continue;
        }
        else if (Tcscasecmp(g_ExpandedSystemPath[count].dscSystemFile, CONFIGURATION_LOCATION_PREVIOUS) == 0)
        {
            g_ExpandedSystemPath[count].expandedPath = &g_PreviousConfigFileName;
            initCount++;
        }        
        else if (Tcscasecmp(g_ExpandedSystemPath[count].dscSystemFile, CONFIGURATION_LOCATION_PULLRUNLOGFILE) == 0)
        {
            g_ExpandedSystemPath[count].expandedPath = &g_PullRunLogFileName;
            initCount++;
        }        
        else if (Tcscasecmp(g_ExpandedSystemPath[count].dscSystemFile, CONFIGURATION_LOCATION_PARTIALBASEDOCUMENT) == 0)
        {
            g_ExpandedSystemPath[count].expandedPath = &g_PartialConfigBaseDocumentInstanceFileName;
                        initCount++;
        }
        else if (Tcscasecmp(g_ExpandedSystemPath[count].dscSystemFile, CONFIGURATION_LOCATION_PARTIALBASEDOCUMENTTMP) == 0)
        {
            g_ExpandedSystemPath[count].expandedPath = &g_PartialConfigBaseDocumentInstanceTmpFileName;
            initCount++;
        }
        else if (Tcscasecmp(g_ExpandedSystemPath[count].dscSystemFile, CONFIGURATION_LOCATION_PARTIALCONFIGURATIONS_STORE) == 0)
        {
            g_ExpandedSystemPath[count].expandedPath = &g_PartialConfigDataStoreName;
            initCount++;
        }
    }    

    if (initCount != count || NitsShouldFault(NitsHere(), NitsAutomatic))
    {
        return GetCimMIError(MI_RESULT_FAILED, cimErrorDetails, ID_LCMHELPER_INIT_EXAPANDENV_FAILED);
    }

    for (count = 0; g_ExpandedSystemPath[count].dscSystemFile != NULL; count++)
    {
        result = GetFullPath(GetConfigPath(), g_ExpandedSystemPath[count].dscSystemFile, &fileFullFrom, cimErrorDetails);
        if (result != MI_RESULT_OK)
        {
            return result;
        }

        result = ExpandPath(fileFullFrom, g_ExpandedSystemPath[count].expandedPath, cimErrorDetails);
        DSC_free(fileFullFrom);
        fileFullFrom = NULL;
        if (result != MI_RESULT_OK )
        {
            return result;
        }
    }

    return result;
}

MI_Result UnInitHandler(
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Uint32 error;

    if (cimErrorDetails == NULL)
    {        
        return MI_RESULT_INVALID_PARAMETER; 
    }
    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once. 

    UnInitCAHandler(cimErrorDetails);

    if (g_PendingConfigFileName != NULL)
    {
        DSC_free(g_PendingConfigFileName);
        g_PendingConfigFileName = NULL;
    }

    if (g_PendingConfigTmpFileName != NULL)
    {
        DSC_free(g_PendingConfigTmpFileName);
        g_PendingConfigTmpFileName = NULL;
    }

    if (g_CurrentConfigFileName != NULL)
    {
        DSC_free(g_CurrentConfigFileName);
        g_CurrentConfigFileName = NULL;
    }

    if(g_PreviousConfigFileName != NULL)
    {
        DSC_free(g_PreviousConfigFileName);
        g_PreviousConfigFileName = NULL;
    }

    if( g_GetConfigFileName != NULL)
    {
        DSC_free(g_GetConfigFileName);
        g_GetConfigFileName = NULL;
    }

   if( g_InventoryFileName != NULL)
    {
        DSC_free(g_InventoryFileName);
        g_InventoryFileName = NULL;
    }

   if( g_InventoryReportFileName != NULL)
    {
        DSC_free(g_InventoryReportFileName);
        g_InventoryReportFileName = NULL;
    }

    if (g_MetaConfigFileName != NULL)
    {
        DSC_free(g_MetaConfigFileName);
        g_MetaConfigFileName = NULL;
    }

    if (g_MetaConfigTmpFileName != NULL)
    {
        DSC_free(g_MetaConfigTmpFileName);
        g_MetaConfigTmpFileName = NULL;
    }

    if (g_ConfigChecksumFileName != NULL)
    {
        DSC_free(g_ConfigChecksumFileName);
        g_ConfigChecksumFileName = NULL;
    }

    if (g_PullRunLogFileName != NULL)
    {
        DSC_free(g_PullRunLogFileName);
        g_PullRunLogFileName = NULL;
    }

    if (g_LCMStatusCodeHistory != NULL)
    {
        DSC_free(g_LCMStatusCodeHistory);
        g_LCMStatusCodeHistory = NULL;
    }

    if (g_PartialConfigDataStoreName != NULL)
    {
        DSC_free(g_PartialConfigDataStoreName);
        g_PartialConfigDataStoreName = NULL;
    }

    if (g_PartialConfigBaseDocumentInstanceFileName != NULL)
    {
        DSC_free(g_PartialConfigBaseDocumentInstanceFileName);
        g_PartialConfigBaseDocumentInstanceFileName = NULL;
    }

    if (g_PartialConfigBaseDocumentInstanceTmpFileName != NULL)
    {
        DSC_free(g_PartialConfigBaseDocumentInstanceTmpFileName);
        g_PartialConfigBaseDocumentInstanceTmpFileName = NULL;
    }

    if (g_metaConfig != NULL)
    {
        MSFT_DSCMetaConfiguration_Delete(g_metaConfig);
        g_metaConfig = NULL;
    }

    if (g_DSCInternalCache)
    {
        MI_Instance_Delete(g_DSCInternalCache);
        g_DSCInternalCache = NULL;
    }

    error = DSC_EventUnRegister();
    if (error != 0)
    {
        return GetCimWin32Error(error, cimErrorDetails, ID_LCMHELPER_ETWUNREGISTREATION_FAILED);
    }

    if (g_registrationManager)
    {
        RegistrationManager_Destroy((RegistrationManager*)g_registrationManager);
        g_registrationManager = NULL;
    }

    Atomic_Swap(&g_InitializationState, NOT_INITIALIZED);
    return MI_RESULT_OK;
}

MI_Result CallTestConfiguration(
    _Out_ MI_Boolean *testStatus, 
    _Inout_ MI_StringA *resourceId,
    _In_ MI_Context* context,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result result = MI_RESULT_OK;
    LCMProviderContext lcmContext = {0};
    ModuleManager *moduleManager = NULL;
    MI_Uint32 resultStatus = 0;

    //Debug Log 
    DSC_EventWriteLocalConfigMethodParameters(__WFUNCTION__,0,0,lcmContext.executionMode);

    *testStatus = MI_FALSE;

    //If the current configuration file is not found, function ends with a corresponding error message
    if (File_ExistT(GetCurrentConfigFileName()) == -1 && File_ExistT(GetPendingConfigFileName()) == -1)
    {
        SetLCMStatusReady();
        return GetCimMIError(MI_RESULT_NOT_FOUND, cimErrorDetails, ID_LCMHELPER_CURRENT_NOTFOUND);
    }

    lcmContext.executionMode = (LCM_EXECUTIONMODE_OFFLINE | LCM_EXECUTIONMODE_ONLINE);
    lcmContext.context = (void*)context;
    result = InitializeModuleManager(0, cimErrorDetails, &moduleManager);    
    if (result != MI_RESULT_OK)
    {
        SetLCMStatusReady();
        return result;
    }
    
    if (moduleManager == NULL || moduleManager->ft == NULL)
    {
        SetLCMStatusReady();
        return GetCimMIError(MI_RESULT_INVALID_PARAMETER, cimErrorDetails,ID_MODMAN_MODMAN_NULLPARAM);   
    }

    SetMessageInContext(ID_OUTPUT_OPERATION_START,ID_OUTPUT_ITEM_TEST,&lcmContext);
    LCM_BuildMessage(&lcmContext, ID_OUTPUT_EMPTYSTRING, EMPTY_STRING, MI_WRITEMESSAGE_CHANNEL_VERBOSE);
    
    if (File_ExistT(GetCurrentConfigFileName()) != -1)
    {
        result = ApplyCurrentConfig(&lcmContext, moduleManager, LCM_EXECUTE_TESTONLY, &resultStatus, cimErrorDetails);
    }

    if (File_ExistT(GetPendingConfigFileName()) != -1)
    {
        DSC_WriteWarning(&lcmContext, ID_LCMHELPER_TEST_OPERATION_AGAINST_PENDING);
        result = ApplyPendingConfig(&lcmContext, moduleManager, LCM_EXECUTE_TESTONLY, &resultStatus, cimErrorDetails);
    }

    moduleManager->ft->Close(moduleManager, NULL);
    if (result == MI_RESULT_OK)
    {
        if (resultStatus == 0)
        {
            *testStatus = MI_FALSE;
            SetMessageInContext(ID_OUTPUT_OPERATION_END,ID_OUTPUT_ITEM_TEST,&lcmContext);
            LCM_BuildMessage(&lcmContext, ID_LCM_WRITEMESSAGE_ENDTESTPROCESSING_FALSE, EMPTY_STRING, MI_WRITEMESSAGE_CHANNEL_VERBOSE);  
        }
        else
        {
            *testStatus = MI_TRUE;
            SetMessageInContext(ID_OUTPUT_OPERATION_END,ID_OUTPUT_ITEM_TEST,&lcmContext);
            LCM_BuildMessage(&lcmContext, ID_LCM_WRITEMESSAGE_ENDTESTPROCESSING_TRUE, EMPTY_STRING, MI_WRITEMESSAGE_CHANNEL_VERBOSE);  
        }
    }
    else
    {
        SetMessageInContext(ID_OUTPUT_OPERATION_FAILED,ID_OUTPUT_ITEM_TEST,&lcmContext);
        LCM_BuildMessage(&lcmContext, ID_LCM_WRITEMESSAGE_ENDTESTPROCESSING_FAIL, EMPTY_STRING, MI_WRITEMESSAGE_CHANNEL_VERBOSE);  
    }

    //Debug Log 
    DSC_EventWriteMethodEnd(__WFUNCTION__);

        return result;
}

/* caller release outinstances */
MI_Result CallGetConfiguration(
    _In_reads_bytes_(dataSize) const MI_Uint8* ConfigData,
    MI_Uint32 dataSize,
    _Inout_ MI_InstanceA *outInstances,
    _In_ MI_Context* context,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result result = MI_RESULT_OK;
    MI_InstanceA getInstances = {0};
    MI_Instance *documentIns = NULL;
    MI_InstanceA getResultInstances = {0};
    LCMProviderContext lcmContext = {0};
    BOOL fResult;
    ModuleManager *moduleManager = NULL;

    //Debug Log 
    DSC_EventWriteLocalConfigMethodParameters(__WFUNCTION__,dataSize,0,lcmContext.executionMode);

    if (cimErrorDetails == NULL)
    {        
        return MI_RESULT_INVALID_PARAMETER; 
    }
    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once. 

    lcmContext.executionMode = (LCM_EXECUTIONMODE_OFFLINE | LCM_EXECUTIONMODE_ONLINE);
    lcmContext.context = (void*)context;    
    result = ValidateConfigurationDirectory(cimErrorDetails);
    if (result != MI_RESULT_OK)
    {
        SetLCMStatusReady();
        if (cimErrorDetails && *cimErrorDetails)
            return result;

        return GetCimMIError(result, cimErrorDetails, ID_LCMHELPER_VALIDATE_CONFGDIR_FAILED);
    }

    // Get.mof
    if (File_ExistT(GetGetConfigFileName()) != -1)
    {
        fResult = File_RemoveT(GetGetConfigFileName());
        if (fResult || NitsShouldFault(NitsHere(), NitsAutomatic))
        {
            SetLCMStatusReady();
            return GetCimWin32Error(MI_RESULT_FAILED, cimErrorDetails, ID_LCMHELPER_DEL_GETFILEBEFORE_FAILED);
        }
    }

    if (dataSize == 0 && ConfigData == NULL)
    {
        // If both the current and pending configuration files do not exist, output a corresponding error message and return
        if (File_ExistT(GetCurrentConfigFileName()) == -1 && File_ExistT(GetPendingConfigFileName()) == -1)
        {
            SetLCMStatusReady();
            return GetCimMIError(MI_RESULT_FAILED, cimErrorDetails, ID_LCMHELPER_CURRENT_NOTFOUND);
        }

        // Copy file contents from the current or pending configuration file to Get.mof
        if (File_ExistT(GetCurrentConfigFileName()) == 0)
        {
            result = File_CopyT(GetCurrentConfigFileName(), GetGetConfigFileName());
        }
        else
        {
            DSC_WriteWarning(&lcmContext, ID_LCMHELPER_GET_OPERATION_AGAINST_PENDING);
            result = File_CopyT(GetPendingConfigFileName(), GetGetConfigFileName());
        }

        if (result != MI_RESULT_OK)
        {
            return result;
        }
    }
    else
    {
        result = SaveFile(GetGetConfigFileName(), ConfigData, dataSize, cimErrorDetails);

        if (result != MI_RESULT_OK )
        {
            SetLCMStatusReady();
            if (cimErrorDetails && *cimErrorDetails)
            {
                return result;
            }

            return GetCimMIError(MI_RESULT_ALREADY_EXISTS, cimErrorDetails, ID_LCMHELPER_SAVE_GETCONF_FAILED);
        }
    }

    result = InitializeModuleManager(0, cimErrorDetails, &moduleManager);
    if (result != MI_RESULT_OK)
    {
        SetLCMStatusReady();
        return result;
    }

    result =  moduleManager->ft->LoadInstanceDocumentFromLocation(moduleManager, 0, GetGetConfigFileName(), cimErrorDetails, &getInstances, &documentIns);
    if (result != MI_RESULT_OK)
    {
        moduleManager->ft->Close(moduleManager, NULL);
        SetLCMStatusReady();
        if (cimErrorDetails && *cimErrorDetails)
            return result;
        
        return GetCimMIError(result, cimErrorDetails, ID_LCMHELPER_LOAD_GETFILE_FAILED);
    }

    if (documentIns != NULL )
    {
        result = ValidateDocumentInstance(documentIns, cimErrorDetails);
        if (result != MI_RESULT_OK)
        {
            CleanUpInstanceCache(&getInstances);
            moduleManager->ft->Close(moduleManager, NULL);
            SetLCMStatusReady();
            MI_Instance_Delete(documentIns);
            return result;
        }
    }

    // Check if at least 1 resource was specified in the instance document
    if (getInstances.size == 0 )
    {
        MI_Instance_Delete(documentIns);
        moduleManager->ft->Close(moduleManager, NULL);
        SetLCMStatusReady();
        return GetCimMIError(MI_RESULT_INVALID_PARAMETER, cimErrorDetails, ID_LCMHELPER_NORESOURCESPECIFIED);
    }

    SetMessageInContext(ID_OUTPUT_OPERATION_START,ID_OUTPUT_ITEM_GET,&lcmContext);
    LCM_BuildMessage(&lcmContext, ID_OUTPUT_EMPTYSTRING, EMPTY_STRING, MI_WRITEMESSAGE_CHANNEL_VERBOSE);

    result = GetConfiguration(&lcmContext, 0, &getInstances, moduleManager, documentIns, &getResultInstances, cimErrorDetails);

    MI_Instance_Delete(documentIns);

    moduleManager->ft->Close(moduleManager, NULL);

    CleanUpInstanceCache(&getInstances);
    if (result != MI_RESULT_OK)
    {
                SetLCMStatusReady();
                if (cimErrorDetails && *cimErrorDetails)
            return result;

        return GetCimMIError(result, cimErrorDetails, ID_LCMHELPER_GET_CONF_FAILED);
    }

    fResult = File_RemoveT(GetGetConfigFileName());
    if (fResult || NitsShouldFault(NitsHere(), NitsAutomatic))
    {
                SetLCMStatusReady();
        return GetCimWin32Error(MI_RESULT_FAILED, cimErrorDetails, ID_LCMHELPER_DEL_GETFILEAFTER_FAILED);
    } 

    outInstances->data = getResultInstances.data;
    outInstances->size = getResultInstances.size;
    
    DSC_EventWriteMethodEnd(__WFUNCTION__);

    return result;
}

MI_Result CallSetConfiguration(
    _In_reads_bytes_(dataSize) const MI_Uint8* ConfigData,
    MI_Uint32 dataSize,
    MI_Uint32 dwFlags,
    MI_Boolean force,
    _In_ MI_Context* context,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result r = MI_RESULT_OK;
    MI_Instance *metaConfigInstance = NULL;
    MI_Value configModeValue;

    LCMProviderContext lcmContext = {0};

    DSC_EventWriteLocalConfigMethodParameters(__WFUNCTION__,dataSize,dwFlags,lcmContext.executionMode);

    lcmContext.executionMode = (LCM_EXECUTIONMODE_OFFLINE | LCM_EXECUTIONMODE_ONLINE);
    lcmContext.context = (void*)context;

    r = GetMetaConfig(&metaConfigInstance);
    EH_CheckResult(r);

    r = DSC_MI_Instance_GetElement(metaConfigInstance, MSFT_DSCMetaConfiguration_ConfigurationMode, &configModeValue, NULL, NULL, NULL);
    EH_CheckResult(r);

    // We tell user that LCM is running in MonitorOnly mode and will be testing only
    if (ShouldMonitorOnly(configModeValue.string) && !(dwFlags & LCM_SET_METACONFIG))
    {
        LCM_BuildMessage(&lcmContext, ID_LCM_MONITORONLY_CONFIGURATIONMODE, EMPTY_STRING, MI_WRITEMESSAGE_CHANNEL_VERBOSE);
    }
    else
    {
        //log method start in verbose
        SetMessageInContext(ID_OUTPUT_OPERATION_START, ID_OUTPUT_ITEM_SET, &lcmContext);
        LCM_BuildMessage(&lcmContext, ID_OUTPUT_EMPTYSTRING, EMPTY_STRING, MI_WRITEMESSAGE_CHANNEL_VERBOSE);
    }

    if (dwFlags & LCM_SETFLAGS_ENABLEWHATIF)
    {
        lcmContext.executionMode |= LCM_SETFLAGS_ENABLEWHATIF;
    }

    SetMessageInContext(ID_OUTPUT_OPERATION_START,ID_OUTPUT_ITEM_SET,&lcmContext);
    LCM_BuildMessage(&lcmContext, ID_OUTPUT_EMPTYSTRING, EMPTY_STRING, MI_WRITEMESSAGE_CHANNEL_VERBOSE);
    r =  SetConfiguration(ConfigData, dataSize, force, &lcmContext, dwFlags, cimErrorDetails);

EH_UNWIND;
    // No need to output set End when LCM is running in 'MonitorOnly' Mode.
    if (!ShouldMonitorOnly(configModeValue.string))
    {
        SetMessageInContext(ID_OUTPUT_OPERATION_END, ID_OUTPUT_ITEM_SET, &lcmContext);
        LCM_BuildMessage(&lcmContext, ID_OUTPUT_EMPTYSTRING, EMPTY_STRING, MI_WRITEMESSAGE_CHANNEL_VERBOSE);
    }

    //Debug Log 
    DSC_EventWriteMethodEnd(__WFUNCTION__);

    return r;
}

MI_Result CallRestoreConfiguration(
    MI_Uint32 dwFlags,
    _In_ MI_Context* context,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result r = MI_RESULT_OK;
    LCMProviderContext lcmContext = {0};
    MI_Uint8A PreviousConfigValue={0};
    MI_Uint32 bufferIndex = 0;

    //Debug Log 
    DSC_EventWriteLocalConfigMethodParameters(__WFUNCTION__,0,dwFlags,lcmContext.executionMode);

    //If the previous configuration file does not exist, output a corresponding error message and return
    if (File_ExistT(GetPreviousConfigFileName())== -1)
    {
                SetLCMStatusReady();
        return GetCimMIError(MI_RESULT_FAILED, cimErrorDetails, ID_LCMHELPER_PREVIOUS_NOTFOUND);
    }

    //Read file contents from the current configuration file into PreviousConfigValue
    r = ReadFileContent(GetPreviousConfigFileName(), &PreviousConfigValue.data, &PreviousConfigValue.size,cimErrorDetails);
    if (r != MI_RESULT_OK)
    {
                SetLCMStatusReady();
        return r;
    }

    GetRealBufferIndex((const MI_ConstUint8A*) &(PreviousConfigValue), &bufferIndex);
    lcmContext.executionMode = (LCM_EXECUTIONMODE_OFFLINE | LCM_EXECUTIONMODE_ONLINE);
    lcmContext.context = (void*)context;
    if(dwFlags & LCM_SETFLAGS_ENABLEWHATIF)
    {
        lcmContext.executionMode |= LCM_SETFLAGS_ENABLEWHATIF;
    }

    SetMessageInContext(ID_OUTPUT_OPERATION_START,ID_OUTPUT_ITEM_ROLLBACK,&lcmContext);
    LCM_BuildMessage(&lcmContext, ID_OUTPUT_EMPTYSTRING, EMPTY_STRING, MI_WRITEMESSAGE_CHANNEL_VERBOSE);
    r =  SetConfiguration(PreviousConfigValue.data + bufferIndex, PreviousConfigValue.size - bufferIndex,MI_FALSE,&lcmContext, dwFlags, cimErrorDetails);

        //Debug Log 
    DSC_EventWriteMethodEnd(__WFUNCTION__);

    return r;
}

MI_Result CallConsistencyEngine(
    _In_ MI_Context* context,
    _In_ MI_Uint32 invokeMode,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result result = MI_RESULT_OK;
    LCMProviderContext lcmContext = {0};
    ModuleManager *moduleManager = NULL;
    MI_Uint32 resultStatus = 0;
    MI_Instance *metaConfigInstance = NULL;
    MI_Uint32 flags;
    MI_Value configModeValue;
        
    DSC_EventWriteLocalConfigMethodParameters(__WFUNCTION__,0,0,lcmContext.executionMode);

    if (cimErrorDetails == NULL)
    {        
        return MI_RESULT_INVALID_PARAMETER; 
    }
    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once. 

    lcmContext.executionMode = (LCM_EXECUTIONMODE_OFFLINE | LCM_EXECUTIONMODE_ONLINE);
    lcmContext.context = (void*)context;
    LCM_BuildMessage(&lcmContext, ID_LCM_WRITEMESSAGE_STARTCONSISTENCY, EMPTY_STRING, MI_WRITEMESSAGE_CHANNEL_VERBOSE);    

    result = ValidateConfigurationDirectory(cimErrorDetails);
    if (result != MI_RESULT_OK)
    {
                SetLCMStatusReady();
        if (cimErrorDetails && *cimErrorDetails)
            return result;

        return GetCimMIError(result, cimErrorDetails, ID_LCMHELPER_VALIDATE_CONFGDIR_FAILED);
    }

    result = InitializeModuleManager(0, cimErrorDetails, &moduleManager);    
    if (result != MI_RESULT_OK)
    {
                SetLCMStatusReady();
        return result;
    }

    result = GetMetaConfig((MSFT_DSCMetaConfiguration **)&metaConfigInstance);
    if (result != MI_RESULT_OK)
    {
                SetLCMStatusReady();
        moduleManager->ft->Close(moduleManager, NULL);
        return result;
    }     

    result = MI_Instance_GetElement(metaConfigInstance, MSFT_DSCMetaConfiguration_ConfigurationMode, &configModeValue, NULL, &flags, NULL);
    if (result != MI_RESULT_OK)
    {
        MSFT_DSCMetaConfiguration_Delete((MSFT_DSCMetaConfiguration *)metaConfigInstance);
        moduleManager->ft->Close(moduleManager, NULL);
                SetLCMStatusReady();
        return result;
    }

    //if it contains partial configurations, look into the partial config folder and save it into pending.mof
    if (ShouldUsePartialConfigurations(metaConfigInstance, MI_FALSE))
    {
        //If the partial configurations directory contains partial files, combine them and save into pending.mof
        result = MergePartialConfigurations(&lcmContext, moduleManager, GetPendingConfigFileName(), GetPartialConfigBaseDocumentInstanceFileName(), cimErrorDetails);
        if (result != MI_RESULT_OK)
        {
            MSFT_DSCMetaConfiguration_Delete((MSFT_DSCMetaConfiguration *)metaConfigInstance);
            moduleManager->ft->Close(moduleManager, NULL);
            SetLCMStatusReady(lcmContext, result);
            return result;
        }
    }

    //We will always try to apply the pending.mof if it's there
    if (File_ExistT(GetPendingConfigFileName()) != -1)
    {
        if (!ShouldUsePartialConfigurations(metaConfigInstance, MI_FALSE))
        {
            LCM_BuildMessage(&lcmContext, ID_LCM_WRITEMESSAGE_CONSISTENCY_PENDINGEXIST, EMPTY_STRING, MI_WRITEMESSAGE_CHANNEL_VERBOSE);
        }
        result = ApplyPendingConfig(&lcmContext, moduleManager, 0, &resultStatus, cimErrorDetails);
        if (result == MI_RESULT_OK && (resultStatus & DSC_RESTART_SYSTEM_FLAG))
        {
            SetLCMStatusReboot();
        }        
    }
    else if (File_ExistT(GetCurrentConfigFileName()) != -1)
    {
        if (ShouldAutoCorrect(configModeValue.string))
        {
            LCM_BuildMessage(&lcmContext, ID_LCM_WRITEMESSAGE_CONSISTENCY_CURRENTEXIST, EMPTY_STRING, MI_WRITEMESSAGE_CHANNEL_VERBOSE);     
            result = ApplyCurrentConfig(&lcmContext, moduleManager, 0, &resultStatus, cimErrorDetails);
            if (result == MI_RESULT_OK && (resultStatus & DSC_RESTART_SYSTEM_FLAG))
            {
                SetLCMStatusReboot();
            }

        }
        else if (ShouldMonitor(configModeValue.string) || ShouldMonitorOnly(configModeValue.string))
        {
            MI_Char* messageEvent=NULL;
            MI_Char* newLine=MI_T("\n");
            result = ApplyCurrentConfig(&lcmContext, moduleManager, LCM_EXECUTE_TESTONLY, &resultStatus, cimErrorDetails);
            if(resultStatus==MI_FALSE)
            {
                Intlstr intlstr = Intlstr_Null;
                GetResourceString(ID_LCM_WRITEMESSAGE_ENDTESTPROCESSING_FALSE,&intlstr);   
                ConcatStrings(&messageEvent,10,newLine, (MI_Char*)intlstr.str);
    

                
                if (messageEvent != NULL)
                {
                    //Output event stating it resulted in false.
                    DSC_EventWriteMessageFromEngineConsistency(messageEvent);
                    DSC_free(messageEvent);
                }

                if(intlstr.str)
                    Intlstr_Free(intlstr);

                // This is a difference between Windows DSC and Linux DSC in ApplyAndMonitor mode. Not sure why.
                if (ShouldMonitor(configModeValue.string))
                {
                    result =  GetCimMIError(MI_RESULT_FAILED, cimErrorDetails, ID_LCM_WRITEMESSAGE_ENDTESTPROCESSING_FALSE);
                }
            }
        }
    }
    else
    {
        // We don't have any pending or current. Nothing to apply - Logging an event for that.
        DSC_EventWriteLCMConsistencyEngineNoCurrentPending();
        result =  MI_RESULT_OK;
    }

    LCM_BuildMessage(&lcmContext, ID_LCM_WRITEMESSAGE_ENDCONSISTENCY, EMPTY_STRING, MI_WRITEMESSAGE_CHANNEL_VERBOSE);  
    MSFT_DSCMetaConfiguration_Delete((MSFT_DSCMetaConfiguration *)metaConfigInstance);
    moduleManager->ft->Close(moduleManager, NULL);

    //Debug Log 
    DSC_EventWriteMethodEnd(__WFUNCTION__);

    return result;
}

MI_Boolean ShouldAutoCorrect(_In_z_ MI_Char* configurationMode)
{
    if(Tcscasecmp(configurationMode, DSC_CONFIGURATIONMODE_APPLYANDAUTOCORRECT)==0)
    {
        return MI_TRUE;
    }
    return MI_FALSE;
}

MI_Boolean ShouldMonitor(_In_z_ MI_Char* configurationMode)
{
    if(Tcscasecmp(configurationMode, DSC_CONFIGURATIONMODE_APPLYANDMONITOR) == 0)
    {
        return MI_TRUE;
    }
    return MI_FALSE;
}

MI_Boolean ShouldMonitorOnly(_In_z_ MI_Char* configurationMode)
{
    if (Tcscasecmp(configurationMode, DSC_CONFIGURATIONMODE_MONITORONLY) == 0)
    {
        return MI_TRUE;
    }
    return MI_FALSE;
}

MI_Result ValidateConfigurationDirectory(
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result result;
    MI_Char *pwConfigDir;

    if (cimErrorDetails == NULL)
    {        
        return MI_RESULT_INVALID_PARAMETER; 
    }
    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once. 

    result = ExpandPath(CONFIGURATION_LOCATION, &pwConfigDir, cimErrorDetails);
    if (result != MI_RESULT_OK)
    {
        return result;
    }

    if (Directory_ExistT(pwConfigDir) == -1)
    {
        DSC_free(pwConfigDir);
        return GetCimMIError(MI_RESULT_FAILED, cimErrorDetails, ID_LCMHELPER_CREATE_CONFDIR_FAILED);
    }
    DSC_free(pwConfigDir);
    return MI_RESULT_OK;
}

MI_Result ExpandPath(
    _In_z_ const MI_Char * pathIn,
    _Outptr_result_maybenull_z_ MI_Char **expandedPath, 
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Uint32 dwReturnSizeInitial = 0;

    dwReturnSizeInitial = Tcslen(pathIn) + 1;

    if (cimErrorDetails == NULL)
    {        
        return MI_RESULT_INVALID_PARAMETER; 
    }
    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once. 

    *expandedPath = (MI_Char*)DSC_malloc(dwReturnSizeInitial* sizeof(MI_Char), NitsHere());
    if (*expandedPath == NULL)
    {
        return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_LCMHELPER_MEMORY_ERROR);
    }

    memcpy(*expandedPath, pathIn, dwReturnSizeInitial* sizeof(MI_Char));

    return MI_RESULT_OK;
}

/*
save the available configurations to temp pending file
*/
MI_Result FilterPartialConfigurations(
        _In_ LCMProviderContext *lcmContext,
        _In_ ModuleManager *moduleManager,
        _In_ MI_Uint32 flags,
        _In_ MI_Instance * documentIns,
        _In_ MI_InstanceA *resourceInstances,
        _In_ MI_Instance *metaConfigInstance,
        _Inout_ MI_Uint32 *resultStatus,
        _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
        MI_Result result;
        PAL_UNUSED(moduleManager);
        PAL_UNUSED(flags);
        PAL_UNUSED(metaConfigInstance);
        PAL_UNUSED(resultStatus);

        result = SerializeInstanceArrayToFile(resourceInstances, GetPendingConfigTmpFileName(), cimErrorDetails, MI_T("ab"), MI_FALSE, lcmContext->serializer);
        if (result != MI_RESULT_OK)
                return result;

        result = SerializeSingleInstanceToFile(documentIns, GetPartialConfigBaseDocumentInstanceTmpFileName(), cimErrorDetails, MI_T("ab"), MI_FALSE, lcmContext->serializer);
        return result;
}

MI_Result MergePartialConfigurations(_In_ LCMProviderContext *lcmContext,
        _In_ ModuleManager* moduleManager,
        _In_z_ const MI_Char* targetMofFile,
        _In_z_ const MI_Char* targetBaseDocumentMergedFile,
        _Outptr_result_maybenull_ MI_Instance** cimErrorDetails)
{
        MI_Result result;
        MI_Char *partialConfigDir = NULL;
        MI_Instance* metaConfigInstance = NULL;
        MI_Char* unexpandedPartialConfigFilePath = NULL;
        MI_Char* partialConfigFilePath = NULL;
        MI_Char* checksumFile = NULL;
        MI_InstanceA resourceInstanceArray = { 0 };
        Internal_Dir *dirHandle = NULL;
        MI_Application application = MI_APPLICATION_NULL;
        MI_Boolean applicationInited = MI_FALSE;
        MI_Serializer serializer = { 0 };
        MI_Value value;
        MI_Boolean errorOccured = MI_FALSE;
        MI_Boolean serializerInited = MI_FALSE;
        MI_Boolean isLocked = MI_FALSE;
        MI_Boolean newBaseDocumentIsPlaced = MI_FALSE; //Bool value marked to true if we added our new base doc instance
        Internal_DirEnt *dirEntry = NULL;
        MI_Instance * baseDocumentInstance = NULL;
        MI_Uint32 resultStatus = 0;

        /****************************** INITIALIZE EVERYTHING *******************************************/
        if (cimErrorDetails == NULL || targetMofFile == NULL || targetBaseDocumentMergedFile == NULL || moduleManager == NULL)
        {
                return MI_RESULT_INVALID_PARAMETER;
        }
        *cimErrorDetails = NULL;
        result = LoadModuleManager(moduleManager, cimErrorDetails);
        RETURN_RESULT_IF_FAILED(result);
        result = ExpandPath(CONFIGURATION_PARTIALCONFIG_STORE, &partialConfigDir, cimErrorDetails);
        RETURN_RESULT_IF_FAILED(result);

        if (File_ExistT(GetPendingConfigFileName()) != -1)
        {
                File_RemoveT(GetPendingConfigFileName());
        }
        if (File_ExistT(GetPartialConfigBaseDocumentInstanceFileName()) != -1)
        {
                File_RemoveT(GetPartialConfigBaseDocumentInstanceFileName());
        }

        result = MI_Application_Initialize(0, NULL, NULL, &application);
        GOTO_CLEANUP_AND_THROW_ERROR_IF_FAILED(result, result, ID_LCMHELPER_ERRORMERGING_PARTIALCONFIGS, cimErrorDetails, Exit)
                applicationInited = MI_TRUE;

        result = MI_Application_NewSerializer_Mof(&application, 0, MOFCODEC_FORMAT, &serializer);
        GOTO_CLEANUP_AND_THROW_ERROR_IF_FAILED(result, result, ID_LCMHELPER_ERRORMERGING_PARTIALCONFIGS, cimErrorDetails, Exit)
                serializerInited = MI_TRUE;

        //We know the directory exists for sure here so no need for checking for that.
        //Open directory and check for the first obtained file
        dirHandle = Internal_Dir_Open(partialConfigDir, NitsMakeCallSite(-3, NULL, NULL, 0));
        if (dirHandle == NULL || NitsShouldFault(NitsHere(), NitsAutomatic))
        {
                result = GetCimMIError(MI_RESULT_FAILED, cimErrorDetails, ID_MODMAN_FINDFIRST_FAILED);
                goto Exit;
        }
        dirEntry = Internal_Dir_Read(dirHandle, MOF_EXTENSION);
        //From now on until pending.mof is fully done, lock everything

        RecursiveLock_Acquire(&gExecutionLock);
        isLocked = MI_TRUE;
        result = GetMetaConfig((MSFT_DSCMetaConfiguration **) &metaConfigInstance);
        GOTO_CLEANUP_IF_FAILED(result, Exit);

        DSC_EventWriteLCMAboutToMergePartial();
        /****************************** LOGIC TO DESERIALIZE AND SERIALIZEs *******************************************/
        //For each file that is obtained, merge it up into the new pending.mof
        while (dirEntry != NULL)
        {
                /* Only process files*/
                if (!dirEntry->isDir)
                {

                        //Concatenate directory and filename
                        result = GetFullPath(partialConfigDir, dirEntry->name, &unexpandedPartialConfigFilePath, cimErrorDetails);
                        GOTO_CLEANUP_IF_FAILED(result, Exit);

                        result = ExpandPath(unexpandedPartialConfigFilePath, &partialConfigFilePath, cimErrorDetails);
                        DSC_free(unexpandedPartialConfigFilePath);
                        GOTO_CLEANUP_IF_FAILED(result, Exit);
                        //Validate the configuration - if its correctly placed - sometimes it could have been published first , n then run another set, and you still need to validate, if not valid, silently delete
                        result = ValidatePartialConfiguration(moduleManager, partialConfigFilePath, metaConfigInstance, cimErrorDetails);
                        if (result != MI_RESULT_OK)
                        {
                                if (*cimErrorDetails)
                                {//Output the warning with any possible underlying error that came with the above function calls
                                        DSC_WriteWarningFromError1Param((MI_Context*) lcmContext->context, cimErrorDetails, ID_LCM_PARTIALCONFIG_DELETINGFILE_WARNING, partialConfigFilePath);
                                        INSTANCE_DELETE_IF_NOT_NULL(*cimErrorDetails);
                                }
                                File_RemoveT(partialConfigFilePath); //Delete the file since its irrelevant
                               
                                ConcatStrings(&checksumFile, 0, partialConfigFilePath, CHECKSUM_EXTENSION);
                                File_RemoveT(checksumFile); //Delete the corresponding checksum file as well 
                                
                                dirEntry = Internal_Dir_Read(dirHandle, MOF_EXTENSION);
                                DSCFREE_IF_NOT_NULL(partialConfigFilePath);
                                DSCFREE_IF_NOT_NULL(checksumFile);
                                                                
                result = MI_RESULT_OK;
                                errorOccured = MI_TRUE;
                                continue; //Carry on to the next file
                        }
                        //Check if the required modules are present
                        
                        /*
                          result = DoPushDependencyCheck((MI_Char*) partialConfigFilePath, cimErrorDetails);
                        
                        if (result == MI_RESULT_OK)
                        {
                                result = moduleManager->ft->LoadInstanceDocumentFromLocation(moduleManager, VALIDATE_DOCUMENT_INSTANCE, partialConfigFilePath, cimErrorDetails, &resourceInstanceArray, &baseDocumentInstance);
                        }
                        */

                        result = moduleManager->ft->LoadInstanceDocumentFromLocation(moduleManager, VALIDATE_DOCUMENT_INSTANCE, partialConfigFilePath, cimErrorDetails, &resourceInstanceArray, &baseDocumentInstance);

                        //If any of the above operations failed, just skip the file and continue to the next one - give them a warning that you're doing this
                        if (result != MI_RESULT_OK)
                        {
                                if (*cimErrorDetails)
                                { //Output the warning with any possible underlying error that came with the above function calls
                                        DSC_WriteWarningFromError1Param((MI_Context*) lcmContext->context, cimErrorDetails, ID_LCM_PARTIALCONFIG_SKIPFILE_WARNING, partialConfigFilePath);
                                        INSTANCE_DELETE_IF_NOT_NULL(*cimErrorDetails);
                                }
                                dirEntry = Internal_Dir_Read(dirHandle, MOF_EXTENSION);
                                DSCFREE_IF_NOT_NULL(partialConfigFilePath);
                                result = MI_RESULT_OK;
                                errorOccured = MI_TRUE;
                                continue; //Carry on to the next file
                        }

                        //Get the partial configuration name from the document
                        result = DSC_MI_Instance_GetElement(baseDocumentInstance, OMI_ConfigurationDocument_Name, &value, NULL, NULL, NULL);
                        GOTO_CLEANUP_IF_FAILED(result, Exit);
                        DSC_EventWriteLCMMergingPartialConfiguration(value.string);

                        //For now its just a simple merge, put all of these deserialized stuff into another file and we'll be done, yay!
                        result = SerializeInstanceArrayToFile(&resourceInstanceArray, GetPendingConfigFileName(), cimErrorDetails, MI_T("ab"), MI_FALSE, &serializer);
                        GOTO_CLEANUP_IF_FAILED(result, Exit);
                        //Free the path for the partial config, to rellocate for the next one.
                        DSCFREE_IF_NOT_NULL(partialConfigFilePath);
                        result = SerializeSingleInstanceToFile(baseDocumentInstance, GetPartialConfigBaseDocumentInstanceFileName(), cimErrorDetails, MI_T("ab"), MI_FALSE, &serializer);
                        GOTO_CLEANUP_IF_FAILED(result, Exit);
                        //Set in the unique base document - happens one time only
                        if (!newBaseDocumentIsPlaced)
                        {
                                value.string = OMI_ConfigurationDocument_PartialConfigAuthor;
                                result = MI_Instance_SetElement(baseDocumentInstance, OMI_ConfigurationDocument_Author, &value, MI_STRING, 0);
                                GOTO_CLEANUP_IF_FAILED(result, Exit);
                                value.string = OMI_ConfigurationDocument_PartialConfigName;
                                result = MI_Instance_SetElement(baseDocumentInstance, OMI_ConfigurationDocument_Name, &value, MI_STRING, 0);
                                GOTO_CLEANUP_IF_FAILED(result, Exit);
                                result = SerializeSingleInstanceToFile(baseDocumentInstance, GetPendingConfigFileName(), cimErrorDetails, MI_T("ab"), MI_FALSE, &serializer);
                                GOTO_CLEANUP_IF_FAILED(result, Exit);
                                newBaseDocumentIsPlaced = MI_TRUE;
                        }
                        //We can't have multiple base document instances, so remove them from each (we'll add one at the end)
                        INSTANCE_DELETE_IF_NOT_NULL(baseDocumentInstance);
                        CleanUpInstanceCache(&resourceInstanceArray); //To clear old file's data                        
                }
                dirEntry = Internal_Dir_Read(dirHandle, MOF_EXTENSION);
        }//End of recursing directory files

        //Now check if the merged file is valid
        if (File_ExistT(GetPartialConfigBaseDocumentInstanceFileName()) != -1)
        {
                result = ValidatePartialConfigMergedFile(moduleManager, GetPendingConfigFileName(), cimErrorDetails);
        }
        else
        {
            if (errorOccured)
            {
                //This means there was an error and hence partial configurations couldn't be merged
                result = GetCimMIError(MI_RESULT_NOT_FOUND, cimErrorDetails, ID_PARTIALCONFIG_FAILEDPARTIALCONFIGS);
            }
            else
            {
                //That means there was no partial config file found that was valid, so throw error
                result = GetCimMIError(MI_RESULT_NOT_FOUND, cimErrorDetails, ID_PARTIALCONFIG_NOPARTIALCONFIGPRESENT);
            }
        }
        GOTO_CLEANUP_IF_FAILED(result, Exit);

        // we have a basic merged pending mof file, We will doing filtering to remove the ones that dependsOn is not satisfied
        result = moduleManager->ft->LoadInstanceDocumentFromLocation(moduleManager, 0, GetPendingConfigFileName(), cimErrorDetails, &resourceInstanceArray, &baseDocumentInstance);

        GOTO_CLEANUP_IF_FAILED(result, Exit);
        // clean up the temp file so the filtered configuration will be saved there
        if (File_ExistT(GetPendingConfigTmpFileName()) != -1)
        {
                File_RemoveT(GetPendingConfigTmpFileName());
        }
        if (File_ExistT(GetPartialConfigBaseDocumentInstanceTmpFileName()) != -1)
        {
                File_RemoveT(GetPartialConfigBaseDocumentInstanceTmpFileName());
        }

        lcmContext->serializer = &serializer;
        result = ProcessPartialConfigurations(lcmContext, moduleManager, 0, &resultStatus, FilterPartialConfigurations, metaConfigInstance, &resourceInstanceArray, cimErrorDetails);
        GOTO_CLEANUP_IF_FAILED(result, Exit);

        result = SerializeSingleInstanceToFile(baseDocumentInstance, GetPendingConfigTmpFileName(), cimErrorDetails, MI_T("ab"), MI_FALSE, &serializer);
        GOTO_CLEANUP_IF_FAILED(result, Exit);
        if (File_ExistT(GetPendingConfigFileName()) != -1)
        {
                File_RemoveT(GetPendingConfigFileName());
        }
        if (File_ExistT(GetPartialConfigBaseDocumentInstanceFileName()) != -1)
        {
                File_RemoveT(GetPartialConfigBaseDocumentInstanceFileName());
        }

        // If Current.mof exists, move Current.mof to Previous.mof
        if (File_ExistT(GetCurrentConfigFileName()) == 0)
        {
            result = CopyConfigAndRemoveSource(CONFIGURATION_LOCATION_CURRENT, CONFIGURATION_LOCATION_PREVIOUS, cimErrorDetails);
            GOTO_CLEANUP_IF_FAILED(result, Exit);
        }

        File_CopyT(GetPendingConfigTmpFileName(), GetPendingConfigFileName());
        File_CopyT(GetPartialConfigBaseDocumentInstanceTmpFileName(), GetPartialConfigBaseDocumentInstanceFileName());

        /****************************** CLEANUIP AND RETURN *******************************************/
Exit:
        DSCFREE_IF_NOT_NULL(partialConfigFilePath);
        DSCFREE_IF_NOT_NULL(partialConfigDir);
        INSTANCE_DELETE_IF_NOT_NULL(baseDocumentInstance);
        INSTANCE_DELETE_IF_NOT_NULL(metaConfigInstance);
        //Note: No need to clean partialConfigInstance variable as it points to a part of metaconfiguration
        if (resourceInstanceArray.data != NULL)
        {
                CleanUpInstanceCache(&resourceInstanceArray);
        }
        if (serializerInited)
        {
                MI_Serializer_Close(&serializer);
                serializerInited = MI_FALSE;
        }

        if (applicationInited)
        {
                MI_Application_Close(&application);
                applicationInited = MI_FALSE;
        }
        if (isLocked)
        {
                RecursiveLock_Release(&gExecutionLock);
                isLocked = MI_FALSE;
        }
        if (dirHandle != NULL)
                Internal_Dir_Close(dirHandle);
        return result;
}

/*Function to add a single instance to a destination file*/
MI_Result SerializeSingleInstanceToFile(_In_ MI_Instance *miInstance,
        _In_z_ const MI_Char* filePath,
        _Outptr_result_maybenull_ MI_Instance** cimErrorDetails,
        _In_z_ const MI_Char* fileOpenMode,
        _In_ MI_Boolean isLockSensitive,
        _Inout_ MI_Serializer *pSerializer)
{
        MI_Uint32 bufferSize = 0;
        MI_Char * buffer = NULL;
        MI_Boolean locked = MI_FALSE;
        MI_Result result = MI_RESULT_OK;

        BOOL bFileInGoodState = FALSE;
        FILE *fp = NULL;
        size_t writeSize = 0;
        if (filePath == NULL || fileOpenMode == NULL || pSerializer == NULL || miInstance == NULL)
        {
                return MI_RESULT_INVALID_PARAMETER;
        }
        *cimErrorDetails = NULL;
        fp = File_OpenT(filePath, fileOpenMode);
        if (fp == NULL)
        {
                result = GetCimMIError1Param(MI_RESULT_FAILED, cimErrorDetails, ID_ENGINEHELPER_OPENFILE_ERROR, filePath);
                goto Exit;
        }
        if (isLockSensitive)
        {
                RecursiveLock_Acquire(&gExecutionLock);
                locked = MI_TRUE;
        }
        bFileInGoodState = FALSE;

        if (buffer)
        {
                DSC_free(buffer);
                buffer = NULL;
                bufferSize = 0;
        }
        // get the size of buffer, ignore the API return (MI_RESULT_FAILED)
        MI_Serializer_SerializeInstance(pSerializer, 0, miInstance, NULL, 0, &bufferSize);

        buffer = (MI_Char *) DSC_malloc(bufferSize, NitsHere());
        if (buffer == NULL)
        {
                result = GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_ENGINEHELPER_MEMORY_ERROR);
                goto Exit;
        }

        result = MI_Serializer_SerializeInstance(pSerializer, 0, miInstance, (MI_Uint8*) buffer, bufferSize, &bufferSize);
        GOTO_CLEANUP_AND_THROW_ERROR_IF_FAILED(result, MI_RESULT_FAILED, ID_LCMHELPER_ERROR_DURING_SERIALIZING, cimErrorDetails, Exit)

                writeSize = fwrite((MI_Uint8*) buffer, 1, bufferSize, fp);
        if (writeSize != bufferSize)
        {
                result = GetCimMIError1Param(MI_RESULT_FAILED, cimErrorDetails, ID_LCMHELPER_ERROR_WRITINGFILE, filePath);
                goto Exit;
        }

        bFileInGoodState = TRUE;

Exit:

        if (fp != NULL)
        {
                File_Close(fp);
                fp = NULL;
        }

        if (!bFileInGoodState)
        {
                File_RemoveT(filePath);
        }

        if (locked && isLockSensitive)
        {
                RecursiveLock_Release(&gExecutionLock);
                locked = MI_FALSE;
        }

        if (buffer != NULL)
        {
                DSC_free(buffer);
                buffer = NULL;
        }
        return result;
}

/*Function to add instances into the destination file.*/
MI_Result SerializeInstanceArrayToFile(_In_ MI_InstanceA *miInstanceArray,
        _In_z_ const MI_Char* filePath,
        _Outptr_result_maybenull_ MI_Instance** cimErrorDetails,
        _In_z_ const MI_Char* fileOpenMode,
        _In_ MI_Boolean isLockSensitive,
        _Inout_ MI_Serializer *pSerializer)
{
        MI_Uint32 bufferSize = 0;
        MI_Char * buffer = NULL;
        MI_Boolean locked = MI_FALSE;
        MI_Result result = MI_RESULT_OK;
        MI_Uint32 xCount = 0;
        MI_Uint32 totalSize = 0;

        BOOL bFileInGoodState = FALSE;
        FILE *fp = NULL;
        size_t writeSize = 0;
        if (filePath == NULL || fileOpenMode == NULL || pSerializer == NULL || miInstanceArray == NULL || cimErrorDetails == NULL)
        {
                return MI_RESULT_INVALID_PARAMETER;
        }
        *cimErrorDetails = NULL;
        fp = File_OpenT(filePath, fileOpenMode);
        if (fp == NULL)
        {
                GetCimMIError1Param(MI_RESULT_FAILED, cimErrorDetails, ID_ENGINEHELPER_OPENFILE_ERROR, filePath);

                result = MI_RESULT_FAILED;
                goto Exit;
        }
        if (isLockSensitive)
        {
                RecursiveLock_Acquire(&gExecutionLock);
                locked = MI_TRUE;
        }
        bFileInGoodState = FALSE;
        for (xCount = 0; xCount < miInstanceArray->size; xCount++)
        {
                MI_Instance * pInstance = miInstanceArray->data[xCount];

                if (buffer)
                {
                        DSC_free(buffer);
                        buffer = NULL;
                        bufferSize = 0;
                }
                // get the size of buffer, ignore the API return (MI_RESULT_FAILED)
                MI_Serializer_SerializeInstance(pSerializer, 0, pInstance, NULL, 0, &bufferSize);

                // check total size
                totalSize += bufferSize;
                if (totalSize > MAX_MOFSIZE)
                {
                        result = GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_ENGINEHELPER_FILESIZE_ERROR);
                        goto Exit;
                }

                buffer = (MI_Char *) DSC_malloc(bufferSize, NitsHere());
                if (buffer == NULL)
                {
                        result = GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_ENGINEHELPER_MEMORY_ERROR);
                        goto Exit;
                }

                result = MI_Serializer_SerializeInstance(pSerializer, 0, pInstance, (MI_Uint8*) buffer, bufferSize, &bufferSize);
                GOTO_CLEANUP_AND_THROW_ERROR_IF_FAILED(result, MI_RESULT_FAILED, ID_LCMHELPER_ERROR_DURING_SERIALIZING, cimErrorDetails, Exit)


                        writeSize = fwrite((MI_Uint8*) buffer, 1, bufferSize, fp);
                if (writeSize != bufferSize)
                {
                        result = GetCimMIError1Param(MI_RESULT_FAILED, cimErrorDetails, ID_LCMHELPER_ERROR_WRITINGFILE, filePath);
                        goto Exit;
                }
        }
        bFileInGoodState = TRUE;

Exit:

        if (fp != NULL)
        {
                File_Close(fp);
                fp = NULL;
        }

        if (!bFileInGoodState)
        {
                File_RemoveT(filePath);
        }

        if (locked && isLockSensitive)
        {
                RecursiveLock_Release(&gExecutionLock);
                locked = MI_FALSE;
        }

        if (buffer != NULL)
        {
                DSC_free(buffer);
                buffer = NULL;
        }



        return result;
}

MI_Result GetFullPath(
        _In_z_ const MI_Char* directoryName,
        _In_z_ const MI_Char *fileName,
        _Outptr_result_maybenull_z_ MI_Char **fullPath,
        _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
        size_t dwPathSize;
        int retValue;

        if (cimErrorDetails == NULL)
        {
                return MI_RESULT_INVALID_PARAMETER;
        }
        *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once. 

        dwPathSize = Tcslen(directoryName) + Tcslen(fileName) + 1 + 1; // one for '\\' and one for null
        *fullPath = (MI_Char*) DSC_malloc(dwPathSize* sizeof(MI_Char), NitsHere());
        if (*fullPath == NULL)
        {
                return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_LCMHELPER_MEMORY_ERROR);
        }

        retValue = Stprintf(*fullPath, dwPathSize, MI_T("%T/%T"), directoryName, fileName);
        
        if (retValue == -1 || NitsShouldFault(NitsHere(), NitsAutomatic))
        {
                DSC_free(*fullPath);
                return GetCimMIError(MI_RESULT_FAILED, cimErrorDetails, ID_LCMHELPER_PRINTF_ERROR);
        }

        return MI_RESULT_OK;
}

MI_Result GetPartialConfigurationPathWithExtension(
        _In_z_ const MI_Char *partialConfigName,
        _Outptr_result_maybenull_z_ MI_Char **partialConfigFullPath,
        _In_z_ const MI_Char *fileExtensionName,
        _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
        size_t dwPathSize;
        int retValue;
        MI_Char * expandedFullPath = NULL;
        if (partialConfigName == NULL)
        {
                return GetCimMIError(MI_RESULT_INVALID_PARAMETER, cimErrorDetails, ID_LCMHELPER_PARTIALCONFIGNAME_NOT_SET);
        }
        if (cimErrorDetails == NULL)
        {
                return MI_RESULT_INVALID_PARAMETER;
        }
        *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once. 
        if (Directory_Exist(GetPartialConfigDataStore()) != 0) //It will be 2 if it doesn't exist - ENOENT
        {
            if (mkdir(GetPartialConfigDataStore(), 0700) != 0) // which means it failed
            {
                return GetCimMIError(MI_RESULT_NOT_FOUND, cimErrorDetails, ID_PARTIALCONFIG_STORECANNOTBE_CREATED);
            }
        }
        dwPathSize = Tcslen(GetPartialConfigDataStore()) + Tcslen(GetPartialConfigSuffix()) + Tcslen(partialConfigName) + Tcslen(fileExtensionName) + 3; // one for '\\' and one for null and one for "_"
        expandedFullPath = (MI_Char*) DSC_malloc(dwPathSize* sizeof(MI_Char), NitsHere());
        if (expandedFullPath == NULL)
        {
                return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_LCMHELPER_MEMORY_ERROR);
        }

        retValue = Stprintf(expandedFullPath, dwPathSize, MI_T("%T/%T_%T%T"), GetPartialConfigDataStore(), GetPartialConfigSuffix(), partialConfigName, fileExtensionName);
        
        if (retValue == -1 || NitsShouldFault(NitsHere(), NitsAutomatic))
        {
                DSC_free(expandedFullPath);
                return GetCimMIError(MI_RESULT_FAILED, cimErrorDetails, ID_LCMHELPER_PRINTF_ERROR);
        }

        *partialConfigFullPath = expandedFullPath;
        return MI_RESULT_OK;
}

/*Function to get the path of partial configuration inside the store*/
MI_Result GetPartialConfigurationPath(
        _In_z_ const MI_Char *partialConfigName,
        _Outptr_result_maybenull_z_ MI_Char **partialConfigFullPath,
        _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
        return GetPartialConfigurationPathWithExtension(partialConfigName, partialConfigFullPath, GetMofExtension(), cimErrorDetails);
}

/*Function to get the checksum path of partial configuration inside the store*/
MI_Result GetPartialConfigurationPathCheckSum(
        _In_z_ const MI_Char *partialConfigName,
        _Outptr_result_maybenull_z_ MI_Char **partialConfigFullPath,
        _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
        return GetPartialConfigurationPathWithExtension(partialConfigName, partialConfigFullPath, GetMofChecksumExtension(), cimErrorDetails);
}



//Function to check if the file exists, delete it if force is used, and then save the configuration data into the destination file path.
MI_Result CheckAndSaveConfigDataIntoFile(_In_z_ const MI_Char *destinationfilePath,
        _In_reads_bytes_(dataSize) const MI_Uint8 *ConfigData,
        _In_ MI_Uint32 dataSize,
        _In_ MI_Boolean force,
        _Outptr_result_maybenull_ MI_Instance ** cimErrorDetails,
        _In_ MI_Uint32 deleteErrorStringID,
        _In_ MI_Uint32 saveErrorStringID)
{
        MI_Result result;
        if (cimErrorDetails == NULL || ConfigData == NULL)
        {
                return MI_RESULT_INVALID_PARAMETER;
        }
        *cimErrorDetails = NULL;
        if (File_ExistT(destinationfilePath) != -1)
        {
                if (force == MI_TRUE)
                {
                        result = RetryDeleteFile(destinationfilePath);
                        if (result != MI_RESULT_OK)
                        {
                                return GetCimMIError(result, cimErrorDetails, deleteErrorStringID);
                        }
                }
                else
                {
                        return GetCimMIError(MI_RESULT_ALREADY_EXISTS, cimErrorDetails, deleteErrorStringID);
                }
        }
        result = SaveFile(destinationfilePath, ConfigData, dataSize, cimErrorDetails);

        if (result != MI_RESULT_OK)
        {
                if (cimErrorDetails && *cimErrorDetails)
                {
                        return result;
                }
                return GetCimMIError(MI_RESULT_ALREADY_EXISTS, cimErrorDetails, saveErrorStringID);
        }
        return result;
}

MI_Result SetConfiguration(
    _In_reads_bytes_(dataSize) const MI_Uint8* ConfigData,
    MI_Uint32 dataSize,
    MI_Boolean force,
    _In_ LCMProviderContext *lcmContext,
    MI_Uint32 dwFlags,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result result = MI_RESULT_OK;
    MI_Result deleteResult = MI_RESULT_OK;
    ModuleManager *moduleManager = NULL;
    MSFT_DSCMetaConfiguration *metaConfigInstance = NULL;
    MI_Value value;
    MI_Uint32 flags;
    MI_Uint32 resultStatus = 0;
    MI_Char *partialConfigFileDataStoreLocation = NULL;

    //Debug Log 
    DSC_EventWriteLocalConfigMethodParameters(__WFUNCTION__,dataSize,dwFlags,lcmContext->executionMode);

    if (cimErrorDetails == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER; 
    }

    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once. 
    result = ValidateConfigurationDirectory(cimErrorDetails);
    if (result != MI_RESULT_OK)
    {   
        if (cimErrorDetails && *cimErrorDetails)
        {
            return result;
        }

        return GetCimMIError(result, cimErrorDetails, ID_LCMHELPER_VALIDATE_CONFGDIR_FAILED);
    }

    result = InitializeModuleManager(0, cimErrorDetails, &moduleManager);
    if (result != MI_RESULT_OK)
    {      
        return result;
    }

    if (moduleManager == NULL)
    {    
        return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_ENGINEHELPER_MEMORY_ERROR);         
    }

    result = GetMetaConfig((MSFT_DSCMetaConfiguration **)&metaConfigInstance);
    if (result != MI_RESULT_OK)
    {
        moduleManager->ft->Close(moduleManager, NULL);
        return GetCimMIError(result, cimErrorDetails, ID_LCM_FAILED_TO_GET_METACONFIGURATION);
    }

    result = MI_Instance_GetElement((MI_Instance*)metaConfigInstance, MSFT_DSCMetaConfiguration_RefreshMode, &value, NULL, &flags, NULL);
    if (result != MI_RESULT_OK)
    {
        MI_Instance_Delete((MI_Instance *)metaConfigInstance);
        moduleManager->ft->Close(moduleManager, NULL);
        return GetCimMIError(result, cimErrorDetails, ID_LCM_FAILED_TO_GET_METACONFIGURATION);
    }

    if (flags & MI_FLAG_NULL)
    {
        value.string = METADATA_REFRESHMODE_PUSH;
    }

    if (Tcscasecmp(value.string , METADATA_REFRESHMODE_PULL) == 0)
    {
        /* Force converts the PULL to a PUSH otherwise PULL cannot convert to PUSH */
        if (force == MI_TRUE)
        {
            value.string = METADATA_REFRESHMODE_PUSH;
            result = MI_Instance_SetElement((MI_Instance*)metaConfigInstance,  MSFT_DSCMetaConfiguration_RefreshMode, &value, MI_STRING, 0);
            if (result != MI_RESULT_OK)
            {
                MI_Instance_Delete((MI_Instance *)metaConfigInstance);
                moduleManager->ft->Close(moduleManager, NULL);
                return result;
            }

            result = SetMetaConfig((MI_Instance*)metaConfigInstance, cimErrorDetails);
            if (result != MI_RESULT_OK)
            {
                MI_Instance_Delete((MI_Instance *)metaConfigInstance);
                moduleManager->ft->Close(moduleManager, NULL);
                return result;
            }
        }
        else
        {
            MI_Instance_Delete((MI_Instance *)metaConfigInstance);
            moduleManager->ft->Close(moduleManager, NULL);
            return GetCimMIError(MI_RESULT_INVALID_PARAMETER, cimErrorDetails, ID_LCM_PUSH_REQUESTED_ON_PULL_WO_FORCE);
        }
    }

    if (dwFlags & LCM_SET_METACONFIG)
    {
        MI_Instance_Delete((MI_Instance *)metaConfigInstance);
        result = SaveFile(GetMetaConfigTmpFileName(), ConfigData, dataSize, cimErrorDetails);
        if (result != MI_RESULT_OK)
        {
            moduleManager->ft->Close(moduleManager, NULL);
            if (cimErrorDetails && *cimErrorDetails)
            {
                return result;
            }

            return GetCimMIError(MI_RESULT_ALREADY_EXISTS, cimErrorDetails, ID_LCMHELPER_SAVE_METATMP_ERROR);
        }

        dwFlags |= VALIDATE_METACONFIG_INSTANCE;
        result = ApplyMetaConfig(lcmContext, moduleManager, dwFlags, &resultStatus, cimErrorDetails);
        moduleManager->ft->Close(moduleManager, NULL);
        deleteResult = RetryDeleteFile(GetMetaConfigTmpFileName());
        if (result != MI_RESULT_OK)
        {
            return result;
        }

        if (deleteResult != MI_RESULT_OK)
        {
            return GetCimMIError(deleteResult, cimErrorDetails, ID_LCMHELPER_DEL_METATMPFILEAFTER_FAILED);
        } 
    }
    else
    {
        //If there's a partial configuration in the metaconfig, do not save as pending, save in data store instead.
        if (ShouldUsePartialConfigurations((MI_Instance*)metaConfigInstance, MI_FALSE)) //Don't need to check if there are files in the partial config directory
        {
            //If it isn't a publishonly command, then this is not supported. Throw an error
            if (!(dwFlags & LCM_SETFLAGS_SAVETOPENDINGONLY))
            {
                result = GetCimMIError(MI_RESULT_NOT_SUPPORTED, cimErrorDetails, ID_LCM_CANNOTUSE_PARTIALCONFIG_PUSHMODE_WITHOUTPUBLISH);
                goto Cleanup;
            }
            DSC_EventWriteLCMIdentifiedModePartial();
            
            //Save the partial config file into the data store.
            result = GetPartialConfigStoreLocation(moduleManager, ConfigData, dataSize, cimErrorDetails, &partialConfigFileDataStoreLocation);
            GOTO_CLEANUP_IF_FAILED(result, Cleanup);
            //Store configFileName as path c:\windows\system32\configuration\partialconfigurations +_+ value.string + .mof
            result = CheckAndSaveConfigDataIntoFile(partialConfigFileDataStoreLocation, ConfigData, dataSize, MI_TRUE, cimErrorDetails, ID_LCMHELPER_OLDCONFPENDING_ERROR, ID_LCMHELPER_SAVE_PENDING_ERROR);
            //Note, you're passing force as true over here always because you want to delete the old existing partial configs no matter what. Check if this is okay , for now we go with this
            GOTO_CLEANUP_IF_FAILED(result, Cleanup);
            result = ValidatePartialConfiguration(moduleManager, partialConfigFileDataStoreLocation, (MI_Instance*) metaConfigInstance, cimErrorDetails);
            GOTO_CLEANUP_IF_FAILED(result, Cleanup);
            
            SetMessageInContext(ID_OUTPUT_OPERATION_END, ID_OUTPUT_ITEM_SET, lcmContext);
            LCM_BuildMessage(lcmContext, ID_LCM_WRITEMESSAGE_SAVETOPARTIAL, EMPTY_STRING, MI_WRITEMESSAGE_CHANNEL_VERBOSE);

Cleanup:
            
            moduleManager->ft->Close(moduleManager, NULL);
            //If there was a failure, ensure the partial config file(if exists) is deleted.
            if (result != MI_RESULT_OK && partialConfigFileDataStoreLocation != NULL)
            {
                RetryDeleteFile(partialConfigFileDataStoreLocation);
            }
            DSCFREE_IF_NOT_NULL(partialConfigFileDataStoreLocation);
        }
        else //Only in the case of no partial configuration should you save it in pending.mof and do pushdependency check.
        {
            // If Current.mof exists, move Current.mof to Previous.mof
            if (File_ExistT(GetCurrentConfigFileName()) == 0)
            {
                result = CopyConfigAndRemoveSource(CONFIGURATION_LOCATION_CURRENT, CONFIGURATION_LOCATION_PREVIOUS, cimErrorDetails);
                if (result != MI_RESULT_OK)
                {
                    MI_Instance_Delete((MI_Instance *)metaConfigInstance);            
                    moduleManager->ft->Close(moduleManager, NULL);
                    return result;
                }
            }

            // If Pending.mof exists, throw an error if Force was not specified
            if (File_ExistT(GetPendingConfigFileName()) != -1)
            {
                if (force == MI_TRUE)
                {
                    deleteResult = RetryDeleteFile(GetPendingConfigFileName());
                    if (deleteResult != MI_RESULT_OK)
                    {
                        MI_Instance_Delete((MI_Instance *)metaConfigInstance);            
                        moduleManager->ft->Close(moduleManager, NULL);
                        return GetCimMIError(deleteResult, cimErrorDetails, ID_LCMHELPER_OLDCONFPENDING_ERROR);
                    }
                }
                else
                {
                    MI_Instance_Delete((MI_Instance *)metaConfigInstance);            
                    moduleManager->ft->Close(moduleManager, NULL);
                    return GetCimMIError(MI_RESULT_ALREADY_EXISTS, cimErrorDetails, ID_LCMHELPER_OLDCONFPENDING_ERROR);
                }
            }
            
            // Save the new configuration to Pending.mof
            result = SaveFile(GetPendingConfigFileName(), ConfigData, dataSize, cimErrorDetails);
            if ( result != MI_RESULT_OK)
            {
                MI_Instance_Delete((MI_Instance *)metaConfigInstance);            
                moduleManager->ft->Close(moduleManager, NULL);
                if (cimErrorDetails && *cimErrorDetails)
                {
                    return result;
                }
                
                return GetCimMIError(MI_RESULT_ALREADY_EXISTS, cimErrorDetails, ID_LCMHELPER_SAVE_PENDING_ERROR);
            }
            
            if (dwFlags & LCM_SETFLAGS_SAVETOPENDINGONLY )
            {
                LCM_BuildMessage(lcmContext, ID_LCM_WRITEMESSAGE_SAVETOPENDING, EMPTY_STRING, MI_WRITEMESSAGE_CHANNEL_VERBOSE); 
                MI_Instance_Delete((MI_Instance *)metaConfigInstance);
                moduleManager->ft->Close(moduleManager, NULL);
                return MI_RESULT_OK;
            }
            
            result = ApplyPendingConfig(lcmContext, moduleManager, dwFlags, &resultStatus, cimErrorDetails);
            if (result != MI_RESULT_OK)
            {
                MI_Instance_Delete((MI_Instance *)metaConfigInstance);
                moduleManager->ft->Close(moduleManager, NULL);
                return result;
            }
            
            if (File_ExistT(g_MetaConfigFileName) == -1)
            {
                // If there is no meta config file, create the one which is default meta config, and creates new task.
                // The operation must be done before reboot operation to avoid possible incomplete update.
                result = SetMetaConfig((MI_Instance *)metaConfigInstance, cimErrorDetails);
                if (result != MI_RESULT_OK)
                {
                    MI_Instance_Delete((MI_Instance *)metaConfigInstance);
                    moduleManager->ft->Close(moduleManager, NULL);
                    return result;
                }
            }
            
            if (resultStatus & DSC_RESTART_SYSTEM_FLAG)
            {
                SetLCMStatusReboot();
            }
            
            MI_Instance_Delete((MI_Instance *)metaConfigInstance);
            moduleManager->ft->Close(moduleManager, NULL);
        }
    }
        
    DSC_EventWriteMethodEnd(__WFUNCTION__);
    if(overWroteUserSpecifiedRefreshFreqMins != NULL)
    {
        MI_Context* mi_context = (MI_Context*) lcmContext->context;
        Intlstr pTempStr = Intlstr_Null;
        GetResourceString1Param(ID_LCMHELPER_OVERWROTE_USER_SPECIFIED_REFRESH_FREQUENCY, overWroteUserSpecifiedRefreshFreqMins , &pTempStr);
#if defined(BUILD_OMS)
        DSC_LOG_WARNING("%s", pTempStr.str);
#else
        MI_Context_WriteWarning(mi_context, pTempStr.str);
#endif
        if(pTempStr.str)
        {
            Intlstr_Free(pTempStr);
        }
        overWroteUserSpecifiedRefreshFreqMins = NULL;
    }
    if (overWroteUserSpecifiedConfModeFreqMins != NULL)
    {
        MI_Context* mi_context = (MI_Context*) lcmContext->context;
        Intlstr pTempStr = Intlstr_Null;
        GetResourceString1Param(ID_LCMHELPER_OVERWROTE_USER_SPECIFIED_CONFMODE_FREQUENCY, overWroteUserSpecifiedConfModeFreqMins, &pTempStr);
#if defined(BUILD_OMS)
        DSC_LOG_WARNING("%s", pTempStr.str);
#else
        MI_Context_WriteWarning(mi_context, pTempStr.str);
#endif
        if (pTempStr.str)
        {
            Intlstr_Free(pTempStr);
        }
        overWroteUserSpecifiedConfModeFreqMins = NULL;
    }

    return result;

}

MI_Result ApplyPendingConfig(
    _In_ LCMProviderContext *lcmContext,
    _In_ ModuleManager *moduleManager,
    _In_ MI_Uint32 flags,
    _Inout_ MI_Uint32 *resultStatus,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result result = MI_RESULT_OK;
    MI_Instance *metaConfigInstance = NULL;
    MI_Value configModeValue;

    if (cimErrorDetails == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once.

    //clear cache for built-in providers
    result = ClearBuiltinProvCache(BUILTIN_PROVIDER_CACHE, cimErrorDetails);
    if (result == MI_RESULT_OK)
    {
        //clear cache for package provider
        result = ClearBuiltinProvCache(PACKAGE_PROVIDER_CACHE, cimErrorDetails);
        if (result == MI_RESULT_OK)
        {
            flags |= LCM_EXECUTE_APPLYNEWCONFIG;
            result = ApplyConfig(lcmContext, GetPendingConfigFileName(), moduleManager, flags, resultStatus, cimErrorDetails);
        }
    }

    // If application failed, restart requested or test flag is set, do not move to Current.mof, and do not delete the Pending.mof file.
    if (result != MI_RESULT_OK || DSC_RESTART_SYSTEM_FLAG & *resultStatus || flags & LCM_EXECUTE_TESTONLY)
    {
        return result;
    }

    //If whatif flag is set, do not move to Current.mof, just delete the pending mof file.
    if (lcmContext->executionMode & LCM_SETFLAGS_ENABLEWHATIF)
    {
        RetryDeleteFile(GetPendingConfigFileName());
        return result;
    }

    result = GetMetaConfig(&metaConfigInstance);
    EH_CheckResult(result);

    result = DSC_MI_Instance_GetElement(metaConfigInstance, MSFT_DSCMetaConfiguration_ConfigurationMode, &configModeValue, NULL, NULL, NULL);
    EH_CheckResult(result); 
    
    // We only move pending to current when not in MonitorOnlyMode. 
    if (!ShouldMonitorOnly(configModeValue.string))
    {
        result = CopyConfigAndRemoveSource(CONFIGURATION_LOCATION_PENDING, CONFIGURATION_LOCATION_CURRENT, cimErrorDetails);
        EH_CheckResult(result);
    }

EH_UNWIND;
    if (metaConfigInstance != NULL)
    {
        MI_Instance_Delete(metaConfigInstance);
    }   
    return result;
}

MI_Result ApplyCurrentConfig(
    _In_ LCMProviderContext *lcmContext,
    _In_ ModuleManager *moduleManager, 
    _In_ MI_Uint32 flags,
    _Inout_ MI_Uint32 *resultStatus,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result miResult;

    if (cimErrorDetails == NULL)
    {        
        return MI_RESULT_INVALID_PARAMETER; 
    }
    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once. 

    miResult = ApplyConfig(lcmContext, GetCurrentConfigFileName(), moduleManager, flags, resultStatus, cimErrorDetails);

    return miResult;
}

MI_Result ApplyMetaConfig(
    _In_ LCMProviderContext *lcmContext,
    _In_ ModuleManager *moduleManager, 
    _In_ MI_Uint32 flags,
    _Inout_ MI_Uint32 *resultStatus,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result miResult;
        
    if (cimErrorDetails == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once.

    miResult = ApplyConfig(lcmContext, GetMetaConfigTmpFileName(), moduleManager, flags, resultStatus, cimErrorDetails);
    EH_CheckResult(miResult);

    miResult = RegisterWithPullServers(lcmContext, (MI_Instance*) g_metaConfig, cimErrorDetails);
    EH_CheckResult(miResult);

    miResult = RegisterWithReportingServers(lcmContext, (MI_Instance*)g_metaConfig, cimErrorDetails);
    EH_CheckResult(miResult);

EH_UNWIND;
    return miResult;
}


typedef struct _PartialConfigBucket /* derives from HashBucket */
{
        struct _PartialConfigBucket* next;
        MI_Char* key; //resourceID
        MI_Instance *partialConfig;
        MI_Instance *documentIns;
        MI_Instance **resourceInstances;
        MI_Uint32 size;
        // number of resources we get from filtering through the combined resource list. 
        // if it is 0 it mean the partial configuration is not available yet
        MI_Uint32 count;
}
PartialConfigBucket;

NITS_EXTERN_C size_t PartialConfigHash(
        const HashBucket* bucket_)
{
        return HashMap_HashProc_PalStringCaseInsensitive(((PartialConfigBucket*) bucket_)->key);
}

NITS_EXTERN_C int PartialConfigEqual(
        const HashBucket* bucket1_,
        const HashBucket* bucket2_)
{
        PartialConfigBucket* bucket1 = (PartialConfigBucket*) bucket1_;
        PartialConfigBucket* bucket2 = (PartialConfigBucket*) bucket2_;
        return Tcscasecmp(bucket1->key, bucket2->key) == 0;
}

NITS_EXTERN_C void PartialConfigRelease(
        HashBucket* bucket_)
{
        PartialConfigBucket* bucket = (PartialConfigBucket*) bucket_;

        DSC_free(bucket);
}

MI_Result ApplyConfigGroup(
        _In_ LCMProviderContext *lcmContext,
        _In_ ModuleManager *moduleManager,
        _In_ MI_Uint32 flags,
        _In_ MI_Instance * documentIns,
        _In_ MI_InstanceA *resourceInstances,
        _In_ MI_Instance *metaConfigInstance,
        _Inout_ MI_Uint32 *resultStatus,
        _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
        MI_Result r;
        MI_Value value;
        MI_Uint32 result_Flags;
        MI_Char* versionNumber;

        r = ValidateDocumentInstance(documentIns, cimErrorDetails);
        if (r != MI_RESULT_OK)
        {
                return r;
        }

        //If its a metaconfiguration , validate its version
        if (flags & VALIDATE_METACONFIG_INSTANCE)
        {
                r = DSC_MI_Instance_GetElement(documentIns, OMI_ConfigurationDocument_Version, &value, NULL, &result_Flags, NULL);
                if (r != MI_RESULT_OK || (result_Flags & MI_FLAG_NULL) != 0)
                {
                        r = MI_RESULT_NOT_FOUND;
                        return GetCimMIError1Param(r, cimErrorDetails, ID_MODMAN_VALIDATE_PROVSCHEMA_NOMANDATORY, OMI_ConfigurationDocument_Version);
                }

                versionNumber = value.string;
                //Adding logic to validate if the versions are correctly numbered based on entities of download manager variables.

                //We know version number is not null, since it will enter here only if that is validated
                r = ValidateVersionNumbersCompatibility(resourceInstances, versionNumber, lcmContext, cimErrorDetails);
                if (r != MI_RESULT_OK)
                {
                        return r;
                }

        }
        // Check if at least 1 resource was specified in the instance document
        if (resourceInstances->size == 0)
        {
                return GetCimMIError(MI_RESULT_INVALID_PARAMETER, cimErrorDetails, ID_LCMHELPER_NORESOURCESPECIFIED);
        }

        r = SendConfigurationApply(lcmContext, flags, resourceInstances, moduleManager, documentIns, resultStatus, cimErrorDetails);
        if (r != MI_RESULT_OK)
        {
                if (cimErrorDetails && *cimErrorDetails)
                        return r;

                return GetCimMIError(r, cimErrorDetails, ID_LCMHELPER_SENDCONFIGAPPLY_ERROR);
        }

        /*move this out*/
        if (!(flags & LCM_EXECUTE_TESTONLY) && (DSC_RESTART_SYSTEM_FLAG & *resultStatus))
        {
                //Log the message here; log different message depends on the value of RebootNodeIfNeeded(winblue:366265)
                MI_Value configModeValue;

                r = DSC_MI_Instance_GetElement(metaConfigInstance, MSFT_DSCMetaConfiguration_RebootNodeIfNeeded, &configModeValue, NULL, NULL, NULL);
                if (r != MI_RESULT_OK)
                {
                        return r;
                }

                //telling user that reboot is scheduled
                if (configModeValue.boolean == MI_TRUE)
                {
                        LCM_BuildMessage(lcmContext, ID_LCM_WRITEMESSAGE_REBOOT, EMPTY_STRING, MI_WRITEMESSAGE_CHANNEL_VERBOSE);
                }
                //telling user that manual reboot is needed
                else
                {
                        LCM_BuildMessage(lcmContext, ID_LCM_WRITEMESSAGE_REBOOTMANUALLY, EMPTY_STRING, MI_WRITEMESSAGE_CHANNEL_VERBOSE);
                }
                /*move this out*/
        }

        return MI_RESULT_OK;
}


MI_Result DependPartialConfigExist(_In_ MI_Instance* partialConfig, _In_ HashMap* self, _Out_ MI_Boolean *dependsOnExist, _Outptr_result_maybenull_ MI_Instance** cimErrorDetails)
{
        MI_Value value;
        MI_Uint32 result_Flags;
        MI_Result r = MI_RESULT_OK;
        PartialConfigBucket searchBucket;
        PartialConfigBucket *bucket;
        MI_Uint32 i;

        if (partialConfig == NULL || self == NULL || dependsOnExist == NULL || cimErrorDetails == NULL)
        {
                return MI_RESULT_INVALID_PARAMETER;
        }
        *cimErrorDetails = NULL;

        *dependsOnExist = MI_TRUE;

        r = DSC_MI_Instance_GetElement(partialConfig, OMI_BaseResource_DependsOn, &value, NULL, &result_Flags, NULL);
        if (r != MI_RESULT_OK)
        {
                // todo: add proper error message
                EH_Fail_(GetCimMIError(r, cimErrorDetails, ID_MODMAN_VALIDATE_DOCINS));
        }

        if (((result_Flags & MI_FLAG_NULL) != 0) || value.stringa.size == 0)
        {
                return MI_RESULT_OK;
        }

        for (i = 0; i < value.stringa.size; i++)
        {
                searchBucket.key = value.stringa.data[i];
                bucket = (PartialConfigBucket*) HashMap_Find(self, (const HashBucket*) &searchBucket);
                if (bucket == NULL)
                {
                        *dependsOnExist = MI_FALSE;
                        // todo: add proper error message
                        EH_Fail_(GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_LCMHELPER_MEMORY_ERROR));
                }

                if (bucket->count == 0)
                {
                        *dependsOnExist = MI_FALSE;
                        return MI_RESULT_OK;
                }
        }

EH_UNWIND;
                return r;
}


MI_Result ProcessPartialConfigurations(
        _In_ LCMProviderContext *lcmContext,
        _In_ ModuleManager *moduleManager,
        _In_ MI_Uint32 flags,
        _Inout_ MI_Uint32 *resultStatus,
        _In_ MI_Result(*ConfigurationProcessor)
        (
        _In_ LCMProviderContext *lcmContext,
        _In_ ModuleManager *moduleManager,
        _In_ MI_Uint32 flags,
        _In_ MI_Instance * documentIns,
        _In_ MI_InstanceA *resourceInstances,
        _In_ MI_Instance *metaConfigInstance,
        _Inout_ MI_Uint32 *resultStatus,
        _Outptr_result_maybenull_ MI_Instance **cimErrorDetails
        ),
        _In_ MI_Instance *metaConfigInstance,
        _In_ MI_InstanceA* resourceInstances,
        _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
        MI_Result r = MI_RESULT_OK;
        MI_Uint32 result_Flags;
        MI_Boolean hashInited = MI_FALSE;
        int hashResult;
        const size_t NUM_LISTS = 32;
        HashMap partialConfigDocumentInMap;
        PartialConfigBucket *bucket = NULL;
        PartialConfigBucket searchBucket;
        MI_Instance **tempInstance = NULL;
        MI_Uint32 xCount = 0;
        MI_InstanceA partialConfigIns = { 0 };
        MI_InstanceA partialConfigDocumentIns = { 0 };
        ExecutionOrderContainer executionContainer = { 0 };
        MI_InstanceA partialConfigResourceInstances = { 0 };
        MSFT_PartialConfiguration *partialConfigIn = NULL;
        MI_Boolean flagAtleastOnePartialWasSet = MI_FALSE;
        MI_Boolean flagPartialConfigFailedToApply = MI_FALSE;
        MI_Char expandedPartialConfigName[MAX_PATH];
        size_t concatResult;
        MI_Value value;
        MI_Value valueId;

        hashResult = HashMap_Init(&partialConfigDocumentInMap, NUM_LISTS, PartialConfigHash, PartialConfigEqual, PartialConfigRelease);
        if (hashResult != 0)
        {
                EH_Fail();
        }
        hashInited = MI_TRUE;

        // build a list of resourceInstance and documentIns
        r = DSC_MI_Instance_GetElement(metaConfigInstance, MSFT_DSCMetaConfiguration_PartialConfigurations, &value, NULL, &result_Flags, NULL);
        if (r != MI_RESULT_OK || (result_Flags & MI_FLAG_NULL) != 0)
        {
                // todo: add proper error message
                EH_Fail_(GetCimMIError(r, cimErrorDetails, ID_MODMAN_VALIDATE_DOCINS));
        }

        tempInstance = (MI_Instance **) DSC_malloc(value.instancea.size * sizeof(MI_Instance*), NitsHere());
        if (tempInstance == NULL)
        {
                EH_Fail_(GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_LCMHELPER_MEMORY_ERROR));
        }

        for (xCount = 0; xCount < value.instancea.size; xCount++)
        {
                tempInstance[xCount] = value.instancea.data[xCount];
                bucket = (PartialConfigBucket*) DSC_malloc(sizeof(PartialConfigBucket), NitsHere());
                if (bucket == NULL)
                {
                        EH_Fail_(GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_LCMHELPER_MEMORY_ERROR));
                }

                r = DSC_MI_Instance_GetElement(tempInstance[xCount], MSFT_DSCMetaConfiguration_ResourceId, &valueId, NULL, &result_Flags, NULL);
                if (r != MI_RESULT_OK || (result_Flags & MI_FLAG_NULL) != 0)
                {
                        // todo: add proper error message
                        EH_Fail_(GetCimMIError(r, cimErrorDetails, ID_MODMAN_VALIDATE_DOCINS));
                }

                bucket->key = valueId.string;
                bucket->partialConfig = tempInstance[xCount];
                bucket->size = 0;
                bucket->count = 0;

                r = HashMap_Insert(&partialConfigDocumentInMap, (HashBucket*) bucket);
                EH_CheckResult(r);
        }

        partialConfigIns.data = tempInstance;
        partialConfigIns.size = value.instancea.size;

        r = ResolveDependency(&partialConfigIns, &executionContainer, cimErrorDetails);
        EH_CheckResult(r);

        r = GetArrayInstancesFromSingleMof(moduleManager, 0, GetPartialConfigBaseDocumentInstanceFileName(), &partialConfigDocumentIns, cimErrorDetails, MI_TRUE);
        EH_CheckResult(r);

        //insert partialConfiguration document into the hashmap 
        for (xCount = 0; xCount < partialConfigDocumentIns.size; xCount++)
        {
                r = DSC_MI_Instance_GetElement(partialConfigDocumentIns.data[xCount], MI_T("Name"), &value, NULL, &result_Flags, NULL);
                if (r != MI_RESULT_OK || (result_Flags & MI_FLAG_NULL) != 0)
                {
                        // todo: add proper error message
                        EH_Fail_(GetCimMIError(r, cimErrorDetails, ID_MODMAN_VALIDATE_DOCINS));
                }

                concatResult = Stprintf(expandedPartialConfigName, MAX_PATH, MI_T("%T%T"), MI_T("[PartialConfiguration]"), value.string);
                if (concatResult == -1)
                {
                        // todo: add proper error message
                        EH_Fail_(GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_LCMHELPER_MEMORY_ERROR));
                }

                searchBucket.key = expandedPartialConfigName;
                bucket = (PartialConfigBucket*) HashMap_Find(&partialConfigDocumentInMap, (const HashBucket*) &searchBucket);
                if (bucket == NULL)
                {
                        // todo: add proper error message
                        EH_Fail_(GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_LCMHELPER_MEMORY_ERROR));
                }
                bucket->documentIns = partialConfigDocumentIns.data[xCount];
        }

        //count each partial configuration resource instances, save in hashmap->size
        for (xCount = 0; xCount < resourceInstances->size; xCount++)
        {
                r = DSC_MI_Instance_GetElement(resourceInstances->data[xCount], OMI_BaseResource_ConfigurationName, &value, NULL, &result_Flags, NULL);
                if (r != MI_RESULT_OK || (result_Flags & MI_FLAG_NULL) != 0)
                {
                        // todo: add proper error message
                        EH_Fail_(GetCimMIError(r, cimErrorDetails, ID_MODMAN_VALIDATE_DOCINS));
                }

                concatResult = Stprintf(expandedPartialConfigName, MAX_PATH, MI_T("%T%T"), MI_T("[PartialConfiguration]"), value.string);
                if (concatResult == -1)
                {
                        // todo: add proper error message
                        EH_Fail_(GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_LCMHELPER_MEMORY_ERROR));
                }
                searchBucket.key = expandedPartialConfigName;
                bucket = (PartialConfigBucket*) HashMap_Find(&partialConfigDocumentInMap, (const HashBucket*) &searchBucket);
                if (bucket == NULL)
                {
                        // todo: add proper error message
                        EH_Fail_(GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_LCMHELPER_MEMORY_ERROR));
                }
                bucket->size++;
        }

        //collect each partial configuration resource instances into hashmap
        for (xCount = 0; xCount < resourceInstances->size; xCount++)
        {
                r = DSC_MI_Instance_GetElement(resourceInstances->data[xCount], OMI_BaseResource_ConfigurationName, &value, NULL, &result_Flags, NULL);
                if (r != MI_RESULT_OK || (result_Flags & MI_FLAG_NULL) != 0)
                {
                        // todo: add proper error message
                        EH_Fail_(GetCimMIError(r, cimErrorDetails, ID_MODMAN_VALIDATE_DOCINS));
                }

                concatResult = Stprintf(expandedPartialConfigName, MAX_PATH, MI_T("%T%T"), MI_T("[PartialConfiguration]"), value.string);
                if (concatResult == -1)
                {
                        // todo: add proper error message
                        EH_Fail_(GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_LCMHELPER_MEMORY_ERROR));
                }

                searchBucket.key = expandedPartialConfigName;
                bucket = (PartialConfigBucket*) HashMap_Find(&partialConfigDocumentInMap, (const HashBucket*) &searchBucket);
                if (bucket == NULL)
                {
                        // todo: add proper error message
                        EH_Fail_(GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_LCMHELPER_MEMORY_ERROR));
                }

                if (bucket->count == 0 && bucket->size > 0)
                {
                        bucket->resourceInstances = (MI_Instance **) DSC_malloc(bucket->size * sizeof(MI_Instance*), NitsHere());
                        if (bucket->resourceInstances == NULL)
                        {
                                EH_Fail_(GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_LCMHELPER_MEMORY_ERROR));
                        }
                }
                bucket->resourceInstances[bucket->count++] = resourceInstances->data[xCount];
        }

        for (xCount = 0; xCount < executionContainer.executionListSize; xCount++)
        {
                partialConfigIn = (MSFT_PartialConfiguration*) partialConfigIns.data[executionContainer.ExecutionList[xCount].resourceIndex];
                searchBucket.key = (MI_Char*) partialConfigIn->ResourceId.value;
                bucket = (PartialConfigBucket*) HashMap_Find(&partialConfigDocumentInMap, (const HashBucket*) &searchBucket);
                if (bucket == NULL)
                {
                        // todo: add proper error message
                        EH_Fail_(GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_LCMHELPER_MEMORY_ERROR));
                }
                partialConfigResourceInstances.data = bucket->resourceInstances;
                partialConfigResourceInstances.size = bucket->size;

                if (bucket->count > 0)
                {
                        MI_Boolean dependsExist;
                        r = DependPartialConfigExist(bucket->partialConfig, &partialConfigDocumentInMap, &dependsExist, cimErrorDetails);
                        EH_CheckResult(r);

                        if (dependsExist)
                        {
                            DSC_EventWriteLCMApplyingPartial(searchBucket.key);
                                r = ConfigurationProcessor(lcmContext, moduleManager, flags, bucket->documentIns, &partialConfigResourceInstances, (MI_Instance*) metaConfigInstance, resultStatus, cimErrorDetails);
                                DSC_free(partialConfigResourceInstances.data);
                                if (r != MI_RESULT_OK)
                                {
                                        //Get the message and print a warning
                                        r = DSC_MI_Instance_GetElement(*cimErrorDetails, MSFT_WMIERROR_MESSAGE, &value, NULL, NULL, NULL);
                                        if (value.string != NULL)
                                        {
                                                DSC_WriteWarning2Param((MI_Context*) lcmContext->context, ID_LCMHELPER_APPLYPARTIALCONFIG_FAILED_WITHERROR, searchBucket.key, value.string);
                                        }
                                        if (*cimErrorDetails != NULL)
                                        {
                                            MI_Instance_Delete(*cimErrorDetails);
                                            *cimErrorDetails = NULL;//Clear error details
                                        }
                                        flagPartialConfigFailedToApply = MI_TRUE;
                                        r = MI_RESULT_OK;
                                }
                                else
                                {
                                        //Check the flag where atleast one was set.
                                        flagAtleastOnePartialWasSet = MI_TRUE;

                                        if (!(flags & LCM_EXECUTE_TESTONLY) && (DSC_RESTART_SYSTEM_FLAG & *resultStatus))
                                        {
                                                //Log the message here; log different message depends on the value of RebootNodeIfNeeded(winblue:366265)
                                                MI_Value configModeValue;

                                                r = DSC_MI_Instance_GetElement(metaConfigInstance, MSFT_DSCMetaConfiguration_RebootNodeIfNeeded, &configModeValue, NULL, NULL, NULL);
                                                if (r != MI_RESULT_OK)
                                                {
                                                        return r;
                                                }

                                                //telling user that reboot is scheduled
                                                if (configModeValue.boolean == MI_TRUE)
                                                {
                                                        LCM_BuildMessage(lcmContext, ID_LCM_WRITEMESSAGE_REBOOT, EMPTY_STRING, MI_WRITEMESSAGE_CHANNEL_VERBOSE);
                                                }
                                                //telling user that manual reboot is needed
                                                else
                                                {
                                                        LCM_BuildMessage(lcmContext, ID_LCM_WRITEMESSAGE_REBOOTMANUALLY, EMPTY_STRING, MI_WRITEMESSAGE_CHANNEL_VERBOSE);
                                                }
                                                
                                                // this is the result from ApplyConfigGroup which requires reboot, we know it is ok in the Else clause
                                                return MI_RESULT_OK;
                                        }
                                }
                        }
                        else
                        {
                                //Only a warning that the dependent configuration was not available.
                                DSC_WriteWarning1Param((MI_Context*) lcmContext->context, ID_LCMHELPER_DEPENDSONFILEDOESNTEXIST_MESSAGE, searchBucket.key);
                                flagPartialConfigFailedToApply = MI_TRUE;
                        }
                }
                EH_CheckResult(r);
        }
        if (!flagAtleastOnePartialWasSet)
        {
                if (flagPartialConfigFailedToApply)
                {
                        EH_CheckResult(r = GetCimMIError(MI_RESULT_NOT_FOUND, cimErrorDetails, ID_PARTIALCONFIG_FAILEDPARTIALCONFIGS));
                }
                else
                {
                        EH_CheckResult(r = GetCimMIError(MI_RESULT_NOT_FOUND, cimErrorDetails, ID_PARTIALCONFIG_NOPARTIALCONFIGPRESENT));
                }
        }

EH_UNWIND;
        CleanUpInstanceCache(&partialConfigDocumentIns);
        if (tempInstance != NULL)
        {
            DSC_free(tempInstance);
        }

        if (hashInited)
        {
                HashMap_Destroy(&partialConfigDocumentInMap);
        }
        FreeExecutionOrderContainer(&executionContainer);

        return r;

}

MI_Result ApplyConfig(
    _In_ LCMProviderContext *lcmContext,
    _In_z_ const MI_Char *configFileLocation,
    _In_ ModuleManager *moduleManager, 
    _In_ MI_Uint32 flags,
    _Inout_ MI_Uint32 *resultStatus,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result r = MI_RESULT_OK;
    MI_InstanceA resourceInstances = {0};
    MI_Instance *documentIns = NULL;
    MI_Uint32 applyConfigFlags = 0;
    MI_Instance *metaConfigInstance = NULL;
    MI_Value configModeValue;

    //Debug Log 
    DSC_EventWriteMessageApplyingConfig(configFileLocation);

    if (cimErrorDetails == NULL)
    {        
        return MI_RESULT_INVALID_PARAMETER; 
    }
    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once. 

    applyConfigFlags = flags &~LCM_EXECUTE_TESTONLY;

    r = GetMetaConfig((MSFT_DSCMetaConfiguration**) &metaConfigInstance);
    EH_CheckResult(r);

    r =  moduleManager->ft->LoadInstanceDocumentFromLocation(moduleManager, applyConfigFlags, configFileLocation, cimErrorDetails, &resourceInstances, &documentIns);
    if (r != MI_RESULT_OK)
    {
        if (cimErrorDetails && *cimErrorDetails)
            return r;

        return GetCimMIError(r, cimErrorDetails, ID_LCMHELPER_LOAD_PENDING_ERROR);
    }

    r = MI_Instance_GetElement(metaConfigInstance, MSFT_DSCMetaConfiguration_ConfigurationMode, &configModeValue, NULL, NULL, NULL);
    EH_CheckResult(r);
    
    // Before applying the configuration check if we are in MonitorOnly Mode and not applying meta configuration And LCM is not disbaled.
    // In these cases we are applying some new or same configuration again and want to run in test only mode. 
    if (ShouldMonitorOnly(configModeValue.string))
    {
        if (!(flags & LCM_SET_METACONFIG) && !(flags & LCM_EXECUTE_SETONLY))
        {   
            flags |= LCM_EXECUTE_TESTONLY;
        }       
    }

    if (ShouldUsePartialConfigurations(metaConfigInstance, MI_TRUE) &&
        !(flags & VALIDATE_METACONFIG_INSTANCE) &&
        !(flags & LCM_EXECUTE_TESTONLY))
    {
        r = ProcessPartialConfigurations(lcmContext, moduleManager, flags, resultStatus, ApplyConfigGroup, metaConfigInstance, &resourceInstances, cimErrorDetails);
        EH_CheckResult(r);
    }
    else
    {
        r = ApplyConfigGroup(lcmContext, moduleManager, flags, documentIns, &resourceInstances, (MI_Instance*) metaConfigInstance, resultStatus, cimErrorDetails);
        EH_CheckResult(r);
    }

EH_UNWIND;

    if (documentIns)
    {
        MI_Instance_Delete(documentIns);
        documentIns = NULL;
    }

    CleanUpInstanceCache(&resourceInstances);  
    
    if (metaConfigInstance != NULL)
    {
        MSFT_DSCMetaConfiguration_Delete((MSFT_DSCMetaConfiguration *) metaConfigInstance);
    }

    return r;
}


//Function to get the name of the partial configuratin's destination location inside the partial config data store.
MI_Result GetPartialConfigStoreLocation(_In_ ModuleManager * moduleManager,
        _In_count_(documentSize) const MI_Uint8* configData,
        _In_ MI_Uint32 documentSize,
        _Outptr_result_maybenull_ MI_Instance** cimErrorDetails,
        _Outptr_result_maybenull_z_  MI_Char** configFileName)
{
        MI_Instance *documentIns = NULL;
        MI_Result r = MI_RESULT_OK;
        MI_Value value;
        MI_Uint32 flags = 0;
        MI_Instance* metaConfigInstance = NULL;
        MI_InstanceA resourceInstances = { 0 };
        if (moduleManager == NULL || configData == NULL || documentSize == 0 || cimErrorDetails == NULL)
        {
                return MI_RESULT_INVALID_PARAMETER;
        }
        *cimErrorDetails = NULL;
        //First get the document instance so we can see what'sthe partial config's name
        r = moduleManager->ft->LoadInstanceDocument(moduleManager, flags, (MI_Uint8*) configData, documentSize, cimErrorDetails, &resourceInstances, &documentIns);
        GOTO_CLEANUP_IF_FAILED(r, Cleanup);

        //Now get document instance's Name property
        r = DSC_MI_Instance_GetElement(documentIns, OMI_ConfigurationDocument_Name, &value, NULL, &flags, NULL);
        if (r != MI_RESULT_OK || (flags & MI_FLAG_NULL))
        {
                //Output error that there is no name present
                r = GetCimMIError2Params(MI_RESULT_INVALID_CLASS, cimErrorDetails, ID_ENGINEHELPER_GET_PROPERTY_X_FROM_Y_FAILED, OMI_ConfigurationDocument_Name, BASE_DOCUMENT_CLASSNAME);
                goto Cleanup;
        }
        //Store configFileName as path c:\windows\system32\configuration\partialconfigurations +_+ value.string + .mof
        r = GetPartialConfigurationPath(value.string, configFileName, cimErrorDetails);
        GOTO_CLEANUP_IF_FAILED(r, Cleanup);
Cleanup:
        CleanUpInstanceCache(&resourceInstances);
        INSTANCE_DELETE_IF_NOT_NULL(documentIns);
        INSTANCE_DELETE_IF_NOT_NULL(metaConfigInstance);
        return r;
}


const MI_Char * GetPendingConfigFileName()
{
    return g_PendingConfigFileName;
}

const MI_Char * GetPartialConfigBaseDocumentInstanceFileName()
{
        return g_PartialConfigBaseDocumentInstanceFileName;
}

const MI_Char * GetPartialConfigBaseDocumentInstanceTmpFileName()
{
        return g_PartialConfigBaseDocumentInstanceTmpFileName;
}

const MI_Char * GetConfigChecksumFileName()
{
    return g_ConfigChecksumFileName;
}

const MI_Char * GetGetConfigFileName()
{
    return g_GetConfigFileName;
}

const MI_Char * GetInventoryFileName()
{
    return g_InventoryFileName;
}

const MI_Char * GetInventoryReportFileName()
{
    return g_InventoryReportFileName;
}

const MI_Char *GetCurrentConfigFileName()
{
    return g_CurrentConfigFileName;
}

const MI_Char *GetMetaConfigTmpFileName()
{
    return g_MetaConfigTmpFileName;
}

const MI_Char *GetPreviousConfigFileName()
{
    return g_PreviousConfigFileName;
}

const MI_Char *GetPullRunLogFileName()
{
    return g_PullRunLogFileName;
}

const MI_Char *GetCurrentLCMStatusCodeHistory()
{
        return g_LCMStatusCodeHistory;
}

const MI_Char * GetPendingConfigTmpFileName()
{
        return g_PendingConfigTmpFileName;
}


/*this will be configured by meta config*/
const MI_Char*GetConfigPath()
{
    return CONFIGURATION_LOCATION;
}

/*This will point to data store of partial configurations*/
const MI_Char * GetPartialConfigDataStore()
{
        return g_PartialConfigDataStoreName;
}

/*This will return the mof extension for partial Configuration files*/
const MI_Char*GetMofExtension()
{
        return MOF_EXTENSION;
}

/*This will return the mof checksum extension for partial Configuration files*/
const MI_Char * GetMofChecksumExtension()
{
        return MOFCHECKSUM_EXTENSION;
}

/*This will point to the unique partial configuration name*/
const MI_Char*GetPartialConfigSuffix()
{
        return PARTIALCONFIG_SUFFIX;
}

MI_Result CopyConfigurationFile(
    _In_z_ const MI_Char *locationFrom,
    _In_z_ const MI_Char *locationTo,
    MI_Boolean backUpNeeded,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result result = MI_RESULT_OK;

    MI_Boolean backupSuccess = MI_FALSE;

    MI_Char *fileFullFrom = NULL;
    MI_Char *fileFullTo = NULL;
    MI_Char *filePathFrom = NULL;
    MI_Char *filePathTo = NULL; 
    MI_Char *fileBackup = NULL;
    
    DSC_EventWriteMessageCopyingConfig(locationFrom, locationTo);
    
    if (cimErrorDetails == NULL)
    {        
        return MI_RESULT_INVALID_PARAMETER; 
    }

    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once.
    *cimErrorDetails = NULL;

    result = GetFullPath(GetConfigPath(), locationFrom, &fileFullFrom, cimErrorDetails);
    EH_CheckResult(result);

    result = ExpandPath(fileFullFrom, &filePathFrom, cimErrorDetails);
    EH_CheckResult(result);

    result = GetFullPath(GetConfigPath(), locationTo, &fileFullTo, cimErrorDetails);
    EH_CheckResult(result);

    result = ExpandPath(fileFullTo, &filePathTo, cimErrorDetails);
    EH_CheckResult(result); 

    if (backUpNeeded)
    {
        result = ExpandPath(CONFIGURATION_LOCATION_BACKUP, &fileBackup, cimErrorDetails);
        EH_CheckResult(result); 

        if (File_CopyT(filePathFrom, fileBackup) == 0)
        {
            backupSuccess = MI_TRUE;
        }
    }

    if (File_ExistT(filePathFrom) == 0)
    {
        if (File_CopyT(filePathFrom, filePathTo) != 0)
        {
            if (backupSuccess)
            {
                File_CopyT(fileBackup, filePathTo);
            }

            result = GetCimMIError2Params(MI_RESULT_FAILED, cimErrorDetails, ID_LCMHELPER_COPY_FAILED, locationFrom, locationTo);
            EH_Fail();
        }
    }
    else
    {
        result = GetCimMIError2Params(MI_RESULT_FAILED, cimErrorDetails, ID_LCMHELPER_COPY_FAILED, locationFrom, locationTo);
        EH_Fail();
    }

EH_UNWIND;
    DSC_free(fileFullFrom);
    DSC_free(fileFullTo);
    DSC_free(filePathFrom);
    DSC_free(filePathTo);
    DSC_free(fileBackup);

    return result;
}

MI_Result CopyConfigAndRemoveSource(
    _In_z_ const MI_Char *sourceConfigFileName,
    _In_z_ const MI_Char *destinationConfigFileName,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result result;

    MI_Char *sourceConfigFullPath = NULL;
    MI_Char *sourceConfigExpandedPath = NULL;

    if (cimErrorDetails == NULL)
    {        
        return MI_RESULT_INVALID_PARAMETER; 
    }

    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once.
    *cimErrorDetails = NULL;    

    result = GetFullPath(GetConfigPath(), sourceConfigFileName, &sourceConfigFullPath, cimErrorDetails);
    EH_CheckResult(result);

    result = ExpandPath(sourceConfigFullPath, &sourceConfigExpandedPath, cimErrorDetails);
    EH_CheckResult(result);    

    result = CopyConfigurationFile(sourceConfigFileName, destinationConfigFileName, MI_TRUE, cimErrorDetails);
    EH_CheckResult(result);

    if (File_RemoveT(sourceConfigExpandedPath) != 0)
    {
        result = GetCimMIError1Param(MI_RESULT_FAILED, cimErrorDetails, ID_LCMHELPER_DEL_FAILED, sourceConfigFileName);
    }

EH_UNWIND;
    DSC_free(sourceConfigFullPath);
    DSC_free(sourceConfigExpandedPath);

    return result;
}

MI_Result SaveFile(
    _In_z_ const MI_Char* filePath,
    _In_reads_bytes_(dataSize) const MI_Uint8* ConfigData,
    MI_Uint32 dataSize,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    FILE *fp;
    size_t result;
    if (cimErrorDetails == NULL)
    {        
        return MI_RESULT_INVALID_PARAMETER; 
    }
    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once.     
    fp = File_OpenT(filePath, MI_T("wb"));
    if( fp == NULL)
    {
        return GetCimMIError(MI_RESULT_FAILED, cimErrorDetails, ID_ENGINEHELPER_OPENFILE_ERROR);
    }
    result = fwrite(ConfigData, 1, dataSize, fp);
    File_Close(fp);
    if( result != dataSize)
    {
        return GetCimMIError(MI_RESULT_FAILED, cimErrorDetails, ID_LCMHELPER_CREATE_CONF_FAILED);
    }
    return MI_RESULT_OK;
}

MI_Result LoadFromPullRunLogFile(_Out_ MI_Uint32 *lastRun, _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    FILE* fStream = NULL;
    DWORD readError = 0;
    /*
     represent number of times pull has run in each pull-consistency loop
     for example pull frequency 15, consistency 30, so ratio of running consistency is 30/15 = 2
     it start from 0 and goes up to 2 - 1  and reset, when it's 1 consistency would run
    */
    MI_Uint32 storedRun = 0;
    DSC_EventWriteMessageSavingConfig(GetPullRunLogFileName());

    if (cimErrorDetails == NULL)
    {        
        return MI_RESULT_INVALID_PARAMETER; 
    }
    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once. 

    *lastRun = 0;

    fStream = File_OpenT(  GetPullRunLogFileName(), MI_T("r+") );
    if( fStream != NULL )
    {
        // Set pointer to beginning of file:
        fseek( fStream, 0L, SEEK_SET );

        readError = fscanf(fStream, MI_T("%d"), &storedRun);

        fclose( fStream );
    }

    *lastRun = storedRun;

    return MI_RESULT_OK;
}

MI_Result SavePullRunLogFile(_In_ MI_Uint32 maxRun, _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result result = MI_RESULT_OK;

    FILE* fStream = NULL;
    DWORD readError = 0;
    DWORD writeError = 0;
    /*
     represent number of times pull has run in each pull-consistency loop
     for example pull frequency 15, consistency 30, so ratio of running consistency is 30/15 = 2
     it start from 0 and goes up to 2 - 1  and reset, when it's 1 consistency would run
    */
    MI_Uint32 storedRun = 0;
    DSC_EventWriteMessageSavingConfig(GetPullRunLogFileName());

    if (cimErrorDetails == NULL)
    {        
        return MI_RESULT_INVALID_PARAMETER; 
    }
    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once. 
    if( File_ExistT(GetPullRunLogFileName()) != -1 )
    {

        fStream = File_OpenT(  GetPullRunLogFileName(), MI_T("r+") );
        if( fStream != NULL)
        {
            MI_Char timeString[MAX_PATH] = {0};

            result = GetNextRefreshTimeHelper(timeString);

            if (result != MI_RESULT_OK)
            {
                return GetCimMIError(result, cimErrorDetails, ID_CONSISTENCY_TASK_FAILURE);
            }

            // Set pointer to beginning of file:
            fseek( fStream, 0L, SEEK_SET );

            // Read value
            readError = fscanf(fStream, MI_T("%d"), &storedRun);

            storedRun = storedRun + 1;

            if(storedRun > maxRun)
            {
                storedRun = 0;
            }

            // Set pointer to beginning of file:
            fseek( fStream, 0L, SEEK_SET );

            writeError = Ftprintf(fStream, MI_T("%d %s"), storedRun, timeString);

            File_Close( fStream );
        }
    }
    else
    {
        fStream = File_OpenT(  GetPullRunLogFileName(), MI_T("w+") );
        if(fStream != NULL)
        {
            MI_Char timeString[MAX_PATH] = {0};

            result = GetNextRefreshTimeHelper(timeString);

            if (result != MI_RESULT_OK)
            {
                return GetCimMIError(result, cimErrorDetails, ID_CONSISTENCY_TASK_FAILURE);
            }

            writeError = Ftprintf(fStream, MI_T("%d %s"), 1, timeString);

            File_Close( fStream );
        }
    }

    return MI_RESULT_OK;
}

/*If the call is coming from WSMAN, first 4 bytes always contain size of the buffer.
If the call is coming from DCOM, it comes as sent by the caller. We will use some optimization here
for better experience for both WSMAN and COM client. Ideally client should send the first 4 bytes
as the size of the buffer.*/
void GetRealBufferIndex(
    _In_ const MI_ConstUint8A *buffer, 
    _Out_ MI_Uint32 *bufferIndex)
{
    MI_Uint32 dwBufferSizeInBuffer = 0;
    char * bufSizePointer = (char*) &dwBufferSizeInBuffer;

    *bufferIndex = 4;  //Assume that buffer was passed correctly and first 4 bytes are size of the buffer.
    if (buffer->size < sizeof(MI_Uint32) )
    {
        return;
    }

    //Read first 4 bytes and get the buffere size.
    bufSizePointer[3] = buffer->data[0];
    bufSizePointer[2] = buffer->data[1];
    bufSizePointer[1] = buffer->data[2];
    bufSizePointer[0] = buffer->data[3]; 

    if (dwBufferSizeInBuffer != buffer->size) 
    {
        //first 4 bytes doest not contain the size of the buffer. only possibility is the call is from WMIDCOM protocol
        // and buffer doesn't contain the size.
        *bufferIndex = 0;
        return;
    }
}

void LCM_WriteMessage_Internal_TimeTaken(
    _In_ MI_Context *context, 
    _In_z_ const MI_Char *resourceId, 
    _In_ MI_Uint32 messageID, 
    _In_ MI_Uint32 tokenItem,
    _In_ const MI_Real64 duration,
    _In_ MI_Uint32 channel)
{
    Intlstr intlstr = Intlstr_Null;
    MI_Char wcTime[DURATION_SIZE] ;
    MI_Char* actionItem=NULL;
    Intlstr tempItem = Intlstr_Null;
    Intlstr tempOperation = Intlstr_Null;
    //Get the resource string from the string table
    if( Stprintf(wcTime, DURATION_SIZE, MI_T("%0.4f"), duration) > 0 )
    {
        GetResourceString(tokenItem,&tempItem);
        GetResourceString(ID_OUTPUT_OPERATION_END,&tempOperation);
        ConcatStrings(&actionItem,TOKENIZED_OUTPUT_PADDING,(MI_Char*)tempOperation.str, (MI_Char*)tempItem.str);
        if( tempOperation.str)
            Intlstr_Free(tempOperation);
        if(tempItem.str)
            Intlstr_Free(tempOperation);
        GetResourceString4Param(messageID, g_JobInformation.deviceName, actionItem, resourceId, wcTime, &intlstr);
            
        if (intlstr.str)
        {
#if !defined(BUILD_OMS)
            MI_Context_WriteMessage(context, channel, intlstr.str);                
#endif
            DSC_EventWriteMessageFromEngine(channel, resourceId, intlstr.str);
            Intlstr_Free(intlstr);
        }
        DSC_free(actionItem);        
    }

}


void LCM_WriteMessage_Internal_Tokenized(
    _In_ LCMProviderContext *lcmContext,  
    _In_z_ const MI_Char *resourceId, 
    _In_ MI_Uint32 channel, 
    _In_z_ const MI_Char *message,
    _In_ MI_Boolean bRunWhatIf)
{
    if ((lcmContext->executionMode & LCM_EXECUTIONMODE_ONLINE) && lcmContext->context)
    {
        Intlstr intlstr = Intlstr_Null;
        GetFullMessageWithTokens(g_JobInformation.deviceName, resourceId, message, lcmContext, &intlstr);
        if (intlstr.str)
        { 
#if defined(BUILD_OMS)
            switch (channel)
            {
                case MI_WRITEMESSAGE_CHANNEL_DEBUG:
                    DSC_LOG_DEBUG("%s", intlstr.str);
                    break;
                case MI_WRITEMESSAGE_CHANNEL_VERBOSE:
                    DSC_LOG_VERBOSE("%s", intlstr.str);
                    break;
                case MI_WRITEMESSAGE_CHANNEL_WARNING:
                    DSC_LOG_WARNING("%s", intlstr.str);
                default:
                    DSC_LOG_INFO("[channel=%d] %s", channel, intlstr.str);
                    break;
            }
#else
            if(lcmContext->messageOperation==0 && lcmContext->messageItem==0) //There is no Action or Item only in the case of a provider message
            {
                DSC_EventWriteDumpMessageFromBuiltinProvider(channel, resourceId, intlstr.str);
            }
            else
            {
                DSC_EventWriteMessageFromEngine(channel, resourceId, intlstr.str);
            }
            lcmContext->messageOperation=0;
            lcmContext->messageItem =0;
            Intlstr_Free(intlstr);
#endif
        }
        
    }
}

void LCM_WriteMessage_EngineInternal(
    _In_ LCMProviderContext *lcmContext,  
    _In_z_ const MI_Char *resourceId, 
    _In_ MI_Uint32 channel, 
    _In_z_ const MI_Char *message)
{
    MI_Boolean shouldProcessFlag=MI_FALSE;
   
    if(lcmContext->executionMode & LCM_SETFLAGS_ENABLEWHATIF)
    {
        shouldProcessFlag=MI_TRUE;
    }

    LCM_WriteMessage_Internal_Tokenized(lcmContext, resourceId, channel, message, shouldProcessFlag);
}

void LCM_WriteMessage_ProviderInternal(
    _In_ LCMProviderContext *lcmContext,  
    _In_z_ const MI_Char *resourceId, 
    _In_ MI_Uint32 channel, 
    _In_z_ const MI_Char *message)
{
    MI_Boolean shouldProcessFlag=MI_FALSE;
   
    if(lcmContext->executionMode & LCM_SETFLAGS_ENABLEWHATIF)
    {
        shouldProcessFlag=MI_TRUE;
    }
    
   LCM_WriteMessage_Internal_Tokenized(lcmContext, resourceId, channel, message, shouldProcessFlag);
}

void LCM_WriteMessageInfo_Internal(
    _In_ LCMProviderContext *lcmContext,  
    _In_opt_z_ const MI_Char *computerName, 
    _In_ MI_Uint32 channel, 
    _In_z_ const MI_Char *userSid)
{
#if !defined(BUILD_OMS)
    if ((lcmContext->executionMode & LCM_EXECUTIONMODE_ONLINE) && lcmContext->context)
    {
        Intlstr resStr = Intlstr_Null;
        const MI_Char *notNullComputerName = (computerName != NULL) ? computerName : g_JobInformation.deviceName;
        GetResourceString2Param(ID_LCM_REPUDIATIONMSG, notNullComputerName, userSid, &resStr);
        if (resStr.str )
        {                  
            MI_Context_WriteMessage((MI_Context*) lcmContext->context, channel, resStr.str);
            Intlstr_Free(resStr);
        }     
    }
#endif
}

void LCM_BuildMessage(
    _In_ LCMProviderContext *lcmContext,  
    _In_ MI_Uint32 errorStringId,
    _In_z_ const MI_Char *resourceId, 
    _In_ MI_Uint32 channel)
{
    Intlstr intlstr = Intlstr_Null;
    GetResourceString(errorStringId, &intlstr);
    if (intlstr.str )
    {
        LCM_WriteMessage(lcmContext, resourceId, channel,intlstr.str);
        Intlstr_Free(intlstr);
    }     
}

void LCM_WriteMessage(
    _In_ LCMProviderContext *lcmContext,  
    _In_z_ const MI_Char *resourceId, 
    _In_ MI_Uint32 channel, 
    _In_z_ const MI_Char *message)
{
    LCM_WriteMessage_EngineInternal(lcmContext, resourceId, channel, message);

    
    return ;
}

void LCM_WriteMessageNoParamWithoutContext(
    _In_z_ const MI_Char *resourceId, 
    _In_ MI_Uint32 errorStringId,
    _In_ MI_Uint32 channel
    )
{
    Intlstr intlstr = Intlstr_Null;
    GetResourceString(errorStringId, &intlstr);
    if (intlstr.str )
    {
        // log to ETW
        DSC_EventWriteMessageFromEngine(channel, resourceId, intlstr.str);
        Intlstr_Free(intlstr);
    }     
}

void LCM_WriteMessage1ParamWithoutContext(
    _In_z_ const MI_Char *resourceId, 
    _In_ MI_Uint32 errorStringId,
    _In_z_ const MI_Char * param1, 
    _In_ MI_Uint32 channel
    )
{
    Intlstr resStr = Intlstr_Null;
    GetResourceString1Param(errorStringId, param1, &resStr);
    if (resStr.str )
    {
        // log to ETW
        DSC_EventWriteMessageFromEngine(channel, resourceId, resStr.str);
        Intlstr_Free(resStr);
    }     
}

void LCM_WriteMessageInfo(
    _In_ LCMProviderContext *lcmContext,  
    _In_opt_z_ const MI_Char *computerName, 
    _In_ MI_Uint32 channel,
    _In_z_ const MI_Char *userSid)
{
    LCM_WriteMessageInfo_Internal(lcmContext, computerName, channel, userSid);

    // log to ETW
    DSC_EventWriteMessageFromEngineInfo(computerName, userSid);
    return ;
}

void LCM_WriteMessageFromProvider(
    _In_ LCMProviderContext *lcmContext,  
    _In_z_ const MI_Char *resourceId, 
    _In_ MI_Uint32 channel, 
    _In_z_ const MI_Char *message)
{
    LCM_WriteMessage_ProviderInternal(lcmContext, resourceId, channel, message);


    return ;
}

//Note: Whenever you update this methods, please update the equivalent LogCAProgressMessage method in CALogInfrastructure.lib
// Source Code: CALog.C
void LCM_WriteProgress(
    _In_ LCMProviderContext *lcmContext, 
    _In_z_ const MI_Char *activity,
    _In_z_ const MI_Char * currentOperation,
    _In_z_ const MI_Char * statusDescroption,
    _In_ MI_Uint32 percentComplete,
    _In_ MI_Uint32 secondsRemaining)
{
#if !defined(BUILD_OMS)
    if ((lcmContext->executionMode & LCM_EXECUTIONMODE_ONLINE) && lcmContext->context)
    {
        size_t targetLength=DEVICE_NAME_SIZE+Tcslen(currentOperation)+50; //+1 for "\0"
        size_t result;
        MI_Char* currentOpWithMachine= (MI_Char*) DSC_malloc(sizeof(MI_Char)*targetLength, NitsMakeCallSite(-3, NULL, NULL, 0));
        if(currentOpWithMachine!=NULL)
        {
            result = Stprintf(currentOpWithMachine, targetLength,MI_T("[%T] %T") ,g_JobInformation.deviceName,currentOperation);
            if (result != -1)
            {
                MI_Context_WriteProgress((MI_Context*) lcmContext->context, activity, currentOpWithMachine, statusDescroption, percentComplete, secondsRemaining);
                                 
            }
            DSC_free(currentOpWithMachine);   
        }
        else
        {
            MI_Context_WriteProgress((MI_Context*) lcmContext->context, activity, currentOperation, statusDescroption, percentComplete, secondsRemaining);
        }
    }
#endif
}

void LCM_WriteStreamParameter(
    _In_ LCMProviderContext* lcmContext,
    _In_z_ const MI_Char* name,
    _In_ const MI_Value* value,
    _In_ MI_Type type,
    _In_ MI_Uint32 flags)
{
#if !defined(BUILD_OMS)
    if (lcmContext->context)
    {
        MI_Context_WriteStreamParameter(lcmContext->context, name, value, type, flags);
    }
#endif
}

void LCM_WriteError(
    _In_ LCMProviderContext *lcmContext,
    _In_ MI_Instance* instanceMIError)
{
    MI_Result result = MI_RESULT_OK;
    MI_Boolean flag = FALSE;
    MI_Uint32 errorCategory;
    MI_Uint32 errorCode;
    MI_ConstStringPtr errorMessage;
    MI_ConstStringPtr messageId;
    MI_ConstStringPtr errorType;

    ExtractCimErrorFields(instanceMIError, &result, &errorCategory, &errorCode, &errorMessage, &messageId, &errorType);

#if defined(BUILD_OMS)
    DSC_LOG_ERROR("Encounterd an error (%d) '%s'\n", errorCode, errorMessage);
#else
    if (lcmContext != NULL && instanceMIError != NULL)
    {
        if((lcmContext->executionMode & LCM_EXECUTIONMODE_ONLINE) && lcmContext->context)
        {
            result = MI_Context_WriteCimError((MI_Context*) lcmContext->context, instanceMIError, &flag);

            if (result != MI_RESULT_OK)
            {
                // Ignore the failure, this doesn't impact the functionality.
            }
        }
    }
#endif
}


void LCM_PromptUserFromProvider(
    _In_ LCMProviderContext *lcmContext, 
    _In_z_ const MI_Char *resourceId, 
    _In_z_   const MI_Char *message,
    _In_ MI_PromptType promptType,
    _Out_ MI_Boolean *flag)
{
    *flag = MI_TRUE;

#if !defined(BUILD_OMS)
    if ((lcmContext->executionMode & LCM_EXECUTIONMODE_ONLINE) && lcmContext->context && promptType == MI_PROMPTTYPE_NORMAL)
    {
        MI_Context_PromptUser((MI_Context*) lcmContext->context, message,MI_PROMPTTYPE_NORMAL, flag);
    }
#endif
}


MI_Result GetMofChecksum(
    _Outptr_result_maybenull_z_  MI_Char** mofChecksum,
    _In_z_ const MI_Char* configName,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result r = MI_RESULT_OK;
    MI_Uint8 *checksumBuffer = NULL;
    MI_Uint8 *tmpchecksumBuffer = NULL;
    MI_Uint32 checksumBufferSize = 0;
    MI_Uint32 mofChecksumSize = 0;
    MI_Char *checkSumFile = NULL;
    MI_Char *configurationFile = NULL;

    if (mofChecksum)
    {
        *mofChecksum = NULL;
    }

    if (cimErrorDetails == NULL)
    {        
        return MI_RESULT_INVALID_PARAMETER; 
    }
    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once. 
    //Check if we need it for partial configuraiton or full configuration
    if( configName == NULL)
    {
        checkSumFile = (MI_Char*)GetConfigChecksumFileName();
        configurationFile = (MI_Char*)GetCurrentConfigFileName();
    }
    else
    {
        r = GetPartialConfigurationPathCheckSum(configName, &checkSumFile, cimErrorDetails);
        if( r != MI_RESULT_OK)
            return r;
        
        r = GetPartialConfigurationPath(configName, &configurationFile, cimErrorDetails);
        if( r != MI_RESULT_OK)
        {
            DSC_free(checkSumFile);
            return r;        
        }
    }

    /* Read checksum file if exists.*/
    if (File_ExistT(checkSumFile) == 0)
    {
        r = ReadFileContent(checkSumFile, &tmpchecksumBuffer, &checksumBufferSize, cimErrorDetails);    
        checksumBuffer = (MI_Uint8*)DSC_malloc(checksumBufferSize + 1, NitsHere());
        memcpy(checksumBuffer, tmpchecksumBuffer, checksumBufferSize);
        checksumBuffer[checksumBufferSize] = '\0';
        DSC_free(tmpchecksumBuffer);
        if( configName != NULL)
        {
            DSC_free(checkSumFile);
            DSC_free(configurationFile);
        }
    }
    else
    {
        MI_Uint32 computedMofChecksumSize = 0;
        MI_Uint8 *computedMofChecksum = NULL;
        MI_Uint8 RawHash[SHA256TRANSFORM_DIGEST_LEN];

        //checksum doesn't exist, we need to compute it.
        /* Read current.mof file if exists.*/
        if (File_ExistT(configurationFile) == 0)
        {
            r = ReadFileContent(configurationFile, &tmpchecksumBuffer, &checksumBufferSize, cimErrorDetails);
            checksumBuffer = (MI_Uint8*)DSC_malloc(checksumBufferSize + 1, NitsHere());
            memcpy(checksumBuffer, tmpchecksumBuffer, checksumBufferSize);
            checksumBuffer[checksumBufferSize] = '\0';
            DSC_free(tmpchecksumBuffer);
            if( configName != NULL)
            {
                DSC_free(checkSumFile);
                DSC_free(configurationFile);
            }
        }
        //use empty string if current.mof is not there
        else
        {
            // Allocate space for the null terminator resulting in an empty string.
            *mofChecksum = (MI_Char*)DSC_malloc( 2 , NitsHere());
            if( configName != NULL)
            {
                DSC_free(checkSumFile);
                DSC_free(configurationFile);
            }
            return MI_RESULT_OK;
        }
        if (r == MI_RESULT_OK)
        {
            PAL_SHA256Transform(checksumBuffer, checksumBufferSize, RawHash);

            computedMofChecksum = DSC_malloc( SHA256TRANSFORM_DIGEST_LEN*2 +1, TLINE);
            if (computedMofChecksum == NULL)
            {
                DSC_free(checksumBuffer);
                return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_ENGINEHELPER_MEMORY_ERROR);                
            }
            {
                // Calculate HEX representation
                char const alphabet[] = "0123456789ABCDEF";
                int iCount = 0;
                computedMofChecksum[SHA256TRANSFORM_DIGEST_LEN*2] = '\0';
                for( iCount = 0; iCount < SHA256TRANSFORM_DIGEST_LEN; iCount++)
                {
                    computedMofChecksum[iCount*2] = alphabet[RawHash[iCount]/16];
                    computedMofChecksum[iCount*2+1] = alphabet[RawHash[iCount]%16];                    
                }
            }

            DSC_free(checksumBuffer);
            checksumBuffer = computedMofChecksum;
            checksumBufferSize = CHECKSUM_SIZE;
        }
    }

    if (r != MI_RESULT_OK)
    {
        return r;
    }

    *mofChecksum = (char *)checksumBuffer;

    return MI_RESULT_OK;
}

MI_Result RunConsistencyEngine(
    _In_ MI_Context* context,
    _In_ MI_Uint32 flags,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result r = MI_RESULT_OK;
    MI_Boolean complianceStatus;

    // Log the event here
    DSC_EventWriteLCMConsistencyEngineRunAttempt();    
    r = CallConsistencyEngine(context, flags, cimErrorDetails);
    if (r == MI_RESULT_OK)
    {
        // update compliance result.
        complianceStatus = MI_TRUE;
        r = UpdateCurrentStatus(&complianceStatus, NULL, NULL, NULL, cimErrorDetails);
        DSC_EventWriteLCMConsistencyEngineRunSuccess();
    }
    else
    {
        MI_Instance *tempCimErrorDetails = NULL;
        // update compliance result.
        complianceStatus = MI_FALSE;
        // Consistency Engine already failed, ignore the error from updating current status. This change is needed for NITS fault injection.
        UpdateCurrentStatus(&complianceStatus, NULL, NULL, NULL, &tempCimErrorDetails);
        MI_Instance_Delete(tempCimErrorDetails);
    }

    return r;
}

MI_Result IsRegisterdForPull(
    _In_ MI_Instance *currentMetaConfigInstance,
    _Out_ MI_Boolean *isPullEnabled,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result result = MI_RESULT_OK;
    MI_Value value;
    MI_Uint32 flags;
    *isPullEnabled = MI_FALSE;

    if (cimErrorDetails == NULL)
    {        
        return MI_RESULT_INVALID_PARAMETER; 
    }
    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once. 

    result = MI_Instance_GetElement(currentMetaConfigInstance, MSFT_DSCMetaConfiguration_RefreshMode,&value, NULL, &flags, NULL);
    if (result != MI_RESULT_OK)
    {
        return GetCimMIError(result, cimErrorDetails, ID_LCM_FAILED_TO_GET_METACONFIGURATION);
    }

    if (flags & MI_FLAG_NULL)
    {
        value.string = DEFAULT_RefreshMode;
    }

    if (value.string && Tcscasecmp(value.string,METADATA_REFRESHMODE_PULL) == 0 )
    {
        *isPullEnabled = MI_TRUE;
    }

    return MI_RESULT_OK;
}

MI_Result RegisterTask(
    _In_ MI_Instance *currentMetaConfigInstance,
    _In_z_ const MI_Char *propName,
    _In_z_ const MI_Char *taskName,
    _In_ MI_Uint32 defaultValue,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result result = MI_RESULT_OK;
    MI_Value refreshFrequencyInMins;
    MI_Uint32 flags;
    MI_Char timeString[MAX_PATH] = {0};

    if (cimErrorDetails == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER; 
    }
    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once. 

    result = MI_Instance_GetElement((MI_Instance*)currentMetaConfigInstance, propName, &refreshFrequencyInMins, NULL, &flags, NULL);
    if (result != MI_RESULT_OK)
    {
        return GetCimMIError(result, cimErrorDetails, ID_LCM_FAILED_TO_GET_METACONFIGURATION);
    }

    result = GetNextRefreshTimeHelper(timeString);

    if (result != MI_RESULT_OK)
    {
        return GetCimMIError(result, cimErrorDetails, ID_LCM_FAILED_TO_GET_METACONFIGURATION);
    }

    result = UpdateTask((MI_Char*)taskName, (MI_Char*)timeString, refreshFrequencyInMins.uint32*60, cimErrorDetails);
    if (result != MI_RESULT_OK)
    {
        return GetCimMIError(result, cimErrorDetails, ID_LCM_FAILED_TO_GET_METACONFIGURATION);
    }

    return result;
}

MI_Result RegisterConsistencyTask(
    _In_ MI_Instance *currentMetaConfigInstance,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
#if defined(BUILD_OMS)
  return MI_RESULT_OK;
#else
    MI_Result result;
    MI_Value refreshMode;
    MI_Uint32 flags;

    if (cimErrorDetails == NULL)
    {        
        return MI_RESULT_INVALID_PARAMETER; 
    }

    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once.

    result = MI_Instance_GetElement((MI_Instance*)currentMetaConfigInstance, MSFT_DSCMetaConfiguration_RefreshMode, &refreshMode, NULL, &flags, NULL);
    if (result != MI_RESULT_OK)
    {
        return result;
    }
    
    if ((flags & MI_FLAG_NULL) == 0 && Tcscasecmp(METADATA_REFRESHMODE_PUSH, refreshMode.string) == 0)
    {
        /*Update for Consistency task using configurationModeFrequencyMins*/
        result = RegisterTask(currentMetaConfigInstance, MSFT_DSCMetaConfiguration_ConfigurationModeFrequencyMins, OMI_CONSISTENCY_TASKSCHEDULE_NAME,
                              DEFAULT_ConfigurationModeFrequencyMins, cimErrorDetails);
    }
    else
    {
        /*Update for Consistency task using RefreshFrequencyMins*/
        result = RegisterTask(currentMetaConfigInstance, MSFT_DSCMetaConfiguration_RefreshFrequencyMins, OMI_CONSISTENCY_TASKSCHEDULE_NAME,
                              DEFAULT_MinRefreshFrequencyMins, cimErrorDetails);
    }

    return result;
#endif
}

MI_Result DoPullServerRefresh(
    _In_ MI_Instance *metaConfigInstance,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result result = MI_RESULT_OK;
    
    LCMProviderContext lcmContext;
    result = LCM_Pull_Execute(&lcmContext, metaConfigInstance, cimErrorDetails);
    
    return result;
}

MI_Result GetConfigurationDownloadManagerInformation(_In_ LCMProviderContext* lcmContext, _In_ MI_Instance *metaInstance, _Out_opt_ MI_Value* v, _Out_opt_ MI_Uint32* flags, _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    if (cimErrorDetails == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *cimErrorDetails = NULL;

    MI_Result result = DSC_MI_Instance_GetElement(metaInstance, MSFT_DSCMetaConfiguration_ConfigurationDownloadManagers, v, NULL, flags, NULL);
    if (result != MI_RESULT_OK)
    {
        return result = GetCimMIError2Params(result, cimErrorDetails, ID_ENGINEHELPER_GET_PROPERTY_X_FROM_Y_FAILED, MSFT_DSCMetaConfiguration_ConfigurationDownloadManagers, OMI_CONFIGURATIONDOWNLOADMANAGER_CLASSNAME);
    }
    return result;
}

MI_Result GetResourceModuleManagerInformation(_In_ LCMProviderContext* lcmContext, _In_ MI_Instance *metaInstance, _Out_opt_ MI_Value* v, _Out_opt_ MI_Uint32* flags, _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    if (cimErrorDetails == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *cimErrorDetails = NULL;

    MI_Result result = DSC_MI_Instance_GetElement(metaInstance, MSFT_DSCMetaConfiguration_ResourceModuleManagers, v, NULL, flags, NULL);
    if (result != MI_RESULT_OK)
    {
        return result = GetCimMIError2Params(result, cimErrorDetails, ID_ENGINEHELPER_GET_PROPERTY_X_FROM_Y_FAILED, MSFT_DSCMetaConfiguration_ResourceModuleManagers, OMI_RESOURCEMODULEMANAGER_CLASSNAME);
    }
    return result;
}

MI_Result GetReportManagerInformation(_In_ LCMProviderContext* lcmContext, _In_ MI_Instance *metaInstance, _Out_opt_ MI_Value* v, _Out_opt_ MI_Uint32* flags, _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    if (cimErrorDetails == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *cimErrorDetails = NULL;

    MI_Result result = DSC_MI_Instance_GetElement(metaInstance, MSFT_DSCMetaConfiguration_ReportManagers, v, NULL, flags, NULL);
    if (result != MI_RESULT_OK)
    {
        return result = GetCimMIError2Params(result, cimErrorDetails, ID_ENGINEHELPER_GET_PROPERTY_X_FROM_Y_FAILED, MSFT_DSCMetaConfiguration_ReportManagers, OMI_REPORTMANAGER_CLASSNAME);
    }
    return result;
}

/*Returns registration information for the ConfigurationManager, resource manager and report manager*/
MI_Result GetRegistrationInformation(_In_ LCMProviderContext* lcmContext, _In_ MI_Instance *metaInstance, _In_ MI_Uint32 isPull, 
    _Inout_ MI_InstanceA *configurationDownloadManagerInstances, _Inout_ MI_InstanceA *resourceModuleManagerInstances, _Inout_ MI_InstanceA *reportManagerInstances,    
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Value value;
    MI_Result result;
    MI_Uint32 flags;

    memset(configurationDownloadManagerInstances, 0, sizeof(MI_InstanceA));
    memset(resourceModuleManagerInstances, 0, sizeof(MI_InstanceA));
    memset(reportManagerInstances, 0, sizeof(MI_InstanceA));
    
    if (cimErrorDetails == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *cimErrorDetails = NULL;

    if (isPull)
    {
        result = GetConfigurationDownloadManagerInformation(lcmContext, metaInstance, &value, &flags, cimErrorDetails);
        if (result == MI_RESULT_OK && !(flags & MI_FLAG_NULL))
        {
            configurationDownloadManagerInstances->data = value.instancea.data;
            configurationDownloadManagerInstances->size = value.instancea.size;
        }

        result = GetResourceModuleManagerInformation(lcmContext, metaInstance, &value, &flags, cimErrorDetails);
        if (result == MI_RESULT_OK && !(flags & MI_FLAG_NULL))
        {
            resourceModuleManagerInstances->data = value.instancea.data;
            resourceModuleManagerInstances->size = value.instancea.size;
        }
    }
    else
    {
        result = GetReportManagerInformation(lcmContext, metaInstance, &value, &flags, cimErrorDetails);
        if (result == MI_RESULT_OK && !(flags & MI_FLAG_NULL))
        {
            reportManagerInstances->data = value.instancea.data;
            reportManagerInstances->size = value.instancea.size;
        }
    }
    
    return result;
}

MI_Result LCM_Do_Register(
    _In_ MI_Instance *metaConfigInstance,
    _In_ MI_Instance *managerInstance,
    _In_z_ MI_Char* agentId,
    _In_z_ MI_Char *thumbprint,
    _In_ MI_Instance *registrationPayload,
    _In_ MI_StringA *configurationNames,
    _In_  MI_Uint32 typeOfManagerInstance,
    _Outptr_result_maybenull_z_  MI_Char** resultStatus,
    _Out_ MI_Uint32* getActionStatusCode,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result result = MI_RESULT_OK;

    if (cimErrorDetails)
    {
        *cimErrorDetails = NULL;
    }

    result = Do_Register(metaConfigInstance, managerInstance, agentId, thumbprint, registrationPayload, configurationNames, 
        typeOfManagerInstance, resultStatus, getActionStatusCode, cimErrorDetails);
    if (result != MI_RESULT_OK)
    {
        return result;
    }

    if (*resultStatus == NULL)
    {
        return GetCimMIError(MI_RESULT_NOT_FOUND, cimErrorDetails, ID_LCM_REGISTER_NORESULTSTATUS);
    }

    //Validate valid status codes
    if (!(Tcscasecmp(*resultStatus, REGISTER_STATUSCODE_CREATED) == 0))
    {
        //Unknown status code return error.
        return GetCimMIError(MI_RESULT_NOT_FOUND, cimErrorDetails, ID_LCM_REGISTER_UNEXPECTEDRESULTSTATUS);
    }

    return result;
}

MI_Result RegisterWithServers(_In_ LCMProviderContext *lcmContext,
    _In_ MI_Instance *metaConfigInstance,
    _In_ MI_Uint32 isPull,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result result = MI_RESULT_OK;    
    MI_InstanceA configurationDownloadManagerInstances = { 0 };
    MI_InstanceA resourceModuleManagerInstances = { 0 };
    MI_InstanceA reportManagerInstances = { 0 };
    MI_Boolean shouldUseV1Protocol = MI_FALSE;
    
    if (cimErrorDetails)
    {
        *cimErrorDetails = NULL;
    }    

    ShouldUseV1Protocol(&shouldUseV1Protocol);
    if (shouldUseV1Protocol == MI_FALSE)
    {
        result = GetRegistrationInformation(lcmContext, metaConfigInstance, isPull,
            &configurationDownloadManagerInstances, &resourceModuleManagerInstances, &reportManagerInstances,
            cimErrorDetails);
        EH_CheckResult(result);

        if (isPull)
        {
            if (configurationDownloadManagerInstances.data != NULL)
            {
                result = DoRegistration(lcmContext, &configurationDownloadManagerInstances, /*typeOfDownloadManagerInstance */ 1, cimErrorDetails);
                EH_CheckResult(result);                
                result = SetDownloadManagerInstancesInMetaConfig(lcmContext, configurationDownloadManagerInstances, /*typeOfDownloadManagerInstance */ 1, cimErrorDetails);
                EH_CheckResult(result);
            }
            if (resourceModuleManagerInstances.data != NULL)
            {
                result = DoRegistration(lcmContext, &resourceModuleManagerInstances, /*typeOfDownloadManagerInstance */ 2, cimErrorDetails);
                EH_CheckResult(result);
                result = SetDownloadManagerInstancesInMetaConfig(lcmContext, resourceModuleManagerInstances, /*typeOfDownloadManagerInstance */ 2, cimErrorDetails);
                EH_CheckResult(result);                
            }
        }
        else
        {
            if (reportManagerInstances.data != NULL)
            {
                result = DoRegistration(lcmContext, &reportManagerInstances, /*typeOfDownloadManagerInstance */ 3, cimErrorDetails);
                EH_CheckResult(result);                
                result = SetDownloadManagerInstancesInMetaConfig(lcmContext, reportManagerInstances, /*typeOfDownloadManagerInstance */ 3, cimErrorDetails);
                EH_CheckResult(result);
            }
        }
        // Update the metaconfig
        result = SetMetaConfig((MI_Instance*)g_metaConfig, cimErrorDetails);
        EH_CheckResult(result);
    }
    else
    {
//        DSC_EventWriteLCMProtocolVersion1InUse(lcmContext->configurationDetails.jobGuidString);
    }


EH_UNWIND;   
    return result;
}

MI_Result DoRegistration(
    _In_ LCMProviderContext* lcmContext,
    _Inout_ MI_InstanceA *managerInstances,    
    _In_ MI_Uint32 typeOfDownloadManagerInstance,    
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)    
{
    MI_Uint32 i = 0;
    MI_Result result = MI_RESULT_OK;
    RegistrationRequest *registrationRequest = NULL;
    RegistrationManager *registrationManager = (RegistrationManager*)g_registrationManager;
    MI_Value value;
    MI_Uint32 flags = 0;    

    if (cimErrorDetails)
    {
        *cimErrorDetails = NULL;
    }
    for (i = 0; i < managerInstances->size; i++)
    {
        registrationRequest = (RegistrationRequest*)DSC_malloc(sizeof(RegistrationRequest), NitsHere());
        if (registrationRequest == NULL)
        {
            EH_Fail_(GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_LCMHELPER_MEMORY_ERROR));
        }

        registrationRequest->registrationData = managerInstances->data[i];
        registrationRequest->typeOfManagerInstance = typeOfDownloadManagerInstance;

        if (typeOfDownloadManagerInstance == 1)
        {
            result = DSC_MI_Instance_GetElement(managerInstances->data[i], MSFT_ConfigurationNames_Name, &value, NULL, &flags, NULL);
            if (result == MI_RESULT_OK && !(flags & MI_FLAG_NULL))
            {
                registrationRequest->configurationNames = value.stringa;
            }
        }

#if defined(BUILD_OMS)
    // Check if RegistrationKey is specified.  If so, allow registration for OMS. Otherwise, do not attempt to register.
    result = MI_Instance_GetElement(registrationRequest->registrationData, MSFT_RegistrationKey_Name, &value, NULL, NULL, NULL);
    if (result == MI_RESULT_OK)
    {
        result = RegistrationManager_RunRequest(registrationManager, registrationRequest, cimErrorDetails);
        EH_CheckResult(result);
        
        result = DeleteRegistrationKeyFromManagerInstance(lcmContext, &managerInstances->data[i], typeOfDownloadManagerInstance, cimErrorDetails);
        EH_CheckResult(result);
    }
#else
    result = RegistrationManager_RunRequest(registrationManager, registrationRequest, cimErrorDetails);
    EH_CheckResult(result);
    
    result = DeleteRegistrationKeyFromManagerInstance(lcmContext, &managerInstances->data[i], typeOfDownloadManagerInstance, cimErrorDetails);
    EH_CheckResult(result);
#endif

    // TODO, insivara : Write events
        result = UpdateMetaConfigWithAgentId(registrationManager->agentId, (MI_Instance*)g_metaConfig);
        EH_CheckResult(result);    

        // In a loop the variable will be reallocated and we lose the reference to the original memory.
        if (registrationRequest != NULL)
        {
            DSC_free(registrationRequest);
            registrationRequest = NULL;
        }
    }

EH_UNWIND;
    if (registrationRequest != NULL)
    {
        DSC_free(registrationRequest);
    }
    return result;
}

// Update DownloadManagerInstances in MetaConfig (These downloadmanager instances will not have the RegistrationKey if Registration has succeeded)
MI_Result SetDownloadManagerInstancesInMetaConfig(
    _In_ LCMProviderContext* lcmContext,
    _In_ MI_InstanceA downloadManagerInstances,
    _In_ MI_Uint32 typeOfDownloadManagerInstance,    
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Value value;
    MI_Result result = MI_RESULT_OK;

    if (cimErrorDetails)
    {
        *cimErrorDetails = NULL;
    }

    if (typeOfDownloadManagerInstance == 1)
    {
        value.instancea = downloadManagerInstances;
        result = MI_Instance_SetElement((MI_Instance*)g_metaConfig, MSFT_DSCMetaConfiguration_ConfigurationDownloadManagers, &value, MI_INSTANCEA, 0);
        if (result != MI_RESULT_OK)
        {
            EH_Fail_(GetCimMIError2Params(result, cimErrorDetails, ID_ENGINEHELPER_GET_PROPERTY_X_FROM_Y_FAILED, MSFT_RegistrationKey_Name, OMI_CONFIGURATIONDOWNLOADMANAGER_CLASSNAME));
        }
    }
    else if (typeOfDownloadManagerInstance == 2)
    {
        value.instancea = downloadManagerInstances;
        result = MI_Instance_SetElement((MI_Instance*)g_metaConfig, MSFT_DSCMetaConfiguration_ResourceModuleManagers, &value, MI_INSTANCEA, 0);
        if (result != MI_RESULT_OK)
        {
            EH_Fail_(GetCimMIError2Params(result, cimErrorDetails, ID_ENGINEHELPER_GET_PROPERTY_X_FROM_Y_FAILED, MSFT_RegistrationKey_Name, OMI_RESOURCEMODULEMANAGER_CLASSNAME));
        }
    }
    else if (typeOfDownloadManagerInstance == 3)
    {
        value.instancea = downloadManagerInstances;
        result = MI_Instance_SetElement((MI_Instance*)g_metaConfig, MSFT_DSCMetaConfiguration_ReportManagers, &value, MI_INSTANCEA, 0);
        if (result != MI_RESULT_OK)
        {
            EH_Fail_(GetCimMIError2Params(result, cimErrorDetails, ID_ENGINEHELPER_GET_PROPERTY_X_FROM_Y_FAILED, MSFT_RegistrationKey_Name, OMI_REPORTMANAGER_CLASSNAME));
        }
    }
EH_UNWIND;
        return result;
}

MI_Result RegisterWithPullServers(_In_ LCMProviderContext *lcmContext,
    _In_ MI_Instance *metaConfigInstance,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    if (cimErrorDetails)
    {
        *cimErrorDetails = NULL;
    }
    return RegisterWithServers(lcmContext, metaConfigInstance, 1 /*IsPull*/, cimErrorDetails);
}

MI_Result RegisterWithReportingServers(_In_ LCMProviderContext *lcmContext,
    _In_ MI_Instance *metaConfigInstance,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    if (cimErrorDetails)
    {
        *cimErrorDetails = NULL;
    }
    return RegisterWithServers(lcmContext, metaConfigInstance, 0 /*IsPull*/, cimErrorDetails);
}

MI_Result DeleteRegistrationKeyFromManagerInstance(_In_ LCMProviderContext *lcmContext,
    _Inout_ MI_Instance **managerInstance,
    MI_Uint32 typeOfDownloadManagerInstance,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result result = MI_RESULT_OK;
    MI_Value registrationKeyValue;
    MI_Uint32 flags;
    MI_Char* downloadManagerClassName = NULL;

    switch (typeOfDownloadManagerInstance)
    {
        case 1:
            downloadManagerClassName = OMI_CONFIGURATIONDOWNLOADMANAGER_CLASSNAME;
            break;
        case 2:
            downloadManagerClassName = OMI_RESOURCEMODULEMANAGER_CLASSNAME;
            break;
        case 3:
            downloadManagerClassName = OMI_REPORTMANAGER_CLASSNAME;
        default:
            break;
    }

    if (cimErrorDetails)
    {
        *cimErrorDetails = NULL;
    }

    result = DSC_MI_Instance_GetElement(*managerInstance, MSFT_RegistrationKey_Name, &registrationKeyValue, NULL, &flags, NULL);
    if (result != MI_RESULT_OK)
    {
        EH_Fail_(GetCimMIError2Params(result, cimErrorDetails, ID_ENGINEHELPER_GET_PROPERTY_X_FROM_Y_FAILED, MSFT_RegistrationKey_Name, downloadManagerClassName));
    }

    registrationKeyValue.string = MI_T("");

    result = MI_Instance_SetElement(*managerInstance, MSFT_RegistrationKey_Name, &registrationKeyValue, MI_STRING, 0);
    if (result != MI_RESULT_OK)
    {
        EH_Fail_(GetCimMIError2Params(result, cimErrorDetails, ID_ENGINEHELPER_GET_PROPERTY_X_FROM_Y_FAILED, MSFT_RegistrationKey_Name, downloadManagerClassName));
    }

EH_UNWIND;
        return result;
}


void ExtractCimErrorFields(
    _In_opt_ MI_Instance* errorDetails,
    _Inout_ MI_Result* miResult,
    _Out_ MI_Uint32* errorCategory,
    _Out_ MI_Uint32* errorCode,
    _Out_ MI_ConstStringPtr* errorMessage,
    _Out_ MI_ConstStringPtr* messageId,
    _Out_ MI_ConstStringPtr* errorType)
{
    /* Extract properties from the error object so we can log them.
    * These calls are best-effort - ignore failures to find/match things. */
    MI_Value value;
    MI_Type type;
    MI_Uint32 flags;
    MI_Result tempResult;

    MI_ConstStringPtr emptyString = MI_T("");

    *errorCategory = MI_ERRORCATEGORY_NOT_SPECIFIED;
    *errorCode = MI_RESULT_FAILED;
    *errorMessage = emptyString;
    *messageId = emptyString;
    *errorType = emptyString;

    tempResult = DSC_MI_Instance_GetElement(errorDetails, MI_T("CIMStatusCode"), &value, &type, &flags, NULL);
    if (tempResult == MI_RESULT_OK &&
        type == MI_UINT16 &&
        (flags & MI_FLAG_NULL) == 0)
    {
        *miResult = value.uint16;
    }

    tempResult = DSC_MI_Instance_GetElement(errorDetails, MI_T("error_Category"), &value, &type, &flags, NULL);
    if (tempResult == MI_RESULT_OK &&
        type == MI_UINT16 &&
        (flags & MI_FLAG_NULL) == 0)
    {
        *errorCategory = value.uint16;
    }

    tempResult = DSC_MI_Instance_GetElement(errorDetails, MI_T("error_Code"), &value, &type, &flags, NULL);
    if (tempResult == MI_RESULT_OK &&
        type == MI_UINT32 &&
        (flags & MI_FLAG_NULL) == 0)
    {
        *errorCode = value.uint32;
    }

    tempResult = DSC_MI_Instance_GetElement(errorDetails, MI_T("Message"), &value, &type, &flags, NULL);
    if (tempResult == MI_RESULT_OK &&
        type == MI_STRING &&
        (flags & MI_FLAG_NULL) == 0)
    {
        *errorMessage = value.string;
    }

    tempResult = DSC_MI_Instance_GetElement(errorDetails, MI_T("MessageID"), &value, &type, &flags, NULL);
    if (tempResult == MI_RESULT_OK &&
        type == MI_STRING &&
        (flags & MI_FLAG_NULL) == 0)
    {
        *messageId = value.string;
    }

    tempResult = DSC_MI_Instance_GetElement(errorDetails, MI_T("error_Type"), &value, &type, &flags, NULL);
    if (tempResult == MI_RESULT_OK &&
        type == MI_STRING &&
        (flags & MI_FLAG_NULL) == 0)
    {
        *errorType = value.string;
    }
}


MI_Result GetErrorForServer(
    _In_ LCMProviderContext *lcmContext,
    _In_ MI_Application *miApp,
    _In_ MI_Instance* instanceMIError,
    _In_opt_z_ const MI_Char * errorMessage,
    _In_opt_z_ const MI_Char *errorSource,
    _In_opt_z_ const MI_Char *resourceId,
    _In_ MI_Uint32 errorCode,
    _Outptr_result_maybenull_ MI_Instance **errorObject)
{
    MI_Value value;
    MI_Value inValue;
    MI_Uint32 flags;
    MI_Boolean couldSetLocaleFromConfStatus = MI_FALSE;
    const MI_Char *internalResourceId = REPORTING_DEFAULT_MESSAGE;
    const MI_Char *internalErrorMessage = REPORTING_DEFAULT_MESSAGE;
    const MI_Char *internalErrorSource = REPORTING_DEFAULT_MESSAGE;
    //const MI_Char *internalLocal = REPORTING_DEFAULT_MESSAGE;
    MI_Result r = MI_RESULT_OK;

    r = DSC_MI_Application_NewInstance(miApp, REPORTING_ERRORCLASSNAME, NULL, errorObject);
    if (r != MI_RESULT_OK)
    {
        return r;
    }
    // ErrorSource
    if (errorSource != NULL)
        internalErrorSource = errorSource;
    value.string = (MI_Char*)internalErrorSource;
    r = MI_Instance_AddElement(*errorObject, REPORTING_ERRORSOURCE, &value, MI_STRING, 0);
    if (r != MI_RESULT_OK)
    {
        MI_Instance_Delete(*errorObject);
        *errorObject = NULL;
        return r;
    }
    // ResourceId
    if (resourceId != NULL)
        internalResourceId = resourceId;
    value.string = (MI_Char*)internalResourceId;
    r = MI_Instance_AddElement(*errorObject, REPORTING_RESOURCEID, &value, MI_STRING, 0);
    if (r != MI_RESULT_OK)
    {
        MI_Instance_Delete(*errorObject);
        *errorObject = NULL;
        return r;
    }
    // ErrorCode
    value.uint32 = errorCode;
    r = MI_Instance_AddElement(*errorObject, REPORTING_ERRORCODE, &value, MI_UINT32, 0);
    if (r != MI_RESULT_OK)
    {
        MI_Instance_Delete(*errorObject);
        *errorObject = NULL;
        return r;
    }
    // ErrorMessage
    if (errorMessage != NULL)
        internalErrorMessage = errorMessage;
    value.string = (MI_Char*)internalErrorMessage;
    r = MI_Instance_AddElement(*errorObject, REPORTING_ERRORMESSAGE, &value, MI_STRING, 0);
    if (r != MI_RESULT_OK)
    {
        MI_Instance_Delete(*errorObject);
        *errorObject = NULL;
        return r;
    }
    // Locale
/* TODO remove:
    if (lcmContext->configurationStatus.size >= 1)
    {
        MSFT_DSCConfigurationStatus *configurationStatus = lcmContext->configurationStatus.data[0];
        if (configurationStatus->Locale.exists)
        {
            couldSetLocaleFromConfStatus = MI_TRUE;
            value.string = (MI_Char*)configurationStatus->Locale.value;
            r = MI_Instance_AddElement(*errorObject, REPORTING_LOCALE, &value, MI_STRING, 0);
            if (r != MI_RESULT_OK)
            {
                MI_Instance_Delete(*errorObject);
                *errorObject = NULL;
                return r;
            }
        }
    }
*/
    //Set the default value of Locale if we could not find it from the ConfigurationStatus structure.
    if (!couldSetLocaleFromConfStatus)
    {
        value.string = REPORTING_DEFAULT_MESSAGE;
        r = MI_Instance_AddElement(*errorObject, REPORTING_LOCALE, &value, MI_STRING, 0);
        if (r != MI_RESULT_OK)
        {
            MI_Instance_Delete(*errorObject);
            *errorObject = NULL;
            return r;
        }
    }
    // Serialized Error
    r = MI_Instance_GetElement(instanceMIError, REPORTING_SERIALIZEDERROR, &inValue, NULL, &flags, NULL);
    if (r == MI_RESULT_OK && !(flags & MI_FLAG_NULL))
    {
        value.string = inValue.string;
        r = MI_Instance_AddElement(*errorObject, REPORTING_INTERNALERRORS, &value, MI_STRING, 0);
        if (r != MI_RESULT_OK)
        {
            MI_Instance_Delete(*errorObject);
            *errorObject = NULL;
            return r;
        }
    }
    return MI_RESULT_OK;
}

MI_Result GetStatusForServer(
    _In_ LCMProviderContext *lcmContext,
    _In_ MI_Application *miApp,
    _Outptr_result_maybenull_ MI_Instance **statusObject)
{
    MI_Value value;
    MI_Boolean couldSetLocaleFromConfStatus = MI_FALSE;
    MI_Result r = MI_RESULT_OK;

    r = DSC_MI_Application_NewInstance(miApp, REPORTING_STATUSCLASSNAME, NULL, statusObject);
    if (r != MI_RESULT_OK)
    {
        return r;
    }

/* TODO remove:
    if (lcmContext->configurationStatus.size >= 1)
    {
        MSFT_DSCConfigurationStatus *configurationStatus = lcmContext->configurationStatus.data[0];
        // Locale
        if (configurationStatus->Locale.exists)
        {
            couldSetLocaleFromConfStatus = MI_TRUE;
            value.string = (MI_Char*)configurationStatus->Locale.value;
            r = MI_Instance_AddElement(*statusObject, REPORTING_LOCALE, &value, MI_STRING, 0);
            if (r != MI_RESULT_OK)
            {
                MI_Instance_Delete(*statusObject);
                *statusObject = NULL;
                return r;
            }
        }
        // Number of resources
        if (configurationStatus->NumberOfResources.exists)
        {
            value.uint32 = configurationStatus->NumberOfResources.value;
            r = MI_Instance_AddElement(*statusObject, REPORTING_NUMBEROFRESOURCES, &value, MI_UINT32, 0);
            if (r != MI_RESULT_OK)
            {
                MI_Instance_Delete(*statusObject);
                *statusObject = NULL;
                return r;
            }
        }
        // Resources In Desired State
        if (configurationStatus->ResourcesInDesiredState.exists && configurationStatus->ResourcesInDesiredState.value.size >= 1)
        {
            value.instancea.data = (MI_Instance**)configurationStatus->ResourcesInDesiredState.value.data;
            value.instancea.size = configurationStatus->ResourcesInDesiredState.value.size;
            r = MI_Instance_AddElement(*statusObject, REPORTING_RESOURCESINDESIREDSTATE, &value, MI_INSTANCEA, 0);
            if (r != MI_RESULT_OK)
            {
                MI_Instance_Delete(*statusObject);
                *statusObject = NULL;
                return r;
            }
        }
        // Resources Not In Desired State
        if (configurationStatus->ResourcesNotInDesiredState.exists && configurationStatus->ResourcesNotInDesiredState.value.size >= 1)
        {
            value.instancea.data = (MI_Instance**)configurationStatus->ResourcesNotInDesiredState.value.data;
            value.instancea.size = configurationStatus->ResourcesNotInDesiredState.value.size;
            r = MI_Instance_AddElement(*statusObject, REPORTING_RESOURCESNOTINDESIREDSTATE, &value, MI_INSTANCEA, 0);
            if (r != MI_RESULT_OK)
            {
                MI_Instance_Delete(*statusObject);
                *statusObject = NULL;
                return r;
            }
        }
    }
*/
    //Set the default value of Locale if we could not find it from the ConfigurationStatus structure.
    if (!couldSetLocaleFromConfStatus)
    {
        value.string = REPORTING_DEFAULT_MESSAGE;
        r = MI_Instance_AddElement(*statusObject, REPORTING_LOCALE, &value, MI_STRING, 0);
        if (r != MI_RESULT_OK)
        {
            MI_Instance_Delete(*statusObject);
            *statusObject = NULL;
            return r;
        }
    }

    return MI_RESULT_OK;
}

MI_Result ReportStatusToServer(
        _In_ LCMProviderContext *lcmContext,
        _In_opt_z_ const MI_Char * errorMessage,
        _In_opt_z_ const MI_Char *errorSource,
        _In_opt_z_ const MI_Char *resourceId,
        _In_ MI_Uint32 errorCode,
        _In_ MI_Boolean bLastReport,
        _In_ MI_Uint32 isStatusReport,
        _In_opt_ MI_Instance* instanceMIError)
{
    MI_Application miApp = MI_APPLICATION_NULL;
    MI_Instance *statusReport = NULL;
    MI_Instance *errorObject = NULL;
    MI_Instance *statusObject = NULL;
    MI_Instance *extendedError = NULL;
    MI_Uint32 getActionStatusCode = 0;
    MI_Result r = MI_RESULT_OK;
    MI_Value value;

    if (g_metaConfig == NULL)
    {
        assert(1);
        return MI_RESULT_FAILED;
    }
    r = DSC_MI_Application_Initialize(0, NULL, NULL, &miApp);
        if (r != MI_RESULT_OK)
        {
                return r;
        }

    r = RegisterWithReportingServers(lcmContext, (MI_Instance*)g_metaConfig, &extendedError);
    if (r != MI_RESULT_OK)
    {
        MI_Application_Close(&miApp);
        return r;
    }

        r = DSC_MI_Application_NewInstance(&miApp, REPORTING_CLASS, NULL, &statusReport);
        if (r != MI_RESULT_OK)
        {
                MI_Application_Close(&miApp);
                return r;
        }
        if (instanceMIError && (isStatusReport == 0 ))
        {
                // Set error object
                r = GetErrorForServer(lcmContext, &miApp, instanceMIError, errorMessage, errorSource, resourceId, errorCode, &errorObject);
                if (r != MI_RESULT_OK)
                {
                        MI_Instance_Delete(statusReport);
                        MI_Application_Close(&miApp);
                        return r;
                }
                if (errorObject != NULL)
                {
                        value.instancea.size = 1;
                        value.instancea.data = &errorObject;
                        r = MI_Instance_AddElement(statusReport, REPORTING_ERRORREPORTNAME, &value, MI_INSTANCEA, 0);
                }
        }
        else if (isStatusReport == 1)
        {
                // Set status object
                r = GetStatusForServer(lcmContext, &miApp, &statusObject);
                if (r != MI_RESULT_OK)
                {
                        MI_Instance_Delete(statusReport);
                        MI_Application_Close(&miApp);
                        return r;
                }
                if (statusObject != NULL)
                {
                        value.instancea.size = 1;
                        value.instancea.data = &statusObject;
                        r = MI_Instance_AddElement(statusReport, REPORTING_STATUSREPORTNAME, &value, MI_INSTANCEA, 0);
                }
        }
        
        
        
        if (!g_bNotFirstTimeReport)
        {
            // Set Report Format version
            value.string = REPORTFORMAT_VERSION_1_0;
            r = MI_Instance_AddElement(statusReport, REPORTING_REPORTFORMATVERSION, &value, MI_STRING, 0);
            if (r != MI_RESULT_OK)
            {
                MI_Application_Close(&miApp);
                MI_Instance_Delete(statusReport);
                MI_Instance_Delete(errorObject);
                MI_Instance_Delete(statusObject);
                return r;
            }
        }

        // Set EndTime
        if (bLastReport)
        {
            PAL_Datetime time;
            // fill start datetime property of configuration status
            CPU_GetLocalTimestamp(&time);
            value.datetime = PalDatetimeToMiDatetime(time);
            r = MI_Instance_AddElement(statusReport, REPORTING_ENDTIME, &value, MI_DATETIME, 0);
            if (r != MI_RESULT_OK)
            {
                MI_Application_Close(&miApp);
                MI_Instance_Delete(statusReport);
                MI_Instance_Delete(errorObject);
                MI_Instance_Delete(statusObject);
                return r;
            }
        }
        
    // Set JobId
    value.string = g_ConfigurationDetails.jobGuidString;
    r = MI_Instance_AddElement(statusReport, REPORTING_JOBID, &value, MI_STRING, 0);
    if (r != MI_RESULT_OK)
    {
        MI_Application_Close(&miApp);
        MI_Instance_Delete(statusReport);
        MI_Instance_Delete(errorObject);
        MI_Instance_Delete(statusObject);
        return r;
    }
    // Set OperationType
    // Set Configuration version
    // Set Start Time
    // Set End Time
    // Set NodeName
    // Set IpAddress
    // Set LCM Version
    // Send these values from ConfigurationStatus

/*
    {
        if (lcmContext->configurationStatus.size >= 1)
        {
            MSFT_DSCConfigurationStatus *configurationStatus = lcmContext->configurationStatus.data[0];
            if (!lcmContext->bNotFirstTimeReport)
            {
                // Set Start Time
                if (configurationStatus->StartDate.exists)
                {
                    value.datetime = configurationStatus->StartDate.value;
                    r = MI_Instance_AddElement(statusReport, REPORTING_STARTTIME, &value, MI_DATETIME, 0);
                    if (r != MI_RESULT_OK)
                    {
                        MI_Application_Close(&miApp);
                        MI_Instance_Delete(statusReport);
                        MI_Instance_Delete(errorObject);
                        MI_Instance_Delete(statusObject);
                        return r;
                    }
                }

                // Set OperationType
                if (configurationStatus->Type.exists)
                {
                    value.string = (MI_Char*)configurationStatus->Type.value;
                    r = MI_Instance_AddElement(statusReport, REPORTING_OPERATIONTYPE, &value, MI_STRING, 0);
                    if (r != MI_RESULT_OK)
                    {
                        MI_Application_Close(&miApp);
                        MI_Instance_Delete(statusReport);
                        MI_Instance_Delete(errorObject);
                        MI_Instance_Delete(statusObject);
                        return r;
                    }
                }
                // Set NodeName
                if (configurationStatus->HostName.exists)
                {
                    value.string = (MI_Char*)configurationStatus->HostName.value;
                    r = MI_Instance_AddElement(statusReport, REPORTING_NODENAME, &value, MI_STRING, 0);
                    if (r != MI_RESULT_OK)
                    {
                        MI_Application_Close(&miApp);
                        MI_Instance_Delete(statusReport);
                        MI_Instance_Delete(errorObject);
                        MI_Instance_Delete(statusObject);
                        return r;
                    }
                }
                // Set Ipadress
                if (configurationStatus->IPV4Addresses.exists || configurationStatus->IPV6Addresses.exists)
                {
                    r = SetIpAddress(configurationStatus, statusReport);
                    if (r != MI_RESULT_OK)
                    {
                        MI_Application_Close(&miApp);
                        MI_Instance_Delete(statusReport);
                        MI_Instance_Delete(errorObject);
                        MI_Instance_Delete(statusObject);
                        return r;

                    }
                }
                // Set LCM version
                if (configurationStatus->LCMVersion.exists)
                {
                    value.string = (MI_Char*)configurationStatus->LCMVersion.value;
                    r = MI_Instance_AddElement(statusReport, REPORTING_LCMVERSION, &value, MI_STRING, 0);
                    if (r != MI_RESULT_OK)
                    {
                        MI_Application_Close(&miApp);
                        MI_Instance_Delete(statusReport);
                        MI_Instance_Delete(errorObject);
                        MI_Instance_Delete(statusObject);
                        return r;
                    }
                }
                // Set Configuration version
                if (lcmContext->documentMetaData.Version)
                {
                    value.string = (MI_Char*)lcmContext->documentMetaData.Version;
                    r = MI_Instance_AddElement(statusReport, REPORTING_CONFIGURATIONVERSION, &value, MI_STRING, 0);
                    if (r != MI_RESULT_OK)
                    {
                        MI_Application_Close(&miApp);
                        MI_Instance_Delete(statusReport);
                        MI_Instance_Delete(errorObject);
                        MI_Instance_Delete(statusObject);
                        return r;
                    }
                }
            }
            // Set Job Id
            if (configurationStatus->JobID.exists)
            {
                value.string = (MI_Char*)configurationStatus->JobID.value;
                // Already added above hence using SetElement
                r = MI_Instance_SetElement(statusReport, REPORTING_JOBID, &value, MI_STRING, 0);
                if (r != MI_RESULT_OK)
                {
                    MI_Application_Close(&miApp);
                    MI_Instance_Delete(statusReport);
                    MI_Instance_Delete(errorObject);
                    MI_Instance_Delete(statusObject);
                    return r;
                }
            }

        }
    }
*/

    // Send it along.

    r = Pull_SendStatusReport(lcmContext, (MI_Instance*)g_metaConfig, statusReport, isStatusReport, &getActionStatusCode, &extendedError);
    MI_Instance_Delete(statusReport);
    MI_Instance_Delete(errorObject);
    MI_Instance_Delete(statusObject);
    MI_Application_Close(&miApp);

    if (!g_bNotFirstTimeReport)
        g_bNotFirstTimeReport = MI_TRUE;

    return r;
}


MI_Result GetMetaConfig(
    _Outptr_ MSFT_DSCMetaConfiguration ** metaConfigInstance)
{
    MI_Result r;
    MI_Boolean bComplianceStatus;
    MI_Uint32 getActionStatusCode;
    MI_Uint32 lcmStatusCode;
    DSC_EventWriteMessageGettingMetaConfig();
    
    GetLatestStatus(&bComplianceStatus, &getActionStatusCode, &lcmStatusCode);
    
    r = UpdateMetaConfigWithLCMState(&lcmStatusCode, (MI_Instance *)g_metaConfig);
    if (r != MI_RESULT_OK)
    {
        return r;
    }
    
    r = UpdateMetaConfigWithAgentId(((RegistrationManager*)g_registrationManager)->agentId, (MI_Instance *)g_metaConfig);
    if (r != MI_RESULT_OK)
    {
        return r;
    }

    r = DSC_MSFT_DSCMetaConfiguration_Clone(g_metaConfig, metaConfigInstance);
    return r;
}

MI_Result SetMetaConfig(
    _In_ const MI_Instance * metaConfigInstance,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result r;
    ModuleManager *moduleManager = NULL;
    MSFT_DSCMetaConfiguration * tmpInstance;
    DSC_EventWriteMessageSettingMetaConfig();

    if (cimErrorDetails == NULL)
    {        
        return MI_RESULT_INVALID_PARAMETER; 
    }
    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once. 

    r = InitializeModuleManager(0, cimErrorDetails, &moduleManager);
    if (r != MI_RESULT_OK)
    {
        return r;
    }

    if (moduleManager == NULL)
    {
        return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_ENGINEHELPER_MEMORY_ERROR);         
    }    

    r = SerializeMetaConfig((MSFT_DSCMetaConfiguration*)metaConfigInstance, moduleManager, cimErrorDetails);

    moduleManager->ft->Close(moduleManager, NULL);
    if (r != MI_RESULT_OK)
    {
        return r;
    }

    r = DSC_MSFT_DSCMetaConfiguration_Clone((MSFT_DSCMetaConfiguration*)metaConfigInstance, &tmpInstance);
    if (r != MI_RESULT_OK)
    {
        GetCimMIError(MI_RESULT_INVALID_PARAMETER, cimErrorDetails, ID_LCMHELPER_METASCHEMA_CLONE_FAILED);
        return r;
    }

    r = UpdateDefaultValueForMetaConfig((MI_Instance*)tmpInstance);
    if (r != MI_RESULT_OK )
    {
        return r;
    }

    r = UpdateMetaConfigWithAgentId(((RegistrationManager*)g_registrationManager)->agentId, (MI_Instance*)tmpInstance);
    if (r != MI_RESULT_OK)
    {
        return r;
    }

    if (g_metaConfig != NULL)
    {
        MSFT_DSCMetaConfiguration_Delete(g_metaConfig);
        g_metaConfig = tmpInstance;
    }

    r = RegisterConsistencyTask((MI_Instance *)g_metaConfig, cimErrorDetails);
    if (r != MI_RESULT_OK)
    {
        return r;
    }    

    return r;
}

void LCM_FinishOperation(
    _In_ LCMProviderContext* lcmContext,
    _In_z_ const MI_Char *operationType,
    _In_opt_ MI_Instance* errorDetails,
    MI_Result result)
{
    MI_Uint32 errorCategory = 0;
    MI_Uint32 errorCode = 0;
    MI_ConstStringPtr errorMessage = NULL;
    MI_ConstStringPtr messageId = NULL;
    MI_ConstStringPtr errorType = NULL;

    if (errorDetails)
    {
        ExtractCimErrorFields(errorDetails, &result, &errorCategory, &errorCode, &errorMessage, &messageId, &errorType);
    }

    if (MI_RESULT_OK == result && errorDetails == NULL)
    {
//        DSC_EventWriteCUMethodResult(g_ConfigurationDetails.jobGuidString, operationType);
    }
    else
    {
//        DSC_EventWriteCUMethodError(g_ConfigurationDetails.jobGuidString, result, errorMessage, messageId, errorCategory, errorCode, errorType);
    }
    if (lcmContext)
    {
        if (lcmContext->bReportErrorsToServer && g_metaConfig && result != MI_RESULT_OK)
        {
            MI_Instance *cimError = errorDetails;
            if (errorDetails == NULL)
            {
                MI_Utilities_CimErrorFromErrorCode((MI_Uint32)result, MI_RESULT_TYPE_MI, NULL, &cimError);
            }
            ReportStatusToServer(lcmContext, errorMessage, REPORTING_TYPE_DSCENGINERESOURCE, REPORTING_TYPE_DSCENGINERESOURCE, errorCode, MI_FALSE, /*isStatusReport*/ 0, cimError);

            ReportStatusToServer(lcmContext, errorMessage, REPORTING_TYPE_DSCENGINERESOURCE, REPORTING_TYPE_DSCENGINERESOURCE, errorCode, MI_FALSE, /*isStatusReport*/ 0, cimError);

            if (errorDetails != cimError && cimError != NULL)
            {
                MI_Instance_Delete(cimError);
            }
        }
    }
    // Clean up CimStatusCodeDesription property as not needed by the client( it was used
    // as a mechanism to move JSON representation of the ErrorRecord (if any) for errors.
    if (errorDetails)
    {
        MI_Value value;
        MI_Uint32 flags;
        result = MI_Instance_GetElement(errorDetails, REPORTING_SERIALIZEDERROR, &value, NULL, &flags, NULL);
        if (result == MI_RESULT_OK && !(flags & MI_FLAG_NULL))
        {
            value.string = MI_T("");
            MI_Instance_SetElement(errorDetails, REPORTING_SERIALIZEDERROR, &value, MI_STRING, 0);
        }
    }
    return;
}


MI_Result UpdateDSCCacheInstance(
    _In_ MI_Application *miApp,
    _Inout_ MI_Instance **dscCacheInstance, 
    _In_opt_ MI_Boolean* complianceStatus,
    _In_opt_ MI_Uint32* getActionStatusCode,
        _In_opt_ MI_Uint32* lcmStatusCode,
    _In_opt_ MI_Char* registeredServerURLs,
    _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Result r = MI_RESULT_OK;
    MI_Value value;
    MI_Instance *cacheElement = NULL;
        //MI_Char *lcmStatusCodeHistory = NULL;

    if (extendedError == NULL)
    {        
        return MI_RESULT_INVALID_PARAMETER; 
    }

    *extendedError = NULL; // Explicitly set *extendedError to NULL as _Outptr_ requires setting this at least once.

    if (*dscCacheInstance)
    {
        if (complianceStatus)
        {
            value.boolean = *complianceStatus;
            r = MI_Instance_SetElement(*dscCacheInstance, DSC_InternalStateCache_ComplianceStatus, &value, MI_BOOLEAN, 0);
            if (r != MI_RESULT_OK)
            {
                return r;
            }
        }

        if (getActionStatusCode)
        {
            value.sint64 = *getActionStatusCode;
            r = MI_Instance_SetElement(*dscCacheInstance, DSC_InternalStateCache_GetActionStatusCode, &value, MI_SINT64, 0);
            if (r != MI_RESULT_OK)
            {
                return r;
            }
        }

                if (lcmStatusCode)
                {
                        value.sint64 = *lcmStatusCode;
                        r = MI_Instance_SetElement(*dscCacheInstance, DSC_InternalStateCache_LCMStatusCode, &value, MI_SINT64, 0);
                        if (r != MI_RESULT_OK)
                        {
                                return r;
                        }
                }

        if (registeredServerURLs)
        {
            value.string = registeredServerURLs;
            r = MI_Instance_SetElement(*dscCacheInstance, DSC_InternalStateCache_RegisteredServerURLs, &value, MI_STRING, 0);
            if (r != MI_RESULT_OK)
            {
                return r;
            }
        }
    }
    else
    {
        r = MI_Application_NewInstance(miApp, INTERNALSTATECACHE_CLASSNAME, NULL, &cacheElement);
        if (r != MI_RESULT_OK)
        {
            return r;
        }

        value.boolean = complianceStatus ? *complianceStatus : MI_FALSE;
        r = MI_Instance_AddElement(cacheElement, DSC_InternalStateCache_ComplianceStatus, &value, MI_BOOLEAN, 0 );
        if (r != MI_RESULT_OK)
        {
            MI_Instance_Delete(cacheElement);
            return r;
        }

        value.sint64 = getActionStatusCode ? *getActionStatusCode : GET_ACTION_STATUS_CODE_SUCCESS;
        r = MI_Instance_AddElement(cacheElement, DSC_InternalStateCache_GetActionStatusCode, &value, MI_SINT64, 0);
        if (r != MI_RESULT_OK)
        {
            MI_Instance_Delete(cacheElement);
            return r;
        }

                value.sint64 = lcmStatusCode ? *lcmStatusCode : LCM_STATUSCODE_READY;
                r = MI_Instance_AddElement(cacheElement, DSC_InternalStateCache_LCMStatusCode, &value, MI_SINT64, 0);
                if (r != MI_RESULT_OK)
                {
                        MI_Instance_Delete(cacheElement);
                        return r;
                }

        value.string = registeredServerURLs ? registeredServerURLs : MI_T("");
        r = MI_Instance_AddElement(cacheElement, DSC_InternalStateCache_RegisteredServerURLs, &value, MI_STRING, 0);
        if (r != MI_RESULT_OK)
        {
            MI_Instance_Delete(cacheElement);
            return r;
        }

        *dscCacheInstance = cacheElement;
    }

    return MI_RESULT_OK;
}

MI_Result InitDSCInternalCache(
    _In_ ModuleManager *moduleManager,
    _Outptr_result_maybenull_ MI_Instance **DSCInternalCache,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result r = MI_RESULT_OK;
    MI_Char *fileExpandedPath = NULL;
    ModuleLoaderObject *moduleLoader = NULL;

    if (cimErrorDetails == NULL || DSCInternalCache == NULL)
    {        
        return MI_RESULT_INVALID_PARAMETER; 
    }

    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once. 
    *DSCInternalCache = NULL;

    moduleLoader = (ModuleLoaderObject*)moduleManager->reserved2;
    
    // reset to default values.
    r = UpdateDSCCacheInstance(moduleLoader->application, DSCInternalCache, NULL, NULL, NULL, NULL, cimErrorDetails);
    if (r != MI_RESULT_OK)
    {
    return r;
    }

    return MI_RESULT_OK;
}

MI_Result InitCacheAndMetaConfig(
    _Outptr_result_maybenull_ MSFT_DSCMetaConfiguration **metaConfig,
    _Outptr_result_maybenull_ MI_Instance **DSCInternalCache,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result r = MI_RESULT_OK;
    ModuleManager *moduleManager = NULL;
    r = InitializeModuleManager(0, cimErrorDetails, &moduleManager);
    if (r != MI_RESULT_OK)
    {
        return r;
    }    

    r = LoadModuleManager(moduleManager, cimErrorDetails);
    if (r != MI_RESULT_OK)
    {
        moduleManager->ft->Close(moduleManager, NULL);
        return r;
    }

    r = InitMetaConfig(moduleManager, metaConfig, cimErrorDetails);
    if (r != MI_RESULT_OK)
    {
        moduleManager->ft->Close(moduleManager, NULL);
        return r;
    }

    r = InitDSCInternalCache(moduleManager, DSCInternalCache, cimErrorDetails);
    moduleManager->ft->Close(moduleManager, NULL);    
    if (r != MI_RESULT_OK)
    {
        if (*metaConfig != NULL)
        {
            MSFT_DSCMetaConfiguration_Delete(*metaConfig);
            *metaConfig = NULL;
        }

        return r;
    }

        UpdateLCMStatusCodeHistory(&g_DSCInternalCache, &g_LCMStatusCodeHistory);

    return MI_RESULT_OK;
}

MI_Result InitMetaConfig(
    _In_ ModuleManager *moduleManager,
    _Outptr_result_maybenull_ MSFT_DSCMetaConfiguration **metaConfig,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result r = MI_RESULT_OK;
    MSFT_DSCMetaConfiguration *tempConfig;
    ModuleLoaderObject *moduleLoader = NULL;

    if (cimErrorDetails == NULL)
    {        
        return MI_RESULT_INVALID_PARAMETER; 
    }
    moduleLoader = (ModuleLoaderObject*)moduleManager->reserved2;
    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once. 

    /* Before we do anything zap the extended error if it is being asked for */
    if (metaConfig)
    {
        *metaConfig = NULL;
    }

    /* initialize by deserialization and clone if mof instance exist */
    r = DeseralizeMetaConfig(&tempConfig, moduleManager, cimErrorDetails);
    if (r != MI_RESULT_OK )
    {
        return r;
    }

    if (tempConfig == NULL)
    {
        /* if meta config mof instance doesn't exist, load default*/
        r = DSC_MI_Application_NewInstance(moduleLoader->application, NULL , &MSFT_DSCMetaConfiguration_rtti, (MI_Instance**) &tempConfig);        
        if (r != MI_RESULT_OK )
        {
            return GetCimMIError(r, cimErrorDetails,ID_LCMHELPER_META_MOF_FAILED);
        }
    }

    r = UpdateDefaultValueForMetaConfig((MI_Instance*)tempConfig);
        if (r != MI_RESULT_OK )
    {
        return r;
    }

    *metaConfig = tempConfig;

    /* apply the meta config : 
    1: create boot time task 
    2: create refresh task, 
    3: create download task if pull, 
    4: cache credential, this can be just use the typed instance*/
    return r;
}


/*Returns constant memory for partialconfigurations held by meta config instance*/
MI_Result GetPartialConfigurations(_In_ MI_Instance *metaInstance, _Inout_ MI_InstanceA *partialConfigurations,
        _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
        MI_Value value;
        MI_Result r;
        MI_Uint32 flags;
        if (cimErrorDetails == NULL)
        {
                return MI_RESULT_INVALID_PARAMETER;
        }
        *cimErrorDetails = NULL;
        memset(partialConfigurations, 0, sizeof(MI_InstanceA));

        r = DSC_MI_Instance_GetElement(metaInstance, MSFT_DSCMetaConfiguration_PartialConfigurations, &value, NULL, &flags, NULL);
        if (r != MI_RESULT_OK)
        {
                return r = GetCimMIError2Params(r, cimErrorDetails, ID_ENGINEHELPER_GET_PROPERTY_X_FROM_Y_FAILED, MSFT_DSCMetaConfiguration_PartialConfigurations, METACONF_RESOURCE_CLASSNAME);;
        }
        if (!(flags & MI_FLAG_NULL))
        {
                partialConfigurations->data = value.instancea.data;
                partialConfigurations->size = value.instancea.size;
        }
        return MI_RESULT_OK;
}
/*Returns constant memory for name held by meta config instance*/
MI_Result GetPartialConfigurationName(_In_ MI_Instance *partialConfiguration,
        _Outptr_result_maybenull_z_ const MI_Char** name,
        _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
        MI_Value value;
        MI_Result r;
        MI_Uint32 flags;
        if (cimErrorDetails == NULL)
        {
                return MI_RESULT_INVALID_PARAMETER;
        }
        *cimErrorDetails = NULL;
        *name = NULL;
        if (partialConfiguration == NULL)
                return MI_RESULT_OK;

        r = DSC_MI_Instance_GetElement(partialConfiguration, OMI_MetaConfigurationResource_ResourceId, &value, NULL, &flags, NULL);
        if (r != MI_RESULT_OK)
        {
                return r = GetCimMIError2Params(r, cimErrorDetails, ID_ENGINEHELPER_GET_PROPERTY_X_FROM_Y_FAILED, OMI_MetaConfigurationResource_ResourceId, METACONF_RESOURCE_CLASSNAME);;
        }
        if (!(flags & MI_FLAG_NULL))
        {
            // ResourceID is in format [[[[]]]]ActualId, so find last occurance of ]. Name can't contain special characters.
            *name = value.string;
        }
        return MI_RESULT_OK;
}

MI_Result UpdateDefaultValueForMetaConfig(
    _Inout_ MI_Instance* self)
{
    MI_Result r = MI_RESULT_OK;
    MI_Value value;
    MI_Uint32 flags;   
    MI_Uint32 factor = 0;
    MI_Value refreshFrequencyMins;
    MI_Value configurationModeFrequencyMins;

        overWroteUserSpecifiedRefreshFreqMins=NULL; //Reset this to Null since this function is called by both InitMetaConfig , and SetMetaConfig. 
        overWroteUserSpecifiedConfModeFreqMins = NULL; //Reset this to Null since this function is called by both InitMetaConfig , and SetMetaConfig. 
    r = DSC_MI_Instance_GetElement((MI_Instance*)self, MSFT_DSCMetaConfiguration_RefreshFrequencyMins, &value, NULL, &flags, NULL);
    if (r != MI_RESULT_OK)
    {
        return r;
    }
    else if (flags & MI_FLAG_NULL || value.uint32 < DEFAULT_MinRefreshFrequencyMins || value.uint32 > DEFAULT_MaxRefreshFrequencyMins)
    {
        if(value.uint32 < DEFAULT_MinRefreshFrequencyMins)
        {
            value.uint32 = DEFAULT_MinRefreshFrequencyMins;
                        overWroteUserSpecifiedRefreshFreqMins = DEFAULT_MinRefreshFrequencyMinsString;
             r = MI_Instance_SetElement((MI_Instance*)self, MSFT_DSCMetaConfiguration_RefreshFrequencyMins, &value, MI_UINT32, 0);
             if (r != MI_RESULT_OK)
          {
                 return r;
             }
        }
         else
         {
             value.uint32 = DEFAULT_MaxRefreshFrequencyMins;
                         overWroteUserSpecifiedRefreshFreqMins = DEFAULT_MaxRefreshFrequencyMinsString;
             r = MI_Instance_SetElement((MI_Instance*)self, MSFT_DSCMetaConfiguration_RefreshFrequencyMins, &value, MI_UINT32, 0);
             if(r != MI_RESULT_OK)
             {
              return r;
             }
        }
    }

    refreshFrequencyMins = value;

    r = DSC_MI_Instance_GetElement((MI_Instance*)self, MSFT_DSCMetaConfiguration_ConfigurationModeFrequencyMins, &value, NULL, &flags, NULL);
    if (r != MI_RESULT_OK)
    {
        return r;
    }
    else if (flags & MI_FLAG_NULL || value.uint32 < DEFAULT_ConfigurationModeFrequencyMins)
    {
        value.uint32 = DEFAULT_ConfigurationModeFrequencyMins;
                overWroteUserSpecifiedConfModeFreqMins = DEFAULT_ConfigurationModueFrequencyMinsString;
        r = MI_Instance_SetElement((MI_Instance*)self, MSFT_DSCMetaConfiguration_ConfigurationModeFrequencyMins, &value, MI_UINT32, 0);
        if (r != MI_RESULT_OK)
        {
            return r;
        }
    }

    configurationModeFrequencyMins = value;

    // refresh should be a whole number factor of configuration
    factor = configurationModeFrequencyMins.uint32 % refreshFrequencyMins.uint32;
    if(factor != 0)
    {
                overWroteUserSpecifiedConfModeFreqMins = DEFAULT_ConfigurationModueFrequencyMinsString;
                //There are two cases: 1.) configurationModeFrequencyMins > refreshFrequencyMins. In this case, the factor obtained by dividing the two will be greater than 1
    //                 2.) configurationModeFrequencyMins < refreshFrequencyMins. In this case the factor will be 0 and so we should set it to refreshFrequencyMins
    factor = (MI_Uint32)(configurationModeFrequencyMins.uint32 / refreshFrequencyMins.uint32);
        if(factor > 0)
    {
        configurationModeFrequencyMins.uint32 = refreshFrequencyMins.uint32 * (factor + 1);
    }
    else
    {
        configurationModeFrequencyMins.uint32 = refreshFrequencyMins.uint32;
    }

        r = MI_Instance_SetElement((MI_Instance*)self, MSFT_DSCMetaConfiguration_ConfigurationModeFrequencyMins, 
            &configurationModeFrequencyMins, MI_UINT32, 0);
        if (r != MI_RESULT_OK)
        {
            return r;
        }
    }

    r = DSC_MI_Instance_GetElement((MI_Instance*)self, MSFT_DSCMetaConfiguration_ConfigurationMode, &value, NULL, &flags, NULL);
    if (r != MI_RESULT_OK)
    {
        return r;
    }
    else if (flags & MI_FLAG_NULL)
    {
        value.string = DEFAULT_ConfigurationMode;
        r = MI_Instance_SetElement((MI_Instance*)self, MSFT_DSCMetaConfiguration_ConfigurationMode, &value, MI_STRING, 0);
        if (r != MI_RESULT_OK)
        {
            return r;
        }
    }

    r = DSC_MI_Instance_GetElement((MI_Instance*)self, MSFT_DSCMetaConfiguration_RebootNodeIfNeeded, &value, NULL, &flags, NULL);
    if (r != MI_RESULT_OK)
    {
        return r;
    }
    else if (flags & MI_FLAG_NULL)
    {
        value.boolean = DEFAULT_RebootNodeIfNeeded;
        r = MI_Instance_SetElement((MI_Instance*)self, MSFT_DSCMetaConfiguration_RebootNodeIfNeeded, &value, MI_BOOLEAN, 0);
        if (r != MI_RESULT_OK)
        {
            return r;
        }
    }

    r = DSC_MI_Instance_GetElement((MI_Instance*)self, MSFT_DSCMetaConfiguration_RefreshMode, &value, NULL, &flags, NULL);
    if (r != MI_RESULT_OK)
    {
        return r;
    }
    else if (flags & MI_FLAG_NULL)
    {
        value.string = DEFAULT_RefreshMode;
        r = MI_Instance_SetElement((MI_Instance*)self, MSFT_DSCMetaConfiguration_RefreshMode, &value, MI_STRING, 0);
        if (r != MI_RESULT_OK)
        {
            return r;
        }
    }

    r = DSC_MI_Instance_GetElement((MI_Instance*)self, MSFT_DSCMetaConfiguration_AllowModuleOverwrite, &value, NULL, &flags, NULL);
    if (r != MI_RESULT_OK)
    {
        return r;
    }
    else if (flags & MI_FLAG_NULL)
    {
        value.boolean = DEFAULT_AllowModuleOverwrite;
        r = MI_Instance_SetElement((MI_Instance*)self, MSFT_DSCMetaConfiguration_AllowModuleOverwrite, &value, MI_BOOLEAN, 0);
        if (r != MI_RESULT_OK)
        {
            return r;
        }
    }   

    return MI_RESULT_OK;
}

MI_Result SerializeMetaConfig(
    _In_ const MSFT_DSCMetaConfiguration *metaConfig,
    _In_ ModuleManager *moduleManager,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    ModuleLoaderObject *moduleLoader = NULL;
    MI_Application * miApplication = NULL;
    MI_Serializer serializer = {0};
    MI_Result r = MI_RESULT_OK;
    MI_ClassA metaSchema = {0};
    wchar_t *serializerBuffer;
    MI_Uint32 bufferUsed = 0;
    MI_Uint32 bufferSize = METACONFIG_MAX_BUFFER_SIZE;

    if (cimErrorDetails == NULL)
    {        
        return MI_RESULT_INVALID_PARAMETER; 
    }
    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once. 

    r = LoadModuleManager(moduleManager, cimErrorDetails);
    if (r != MI_RESULT_OK)
    {
        return r;
    }    

    moduleLoader = (ModuleLoaderObject*)moduleManager->reserved2;
    if (moduleLoader == NULL || NitsShouldFault(NitsHere(), NitsAutomatic))
    {
        return GetCimMIError(MI_RESULT_INVALID_CLASS, cimErrorDetails, ID_LCMHELPER_MODLOADNULL_FAILED);
    }

    miApplication = moduleLoader->application;
    r  = GetMetaConfigSchema(miApplication, moduleLoader->deserializer, moduleLoader->options, &metaSchema, cimErrorDetails);
    if (r != MI_RESULT_OK)
    {
        return r;
    }

    if (metaSchema.data == NULL || metaSchema.data[ metaSchema.size -1]->classDecl == NULL || NitsShouldFault(NitsHere(), NitsAutomatic))
    {
        return GetCimMIError(MI_RESULT_INVALID_PARAMETER, cimErrorDetails, ID_LCMHELPER_METASCHEMA_LOADNULL_FAILED);
    }
    if (Tcscasecmp(metaSchema.data[metaSchema.size -1]->classDecl->name, METACONFIG_CLASSNAME) != 0  || NitsShouldFault(NitsHere(), NitsAutomatic))
    {
        //delete metaSchema classA
        return GetCimMIError(MI_RESULT_INVALID_CLASS, cimErrorDetails, ID_LCMHELPER_METASCHEMA_NAMEMISMATCH_FAILED);
    }

    CleanUpClassCache(&metaSchema);
    r = MI_Application_NewSerializer_Mof(miApplication, 0, MOFCODEC_FORMAT, &serializer);
    if (r!=MI_RESULT_OK || NitsShouldFault(NitsHere(), NitsAutomatic))
    {
        return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_LCMHELPER_METASCHEMA_CREATESERIALIZE_FAILED);
    }

    serializerBuffer = (wchar_t*) DSC_malloc(bufferSize, NitsHere());
    if (serializerBuffer == NULL)
    {
        return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_LCMHELPER_MEMORY_ERROR);
    }

    r = MI_Serializer_SerializeInstance(&serializer, 0, &metaConfig->__instance, (MI_Uint8*) serializerBuffer, bufferSize , &bufferUsed);
    if (r != MI_RESULT_OK && bufferUsed > 0 )
    {
        //We need to reallocate the buffer.
        DSC_free(serializerBuffer);
        serializerBuffer = (wchar_t*) DSC_malloc(bufferUsed, NitsHere());
        if (serializerBuffer == NULL)
        {
            MI_Serializer_Close(&serializer);
            return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_LCMHELPER_MEMORY_ERROR);
        }  
        bufferSize = bufferUsed ;
        bufferUsed = 0;
        r = MI_Serializer_SerializeInstance(&serializer, 0, &metaConfig->__instance, (MI_Uint8*) serializerBuffer, bufferSize , &bufferUsed);
    }

    MI_Serializer_Close(&serializer);
    if (r != MI_RESULT_OK || NitsShouldFault(NitsHere(), NitsAutomatic))
    {
        DSC_free(serializerBuffer);
        return GetCimMIError(MI_RESULT_INVALID_CLASS, cimErrorDetails, ID_LCMHELPER_METASCHEMA_SERIALIZE_FAILED);
    }

    r = SaveMetaConfig((MI_Uint8*) serializerBuffer, bufferUsed, cimErrorDetails);
    if (r != MI_RESULT_OK)
    {
        DSC_free(serializerBuffer);
        return r;
    }

    DSC_free(serializerBuffer);
    return MI_RESULT_OK;
}

MI_Result SaveMetaConfig(
    _In_reads_bytes_(dataSize) const MI_Uint8* ConfigData,
    MI_Uint32 dataSize,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result result;
    MI_Result resultInternal;
    int fResult;

    if (cimErrorDetails == NULL)
    {        
        return MI_RESULT_INVALID_PARAMETER; 
    }

    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once. 
    if (File_ExistT(g_MetaConfigFileName) != -1 || NitsShouldFault(NitsHere(), NitsAutomatic))
    {
        result = CopyConfigurationFile(CONFIGURATION_LOCATION_METACONFIG, CONFIGURATION_LOCATION_METACONFIG_BACKUP, MI_FALSE, cimErrorDetails);
        if (result != MI_RESULT_OK)
        {
            return result;
        }

        fResult = File_RemoveT(g_MetaConfigFileName);
        if (fResult != 0|| NitsShouldFault(NitsHere(), NitsAutomatic) )
        {

            return GetCimWin32Error(MI_RESULT_FAILED, cimErrorDetails, ID_LCMHELPER_METASCHEMA_DELETE_FAILED);
        }  
    }

    result = SaveFile(g_MetaConfigFileName, (MI_Uint8*)ConfigData, dataSize, cimErrorDetails);
    if (result != MI_RESULT_OK)
    {
        resultInternal=CopyConfigurationFile(CONFIGURATION_LOCATION_METACONFIG_BACKUP, CONFIGURATION_LOCATION_METACONFIG, MI_FALSE, cimErrorDetails);
        if (resultInternal != MI_RESULT_OK)
        {
            DSC_EventWriteCopyConfigurationFailed(CONFIGURATION_LOCATION_METACONFIG_BACKUP,CONFIGURATION_LOCATION_METACONFIG);
        }

        return result;
    }

    return result;
}

MI_Result DeseralizeMetaConfig(
    _Outptr_result_maybenull_ MSFT_DSCMetaConfiguration **metaConfig,
    _In_ ModuleManager *moduleManager,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    ModuleLoaderObject *moduleLoader = NULL;
    MI_Application * miApplication = NULL;
    MI_Result r = MI_RESULT_OK;
    MI_ClassA metaSchema = {0};
    MI_Uint8 *metaConfigBuffer = NULL;
    MI_Uint32 bufferSize = 0;
    MI_Uint32 deserializeSize;
    MI_Instance *newInstance = NULL;

    if (cimErrorDetails == NULL)
    {        
        return MI_RESULT_INVALID_PARAMETER; 
    }
    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once. 

    if (metaConfig)
    {
        *metaConfig = NULL;
    }

    if (File_ExistT(g_MetaConfigFileName) == -1)
    {
        return MI_RESULT_OK;
    }

    moduleLoader = (ModuleLoaderObject*)moduleManager->reserved2;
    if (moduleLoader == NULL)
    {
        return GetCimMIError(MI_RESULT_INVALID_CLASS, cimErrorDetails, ID_LCMHELPER_MODLOADNULL_FAILED);
    }

    miApplication = moduleLoader->application;

    r  = GetMetaConfigSchema(miApplication, moduleLoader->deserializer, moduleLoader->options, &metaSchema, cimErrorDetails);
    if (r != MI_RESULT_OK)
    {
        return r;
    }

    if (metaSchema.data == NULL || metaSchema.data[ metaSchema.size -1]->classDecl == NULL)
    {
        return GetCimMIError(MI_RESULT_INVALID_PARAMETER, cimErrorDetails, ID_LCMHELPER_METASCHEMA_NAMEMISMATCH_FAILED);
    }

    if (Tcscasecmp(metaSchema.data[ metaSchema.size -1]->classDecl->name, METACONFIG_CLASSNAME) != 0 )
    {
        return GetCimMIError(MI_RESULT_INVALID_CLASS, cimErrorDetails, ID_LCMHELPER_METASCHEMA_NAMEMISMATCH_FAILED);
    }

    /* read meta config mof file into buffer and with size*/
    r = ReadFileContent(g_MetaConfigFileName, &metaConfigBuffer, &bufferSize, cimErrorDetails);
    if (r != MI_RESULT_OK)
    {
        CleanUpClassCache(&metaSchema);
        return MI_RESULT_FAILED;
    }

    r = MI_Deserializer_DeserializeInstance(moduleLoader->deserializer, 0, metaConfigBuffer, bufferSize, &metaSchema.data[0], metaSchema.size, NULL, NULL, &deserializeSize, &newInstance, cimErrorDetails);
    CleanUpClassCache(&metaSchema);

    if (r != MI_RESULT_OK)
    {
        DSC_free(metaConfigBuffer);
        AppendWMIError1ParamID(*cimErrorDetails, ID_LCM_FAILED_TO_GET_METACONFIGURATION);
        return r;
    }

    DSC_free(metaConfigBuffer);
    r = DSC_MSFT_DSCMetaConfiguration_Clone((MSFT_DSCMetaConfiguration*)newInstance, metaConfig);
    MI_Instance_Delete(newInstance);
    if (r != MI_RESULT_OK)
    {
        return GetCimMIError(MI_RESULT_INVALID_PARAMETER, cimErrorDetails, ID_LCMHELPER_METASCHEMA_CLONE_FAILED);
    }

    return MI_RESULT_OK;
}

MI_Result TaskHelper(
    _In_z_ MI_Char* taskPath,
    _In_z_ MI_Char* taskName,
    _In_z_ const MI_Char* methodName,
    _In_ ModuleManager *moduleManager,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    ModuleLoaderObject *moduleLoader = NULL;
    MI_Application * miApplication = NULL;
    MI_Instance *parameter = NULL;
    MI_Session session = MI_SESSION_NULL;
    MI_Operation operation = MI_OPERATION_NULL;
    MI_OperationCallbacks callbacks = MI_OPERATIONCALLBACKS_NULL;
    MI_Value pathValue;
    MI_Value nameValue;
    MI_Result r = MI_RESULT_OK;
    const MI_Instance *result;
    MI_Boolean moreResults;
    MI_Result resultCode = MI_RESULT_INVALID_PARAMETER;
    const MI_Char *errorMessage; 

    if (cimErrorDetails == NULL)
    {        
        return MI_RESULT_INVALID_PARAMETER; 
    }
    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once. 

    moduleLoader = (ModuleLoaderObject*)moduleManager->reserved2;
    if (moduleLoader == NULL)
    {
        return GetCimMIError(MI_RESULT_INVALID_CLASS, cimErrorDetails, ID_LCMHELPER_MODLOADNULL_FAILED);
    }

    miApplication = moduleLoader->application;
    r = DSC_MI_Application_NewInstance(miApplication, MI_T("__Parameter"), NULL, &parameter);
    if (r != MI_RESULT_OK)
    {
        return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_LCMHELPER_TASK_CREATEPARAM_FAILED);
    }

    pathValue.string = taskPath;
    r = DSC_MI_Instance_AddElement(parameter, TASKSCHEDULE_PROVDER_PATH_NAME, &pathValue, MI_STRING, 0);
    if (r != MI_RESULT_OK)
    {
        return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_LCMHELPER_TASK_ADDPARAM_FAILED);
    }

    nameValue.string = taskName;
    r = DSC_MI_Instance_AddElement(parameter, TASKSCHEDULE_PROVDER_NAME_NAME, &nameValue, MI_STRING, 0);
    if (r != MI_RESULT_OK)
    {
        return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_LCMHELPER_TASK_ADDPARAM_FAILED);
    }

    r = DSC_MI_Application_NewSession(miApplication, NULL, NULL, NULL, NULL, NULL, &session);
    if (r != MI_RESULT_OK)
    {
        return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_LCMHELPER_TASK_CREATESESSION_FAILED);
    }

    MI_Session_Invoke(&session, 0, 0, TASKSCHEDULE_PROVDER_NAMESPACE, TASKSCHEDULE_PROVDER_NAME, methodName, NULL, parameter, &callbacks, &operation);
    MI_Operation_GetInstance(&operation, &result, &moreResults, &resultCode, &errorMessage, (const MI_Instance **)cimErrorDetails);
    if (resultCode != MI_RESULT_OK)
    {
        //write error message
        r = MI_RESULT_FAILED;
    }

    MI_Operation_Close(&operation);
    MI_Session_Close(&session, NULL, NULL);
    return r;
}

MI_Uint32 GetCallSetConfigurationFlags(_In_opt_ MI_Context* context)
{
    MI_Uint32 flags = LCM_SETFLAGS_DEFAULT;
    MI_Boolean whatifFlag=MI_FALSE;
    MI_Type type1;
    MI_Type type2;
    MI_Value value1;
    MI_Value value2;
    MI_Result result;
    if (context)
    {
        result = MI_Context_GetCustomOption(context,DSCWHATIFENABLED, &type1, &value1);
        if (result == MI_RESULT_OK && type1 == MI_BOOLEAN && value1.boolean == MI_TRUE)
        {
            whatifFlag =MI_TRUE;
        }
        
       result= MI_Context_GetCustomOptionAt(context,DSC_INDEX_OPTION_SHOULDPROCESS,NULL,&type2,&value2);
       if (result==MI_RESULT_OK  && type2==MI_SINT32)
       {
            // if whatif or shouldprocess is used.  
            if (!value2.sint32)
            {
                whatifFlag=MI_TRUE;
            }        
       }
       if(whatifFlag)
       {
           flags |= LCM_SETFLAGS_ENABLEWHATIF;
       }
    }
    return flags;
}



MI_Result CopyConfigurationFileFromTemp(
    _In_z_ const MI_Char *locationFrom,
    _In_z_ const MI_Char *locationTo,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result result = MI_RESULT_OK;

    if (cimErrorDetails == NULL)
    {        
        return MI_RESULT_INVALID_PARAMETER; 
    }

    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once.
    *cimErrorDetails = NULL;     

    if (File_ExistT(locationFrom) != -1)
    {
        if (File_CopyT(locationFrom, locationTo) != 0)
        {
            return GetCimMIError2Params(MI_RESULT_FAILED, cimErrorDetails, ID_LCMHELPER_COPY_FAILED, locationFrom, locationTo);
        }
    }
    else
    {
        return GetCimMIError2Params(MI_RESULT_FAILED, cimErrorDetails, ID_LCMHELPER_COPY_FAILED, locationFrom, locationTo);
    }

    return result;
}


MI_Result CopyConfigurationChecksum(
        _In_z_ const MI_Char *mofFilePath,
        _In_z_ const MI_Char *locationTo,
        _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
        MI_Char checksumPath[MAX_PATH];
        size_t result;
        result = Stprintf(checksumPath, MAX_PATH, MI_T("%T%T"), mofFilePath, CHECKSUM_EXTENSION);
        if (result == -1)
        {
                return GetCimMIError(MI_RESULT_FAILED, cimErrorDetails, ID_LCMHELPER_PRINTF_ERROR);
        }

        return CopyConfigurationFileFromTemp(checksumPath, locationTo, cimErrorDetails);
}

/* It is assumed that */
MI_Result UpdateMetaConfigForWebDownloadManager(
    _In_z_ MI_Char *certificateThumbprint,
    _Inout_ MI_Instance *metaConfigInstance, 
    _Inout_ MI_InstanceA *customData,
    _Inout_ MI_Boolean *bMetaConfigUpdated,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result result = MI_RESULT_OK;
    MI_Uint32 xCount;    
    MI_Uint32 flags; 
    MI_Value certificateValue;  
    MI_Instance **newCustomData;
    MI_Instance *certificateIDInstance ;
    MI_Sint32 serverURLIndex = -1;
    MI_Value value;

    if (cimErrorDetails)
    {
        *cimErrorDetails = NULL;
    }

    for(xCount = 0 ; xCount < customData->size; xCount++)
    {
        result = MI_Instance_GetElement(customData->data[xCount], MSFT_KEYVALUEPAIR_CLASS_KEY, &certificateValue, NULL, &flags, NULL);
        if (result == MI_RESULT_OK && !(flags & MI_FLAG_NULL) && Tcscasecmp(certificateValue.string, MSFT_DSCPULL_CertificateID) == 0 )
        {
            //found it. 
            break;
        }
        else if (result == MI_RESULT_OK && !(flags & MI_FLAG_NULL) && Tcscasecmp(certificateValue.string, MSFT_DSCPULL_SERVERURL) == 0 )
        {
            serverURLIndex = (MI_Sint32) xCount;
        }
    }

    result = MI_RESULT_OK;
    //If not found, add it.
    if (xCount == customData->size && serverURLIndex >= 0)
    {           
        result = MI_Instance_Clone(customData->data[serverURLIndex], &certificateIDInstance);
        if (result != MI_RESULT_OK)
        {
            return result;
        }

        // Add the element here.
        value.string = MSFT_DSCPULL_CertificateID;
        result = MI_Instance_SetElement(certificateIDInstance, MSFT_KEYVALUEPAIR_CLASS_KEY, &value, MI_STRING, 0);
        if (result != MI_RESULT_OK)
        {
            MI_Instance_Delete(certificateIDInstance);          
            return result;
        }

        value.string = certificateThumbprint;
        result = MI_Instance_SetElement(certificateIDInstance, MSFT_KEYVALUEPAIR_CLASS_VALUE, &value, MI_STRING, 0);        
        if (result != MI_RESULT_OK)
        {
            MI_Instance_Delete(certificateIDInstance);              
            return result;
        }

        //Update InstanceArray.
        newCustomData = (MI_Instance **)DSC_malloc( sizeof(MI_Instance*) * (customData->size +1), TLINE);
        if (result != MI_RESULT_OK)
        {
            MI_Instance_Delete(certificateIDInstance);             
            return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_LCMHELPER_MEMORY_ERROR);
        }

        // copy existing customData
        memcpy(newCustomData, customData->data, sizeof(MI_Instance*) * customData->size );
        newCustomData[customData->size] = certificateIDInstance;
        value.instancea.data = newCustomData;
        value.instancea.size = customData->size + 1;
        result = MI_Instance_SetElement(metaConfigInstance, MSFT_DSCMetaConfiguration_DownloadManagerCustomData, &value, MI_INSTANCEA, 0);
        if (result != MI_RESULT_OK)
        {
            return result;
        }

        *bMetaConfigUpdated = MI_TRUE;
    }   

    return MI_RESULT_OK;
}

MI_Result UpdateMetaConfigWithLCMState(
        _In_z_ MI_Uint32 *lcmStatusCode,
    _Inout_ MI_Instance *metaConfigInstance)
{
        MI_Result r = MI_RESULT_OK;
    MI_Value value;
        // set default value
        value.string = LCM_STATUS_READY;

        if (*lcmStatusCode == LCM_STATUSCODE_READY)
        {
                value.string = LCM_STATUS_READY;
        }
        if (*lcmStatusCode == LCM_STATUSCODE_BUSY)
        {
                value.string = LCM_STATUS_BUSY;
        }
        if (*lcmStatusCode == LCM_STATUSCODE_REBOOT)
        {
                value.string = LCM_STATUS_REBOOT;
        }
        // extended status should be added here ...
        
        r = MI_Instance_SetElement(metaConfigInstance, MSFT_DSCMetaConfiguration_LocalConfigurationManagerState, &value, MI_STRING, 0);
    if (r != MI_RESULT_OK)
    {
        return r;
    }

        return MI_RESULT_OK;
}

MI_Result UpdateMetaConfigWithCertificateThumbprint(
    _In_z_ MI_Char *certificateThumbprint,
    _Inout_ MI_Instance *metaConfigInstance,
    _Out_ MI_Boolean *bMetaConfigUpdated,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result result = MI_RESULT_OK;
    MI_Value value;
    MI_Uint32 flags; 
    if (cimErrorDetails)
    {
        *cimErrorDetails = NULL;
    }

    *bMetaConfigUpdated = MI_FALSE;
    //update metaconfiginstance if needed.
    // Check if CertificateID is specified in meta config
    result = MI_Instance_GetElement(metaConfigInstance, MSFT_DSCMetaConfiguration_CertificateID, &value, NULL, &flags, NULL);
    if (result != MI_RESULT_OK)
    {
        return result;
    }

    if ( (flags & MI_FLAG_NULL) || value.string == NULL )
    {
        // we need to update metaconfig Instance
        value.string = certificateThumbprint;
        result = MI_Instance_SetElement(metaConfigInstance, MSFT_DSCMetaConfiguration_CertificateID, &value, MI_STRING, 0);
        if (result != MI_RESULT_OK)
        {
            return result;
        }    
        *bMetaConfigUpdated = MI_TRUE;
    }

    //Web download manager also needs it. update it if needed.
    // 1. Get the downloadManagerName 
    result = MI_Instance_GetElement(metaConfigInstance, MSFT_DSCMetaConfiguration_DownloadManagerName, &value, NULL, &flags, NULL);
    if (result != MI_RESULT_OK)
    {
        return result;
    }

    if ( !((flags & MI_FLAG_NULL) || value.string == NULL ))
    {
        // 2. compare it with webdownloadmanager
        if (Tcscasecmp(DSC_PULL_DOWNLOADMANAGER_WEB, value.string) == 0 )
        {
            // get custom data object.
            result = MI_Instance_GetElement(metaConfigInstance, MSFT_DSCMetaConfiguration_DownloadManagerCustomData, &value, NULL, &flags, NULL);
            if (result == MI_RESULT_OK && !( (flags & MI_FLAG_NULL) && value.instancea.size == 0))
            {
                result = UpdateMetaConfigForWebDownloadManager(certificateThumbprint, metaConfigInstance, &value.instancea, bMetaConfigUpdated, cimErrorDetails);
                if (result != MI_RESULT_OK)
                {
                    return result;
                }                  
            }
        }     
    }

    return MI_RESULT_OK;
}

MI_Boolean IsGetConfigurationFirstTime()
{
    return File_ExistT(GetCurrentConfigFileName()) == -1 ? MI_TRUE:MI_FALSE;
}

MI_Result UpdateCurrentStatus(
    _In_opt_ MI_Boolean *complianceStatus,
    _In_opt_ MI_Uint32 *getActionStatusCode,
        _In_opt_ MI_Uint32 *lcmStatusCode,
    _In_opt_ MI_Char* registeredServerURLs,
    _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Application miApp = MI_APPLICATION_NULL;
    MI_Result r = MI_RESULT_OK;
    MI_Char *serializerBuffer = NULL;
    MI_Uint32 bufferUsed = 0;
    MI_Uint32 bufferSize = METACONFIG_MAX_BUFFER_SIZE;
    MI_Serializer serializer = {0};
    MI_Char *fileExpandedPath = NULL;

    r = DSC_MI_Application_Initialize(0, NULL, NULL, &miApp);   
    if (r != MI_RESULT_OK)
    {
        return GetCimMIError(r, extendedError, ID_MODMAN_APPINIT_FAILED);
    }

    r = UpdateDSCCacheInstance(&miApp, &g_DSCInternalCache, complianceStatus, getActionStatusCode, lcmStatusCode, registeredServerURLs, extendedError);
    if (r != MI_RESULT_OK)
    {
        MI_Application_Close(&miApp);
        return r;
    }

    serializerBuffer = (MI_Char*) DSC_malloc(bufferSize, NitsHere());
    if (serializerBuffer == NULL)
    {
        MI_Application_Close(&miApp);
        return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, extendedError, ID_LCMHELPER_MEMORY_ERROR);
    }

    r = MI_Application_NewSerializer_Mof(&miApp, 0, MOFCODEC_FORMAT, &serializer);
    if (r != MI_RESULT_OK )
    {
        DSC_free(serializerBuffer);
        MI_Application_Close(&miApp);        
        return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, extendedError, ID_LCMHELPER_METASCHEMA_CREATESERIALIZE_FAILED);
    }

    r = MI_Serializer_SerializeInstance(&serializer, 0, g_DSCInternalCache, (MI_Uint8*) serializerBuffer, bufferSize , &bufferUsed);
    MI_Serializer_Close(&serializer);
    MI_Application_Close(&miApp);     
    if (r != MI_RESULT_OK )    
    {
        DSC_free(serializerBuffer);  
        return GetCimMIError(MI_RESULT_INVALID_CLASS, extendedError, ID_LCMHELPER_METASCHEMA_CREATESERIALIZE_FAILED);
    }

    r = ExpandPath(CONFIGURATION_LOCATION_INTERNALSTATECACHE, &fileExpandedPath, extendedError);
    if (r != MI_RESULT_OK )    
    {
        DSC_free(serializerBuffer);  
        return r;
    }

    File_RemoveT(fileExpandedPath);
    r = SaveFile(fileExpandedPath, (MI_Uint8*) serializerBuffer, bufferUsed, extendedError);
    DSC_free(serializerBuffer); 
    DSC_free(fileExpandedPath);

        UpdateLCMStatusCodeHistory(&g_DSCInternalCache, &g_LCMStatusCodeHistory);

    return r;
}

void GetLatestStatus(
    _Out_ MI_Boolean *complianceStatus,
    _Out_ MI_Uint32 *getActionStatusCode,
        _Out_ MI_Uint32 *lcmStatusCode)
{
    MI_Value value;
    MI_Type  type;
    MI_Uint32 flags;

    *complianceStatus = MI_FALSE;
    *getActionStatusCode = GET_ACTION_STATUS_CODE_SUCCESS;
        *lcmStatusCode = LCM_STATUSCODE_READY;
    if (g_DSCInternalCache)
    {
        if (MI_Instance_GetElement(g_DSCInternalCache, DSC_InternalStateCache_ComplianceStatus, &value, &type, &flags, NULL) == MI_RESULT_OK)
        {
            *complianceStatus = value.boolean;
        }

        if (MI_Instance_GetElement(g_DSCInternalCache, DSC_InternalStateCache_GetActionStatusCode, &value, &type, &flags, NULL) == MI_RESULT_OK)
        {
            *getActionStatusCode = (MI_Uint32)value.sint64;
        }

                if (MI_Instance_GetElement(g_DSCInternalCache, DSC_InternalStateCache_LCMStatusCode, &value, &type, &flags, NULL) == MI_RESULT_OK)
                {
                        *lcmStatusCode = (MI_Uint32)value.sint64;
                }
    }
}

MI_Result LCM_Pull_ExecuteActionPerConfiguration(
        _In_ LCMProviderContext *lcmContext,
        _In_ MI_Instance *metaConfigInstance,
        _In_opt_z_ MI_Char *partialConfigName,
        _In_z_ MI_Char *checkSum,
        _In_ MI_Boolean bIsCompliant,
        _In_ MI_Uint32 lastGetActionStatusCode,
        _Out_ MI_Uint32 * numModulesInstalled,
        _Outptr_result_maybenull_z_  MI_Char** resultStatus,
        _Inout_ MI_Uint32* getActionStatusCode,
        _Outptr_result_maybenull_ OverAllGetActionResponse** serverAssignedConfigurations,
        _In_ ModuleManager *moduleManager,
        _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
        MI_Result result = MI_RESULT_OK;
        MI_Uint32 resultExecutionStatus = 0;
        result = LCM_Pull_GetAction(lcmContext, metaConfigInstance, partialConfigName, checkSum, bIsCompliant, *getActionStatusCode, resultStatus, getActionStatusCode, serverAssignedConfigurations, cimErrorDetails);
        if (result == MI_RESULT_OK)
        {
                // only if we are asked to get the current configuration, we will do it immediately
                if (*resultStatus && Tcscasecmp(*resultStatus, PULL_STATUSCODE_GETCONFIG) == 0)
                {
                        // for now download just the first configuration until partial configuration supported
                        MI_Char* assignedConfig = NULL;
                        
                        // MI_Char* status = ((*serverAssignedConfigurations)->Details)->Status;
                        if (*serverAssignedConfigurations != NULL && ((*serverAssignedConfigurations)->Details) != NULL)
                        {
                            assignedConfig = ((*serverAssignedConfigurations)->Details)->ConfigurationName;
                        }
                        result = LCM_Pull_GetConfiguration(lcmContext, moduleManager, metaConfigInstance, partialConfigName, numModulesInstalled, assignedConfig, &resultExecutionStatus, getActionStatusCode, cimErrorDetails);
                        
                        if (assignedConfig != NULL)
                        {
                            DSC_free(assignedConfig);
                            ((*serverAssignedConfigurations)->Details)->ConfigurationName = NULL;
                        }

                        if (result != MI_RESULT_OK)
                        {
                            DSC_free(*resultStatus);
                            *resultStatus = NULL;
                        }
                }
        }
        else
        {
                if (partialConfigName)
                {
                        // If this was retreiving partial configuration, we log and ignore the failue. It will be handled later when
                        // applying the configuration.
                        DSC_EventWritePartialConfigurationNotAvailable(partialConfigName);
                        result = MI_RESULT_OK;
                        if (cimErrorDetails && *cimErrorDetails)
                        {
                                MI_Instance_Delete(*cimErrorDetails);
                                *cimErrorDetails = NULL;
                        }
                }
        }
        return result;
}

MI_Result MI_CALL LCM_Pull_Execute(
        _In_ LCMProviderContext *lcmContext,
        _In_ MI_Instance *metaConfigInstance,
        _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
        //get the meta config instance.
        MI_Char* resultStatus = NULL;
        MI_Result result = MI_RESULT_OK;
        ModuleManager *moduleManager = NULL;
        MI_Boolean bComplianceStatus;
        MI_Uint32 getActionStatusCode;
        MI_Uint32 lcmStatusCode;
        MI_Uint32 resultExecutionStatus = 0;
        MI_Char *checkSumValue = NULL;
        MI_Char *partialConfigName = NULL;
        MI_InstanceA partialConfigurations = { 0 };
        MI_Boolean complianceStatus = MI_FALSE;
        MI_Boolean bFullConfiguration = MI_FALSE;
        MI_Boolean bGotNewConfiguration = MI_FALSE;
        MI_Result updateResult = MI_RESULT_OK;
        MI_Instance* updateErrorDetails = NULL;
        MI_Uint32 xCount;
        MI_Uint32 numModulesInstalled = 0;
        int retval;
        OverAllGetActionResponse* serverAssignedConfigurations = NULL;


        SetLCMProviderContext( lcmContext, (LCM_EXECUTIONMODE_OFFLINE | LCM_EXECUTIONMODE_ONLINE) , NULL);
        result = InitializeModuleManager(0, cimErrorDetails, &moduleManager);
        if (result != MI_RESULT_OK)
        {
                return result;
        }

        if (moduleManager == NULL)
        {
                return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_ENGINEHELPER_MEMORY_ERROR);
        }

        result = RegisterWithPullServers(lcmContext, metaConfigInstance, cimErrorDetails);
        if (result != MI_RESULT_OK)
        {
            moduleManager->ft->Close(moduleManager, NULL);
            return result;
        }

        GetLatestStatus(&bComplianceStatus, &getActionStatusCode, &lcmStatusCode);

        /* Now we need to figure out if we have to download partial configurations.*/
        // r = GetPartialConfigurationPath(value.string, configFileName, cimErrorDetails);
        if (ShouldUsePartialConfigurations(metaConfigInstance, MI_FALSE)) //Don't need to check if there are files in the partial config directory
        {
                result = GetPartialConfigurations((MI_Instance*) metaConfigInstance, &partialConfigurations, cimErrorDetails);
                if (result != MI_RESULT_OK)
                {
                        moduleManager->ft->Close(moduleManager, NULL);
                        return result;
                }
        }
        else
        {
                // We have exactly 1 element to process, GetPartialConfigurationName handles NULL and return
                // appropriately for full configurations for rest of the system to work properly
                partialConfigurations.size = 1;
                bFullConfiguration = MI_TRUE;
        }
        for (xCount = 0; xCount < partialConfigurations.size && result == MI_RESULT_OK; xCount++)
        {
            result = GetPartialConfigurationName(bFullConfiguration ? NULL : partialConfigurations.data[xCount], (const MI_Char**)&partialConfigName, cimErrorDetails);
            if (partialConfigName != NULL)
            {
                DSC_EventWriteLCMPullingPartial(partialConfigName);//Log that we're pulling this partial configuration
            }
            if (result != MI_RESULT_OK)
            {
                    moduleManager->ft->Close(moduleManager, NULL);
                    return result;
            }
            result = GetMofChecksum(&checkSumValue, partialConfigName, cimErrorDetails);
            if (result != MI_RESULT_OK)
            {
                    moduleManager->ft->Close(moduleManager, NULL);
                    return result;
            }
            result = LCM_Pull_ExecuteActionPerConfiguration(lcmContext, metaConfigInstance, partialConfigName,
                                                            checkSumValue, bComplianceStatus, getActionStatusCode, 
                                                            &numModulesInstalled, &resultStatus, &getActionStatusCode, &serverAssignedConfigurations,
                                                            moduleManager, cimErrorDetails);
            DSC_free(checkSumValue);

            if (serverAssignedConfigurations)
            {
                // Release the memory block assigned to each configuration status and then release the overall configurationstatus (serverAssignedConfigurations)
                // Please refer to the struct definition of 'OverAllGetActionResponse' to understand the logic below easily. 
                if (serverAssignedConfigurations != NULL)
                {
                    MI_Uint32 totalConfigCounts = (serverAssignedConfigurations)->NumberOfConfiguration;
                    MI_Uint32 memoryOffset;
                    for (memoryOffset = 0; memoryOffset < totalConfigCounts; memoryOffset++)
                    {
                        ConfigurationStatus* currentMemoryBlock = (serverAssignedConfigurations)->Details + memoryOffset;
                        if (currentMemoryBlock != NULL)
                        {
                            DSC_free(currentMemoryBlock);
                            currentMemoryBlock = NULL;
                        }
                    }
                    DSC_free(serverAssignedConfigurations);
                }            
                serverAssignedConfigurations = NULL;       
            }

            if (result == MI_RESULT_OK)
            {
                // If GetAction required us to get the configuraiton then only we have to apply the new configuration.
                if (resultStatus && Tcscasecmp(resultStatus, PULL_STATUSCODE_GETCONFIG) == 0)
                {
                        bGotNewConfiguration = MI_TRUE;
                }
                else if (resultStatus && Tcscasecmp(resultStatus, PULL_STATUSCODE_RETRY) == 0)
                {
                        // Pull server didn't have configuration file, ignore the failure, it will be handled later.
                        DSC_EventWritePartialConfigurationNotAvailable(partialConfigName);
                }
                if (resultStatus)
                {
                        DSC_free(resultStatus);
                }
            }
        }
        if (result == MI_RESULT_OK && bGotNewConfiguration)
        {
            if (!bFullConfiguration)
            {
                //If the partial configurations directory contains partial files, combine them and save into pending.mof
                result = MergePartialConfigurations(lcmContext, moduleManager, GetPendingConfigFileName(), GetPartialConfigBaseDocumentInstanceFileName(), cimErrorDetails);
                EH_CheckResult(result);
            }
            // If everything is good we will apply the configuration here.
            if (result == MI_RESULT_OK)
            {
                // If Current.mof exists, move Current.mof to Previous.mof
                if (File_ExistT(GetCurrentConfigFileName()) == 0)
                {
                    result = CopyConfigAndRemoveSource(CONFIGURATION_LOCATION_CURRENT, CONFIGURATION_LOCATION_PREVIOUS, cimErrorDetails);
                    EH_CheckResult(result);
                }

                if (numModulesInstalled > 0)
                {
                    result = ModuleManager_Update(moduleManager, cimErrorDetails);
                    if (result != MI_RESULT_OK)
                    {
                        return result;
                    }
#if defined(BUILD_OMS)
                    if (g_DscHost == MI_FALSE)
                    {
                        system(OMI_RELOAD_COMMAND);
                    }
#else
                    system(OMI_RELOAD_COMMAND);
#endif
                }

                result = ApplyPendingConfig(lcmContext, moduleManager, 0, &resultExecutionStatus, cimErrorDetails);
                if (result == MI_RESULT_OK && (resultExecutionStatus & DSC_RESTART_SYSTEM_FLAG))
                {
                    SetLCMStatusReboot(lcmContext);
                }
                
                if (result == MI_RESULT_OK)
                {
                    // update compliance result.
                    complianceStatus = MI_TRUE;
                    result = UpdateCurrentStatus(&complianceStatus, NULL, NULL, NULL, cimErrorDetails);
                    if (result == MI_RESULT_OK)
                    {
                        // result = CopyConfigurationChecksum(mofFileName, cimErrorDetails);
                    }
                }
            }
        }

        if (result != MI_RESULT_OK)
        {
                // log it here.
                DSC_EventWriteLCMPullEngineError(CA_ACTIVITY_NAME,
                        GetDownloadManagerName(metaConfigInstance),
                        result,
                        GetErrorDetail(*cimErrorDetails));
        }

        updateResult = UpdateCurrentStatus(NULL, &getActionStatusCode, NULL, NULL, &updateErrorDetails);
        if (updateResult != MI_RESULT_OK)
        {
                if (result == MI_RESULT_OK)
                {
                        // update the error with it.
                        result = updateResult;
                        *cimErrorDetails = updateErrorDetails;
                }
                else
                {
                        // retain original error and delete error instance.
                        if (updateErrorDetails)
                        {
                                MI_Instance_Delete(updateErrorDetails);
                        }
                }
        }

EH_UNWIND;
        moduleManager->ft->Close(moduleManager, NULL);


        return result;
}

MI_Result LCM_Pull_GetAction(
    _In_ LCMProviderContext *lcmContext,
    _In_ MI_Instance *metaConfigInstance,
    _In_opt_z_ MI_Char *partialConfigName,
    _In_z_ MI_Char *checkSum,
    _In_ MI_Boolean bIsCompliant,
    _In_ MI_Uint32 lastGetActionStatusCode,
    _Outptr_result_maybenull_z_  MI_Char** resultStatus,
    _Out_ MI_Uint32* getActionStatusCode,
    _Outptr_result_maybenull_ OverAllGetActionResponse** serverAssignedConfigurations,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result result = MI_RESULT_OK;
    MI_Value value;
    MI_Uint32 flags;
    if (cimErrorDetails )
    {
        *cimErrorDetails = NULL;
    }

    result = Pull_GetActionWebDownloadManager(lcmContext, metaConfigInstance, partialConfigName, checkSum, bIsCompliant, lastGetActionStatusCode, resultStatus, getActionStatusCode, serverAssignedConfigurations, cimErrorDetails);

    if (result != MI_RESULT_OK)
    {
        return result;
    }

    if (*resultStatus == NULL )
    {
        return GetCimMIError(MI_RESULT_NOT_FOUND, cimErrorDetails,ID_LCM_PULL_GETACTION_NORESULTSTATUS);
    }

    //Validate valid status codes
    if (!(Tcscasecmp(*resultStatus, PULL_STATUSCODE_OK) == 0 ||
        Tcscasecmp(*resultStatus, PULL_STATUSCODE_GETCONFIG) == 0 ||
        Tcscasecmp(*resultStatus, PULL_STATUSCODE_UPDATEMETACONFIG) == 0))
    {
        //Uknown status code return error.
        return GetCimMIError(MI_RESULT_NOT_FOUND, cimErrorDetails, ID_LCM_PULL_GETACTION_UNEXPECTEDRESULTSTATUS);       
    }

    return result;
}

//It is assumed that the caller has already called InitHandler. It will be called automatically if this function is used from WMIv2 provider interface.
MI_Result LCM_Pull_GetConfiguration(
        _In_ LCMProviderContext *lcmContext,
        _In_ ModuleManager *moduleManager,
        _In_ MI_Instance *metaConfigInstance,
        _In_opt_z_ MI_Char *partialConfigName,
        _Out_ MI_Uint32 * numModulesInstalled,
        _In_opt_z_ MI_Char *assignedConfiguration,
        _Inout_ MI_Uint32 *resultExecutionStatus,
        _Out_ MI_Uint32 *getActionStatusCode,
        _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Char* mofFileName = NULL;
    MI_Result result = MI_RESULT_OK;
    MI_Char *targetMofPath = NULL;
    MI_Char *targetMofChecksumPath = NULL;
    MI_Char *resultStatus = NULL;
    MI_Char * directoryName = NULL;
    char command[1024];

    if (cimErrorDetails == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }

    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once.

    result = Pull_GetConfigurationWebDownloadManager(lcmContext, metaConfigInstance, partialConfigName, assignedConfiguration, &mofFileName, &directoryName, numModulesInstalled, &resultStatus, getActionStatusCode, cimErrorDetails);
    if (result != MI_RESULT_OK)
    {
        if (directoryName != NULL)
        {
            snprintf(command, 1024, "rm -rf %s", directoryName);
            system(command);
            DSC_LOG_INFO("Executed '%T'\n", command);
            free(directoryName);
        }
        return result;
    }

    if (resultStatus == NULL)
    {
        EH_Check((result = GetCimMIError(MI_RESULT_NOT_FOUND, cimErrorDetails, ID_LCM_PULL_GETCONF_NORESULTSTATUS)) == MI_RESULT_OK);
    }

    if (Tcscasecmp(resultStatus, PULL_STATUSCODE_OK) == 0)
    {
        if (partialConfigName != NULL)
        {
            result = GetPartialConfigurationPath(partialConfigName, &targetMofPath, cimErrorDetails);
            EH_Check(result == MI_RESULT_OK);

            result = GetPartialConfigurationPathCheckSum(partialConfigName, &targetMofChecksumPath, cimErrorDetails);
            EH_Check(result == MI_RESULT_OK);

            result = ValidatePartialConfiguration(moduleManager, mofFileName, metaConfigInstance, cimErrorDetails);
            EH_Check(result == MI_RESULT_OK);
        }
        else
        {
            // If Current.mof exists, move Current.mof to Previous.mof
            if (File_ExistT(GetCurrentConfigFileName()) == 0)
            {
                result = CopyConfigAndRemoveSource(CONFIGURATION_LOCATION_CURRENT, CONFIGURATION_LOCATION_PREVIOUS, cimErrorDetails);
                EH_CheckResult(result);
            }

            targetMofPath = (MI_Char*) GetPendingConfigFileName();
            targetMofChecksumPath = (MI_Char*) GetConfigChecksumFileName();
        }

        result = CopyConfigurationFileFromTemp(mofFileName, targetMofPath, cimErrorDetails);
        if (result == MI_RESULT_OK)
        {
            result = CopyConfigurationChecksum(mofFileName, targetMofChecksumPath, cimErrorDetails);
        }
        EH_Check(result == MI_RESULT_OK);
    }
    else if (Tcscasecmp(resultStatus, PULL_STATUSCODE_RETRY) != 0)
    {
        //Uknown status code return error.
        result = GetCimMIError(MI_RESULT_NOT_FOUND, cimErrorDetails, ID_LCM_PULL_GETCONF_UNEXPECTEDRESULTSTATUS);
        EH_Check(result == MI_RESULT_OK);
    }
EH_UNWIND;
    DSCFREE_IF_NOT_NULL(resultStatus);
    if (mofFileName != NULL)
    {
        CleanupTempDirectory(mofFileName);
        DSC_free(mofFileName);
    }
    if (partialConfigName != NULL)//In this case targetmof path were dynamically assigned
    {
        DSCFREE_IF_NOT_NULL(targetMofPath);
        DSCFREE_IF_NOT_NULL(targetMofChecksumPath);
    }

    // Delete temp directory if exists
    if (directoryName != NULL)
    {
        snprintf(command, 1024, "rm -rf %s", directoryName);
        system(command);
        DSC_LOG_INFO("Executed '%T'\n", command);
        free(directoryName);
    }
    return result;
}

MI_Result ClearBuiltinProvCache(
    _In_z_ const MI_Char* cachePath,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result result;
    MI_Char *pwCacheDir;

    if (cimErrorDetails == NULL)
    {        
        return MI_RESULT_INVALID_PARAMETER; 
    }
    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once. 

    result = ExpandPath(cachePath, &pwCacheDir, cimErrorDetails);
    if (result != MI_RESULT_OK)
    {
        return result;
    }

    if (File_ExistT(pwCacheDir) != -1)
    {
        DSC_free(pwCacheDir);
        return MI_RESULT_OK;
    }

    LCM_WriteMessageNoParamWithoutContext(DSC_RESOURCENAME, ID_LCM_CLEAR_PROVIDER_CACHE, MI_WRITEMESSAGE_CHANNEL_WARNING);

    RecursivelyDeleteDirectory(pwCacheDir);
    DSC_free(pwCacheDir);
    return MI_RESULT_OK;
}

void ConcatStrings(_Outptr_result_z_ MI_Char** target, _In_ MI_Uint32 padding, _In_z_ MI_Char* source1, _In_z_ MI_Char* source2)
{
    *target = NULL;
    if(source1 && source2)
    {
        size_t targetLength=Tcslen(source1)+Tcslen(source2)+1+padding; //+1 for "\0"
        *target= (MI_Char*) DSC_malloc(targetLength*sizeof(MI_Char), NitsMakeCallSite(-3, NULL, NULL, 0));
        if(*target!=NULL)
        {
            if (Stprintf( *target, targetLength,MI_T("%-7s%-9s") ,source1,source2) <=0 )
            {
                DSC_free(*target);
                *target=NULL;
                    
            }
        }
    }
}

void GetFullMessageWithTokens(_In_z_ const MI_Char * deviceName, _In_z_ const MI_Char * resourceId,
                     _In_z_ const MI_Char * message, _In_ LCMProviderContext *lcmContext, Intlstr *intlstr)
{
    if(lcmContext!=NULL)
    {
        Intlstr tempOperation = Intlstr_Null;
        Intlstr tempItem = Intlstr_Null;
        MI_Char* actionItem = NULL;
        MI_Uint32 errorId;
           
        if(lcmContext->messageOperation && lcmContext->messageItem)
        {
            GetResourceString(lcmContext->messageOperation,&tempOperation);
            GetResourceString(lcmContext->messageItem,&tempItem);
            if(resourceId!=NULL && (Tcscmp(resourceId,EMPTY_STRING)==0))
            {
                errorId = ID_OUTPUT_FORMATNORESOURCE;
            }
            else
            {
                errorId=ID_OUTPUT_FORMATOUTPUT;
            }
        }
        else
        {
            errorId = ID_OUTPUT_FORMATPROVIDER;
            GetResourceString(ID_OUTPUT_EMPTYSTRING,&tempOperation);
            GetResourceString(ID_OUTPUT_EMPTYSTRING,&tempItem);
        }

        ConcatStrings(&actionItem,TOKENIZED_OUTPUT_PADDING,(MI_Char*)tempOperation.str,(MI_Char*)tempItem.str);
        if( tempOperation.str)
            Intlstr_Free(tempOperation);
        if( tempItem.str)
            Intlstr_Free(tempItem);

        GetResourceString4Param(errorId, deviceName, actionItem, resourceId, message, intlstr);
        DSC_free(actionItem);
    }    
    //result = Stprintf(fullMessage, msgLen,format , g_JobInformation.deviceName, actionItem, resourceId, message);
}

MI_Result TimeToRunConsistencyCheck(
    _Out_ MI_Boolean *bRunConsistencyCheck,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result result = MI_RESULT_OK;
    MI_Instance *currentMetaConfigInstance = NULL;
    MI_Value pullActionFrequencyInMins;
    MI_Value testConfigurationFrequencyInMins;
    MI_Uint32 flags = 0;  
    MI_Uint32 executionSetSize = 0;
    MI_Uint32 lastExecutionRun = 0;

    if (cimErrorDetails == NULL)
    {        
        return MI_RESULT_INVALID_PARAMETER; 
    }
    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once. 

    *bRunConsistencyCheck = MI_FALSE;

    result = GetMetaConfig((MSFT_DSCMetaConfiguration **)&currentMetaConfigInstance);
    if (result != MI_RESULT_OK)
    {
        return GetCimMIError(result, cimErrorDetails, ID_LCM_FAILED_TO_GET_METACONFIGURATION);
    }

    result = MI_Instance_GetElement((MI_Instance*)currentMetaConfigInstance, MSFT_DSCMetaConfiguration_ConfigurationModeFrequencyMins, &testConfigurationFrequencyInMins, NULL, &flags, NULL);
    if (result == MI_RESULT_OK)
    {
        if (flags & MI_FLAG_NULL)
        {
            testConfigurationFrequencyInMins.uint32 = DEFAULT_ConfigurationModeFrequencyMins;
        }
    }

    result = MI_Instance_GetElement((MI_Instance*)currentMetaConfigInstance, MSFT_DSCMetaConfiguration_RefreshFrequencyMins, &pullActionFrequencyInMins, NULL, &flags, NULL);
    if (result == MI_RESULT_OK)
    {
        if (flags & MI_FLAG_NULL)
        {
            pullActionFrequencyInMins.uint32 = DEFAULT_MinRefreshFrequencyMins;
        }
    }

    if(pullActionFrequencyInMins.uint32 > 0)
    {
        executionSetSize = testConfigurationFrequencyInMins.uint32 / pullActionFrequencyInMins.uint32 - 1;
    }

    result =  LoadFromPullRunLogFile(&lastExecutionRun, cimErrorDetails);

    if (result != MI_RESULT_OK)
    {
        MI_Instance_Delete(currentMetaConfigInstance);
        return GetCimMIError(result, cimErrorDetails, ID_LCM_CREATE_PULL_RUN_LOG_FILE);
    }

    result = SavePullRunLogFile(executionSetSize, cimErrorDetails);

    if (result != MI_RESULT_OK)
    {
        MI_Instance_Delete(currentMetaConfigInstance);
        return GetCimMIError(result, cimErrorDetails, ID_LCM_CREATE_PULL_RUN_LOG_FILE);
    }

    // Compare the executionSetSize to the number of runs that have previously executed.
    if(executionSetSize == lastExecutionRun)
    {
        *bRunConsistencyCheck = MI_TRUE;
    }

    MI_Instance_Delete(currentMetaConfigInstance);
    return MI_RESULT_OK;
}

MI_Result SetLCMStatusBusy()
{
        MI_Uint32 lcmStatus;
        MI_Instance *extendedError;
        MI_Result r;
        
        g_currentError[0] = '\0';

        if (!g_LCMPendingReboot)
        {
                lcmStatus = LCM_STATUSCODE_BUSY;
                r = UpdateCurrentStatus(NULL, NULL, &lcmStatus, NULL, &extendedError);
#ifdef TEST_BUILD
                NitsAssert(r == MI_RESULT_OK, "UpdateCurrentStatus should succeed");
#endif
                r = UpdateMetaConfigWithLCMState(&lcmStatus, (MI_Instance *)g_metaConfig);
#ifdef TEST_BUILD
                NitsAssert(r == MI_RESULT_OK, "UpdateCurrentStatus should succeed");
#endif
        }

        if (g_registrationManager != NULL && ((RegistrationManager*)g_registrationManager)->agentId != NULL)
        {
            UpdateMetaConfigWithAgentId( ((RegistrationManager*)g_registrationManager)->agentId, (MI_Instance*)g_metaConfig);
        }
        
        ReportStatusToServer(NULL, NULL, NULL, NULL, 0, MI_FALSE, /*isStatusReport*/ 1, (MI_Instance*)NULL);

        // errors in above invocation are silently ignored since failure of updating status should not block other operations
        r = MI_RESULT_OK;
        return r;
}

MI_Result SetLCMStatusReady()
{
        MI_Uint32 lcmStatus;
        MI_Instance *extendedError;
        MI_Result r;

        if (!g_LCMPendingReboot)
        {
                lcmStatus = LCM_STATUSCODE_READY;
                r = UpdateCurrentStatus(NULL, NULL, &lcmStatus, NULL, &extendedError);
#ifdef TEST_BUILD
                NitsAssert(r == MI_RESULT_OK, "UpdateCurrentStatus should succeed");
#endif
                r = UpdateMetaConfigWithLCMState(&lcmStatus, (MI_Instance *)g_metaConfig);
#ifdef TEST_BUILD
                NitsAssert(r == MI_RESULT_OK, "UpdateCurrentStatus should succeed");
#endif
        }

        ReportStatusToServer(NULL, NULL, NULL, NULL, 0, MI_TRUE, /*isStatusReport*/ 1, (MI_Instance*)NULL);

        // errors in above invocation are silently ignored since failure of updating status should not block other operations
        r = MI_RESULT_OK;
        return r;
}

MI_Result SetLCMStatusReboot()
{
        MI_Uint32 lcmStatus;
        MI_Instance *extendedError;
        MI_Result r;

        g_LCMPendingReboot = MI_TRUE;
        lcmStatus = LCM_STATUSCODE_REBOOT;
        r = UpdateCurrentStatus(NULL, NULL, &lcmStatus, NULL, &extendedError);
#ifdef TEST_BUILD
                NitsAssert(r == MI_RESULT_OK, "UpdateCurrentStatus should succeed");
#endif
        r = UpdateMetaConfigWithLCMState(&lcmStatus, (MI_Instance *)g_metaConfig);
#ifdef TEST_BUILD
                NitsAssert(r == MI_RESULT_OK, "UpdateCurrentStatus should succeed");
#endif

        // errors in above invocation are silently ignored since failure of updating status should not block other operations
        r = MI_RESULT_OK;
        return r;
}

MI_Result UpdateLCMStatusCodeHistory(
        _In_ MI_Instance **dscInternalCache,
        _Outptr_result_maybenull_z_ MI_Char **lcmStatusCodeHistory)
{
        MI_Value value;
        MI_Type  type;
    MI_Uint32 flags;
        MI_Char tempCodeStr[LCM_STATUSCODE_HISTORY_STR_SIZE];
        int retValue;
        
        if (lcmStatusCodeHistory == NULL || *dscInternalCache == NULL)
        {
                return MI_RESULT_INVALID_PARAMETER;
        }

        if (MI_Instance_GetElement(*dscInternalCache, DSC_InternalStateCache_LCMStatusCode, &value, &type, &flags, NULL) != MI_RESULT_OK)
        {
                return MI_RESULT_FAILED;
        }
        retValue = Stprintf(tempCodeStr, LCM_STATUSCODE_HISTORY_STR_SIZE, MI_T("%d,"), value.sint64);
        if (retValue == -1)
        {
                return MI_RESULT_FAILED;
        }

        if (GetCurrentLCMStatusCodeHistory() == NULL)
        {
                *lcmStatusCodeHistory = (MI_Char*)DSC_malloc(MAX_LCM_STATUSCODE_HISTORY_SIZE * sizeof(MI_Char), NitsHere());
                if (*lcmStatusCodeHistory == NULL)
                {
                        return MI_RESULT_SERVER_LIMITS_EXCEEDED;
                }

                retValue = TcsStrlcpy(*lcmStatusCodeHistory, tempCodeStr, MAX_LCM_STATUSCODE_HISTORY_SIZE);
        }
        else
        {
            retValue = TcsStrlcat(*lcmStatusCodeHistory, tempCodeStr, MAX_LCM_STATUSCODE_HISTORY_SIZE);

        }

        if (retValue == -1)
        {
                return MI_RESULT_FAILED;
        }

        return MI_RESULT_OK;
}

MI_Result GetLCMStatusCodeHistory(
        _Outptr_result_maybenull_z_ MI_Char **lcmStatusCodeHistory,
        _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
        int retValue;
        MI_Uint32 lcmStatus;

        if (cimErrorDetails == NULL)
        {
                return MI_RESULT_INVALID_PARAMETER;
        }
        *cimErrorDetails = NULL;

        if (GetCurrentLCMStatusCodeHistory() == NULL)
        {
                *lcmStatusCodeHistory = NULL;
        }
        else
        {
                *lcmStatusCodeHistory = (MI_Char*)DSC_malloc(MAX_LCM_STATUSCODE_HISTORY_SIZE * sizeof(MI_Char), NitsHere());
                if (*lcmStatusCodeHistory == NULL)
                {
                        return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_LCMHELPER_MEMORY_ERROR);
                }
                retValue = TcsStrlcpy(*lcmStatusCodeHistory, GetCurrentLCMStatusCodeHistory(), MAX_LCM_STATUSCODE_HISTORY_SIZE);

                if (retValue == -1)
                {
                        DSC_free(*lcmStatusCodeHistory);
                        *lcmStatusCodeHistory = NULL;
                        return GetCimMIError(MI_RESULT_FAILED, cimErrorDetails, ID_LCMHELPER_PRINTF_ERROR);
                }
        }
        DSC_free(g_LCMStatusCodeHistory);
        g_LCMStatusCodeHistory = NULL;

        lcmStatus = LCM_STATUSCODE_READY;
        UpdateCurrentStatus(NULL, NULL, &lcmStatus, NULL, cimErrorDetails);
        return MI_RESULT_OK;
}


/* caller release outinstances */
MI_Result CallPerformInventory(
    _In_ MI_Char * InMOF,
    _Inout_ MI_InstanceA *outInstances,
    _In_ MI_Context* context,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result result = MI_RESULT_OK;
    MI_InstanceA inventoryInstances = {0};
    MI_Instance *documentIns = NULL;
    MI_InstanceA inventoryResultInstances = {0};
    LCMProviderContext lcmContext = {0};
    BOOL fResult;
    ModuleManager *moduleManager = NULL;

    if (cimErrorDetails == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER; 
    }
    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once. 

    lcmContext.executionMode = (LCM_EXECUTIONMODE_OFFLINE | LCM_EXECUTIONMODE_ONLINE);
    lcmContext.context = (void*)context;    
    result = ValidateConfigurationDirectory(cimErrorDetails);
    if (result != MI_RESULT_OK)
    {
        SetLCMStatusReady();
        if (cimErrorDetails && *cimErrorDetails)
            return result;

        return GetCimMIError(result, cimErrorDetails, ID_LCMHELPER_VALIDATE_CONFGDIR_FAILED);
    }

    if (InMOF == NULL || InMOF[0] == '\0')
    {
        InMOF = GetInventoryFileName();
    }

    if (File_ExistT(InMOF) != 0) // CodeQL [cpp/path-injection] Safe Path: Currently only known paths are considered
    {
    SetLCMStatusReady();
    return GetCimMIError(MI_RESULT_FAILED, cimErrorDetails, ID_LCMHELPER_INVENTORY_MOF_DOESNT_EXIST);
    }

    result = InitializeModuleManager(0, cimErrorDetails, &moduleManager);
    if (result != MI_RESULT_OK)
    {
        SetLCMStatusReady();
        return result;
    }

    result =  moduleManager->ft->LoadInstanceDocumentFromLocation(moduleManager, 0, InMOF, cimErrorDetails, &inventoryInstances, &documentIns);
    if (result != MI_RESULT_OK)
    {
        moduleManager->ft->Close(moduleManager, NULL);
        SetLCMStatusReady();
        if (cimErrorDetails && *cimErrorDetails)
            return result;
        
        return GetCimMIError(result, cimErrorDetails, ID_LCMHELPER_LOAD_INVENTORY_FAILED);
    }

    if (documentIns != NULL)
    {
        result = ValidateInventoryDocumentInstance(documentIns, cimErrorDetails);
        if (result != MI_RESULT_OK)
        {
            CleanUpInstanceCache(&inventoryInstances);
            moduleManager->ft->Close(moduleManager, NULL);
            SetLCMStatusReady();
            MI_Instance_Delete(documentIns);
            return result;
        }
    }

    // TODO: is it ok for the document to have no instances? or not exist?
    // Check if at least 1 resource was specified in the instance document
    if (inventoryInstances.size == 0 )
    {
        MI_Instance_Delete(documentIns);
        moduleManager->ft->Close(moduleManager, NULL);
        SetLCMStatusReady();
        return GetCimMIError(MI_RESULT_INVALID_PARAMETER, cimErrorDetails, ID_LCMHELPER_NORESOURCESPECIFIED);
    }

    SetMessageInContext(ID_OUTPUT_OPERATION_START,ID_OUTPUT_ITEM_INVENTORY,&lcmContext);
    LCM_BuildMessage(&lcmContext, ID_OUTPUT_EMPTYSTRING, EMPTY_STRING, MI_WRITEMESSAGE_CHANNEL_VERBOSE);

    result = PerformInventory(&lcmContext, 0, &inventoryInstances, moduleManager, documentIns, &inventoryResultInstances, cimErrorDetails);
    
    MI_Instance_Delete(documentIns);

    moduleManager->ft->Close(moduleManager, NULL);

    CleanUpInstanceCache(&inventoryInstances);
    if (result != MI_RESULT_OK)
    {
    SetLCMStatusReady();
    if (cimErrorDetails && *cimErrorDetails)
            return result;
    
        return GetCimMIError(result, cimErrorDetails, ID_LCMHELPER_INVENTORY_CONF_FAILED);
    }

    outInstances->data = inventoryResultInstances.data;
    outInstances->size = inventoryResultInstances.size;

    //Debug Log 
    DSC_EventWriteMethodEnd(__WFUNCTION__);

    return MI_RESULT_OK;
}
