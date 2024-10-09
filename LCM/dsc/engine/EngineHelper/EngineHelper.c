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
#include <MI.h>
#include <errno.h>
#include <string.h>
#include "EngineHelper.h"
#include "DSC_Systemcalls.h"
#include "Resources_LCM.h"
#include "EventWrapper.h"

extern Loc_Mapping g_LocMappingTable[];
extern MI_Uint32 g_LocMappingTableSize;
void *g_registrationManager;
char g_currentError[5001];
StatusReport_ResourceNotInDesiredState * g_rnids = NULL;

#if defined(BUILD_OMS)
MI_Boolean g_DscHost = MI_FALSE;
#endif

BaseResourceConfiguration g_BaseResourceConfiguration[] =
{
    {MI_T("ResourceId"),         MI_STRING},
    {MI_T("SourceInfo"),         MI_STRING},
    {MI_T("DependsOn"),          MI_STRINGA},
    {MI_T("ModuleName"),         MI_STRING},
    {MI_T("ModuleVersion"),      MI_STRING},
    {MI_T("ConfigurationName"), MI_STRING},
    {NULL,                  0}
};

const MI_Char * GetSchemaSearchPath()
{
    return CONFIGURATION_SCHEMA_SEARCH_PATH;
}

const MI_Char * GetSchemaSearchPathProgFiles()
{
    return CONFIGURATION_PROGFILES_SCHEMA_SEARCH_PATH;
}

const MI_Char * GetRegistrationInstanceSearchPath()
{
    return CONFIGURATION_REGINSTANCE_SEARCH_PATH;
}

const MI_Char * GetRegistrationInstanceSearchPathProgFiles()
{
    return CONFIGURATION_PROGFILES_REGINSTANCE_SEARCH_PATH;
}

const MI_Char * GetCoreSchemaPath()
{
    return CONFIGURATION_BASESCHEMA_MOF_PATH;
}

JobInformation g_JobInformation={EMPTY_STRING};

void SetJobDeviceName()
{
    if (GetComputerHostName(g_JobInformation.deviceName, (int)DEVICE_NAME_SIZE) != 0)
    {
            Stprintf(g_JobInformation.deviceName, DEVICE_NAME_SIZE, MI_T("%T"), EMPTY_STRING);
            return;
    }

}

MI_Result AppendWMIError1Param(
                        _Inout_ MI_Instance *cimErrorDetails,
                        _In_z_ const MI_Char * pszFormat,
                        _In_z_ const MI_Char * param1
                        )
{
    MI_Result r = MI_RESULT_OK;
    MI_Char *message = NULL;
    MI_Value value;

    r = DSC_MI_Instance_GetElement(cimErrorDetails, MSFT_WMIERROR_MESSAGE, &value, NULL, NULL, NULL);
    if( r == MI_RESULT_OK )
    {
        size_t msgLen = Tcslen(value.string) + Tcslen(param1) + Tcslen(pszFormat) + 1;
        message = (MI_Char*) DSC_malloc(sizeof(MI_Char) *msgLen, NitsMakeCallSite(-3, NULL, NULL, 0));
        if(message)
        {
            if( Stprintf(message, msgLen, pszFormat, value.string, param1))
            {
                value.string = message;
                r = MI_Instance_SetElement(cimErrorDetails, MSFT_WMIERROR_MESSAGE, &value, MI_STRING, 0);
            }

            DSC_free(message);
        }
    }
    return r;
}

MI_Result AppendWMIError1ParamID(
                        _Inout_ MI_Instance *cimErrorDetails,
                        _In_ MI_Uint32 errorStringId
                        )
{
    MI_Result r = MI_RESULT_OK;
    Intlstr intlstr = Intlstr_Null;
    GetResourceString(errorStringId, &intlstr);
    if( intlstr.str )
    {
        r = AppendWMIError1Param(cimErrorDetails, MI_T("%s %s"), intlstr.str);
        Intlstr_Free(intlstr);
    }

    return r;
}

_Always_(_Ret_range_(==, result))
MI_Result  CreateMiInstanceErrorObject(
        _Outptr_result_maybenull_ MI_Instance **p_cim_error_details,
        _In_z_ const MI_Char *p_format,
        ...
    )
{
    MI_Result result = MI_RESULT_OK;
    va_list arguments;
    va_list argumentsGetLength;
    size_t messageLength;
    size_t finalMessageLength;

    MI_Char *message = NULL;
    p_cim_error_details = NULL;

    va_start(arguments, p_format);

    // Copy arguments variable list for calculating the length
    va_copy(argumentsGetLength, arguments);
    messageLength = vsnprintf( NULL, 0, p_format, arguments );
    va_end(argumentsGetLength);
    if (messageLength < 0)
    {
        result = MI_RESULT_FAILED;
        MI_Utilities_CimErrorFromErrorCode( result, MI_RESULT_TYPE_MI, MI_T("Unable to calculate string length"), p_cim_error_details );
        goto cleanup;
    }

    // Allocate a buffer with the right size +1 for the null termination character
    message = (MI_Char*)DSC_malloc((messageLength + 1) * sizeof(MI_Char), NitsHere());

    if (message == NULL)
    {
        result = MI_RESULT_SERVER_LIMITS_EXCEEDED;
        MI_Utilities_CimErrorFromErrorCode( result, MI_RESULT_TYPE_MI, MI_T("Unable to allocate memory for message"), p_cim_error_details );
        goto cleanup;
    }

    finalMessageLength = vsnprintf( message, messageLength + 1, p_format, arguments );

    if (finalMessageLength < 0)
    {
        result = MI_RESULT_FAILED;
        MI_Utilities_CimErrorFromErrorCode( result, MI_RESULT_TYPE_MI, MI_T("Unable to format message"), p_cim_error_details );
        goto cleanup;
    }

    va_end(arguments);
   
    result = MI_Utilities_CimErrorFromErrorCode( result, MI_RESULT_TYPE_MI, message, p_cim_error_details );

cleanup:

    if (message != NULL)
    {
        DSC_free(message);
    }
    
    return result;
}

_Always_(_Ret_range_(==, result))
MI_Result GetCimMIError(MI_Result result ,
                        _Outptr_result_maybenull_ MI_Instance **cimErrorDetails,
                        _In_ MI_Uint32 errorStringId )
{
    Intlstr intlstr = Intlstr_Null;
    GetResourceString(errorStringId, &intlstr);

    MI_Utilities_CimErrorFromErrorCode( (MI_Uint32)result, MI_RESULT_TYPE_MI, intlstr.str, cimErrorDetails);
    strncpy(g_currentError, intlstr.str, 5000);
    DSC_EventWriteCIMError(intlstr.str,(MI_Uint32)result);
    if( intlstr.str)
        Intlstr_Free(intlstr);

    return result;
}

MI_Result GetCimWin32Error(MI_Uint32 result ,
                        _Outptr_result_maybenull_ MI_Instance **cimErrorDetails,
                        _In_ MI_Uint32 errorStringId )
{
    Intlstr intlstr = Intlstr_Null;
    GetResourceString(errorStringId, &intlstr);

    MI_Utilities_CimErrorFromErrorCode( (MI_Uint32)result, MI_RESULT_TYPE_WIN32, intlstr.str, cimErrorDetails);
    strncpy(g_currentError, intlstr.str, 5000);
    DSC_EventWriteCIMError(intlstr.str,(MI_Uint32)result);
    if( intlstr.str)
        Intlstr_Free(intlstr);
    return MI_RESULT_FAILED;
}

_Always_(_Ret_range_(==, result))
MI_Result GetCimMIError1Param(MI_Result result ,
                        _Outptr_result_maybenull_ MI_Instance **cimErrorDetails,
                        _In_ MI_Uint32 errorStringId,
                        _In_z_ const MI_Char * param1)
{
    BOOL errorInitialized = FALSE;
    Intlstr resIntlstr = Intlstr_Null;

    GetResourceString1Param(errorStringId, param1, &resIntlstr);
    if( resIntlstr.str )
    {
        MI_Utilities_CimErrorFromErrorCode((MI_Uint32)result, MI_RESULT_TYPE_MI, resIntlstr.str, cimErrorDetails);
        strncpy(g_currentError, resIntlstr.str, 5000);
        DSC_EventWriteCIMError(resIntlstr.str,(MI_Uint32)result);
        errorInitialized = TRUE;
        Intlstr_Free(resIntlstr);
    }
    if(!errorInitialized)
    {
        MI_Utilities_CimErrorFromErrorCode((MI_Uint32)result, MI_RESULT_TYPE_MI, NULL, cimErrorDetails);

    }
    return result;
}

_Always_(_Ret_range_(==, result))
MI_Result GetCimMIError2Params(MI_Result result ,
                        _Outptr_result_maybenull_ MI_Instance **cimErrorDetails,
                        _In_ MI_Uint32 errorStringId,
                        _In_z_ const MI_Char * param1,
                        _In_z_ const MI_Char * param2
                        )
{
    BOOL errorInitialized = FALSE;
    Intlstr resIntlstr = Intlstr_Null;

    GetResourceString2Param(errorStringId, param1, param2, &resIntlstr);
    if( resIntlstr.str )
    {
        MI_Utilities_CimErrorFromErrorCode((MI_Uint32)result, MI_RESULT_TYPE_MI, resIntlstr.str, cimErrorDetails);
        strncpy(g_currentError, resIntlstr.str, 5000);
        DSC_EventWriteCIMError(resIntlstr.str,(MI_Uint32)result);
        errorInitialized = TRUE;
        Intlstr_Free(resIntlstr);
    }
    if(!errorInitialized)
    {
        MI_Utilities_CimErrorFromErrorCode((MI_Uint32)result, MI_RESULT_TYPE_MI, NULL, cimErrorDetails);

    }
    return result;
}

_Always_(_Ret_range_(==, result))
MI_Result GetCimMIError3Params(MI_Result result ,
                        _Outptr_result_maybenull_ MI_Instance **cimErrorDetails,
                        _In_ MI_Uint32 errorStringId,
                        _In_z_ const MI_Char * param1,
                        _In_z_ const MI_Char * param2,
                        _In_z_ const MI_Char * param3
                        )
{
    BOOL errorInitialized = FALSE;
    Intlstr resIntlstr = Intlstr_Null;

    GetResourceString3Param(errorStringId, param1, param2, param3, &resIntlstr);
    if( resIntlstr.str )
    {
        MI_Utilities_CimErrorFromErrorCode((MI_Uint32)result, MI_RESULT_TYPE_MI, resIntlstr.str, cimErrorDetails);
        strncpy(g_currentError, resIntlstr.str, 5000);
        DSC_EventWriteCIMError(resIntlstr.str,(MI_Uint32)result);
        errorInitialized = TRUE;
        Intlstr_Free(resIntlstr);
    }
    if(!errorInitialized)
    {
        MI_Utilities_CimErrorFromErrorCode((MI_Uint32)result, MI_RESULT_TYPE_MI, NULL, cimErrorDetails);

    }
    return result;
}

_Always_(_Ret_range_(==, result))
MI_Result GetCimMIError4Params(MI_Result result ,
                        _Outptr_result_maybenull_ MI_Instance **cimErrorDetails,
                        _In_ MI_Uint32 errorStringId,
                        _In_z_ const MI_Char * param1,
                        _In_z_ const MI_Char * param2,
                        _In_z_ const MI_Char * param3,
                        _In_z_ const MI_Char * param4
                        )
{
    BOOL errorInitialized = FALSE;
    Intlstr resIntlstr = Intlstr_Null;

    GetResourceString4Param(errorStringId, param1, param2, param3, param4, &resIntlstr);
    if( resIntlstr.str )
    {
        MI_Utilities_CimErrorFromErrorCode((MI_Uint32)result, MI_RESULT_TYPE_MI, resIntlstr.str, cimErrorDetails);
        strncpy(g_currentError, resIntlstr.str, 5000);
        DSC_EventWriteCIMError(resIntlstr.str,(MI_Uint32)result);
        errorInitialized = TRUE;
        Intlstr_Free(resIntlstr);
    }
    if(!errorInitialized)
    {
        MI_Utilities_CimErrorFromErrorCode((MI_Uint32)result, MI_RESULT_TYPE_MI, NULL, cimErrorDetails);

    }
    return result;
}

MI_Result AppendWMIErrorWithResourceID(
                        _Inout_ MI_Instance *cimErrorDetails,
                        _In_z_ const MI_Char * resourceId
                        )
{
    MI_Result r = MI_RESULT_OK;
    Intlstr intlstr = Intlstr_Null;
    MI_Value value;

    r = DSC_MI_Instance_GetElement(cimErrorDetails, MSFT_WMIERROR_MESSAGE, &value, NULL, NULL, NULL);
    if( r == MI_RESULT_OK )
    {
        GetResourceString2Param(ID_CA_MOVETODESIREDSTATE_FAILED_APPEND_RESOURCEID, value.string, resourceId, &intlstr);
        if( intlstr.str)
        {
            value.string = (MI_Char*)intlstr.str;
            r = MI_Instance_SetElement(cimErrorDetails, MSFT_WMIERROR_MESSAGE, &value, MI_STRING, 0);
            Intlstr_Free(intlstr);
        }
    }
    return r;
}

MI_Result ResolvePath(_Outptr_opt_result_maybenull_z_ MI_Char **envResolvedPath,
                      _Outptr_opt_result_maybenull_z_ MI_Char **searchPath,
                      _In_z_ const MI_Char *envPath,
                      _In_z_ const MI_Char *searchPattern,
                      _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Uint32 dwReturnSizeInitial = 0, dwReturnSize = (MI_Uint32) (Tcslen(envPath)+1);
    int result = 0;
    const MI_Char *pathToUse = envPath;

    if (extendedError == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *extendedError = NULL;  // Explicitly set *extendedError to NULL as _Outptr_ requires setting this at least once.

    if( searchPath)
    {
        *searchPath = NULL;
    }

    if( envResolvedPath != NULL )
    {
        dwReturnSizeInitial = Tcslen(envPath) + 1;

        *envResolvedPath = (MI_Char*)DSC_malloc(dwReturnSizeInitial* sizeof(MI_Char), NitsHere());
        if( *envResolvedPath == NULL)
        {
            return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, extendedError, ID_LCMHELPER_MEMORY_ERROR);
        }

        memcpy(*envResolvedPath, envPath, dwReturnSizeInitial * sizeof(MI_Char));
        pathToUse = *envResolvedPath;
    }

    if( searchPath != NULL)
    {
        dwReturnSize += (MI_Uint32) (Tcslen(searchPattern) + 1);// %s\\%s

        /* Create Search Path*/
        *searchPath = (MI_Char*)DSC_malloc(dwReturnSize* sizeof(MI_Char), NitsHere()); // %s\\%s
        if( *searchPath == NULL)
        {
            if( envResolvedPath != NULL )
            {
                DSC_free(*envResolvedPath);
                *envResolvedPath = NULL;
            }
            return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, extendedError, ID_LCMHELPER_MEMORY_ERROR);
        }
        result = Stprintf(*searchPath, dwReturnSize, MI_T("%T/%T"), pathToUse, searchPattern);

        if( result <= 0 || NitsShouldFault(NitsHere(), NitsAutomatic))
        {
            if( envResolvedPath != NULL )
            {
                DSC_free(*envResolvedPath);
                *envResolvedPath = NULL;
            }

            DSC_free(*searchPath);
            return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_LCMHELPER_PRINTF_ERROR);
        }
    }
    return MI_RESULT_OK;
}

void SetJobId()
{
    MI_Char *palUuid;
    if(g_ConfigurationDetails.hasSetDetail==MI_TRUE)
    {
        return; //Which means details were set before.
    }

    palUuid = Generate_UUID(  NitsMakeCallSite(-3, NULL, NULL, 0) );
    if(palUuid == NULL)
    {
        return;
    }
    memcpy(g_ConfigurationDetails.jobGuidString, palUuid, JOB_UUID_LENGTH);
    g_ConfigurationDetails.jobGuidString[36] = '\0';
    PAL_Free(palUuid);
    g_ConfigurationDetails.hasSetDetail=MI_TRUE;
}
void ResetJobId()
{
    g_ConfigurationDetails.hasSetDetail=MI_FALSE;
}

void CleanUpClassCache(_Inout_ MI_ClassA *miClassArray)
{
    MI_Uint32 xCount = 0;
    if( miClassArray == NULL || miClassArray->size == 0 )
    {
        return;
    }
    for(xCount = 0; xCount < miClassArray->size; xCount++)
    {
        MI_Class_Delete(miClassArray->data[xCount]);
    }
    DSC_free(miClassArray->data);
    miClassArray->data = 0;
    miClassArray->size = 0;
}

void CleanUpInstanceCache(_Inout_ MI_InstanceA *instanceArray)
{
    MI_Uint32 xCount = 0;
    if( instanceArray == NULL || instanceArray->size == 0 )
    {
        return;
    }
    for(xCount = 0; xCount < instanceArray->size; xCount++)
    {
        MI_Instance_Delete(instanceArray->data[xCount]);
    }
    DSC_free(instanceArray->data);
    instanceArray->data = 0;
    instanceArray->size = 0;
}

void InitLocTable()
{
    // Sort the table based on locId.
    MI_Uint32 xCount, yCount;
    Loc_Mapping tempMapping;
    for(xCount = 0; xCount < g_LocMappingTableSize; xCount++)
    {
        for( yCount = xCount+1; yCount < g_LocMappingTableSize; yCount++)
        {
            if( g_LocMappingTable[xCount].locId > g_LocMappingTable[yCount].locId )
            {
                //swap
                memcpy(&tempMapping, &g_LocMappingTable[xCount], sizeof(Loc_Mapping));
                memcpy(&g_LocMappingTable[xCount], &g_LocMappingTable[yCount], sizeof(Loc_Mapping));
                memcpy(&g_LocMappingTable[yCount], &tempMapping,sizeof(Loc_Mapping));
            }
        }
    }
}

int Get_LocMappingIndex(_In_ MI_Uint32 errorStringId)
{
    MI_Uint32 high = g_LocMappingTableSize-1;
    MI_Uint32 low = 0;
    while( low<=high)
    {
        MI_Uint32 mid = (low+high)/2;
        if( errorStringId < g_LocMappingTable[mid].locId )
        {
            high = mid - 1;
        }
        else if( errorStringId > g_LocMappingTable[mid].locId )
        {
            low = mid+1;
        }
        else
        {
            return mid;
        }

    }
    return -1;
}

/*
    The current code base uses string ids that are not defined in g_locMappingTable.  To avoid
    runtime crashes, use placeholder strings until all string ids are fixed.
    See strings.h for g_UndefinedMessageTable which contains generic format messages
    the includes the message id and up to 3 of the error message parameter values.
*/
extern Loc_Mapping g_UndefinedMessageTable[];
#define ERROR_ID_SIZE 10

void GetUndefinedString(_In_ MI_Uint32 errorStringId, _Inout_ Intlstr *resStr)
{
    MI_Char id[ERROR_ID_SIZE];
    Stprintf(id,ERROR_ID_SIZE,MI_T("%d"), errorStringId);
    *resStr = g_UndefinedMessageTable[0].LocFunctionOneArgs(id);
}

void GetResourceString( _In_ MI_Uint32 errorStringId, _Inout_ Intlstr *resStr)
{
   int index = Get_LocMappingIndex(errorStringId);
   if( index >= 0 )
   {
       *resStr = g_LocMappingTable[index].LocFunctionZeroArgs();
   }
   else
   {
       GetUndefinedString(errorStringId, resStr);
   }
}

void GetResourceString1Param( _In_ MI_Uint32 errorStringId, _In_z_ const MI_Char * param1, _Inout_ Intlstr *resStr)
{
   int index = Get_LocMappingIndex(errorStringId);
   if( index >= 0 )
   {
       *resStr = g_LocMappingTable[index].LocFunctionOneArgs((MI_Char*)param1);
   }
   else
   {
       GetUndefinedString(errorStringId, resStr);
   }
}

void GetResourceString2Param( _In_ MI_Uint32 errorStringId, _In_z_ const MI_Char * param1,
                               _In_z_ const MI_Char * param2,_Inout_ Intlstr *resStr)
{
   int index = Get_LocMappingIndex(errorStringId);
   if( index >= 0 )
   {
       *resStr = g_LocMappingTable[index].LocFunctionTwoArgs((MI_Char*)param1, (MI_Char*)param2);
   }
   else
   {
       GetUndefinedString(errorStringId, resStr);
   }
}
 
void GetResourceString3Param( _In_ MI_Uint32 errorStringId, _In_z_ const MI_Char * param1,
               _In_z_ const MI_Char * param2, _In_z_ const MI_Char * param3,_Inout_ Intlstr *resStr)
{
   int index = Get_LocMappingIndex(errorStringId);
   if( index >= 0 )
   {
       *resStr = g_LocMappingTable[index].LocFunctionThreeArgs((MI_Char*)param1, (MI_Char*)param2, (MI_Char*)param3);
   }
   else
   {
       GetUndefinedString(errorStringId, resStr);
   }
}

void GetResourceString4Param( _In_ MI_Uint32 errorStringId, _In_z_ const MI_Char * param1,
               _In_z_ const MI_Char * param2, _In_z_ const MI_Char * param3,  _In_z_ const MI_Char * param4, _Inout_ Intlstr *resStr)
{
   int index = Get_LocMappingIndex(errorStringId);
   if( index >= 0 )
   {
       *resStr = g_LocMappingTable[index].LocFunctionFourArgs((MI_Char*)param1, (MI_Char*)param2, (MI_Char*)param3, (MI_Char*)param4);
   }
   else
   {
       GetUndefinedString(errorStringId, resStr);
   }
}

void CleanUpDeserializerClassCache(_Inout_ MI_ClassA *miClassArray)
{
    if( miClassArray == NULL || miClassArray->size == 0 )
    {
        return;
    }

    MI_Deserializer_ReleaseClassArray(miClassArray);
}

void CleanUpDeserializerInstanceCache(_Inout_ MI_InstanceA *instanceArray)
{
    if( instanceArray == NULL || instanceArray->size == 0 )
    {
        return;
    }

    MI_Deserializer_ReleaseInstanceArray(instanceArray);
}

/*caller will cleanup inputInstanceArray and outputInstanceArray*/
MI_Result UpdateInstanceArray(_In_ MI_InstanceA *inputInstanceArray,
                         _Inout_ MI_InstanceA *outputInstanceArray,
                        _Outptr_result_maybenull_ MI_Instance **extendedError,
                        _In_ MI_Boolean bInputUsingSerializedAPI)
{
    MI_Uint32 xCount = 0;
    size_t newSize = inputInstanceArray->size + outputInstanceArray->size;
    // Fail if newSize would be truncated later on
    if ( newSize > 0xFFFFFFFF )
    {
        return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, extendedError, ID_ENGINEHELPER_MEMORY_ERROR);
    }

    MI_Instance **tempOutput = NULL;

    if (extendedError == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *extendedError = NULL;  // Explicitly set *extendedError to NULL as _Outptr_ requires setting this at least once.

    if( inputInstanceArray->size == 0 )
    {
        return MI_RESULT_OK;
    }
    tempOutput = (MI_Instance **)DSC_malloc( newSize * sizeof(MI_Instance*), NitsHere());
    if(tempOutput == NULL)
    {
        return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, extendedError, ID_ENGINEHELPER_MEMORY_ERROR);
    }
    /*Copy current list to bigger list*/
    for( xCount = 0 ; xCount < outputInstanceArray->size; xCount++)
    {
        tempOutput[xCount] = outputInstanceArray->data[xCount];
    }
    /*Copy new list to bigger list*/
    for( xCount = outputInstanceArray->size ; xCount < newSize; xCount++)
    {
        tempOutput[xCount] = inputInstanceArray->data[xCount - outputInstanceArray->size];
        inputInstanceArray->data[xCount - outputInstanceArray->size] = NULL;
    }

    /*Free memory and update pointers*/
    if( bInputUsingSerializedAPI)
        MI_Deserializer_ReleaseInstanceArray(inputInstanceArray);
    else
        DSC_free(inputInstanceArray->data);

    DSC_free(outputInstanceArray->data);
    outputInstanceArray->data = tempOutput;
    outputInstanceArray->size = newSize;
    return MI_RESULT_OK;
}

void CleanUpGetCache(_Inout_ MI_InstanceA *instanceArray)
{
    if( instanceArray == NULL )
    {
        return;
    }

    MI_Deserializer_ReleaseInstanceArray(instanceArray);
}



/*caller will cleanup inputClassArray and outputClassArray*/
MI_Result UpdateClassArray(_In_ MI_ClassA *inputClassArray,
                           _Inout_ MI_ClassA *outputClassArray,
                           _Outptr_result_maybenull_ MI_Instance **extendedError,
                           _In_ MI_Boolean bInputUsingSerializedAPI)
{
    MI_Uint32 xCount = 0;
    size_t newSize = inputClassArray->size + outputClassArray->size;
    // Fail if newSize would be truncated later on
    if ( newSize > 0xFFFFFFFF )
    {
        return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, extendedError, ID_ENGINEHELPER_MEMORY_ERROR);
    }

    MI_Class **tempOutput = NULL;

    if (extendedError == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *extendedError = NULL;  // Explicitly set *extendedError to NULL as _Outptr_ requires setting this at least once.

    if( inputClassArray->size == 0 )
    {
        return MI_RESULT_OK;
    }
    tempOutput = (MI_Class **)DSC_malloc( newSize * sizeof(MI_Class*), NitsHere());
    if( tempOutput == NULL)
    {
        return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, extendedError, ID_ENGINEHELPER_MEMORY_ERROR);
    }
    /*Copy current list to bigger list*/
    for( xCount = 0 ; xCount < outputClassArray->size; xCount++)
    {
        tempOutput[xCount] = outputClassArray->data[xCount];
    }
    /*Copy new list to bigger list*/
    for( xCount = outputClassArray->size ; xCount < newSize; xCount++)
    {
        tempOutput[xCount] = inputClassArray->data[xCount - outputClassArray->size];
        inputClassArray->data[xCount - outputClassArray->size] = NULL;
    }

    /*Update pointers*/
    if( bInputUsingSerializedAPI)
        MI_Deserializer_ReleaseClassArray(inputClassArray);
    else
        DSC_free(inputClassArray->data);

    DSC_free(outputClassArray->data);
    outputClassArray->data = tempOutput;
    outputClassArray->size = newSize;
    return MI_RESULT_OK;

}

const MI_Char * GetDownloadManagerName( _In_ MI_Instance *inst)
{
    MI_Result r = MI_RESULT_OK;
    MI_Value value;
    r = MI_Instance_GetElement(inst, MSFT_DSCMetaConfiguration_DownloadManagerName, &value, NULL, NULL, NULL);
    if( r != MI_RESULT_OK)
    {
        return NULL;
    }
    return (const MI_Char*)value.string;
}

const MI_Char * GetErrorDetail( _In_ MI_Instance *inst)
{
    MI_Result r = MI_RESULT_OK;
    MI_Value value;
    r = MI_Instance_GetElement(inst, MI_T("Message"), &value, NULL, NULL, NULL);
    if( r != MI_RESULT_OK)
    {
        return NULL;
    }
    return (const MI_Char*)value.string;
}


/* caller will need to release the memory  for content buffer */
MI_Result ReadFileContent(_In_z_ const MI_Char *pFileName,
                          _Outptr_result_buffer_maybenull_(*pBufferSize) MI_Uint8 ** pBuffer,
                          _Out_ MI_Uint32 * pBufferSize,
                          _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    FILE *fp;
    size_t result;
    unsigned long fileLen;
    size_t nSizeWritten;

    *pBuffer = 0;
    *pBufferSize = 0;
    fp = File_OpenT(pFileName, MI_T("rb"));
    if( fp == NULL)
    {
        return GetCimMIError(MI_RESULT_FAILED, cimErrorDetails, ID_ENGINEHELPER_OPENFILE_ERROR);
    }

    // Get File size
    result = fseek(fp, 0, SEEK_END);
    if(result)
    {
        File_Close(fp);
        return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_ENGINEHELPER_FILESIZE_ERROR);
    }
    fileLen = ftell(fp);
    if(fileLen > MAX_MOFSIZE )
    {
        File_Close(fp);
        return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_ENGINEHELPER_FILESIZE_ERROR);
    }
    result = fseek(fp, 0, SEEK_SET);
    if(result)
    {
        File_Close(fp);
        return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_ENGINEHELPER_FILESIZE_ERROR);
    }

    *pBuffer = (MI_Uint8*)DSC_malloc(fileLen, NitsHere());
    if(*pBuffer == NULL)
    {
        File_Close(fp);
        return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_ENGINEHELPER_FILESIZE_ERROR);
    }

    // Read the contents
    nSizeWritten = fread(*pBuffer, 1, fileLen, fp);

    File_Close(fp);
    if( nSizeWritten != fileLen)
    {
        DSC_free(*pBuffer);
        *pBuffer = NULL;
        return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_ENGINEHELPER_READFILE_ERROR);
    }
    *pBufferSize = (MI_Uint32)fileLen;
    return MI_RESULT_OK;
}

void CleanupTempDirectory(_In_z_ MI_Char *mofFileName)
{
    /*It is ok if we fail to cleanup temp directory.*/
    MI_Char *lastOccurancePointer = Tcsrchr(mofFileName, MI_T('/'));

    if( lastOccurancePointer != NULL )
    {
        size_t lastOccuranceIndex = 0;
        lastOccuranceIndex = lastOccurancePointer - mofFileName;
        if( Tcslen(mofFileName) > lastOccuranceIndex+1 )
        {
            mofFileName[lastOccuranceIndex] =  MI_T('\0');
            RecursivelyDeleteDirectory(mofFileName);
        }
    }
}

MI_Boolean IsConfirmUsed(_In_opt_ MI_Context* context)
{
    MI_Result result=MI_RESULT_OK;
    MI_Type type;
    MI_Value value;
    MI_Boolean confirmUsedFlag=MI_FALSE;
    //In the case of CIM cmdlets, the option gets set at a particular index whenever whatif is called.
    if(context)
    {
        result= MI_Context_GetCustomOptionAt(context,DSC_INDEX_OPTION_CONFIRM,NULL,&type,&value);

        if(result==MI_RESULT_OK && type==MI_SINT32 && value.sint32 == DSC_CONFIRMOPTION_SET_VALUE)
        {
            confirmUsedFlag=MI_TRUE;
        }
    }
    return confirmUsedFlag;

}

void RecursivelyDeleteDirectory(_In_z_ MI_Char *directoryPath)
{
    MI_Char pathTempVar[MAX_PATH];
    Internal_Dir *dirHandle = NULL;
    Internal_DirEnt *dirEntry = NULL;
    dirHandle = Internal_Dir_Open(directoryPath, NitsMakeCallSite(-3, NULL, NULL, 0) );
    if( dirHandle != NULL )
    {
        dirEntry =  Internal_Dir_Read(dirHandle, NULL);

        while (dirEntry != NULL )
        {
            if(Stprintf(pathTempVar, MAX_PATH, MI_T("%T/%T"), directoryPath, dirEntry->name) >0 )
            {
                if(Tcscasecmp(MI_T(".."), dirEntry->name) == 0 ||
                   Tcscasecmp(MI_T("."), dirEntry->name)==0 )
                {
                }
                else if(dirEntry->isDir)
                {
                    RecursivelyDeleteDirectory(pathTempVar);
                }
                else
                {
                    File_RemoveT(pathTempVar);
                }
            }
            dirEntry =  Internal_Dir_Read(dirHandle, NULL); //Next
        }

        Internal_Dir_Close( dirHandle);
    }
    Directory_Remove(directoryPath);
}

void SQMLogResourceCountData(_In_z_ const MI_Char* providerName,_In_ MI_Uint32 resourceCount)
{
    // Nothing to do in Linux
}

// Method also frees memory of pTempStr
void DSC_WriteWarningHelper(
    _In_ LCMProviderContext* lcmContext,
    _In_ Intlstr pTempStr)
{
    if (pTempStr.str == NULL) return;

    LCM_WriteMessage_Internal_Tokenized(lcmContext, EMPTY_STRING, MI_WRITEMESSAGE_CHANNEL_WARNING, pTempStr.str, MI_FALSE);
    DSC_EventWriteCUMethodWarning(pTempStr.str);
    Intlstr_Free(pTempStr);
}

void DSC_WriteWarning(
    _In_ LCMProviderContext* lcmContext,
    _In_ MI_Uint32 messageID)
{
    Intlstr pTempStr = Intlstr_Null;
    GetResourceString(messageID, &pTempStr);

    DSC_WriteWarningHelper(lcmContext, pTempStr);
}

void DSC_WriteWarning1Param(_In_ MI_Context* context,
                      _In_ MI_Uint32 messageID,
                      _In_z_ MI_Char* param1)
{

    Intlstr pTempStr = Intlstr_Null;
    GetResourceString1Param(messageID, param1, &pTempStr);
    if (pTempStr.str)
    {
#if !defined(BUILD_OMS)
        MI_Context_WriteWarning(context, pTempStr.str);
#endif
        DSC_EventWriteCUMethodWarning(pTempStr.str);
        Intlstr_Free(pTempStr);
    }
}

void DSC_WriteWarning2Param(_In_ MI_Context* context,
    _In_ MI_Uint32 messageID,
    _In_z_ MI_Char* param1,
    _In_z_ MI_Char* param2)
{

    Intlstr pTempStr = Intlstr_Null;
    GetResourceString2Param(messageID, param1, param2, &pTempStr);
    if (pTempStr.str)
    {
#if !defined(BUILD_OMS)
        MI_Context_WriteWarning(context, pTempStr.str);
#endif
        DSC_EventWriteCUMethodWarning(pTempStr.str);
        Intlstr_Free(pTempStr);
    }
}

void DSC_WriteWarningFromError1Param(_In_ MI_Context* context,
    _In_ MI_Instance ** cimErrorDetails,
    _In_ MI_Uint32 messageID,
    _In_z_ MI_Char* param1)
{

    Intlstr pTempStr = Intlstr_Null;
    MI_Result r = MI_RESULT_OK;
    MI_Value value;
    //First get the error message, which will be the second parameter of the warnings
    if (cimErrorDetails != NULL && *cimErrorDetails != NULL)
    {
        r = DSC_MI_Instance_GetElement(*cimErrorDetails, MSFT_WMIERROR_MESSAGE, &value, NULL, NULL, NULL);
        if (r == MI_RESULT_OK)
        {
            GetResourceString2Param(messageID, param1, value.string, &pTempStr);
            if (pTempStr.str)
            {
#if !defined(BUILD_OMS)
                MI_Context_WriteWarning(context, pTempStr.str);
#endif
                DSC_EventWriteCUMethodWarning(pTempStr.str);
                Intlstr_Free(pTempStr);
            }
        }
    }
}

/*Function that returns true if the instance contains a defined value for the property passed in*/
MI_Boolean InstanceContainsProperty(_In_ MI_Instance* instance,
    _In_z_ const MI_Char* property)
{
    MI_Value value;
    MI_Result r;
    MI_Uint32 flags;
    if (instance == NULL || property == NULL)
        return MI_RESULT_INVALID_PARAMETER;
    r = DSC_MI_Instance_GetElement(instance, property, &value, NULL, &flags, NULL);
    if (r != MI_RESULT_OK)
        return MI_FALSE;
    //Check if the property is defined
    if (flags & MI_FLAG_NULL)
    {
        return MI_FALSE;
    }
    return MI_TRUE;
}

MI_Result ExpandPath(_In_z_ const MI_Char * pathIn,
    _Outptr_result_maybenull_z_ MI_Char **expandedPath,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails);

/*Function to validate if a directory has files.*/
MI_Result ValidateDirectoryHasFiles(_In_z_ const MI_Char* directoryName)
{
    MI_Result result;
    MI_Char *pwConfigDir = NULL;
    MI_Instance* cimErrorDetails = NULL;
    Internal_Dir *dirHandle = NULL;
    Internal_DirEnt *dirEntry = NULL;
    result = ExpandPath(directoryName, &pwConfigDir, &cimErrorDetails);
    RETURN_RESULT_IF_FAILED(result); //Result is being written by expand path, so just returning it as is

    if (Directory_ExistT(pwConfigDir) == -1)
    {
        DSC_free(pwConfigDir);
        return MI_RESULT_NOT_FOUND;
    }
    dirHandle = Internal_Dir_Open(pwConfigDir, NitsMakeCallSite(-3, NULL, NULL, 0));
    DSC_free(pwConfigDir);
    if (dirHandle != NULL)
    {
        dirEntry = Internal_Dir_Read(dirHandle, MOF_EXTENSION);
        if (dirEntry != NULL)
        {
            //That means the first file was got, and it contains files.
            Internal_Dir_Close(dirHandle);
            return MI_RESULT_OK;
        }
    }
    return MI_RESULT_NOT_FOUND;
}


/*Function to return true if we should use partial configurations instead of full. If shouldCheckPartialConfigDirectoryForFiles is set to true, then it will also check for non empty directory status before returning*/
MI_Boolean ShouldUsePartialConfigurations(_In_ MI_Instance* metaConfigInstance,
    _In_ MI_Boolean shouldCheckPartialConfigDirectoryForFiles)
{
    if (InstanceContainsProperty(metaConfigInstance, MSFT_DSCMetaConfiguration_PartialConfigurations))
    {
        if (shouldCheckPartialConfigDirectoryForFiles)
        {
            //Check for partial config directory, if it has files
            if (ValidateDirectoryHasFiles(CONFIGURATION_PARTIALCONFIG_STORE) == MI_RESULT_OK)
            {
                return MI_TRUE;
            }

        }
        else
        {
            return MI_TRUE;
        }
    }
    return MI_FALSE;
}

/*Function to concatenate two strings - Must free the target string after use*/
MI_Result DSCConcatStrings(_Outptr_result_z_ MI_Char** target, _In_ MI_Uint32 padding, _In_z_ const MI_Char* source1, _In_z_ const MI_Char* source2, _In_z_ const MI_Char* pattern)
{
    MI_Result result = MI_RESULT_FAILED;
    *target = NULL;
    if (source1 && source2)
    {
        size_t targetLength = Tcslen(source1) + Tcslen(source2) + 1 + padding; //+1 for "\0"
        *target = (MI_Char*)DSC_malloc(targetLength*sizeof(MI_Char), NitsMakeCallSite(-3, NULL, NULL, 0));
        if (*target != NULL)
        {
            if (Stprintf(*target, targetLength, pattern, source1, source2) <= 0)
            {
                DSC_free(*target);
                *target = NULL;
            }
            else
            {
                result = MI_RESULT_OK;
            }
        }
    }
    return result;
}

MI_Datetime PalDatetimeToMiDatetime(_In_ PAL_Datetime inDatetime)
{
    MI_Datetime outDatetime;

    outDatetime.isTimestamp = inDatetime.isTimestamp == 0 ? 0 : 1;

    outDatetime.u.timestamp.year = (MI_Uint32)inDatetime.u.timestamp.year;
    outDatetime.u.timestamp.month = (MI_Uint32)inDatetime.u.timestamp.month;
    outDatetime.u.timestamp.day = (MI_Uint32)inDatetime.u.timestamp.day;
    outDatetime.u.timestamp.hour = (MI_Uint32)inDatetime.u.timestamp.hour;
    outDatetime.u.timestamp.minute = (MI_Uint32)inDatetime.u.timestamp.minute;
    outDatetime.u.timestamp.second = (MI_Uint32)inDatetime.u.timestamp.second;
    outDatetime.u.timestamp.microseconds = (MI_Uint32)inDatetime.u.timestamp.microseconds;
    outDatetime.u.timestamp.utc = (MI_Uint32)inDatetime.u.timestamp.utc;
    
    return outDatetime;
}



MI_Result UpdateMetaConfigWithAgentId(
    _In_z_ MI_Char *agentId,
    _Inout_ MI_Instance *metaConfigInstance)
{
    MI_Result r = MI_RESULT_OK;
    MI_Value value;
    // set default value
    value.string = agentId;

    r = MI_Instance_SetElement(metaConfigInstance, MSFT_DSCMetaConfiguration_AgentId, &value, MI_STRING, 0);
    if (r != MI_RESULT_OK)
    {
        return r;
    }

    return MI_RESULT_OK;
}


MI_Result StripBracesFromGuid(
    _In_z_ MI_Char* inputGuid,
    _Outptr_result_maybenull_z_ MI_Char** resultGuid, 
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    size_t resultGuidLength = 0;
    int retValue;
    MI_Char* token = NULL;
    MI_Char* next_token = NULL;

    resultGuidLength = Tcslen(inputGuid) - 2 /* For removing { and } */ + 1 /* For \0 */;
    *resultGuid = (MI_Char*)DSC_malloc(resultGuidLength * sizeof(MI_Char), NitsHere());

    token = Tcstok(inputGuid, "{", &next_token);
    // At this point, token = 77762b23-8e52-4610-afa7-e480f7f18684}
    // We write everything except the last "}"    
    retValue = Stprintf(*resultGuid, resultGuidLength + 1, MI_T("%T"), token);
    if (retValue == -1 || NitsShouldFault(NitsHere(), NitsAutomatic))
    {
        DSC_free(*resultGuid);
    }
    (*resultGuid)[resultGuidLength - 1] = MI_T('\0');

EH_UNWIND;
        return MI_RESULT_OK;
}

MI_Result ShouldUseV1Protocol(
    _Inout_ MI_Boolean* isV1MetaConfig)
{
    MI_Result result = MI_RESULT_OK;
    MI_Value value;
    MI_Uint32 flags;

    result = DSC_MI_Instance_GetElement((MI_Instance*)g_metaConfig, MSFT_DSCMetaConfiguration_ConfigurationID, &value, NULL, &flags, NULL);
    if (result == MI_RESULT_OK && !(flags & MI_FLAG_NULL) && (Tcscasecmp(MI_T(""), value.string) != 0))
    {
        *isV1MetaConfig = MI_TRUE;
    }

    return result;
}


MI_Result GetAgentInformation(
    _Inout_ MI_Instance** registrationPayload)
{
    MI_Application miApp = MI_APPLICATION_NULL;
//    NetworkInformation networkInformation = { 0 };
    MI_Result result = MI_RESULT_OK;
    /* TODO: used in below implementation
    MI_Boolean applicationInitialized = MI_FALSE;
    MI_Value value;
    MI_Char *ipAddress = NULL;
    MI_Uint32 count = 0;
    */
    if (g_metaConfig == NULL)
    {
        assert(1);
        return MI_RESULT_FAILED;
    }
    result = DSC_MI_Application_Initialize(0, NULL, NULL, &miApp);
    EH_CheckResult(result);

    result = DSC_MI_Application_NewInstance(&miApp, AGENT_REGISTRATION_CLASS, NULL, registrationPayload);
    EH_CheckResult(result);
/* TODO: implement these
    applicationInitialized = MI_TRUE;

    // Set IPAddress 
    result = GetIPAndMacAddresses(lcmContext, &networkInformation);
    // TODO : TO be uncommented afer Jane's fix for MSFT:2004179 FIs to REL
    //EH_CheckResult(result);    

    result = GetIpAddressesInStringFormat(networkInformation.ipV4Addresses, networkInformation.ipV4Count, networkInformation.ipV6Addresses, networkInformation.ipV6Count, &ipAddress);
    EH_CheckResult(result);

    value.string = ipAddress;
    result = MI_Instance_AddElement(*registrationPayload, REPORTING_IPADDRESS, &value, MI_STRING, 0);
    EH_CheckResult(result);    

    // Set HostName
    value.string = g_JobInformation.deviceName;
    result = MI_Instance_AddElement(*registrationPayload, REPORTING_NODENAME, &value, MI_STRING, 0);
    EH_CheckResult(result);

    // Set LCMVersion
    value.string = LCM_CURRENT_VERSION;
    result = MI_Instance_AddElement(*registrationPayload, REPORTING_LCMVERSION, &value, MI_STRING, 0);
    EH_CheckResult(result);

    EH_UNWIND;
    if (applicationInitialized == MI_TRUE)
    {
        MI_Application_Close(&miApp);
        applicationInitialized = MI_FALSE;
    }
    for (count = 0; count < networkInformation.ipV4Count; count++)
    {
        DSCFREE_IF_NOT_NULL(networkInformation.ipV4Addresses[count]);
    }
    DSCFREE_IF_NOT_NULL(networkInformation.ipV4Addresses);
    for (count = 0; count < networkInformation.ipV6Count; count++)
    {
        DSCFREE_IF_NOT_NULL(networkInformation.ipV6Addresses[count]);
    }
    DSCFREE_IF_NOT_NULL(networkInformation.ipV6Addresses);
    for (count = 0; count < networkInformation.macCount; count++)
    {
        DSCFREE_IF_NOT_NULL(networkInformation.macAddresses[count]);
    }
    DSCFREE_IF_NOT_NULL(networkInformation.macAddresses);
    networkInformation.ipV4Count = 0;
    networkInformation.ipV6Count = 0;
    networkInformation.macCount = 0;
    networkInformation.addressCount = 0;

    DSC_free(ipAddress);
*/
EH_UNWIND;
    MI_Application_Close(&miApp);
    /* applicationInitialized = MI_FALSE; */
    return result;
}

MI_Char* DSC_strdup(MI_Char* s)
{
    MI_Char* result;
    size_t s_len;

    if (s == NULL)
    {
        return NULL;
    }
    
    s_len = strlen(s);
    result = DSC_malloc((s_len + 1) * sizeof(MI_Char), NitsHere());
    memcpy(result, s, s_len);
    result[s_len] = '\0';
    return result;
}

StatusReport_ResourceNotInDesiredState * Construct_StatusReport_RNIDS(
    char* SourceInfo,
    char* ModuleName,
    char* DurationInSeconds,
    char* InstanceName,
    char* StartDate,
    char* ResourceName,
    char* ModuleVersion,
    char* RebootRequested,
    char* ResourceId,
    char* ConfigurationName,
    char* InDesiredState
    )
{
    StatusReport_ResourceNotInDesiredState * ptr = (StatusReport_ResourceNotInDesiredState *) DSC_malloc(sizeof(StatusReport_ResourceNotInDesiredState), NitsHere());
    ptr->SourceInfo = DSC_strdup(SourceInfo);
    ptr->ModuleName = DSC_strdup(ModuleName);
    ptr->DurationInSeconds = DSC_strdup(DurationInSeconds);
    ptr->InstanceName = DSC_strdup(InstanceName);
    ptr->StartDate = DSC_strdup(StartDate);
    ptr->ResourceName = DSC_strdup(ResourceName);
    ptr->ModuleVersion = DSC_strdup(ModuleVersion);
    ptr->RebootRequested = DSC_strdup(RebootRequested);
    ptr->ResourceId = DSC_strdup(ResourceId);
    ptr->ConfigurationName = DSC_strdup(ConfigurationName);
    ptr->InDesiredState = DSC_strdup(InDesiredState);
    return ptr;
}

void Destroy_StatusReport_RNIDS(StatusReport_ResourceNotInDesiredState* ptr)
{
    if (ptr == NULL)
        return;
    
    if (ptr->SourceInfo != NULL)
        DSC_free(ptr->SourceInfo);
    if (ptr->ModuleName != NULL)
        DSC_free(ptr->ModuleName);
    if (ptr->DurationInSeconds != NULL)
        DSC_free(ptr->DurationInSeconds);
    if (ptr->InstanceName != NULL)
        DSC_free(ptr->InstanceName);
    if (ptr->StartDate != NULL)
        DSC_free(ptr->StartDate);
    if (ptr->ResourceName != NULL)
        DSC_free(ptr->ResourceName);
    if (ptr->ModuleVersion != NULL)
        DSC_free(ptr->ModuleVersion);
    if (ptr->RebootRequested != NULL)
        DSC_free(ptr->RebootRequested);
    if (ptr->ResourceId != NULL)
        DSC_free(ptr->ResourceId);
    if (ptr->ConfigurationName != NULL)
        DSC_free(ptr->ConfigurationName);
    if (ptr->InDesiredState != NULL)
        DSC_free(ptr->InDesiredState);

}

MI_Result  Convert_MIInstance_JSON (
        const MI_Instance* p_instance,
        JSON_Value** p_result_root_value
    )
{
    MI_Result result = MI_RESULT_OK;

    *p_result_root_value = json_value_init_object();
    JSON_Object *result_root_object = json_value_get_object(*p_result_root_value);

    if (p_instance == NULL || p_instance->classDecl == NULL)
    {
        return result;
    }

    MI_Uint32 i = 0;
    for(i = 0 ; i < p_instance->classDecl->numProperties; i++)
    {
        MI_Value value;
        MI_Type type;
        MI_Uint32 flags;
        const MI_Char *name;
        MI_Result r = MI_RESULT_OK;
        r = MI_Instance_GetElementAt(p_instance, i, &name, &value, &type, &flags);
        if( r == MI_RESULT_OK)
        {
            if( flags & MI_FLAG_NULL)
            {
                json_object_set_null(result_root_object, p_instance->classDecl->properties[i]->name);
            }
            else
            {
                switch(type) {
                    case MI_BOOLEAN :{
                        json_object_set_boolean(result_root_object, p_instance->classDecl->properties[i]->name, value.boolean);
                        break;
                    }
                    case MI_UINT8 : {
                        json_object_set_number(result_root_object, p_instance->classDecl->properties[i]->name, value.uint8);
                        break;
                    }
                    case MI_UINT16 :{
                        json_object_set_number(result_root_object, p_instance->classDecl->properties[i]->name, value.uint16);
                        break;
                    }
                    case MI_UINT32 : {
                        json_object_set_number(result_root_object, p_instance->classDecl->properties[i]->name, value.uint32);
                        break;
                    }
                    case MI_UINT64 : {
                        json_object_set_number(result_root_object, p_instance->classDecl->properties[i]->name, value.uint64);
                        break;
                    }
                    case MI_SINT8 : {
                        json_object_set_number(result_root_object, p_instance->classDecl->properties[i]->name, value.sint8);
                        break;
                    }
                    case MI_SINT16 : {
                        json_object_set_number(result_root_object, p_instance->classDecl->properties[i]->name, value.sint16);
                        break;
                    }
                    case MI_SINT32 : {
                        json_object_set_number(result_root_object, p_instance->classDecl->properties[i]->name, value.sint32);
                        break;
                    }
                    case MI_SINT64 : {
                        json_object_set_number(result_root_object, p_instance->classDecl->properties[i]->name, value.sint64);
                        break;
                    }
                    case MI_REAL32 : {
                        json_object_set_number(result_root_object, p_instance->classDecl->properties[i]->name, value.real32);
                        break;
                    }
                    case MI_REAL64 : {
                        json_object_set_number(result_root_object, p_instance->classDecl->properties[i]->name, value.real64);
                        break;
                    }
                    case MI_CHAR16 : {
                        json_object_set_string(result_root_object, p_instance->classDecl->properties[i]->name, value.char16);
                        break;
                    }
                    case MI_DATETIME : {
                        MI_Datetime val = value.datetime;
                        char buffer[100]; 
                        if (val.isTimestamp)
                        {
                            sprintf(buffer, "%.4d/%.2d%.2d %.2d:%.2d:%.2d:%.2d - %d",
                                val.u.timestamp.year, val.u.timestamp.month, val.u.timestamp.day,
                                val.u.timestamp.hour, val.u.timestamp.minute, val.u.timestamp.second, val.u.timestamp.microseconds,
                                val.u.timestamp.utc);
                        }
                        else
                        {
                            sprintf(buffer, "%.2d:%.2d:%.2d:%.2d:%.6d",
                                val.u.interval.days,
                                val.u.interval.hours,
                                val.u.interval.minutes,
                                val.u.interval.seconds,
                                val.u.interval.microseconds);
                        }
                        json_object_set_string(result_root_object, p_instance->classDecl->properties[i]->name, buffer);
                        break;
                    }
                    case MI_STRING : {
                        json_object_set_string(result_root_object, p_instance->classDecl->properties[i]->name, value.string);
                        break;
                    }
                    case MI_INSTANCE : {
                        JSON_Value *json_value;
                        Convert_MIInstance_JSON(value.instance, &json_value);
                        json_object_set_value(result_root_object, p_instance->classDecl->properties[i]->name, json_value);
                        break;
                    }
                    case MI_BOOLEANA : {
                        MI_Uint32 j = 0;
                        JSON_Value *json_array_value = json_value_init_array();
                        if(json_array_value == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        JSON_Object *json_array_object = json_value_get_object(json_array_value);
                        if(json_array_object == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        
                        for(j = 0; j < value.booleana.size; j++)
                        {
                            json_array_append_boolean(json_array_object, value.booleana.data[j]);
                        }

                        json_object_set_value(result_root_object, p_instance->classDecl->properties[i]->name, json_array_value);
                        break;
                    }
                    case MI_UINT8A : {
                        MI_Uint32 j = 0;
                        JSON_Value *json_array_value = json_value_init_array();
                        if(json_array_value == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        JSON_Object *json_array_object = json_value_get_object(json_array_value);
                        if(json_array_object == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        
                        for(j = 0; j < value.uint8a.size; j++)
                        {
                            json_array_append_number(json_array_object, value.uint8a.data[j]);
                        }

                        json_object_set_value(result_root_object, p_instance->classDecl->properties[i]->name, json_array_value);
                        break;
                    }
                    case MI_UINT16A : {
                        MI_Uint32 j = 0;
                        JSON_Value *json_array_value = json_value_init_array();
                        if(json_array_value == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        JSON_Object *json_array_object = json_value_get_object(json_array_value);
                        if(json_array_object == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        
                        for(j = 0; j < value.uint16a.size; j++)
                        {
                            json_array_append_number(json_array_object, value.uint16a.data[j]);
                        }

                        json_object_set_value(result_root_object, p_instance->classDecl->properties[i]->name, json_array_value);
                        break;
                    }
                    case MI_UINT32A : {
                        MI_Uint32 j = 0;
                        JSON_Value *json_array_value = json_value_init_array();
                        if(json_array_value == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        JSON_Object *json_array_object = json_value_get_object(json_array_value);
                        if(json_array_object == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        
                        for(j = 0; j < value.uint32a.size; j++)
                        {
                            json_array_append_number(json_array_object, value.uint32a.data[j]);
                        }

                        json_object_set_value(result_root_object, p_instance->classDecl->properties[i]->name, json_array_value);
                        break;
                    }
                    case MI_UINT64A : {
                        MI_Uint32 j = 0;
                        JSON_Value *json_array_value = json_value_init_array();
                        if(json_array_value == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        JSON_Object *json_array_object = json_value_get_object(json_array_value);
                        if(json_array_object == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        
                        for(j = 0; j < value.uint64a.size; j++)
                        {
                            json_array_append_number(json_array_object, value.uint64a.data[j]);
                        }

                        json_object_set_value(result_root_object, p_instance->classDecl->properties[i]->name, json_array_value);
                        break;
                    }
                    case MI_SINT8A : {
                        MI_Uint32 j = 0;
                        JSON_Value *json_array_value = json_value_init_array();
                        if(json_array_value == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        JSON_Object *json_array_object = json_value_get_object(json_array_value);
                        if(json_array_object == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        
                        for(j = 0; j < value.sint8a.size; j++)
                        {
                            json_array_append_number(json_array_object, value.sint8a.data[j]);
                        }

                        json_object_set_value(result_root_object, p_instance->classDecl->properties[i]->name, json_array_value);
                        break;
                    }
                    case MI_SINT16A : {
                        MI_Uint32 j = 0;
                        JSON_Value *json_array_value = json_value_init_array();
                        if(json_array_value == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        JSON_Object *json_array_object = json_value_get_object(json_array_value);
                        if(json_array_object == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        
                        for(j = 0; j < value.sint16a.size; j++)
                        {
                            json_array_append_number(json_array_object, value.sint16a.data[j]);
                        }

                        json_object_set_value(result_root_object, p_instance->classDecl->properties[i]->name, json_array_value);
                        break;
                    }
                    case MI_SINT32A : {
                        MI_Uint32 j = 0;
                        JSON_Value *json_array_value = json_value_init_array();
                        if(json_array_value == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        JSON_Object *json_array_object = json_value_get_object(json_array_value);
                        if(json_array_object == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        
                        for(j = 0; j < value.sint32a.size; j++)
                        {
                            json_array_append_number(json_array_object, value.sint32a.data[j]);
                        }

                        json_object_set_value(result_root_object, p_instance->classDecl->properties[i]->name, json_array_value);
                        break;
                    }
                    case MI_SINT64A : {
                        MI_Uint32 j = 0;
                        JSON_Value *json_array_value = json_value_init_array();
                        if(json_array_value == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        JSON_Object *json_array_object = json_value_get_object(json_array_value);
                        if(json_array_object == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        
                        for(j = 0; j < value.sint64a.size; j++)
                        {
                            json_array_append_number(json_array_object, value.sint64a.data[j]);
                        }

                        json_object_set_value(result_root_object, p_instance->classDecl->properties[i]->name, json_array_value);
                        break;
                    }
                    case MI_REAL32A : {
                        MI_Uint32 j = 0;
                        JSON_Value *json_array_value = json_value_init_array();
                        if(json_array_value == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        JSON_Object *json_array_object = json_value_get_object(json_array_value);
                        if(json_array_object == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        
                        for(j = 0; j < value.real32a.size; j++)
                        {
                            json_array_append_number(json_array_object, value.real32a.data[j]);
                        }

                        json_object_set_value(result_root_object, p_instance->classDecl->properties[i]->name, json_array_value);
                        break;
                    }
                    case MI_REAL64A : {
                        MI_Uint32 j = 0;
                        JSON_Value *json_array_value = json_value_init_array();
                        if(json_array_value == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        JSON_Object *json_array_object = json_value_get_object(json_array_value);
                        if(json_array_object == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        
                        for(j = 0; j < value.real64a.size; j++)
                        {
                            json_array_append_number(json_array_object, value.real64a.data[j]);
                        }

                        json_object_set_value(result_root_object, p_instance->classDecl->properties[i]->name, json_array_value);
                        break;
                    }
                    case MI_CHAR16A : {
                        MI_Uint32 j = 0;
                        JSON_Value *json_array_value = json_value_init_array();
                        if(json_array_value == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        JSON_Object *json_array_object = json_value_get_object(json_array_value);
                        if(json_array_object == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        
                        for(j = 0; j < value.real64a.size; j++)
                        {
                            json_array_append_string(json_array_object, value.char16a.data[j]);
                        }

                        json_object_set_value(result_root_object, p_instance->classDecl->properties[i]->name, json_array_value);
                        break;
                    }
                    case MI_DATETIMEA : {
                        MI_Uint32 j = 0;
                        JSON_Value *json_array_value = json_value_init_array();
                        if(json_array_value == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        JSON_Object *json_array_object = json_value_get_object(json_array_value);
                        if(json_array_object == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        
                        for(j = 0; j < value.real64a.size; j++)
                        {
                            MI_Datetime val = value.datetime;
                            char buffer[100]; 
                            if (val.isTimestamp)
                            {
                                sprintf(buffer, "%.4d/%.2d%.2d %.2d:%.2d:%.2d:%.2d - %d",
                                    val.u.timestamp.year, val.u.timestamp.month, val.u.timestamp.day,
                                    val.u.timestamp.hour, val.u.timestamp.minute, val.u.timestamp.second, val.u.timestamp.microseconds,
                                    val.u.timestamp.utc);
                            }
                            else
                            {
                                sprintf(buffer, "%.2d:%.2d:%.2d:%.2d:%.6d",
                                    val.u.interval.days,
                                    val.u.interval.hours,
                                    val.u.interval.minutes,
                                    val.u.interval.seconds,
                                    val.u.interval.microseconds);
                            }
                            json_array_append_string(json_array_object, buffer);
                        }

                        json_object_set_value(result_root_object, p_instance->classDecl->properties[i]->name, json_array_value);
                        break;

                        
                        break;
                    }
                    case MI_STRINGA : {
                        MI_Uint32 j = 0;
                        JSON_Value *json_array_value = json_value_init_array();
                        if(json_array_value == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        JSON_Object *json_array_object = json_value_get_object(json_array_value);
                        if(json_array_object == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        
                        for(j = 0; j < value.real64a.size; j++)
                        {
                            json_array_append_string(json_array_object, value.stringa.data[j]);
                        }

                        json_object_set_value(result_root_object, p_instance->classDecl->properties[i]->name, json_array_value);
                        break;
                    }
                    case MI_INSTANCEA : {
                        MI_Uint32 j = 0;
                        JSON_Value *json_array_value = json_value_init_array();
                        if(json_array_value == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        JSON_Object *json_array_object = json_value_get_object(json_array_value);
                        if(json_array_object == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        
                        for(j = 0; j < value.real64a.size; j++)
                        {
                            JSON_Value *json_val;
                            Convert_MIInstance_JSON(value.instancea.data[j], &json_val);
                            json_array_append_value(json_array_object, json_val);
                        }

                        json_object_set_value(result_root_object, p_instance->classDecl->properties[i]->name, json_array_value);
                        break;
                    }
                }
            }
        }
    }

    return result;
}

MI_Result  Convert_MiClass_JSON (
        const MI_Class* p_instance,
        JSON_Value** p_result_root_value
    )
{
    MI_Result result = MI_RESULT_OK;

    *p_result_root_value = json_value_init_object();
    JSON_Object *result_root_object = json_value_get_object(*p_result_root_value);

    if (p_instance == NULL || p_instance->classDecl == NULL)
    {
        return result;
    }

    MI_Uint32 i = 0;
    for(i = 0 ; i < p_instance->classDecl->numProperties; i++)
    {
        MI_Value value;
        MI_Type type;
        MI_Uint32 flags;
        const MI_Char *name;
        MI_Result r = MI_RESULT_OK;
        r = MI_Instance_GetElementAt(p_instance, i, &name, &value, &type, &flags);
        if( r == MI_RESULT_OK)
        {
            // if(Tcscasecmp(name, MI_T("ErrorSource")) == 0 )
            // {
            //     value.string = (MI_Char*)MI_T("Default");
            //     MI_Instance_SetElement(const_cast<MI_Instance*>(p_instance), name, &value, MI_STRING, 0);
            //     MI_Instance_GetElement(p_instance, name, &value, &type, &flags, NULL);
            // }
            if( flags & MI_FLAG_NULL)
            {
                json_object_set_null(result_root_object, p_instance->classDecl->properties[i]->name);
            }
            else
            {
                switch(type) {
                    case MI_BOOLEAN :{
                        json_object_set_boolean(result_root_object, p_instance->classDecl->properties[i]->name, value.boolean);
                        break;
                    }
                    case MI_UINT8 : {
                        json_object_set_number(result_root_object, p_instance->classDecl->properties[i]->name, value.uint8);
                        break;
                    }
                    case MI_UINT16 :{
                        json_object_set_number(result_root_object, p_instance->classDecl->properties[i]->name, value.uint16);
                        break;
                    }
                    case MI_UINT32 : {
                        json_object_set_number(result_root_object, p_instance->classDecl->properties[i]->name, value.uint32);
                        break;
                    }
                    case MI_UINT64 : {
                        json_object_set_number(result_root_object, p_instance->classDecl->properties[i]->name, value.uint64);
                        break;
                    }
                    case MI_SINT8 : {
                        json_object_set_number(result_root_object, p_instance->classDecl->properties[i]->name, value.sint8);
                        break;
                    }
                    case MI_SINT16 : {
                        json_object_set_number(result_root_object, p_instance->classDecl->properties[i]->name, value.sint16);
                        break;
                    }
                    case MI_SINT32 : {
                        json_object_set_number(result_root_object, p_instance->classDecl->properties[i]->name, value.sint32);
                        break;
                    }
                    case MI_SINT64 : {
                        json_object_set_number(result_root_object, p_instance->classDecl->properties[i]->name, value.sint64);
                        break;
                    }
                    case MI_REAL32 : {
                        json_object_set_number(result_root_object, p_instance->classDecl->properties[i]->name, value.real32);
                        break;
                    }
                    case MI_REAL64 : {
                        json_object_set_number(result_root_object, p_instance->classDecl->properties[i]->name, value.real64);
                        break;
                    }
                    case MI_CHAR16 : {
                        json_object_set_string(result_root_object, p_instance->classDecl->properties[i]->name, value.char16);
                        break;
                    }
                    case MI_DATETIME : {
                        MI_Datetime val = value.datetime;
                        char buffer[100]; 
                        if (val.isTimestamp)
                        {
                            sprintf(buffer, "%.4d/%.2d%.2d %.2d:%.2d:%.2d:%.2d - %d",
                                val.u.timestamp.year, val.u.timestamp.month, val.u.timestamp.day,
                                val.u.timestamp.hour, val.u.timestamp.minute, val.u.timestamp.second, val.u.timestamp.microseconds,
                                val.u.timestamp.utc);
                        }
                        else
                        {
                            sprintf(buffer, "%.2d:%.2d:%.2d:%.2d:%.6d",
                                val.u.interval.days,
                                val.u.interval.hours,
                                val.u.interval.minutes,
                                val.u.interval.seconds,
                                val.u.interval.microseconds);
                        }
                        json_object_set_string(result_root_object, p_instance->classDecl->properties[i]->name, buffer);
                        break;
                    }
                    case MI_STRING : {
                        json_object_set_string(result_root_object, p_instance->classDecl->properties[i]->name, value.string);
                        break;
                    }
                    case MI_INSTANCE : {
                        JSON_Value *json_value;
                        Convert_MIInstance_JSON(value.instance, &json_value);
                        json_object_set_value(result_root_object, p_instance->classDecl->properties[i]->name, json_value);
                        break;
                    }
                    case MI_BOOLEANA : {
                        MI_Uint32 j = 0;
                        JSON_Value *json_array_value = json_value_init_array();
                        if(json_array_value == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        JSON_Object *json_array_object = json_value_get_object(json_array_value);
                        if(json_array_object == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        
                        for(j = 0; j < value.booleana.size; j++)
                        {
                            json_array_append_boolean(json_array_object, value.booleana.data[j]);
                        }

                        json_object_set_value(result_root_object, p_instance->classDecl->properties[i]->name, json_array_value);
                        break;
                    }
                    case MI_UINT8A : {
                        MI_Uint32 j = 0;
                        JSON_Value *json_array_value = json_value_init_array();
                        if(json_array_value == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        JSON_Object *json_array_object = json_value_get_object(json_array_value);
                        if(json_array_object == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        
                        for(j = 0; j < value.uint8a.size; j++)
                        {
                            json_array_append_number(json_array_object, value.uint8a.data[j]);
                        }

                        json_object_set_value(result_root_object, p_instance->classDecl->properties[i]->name, json_array_value);
                        break;
                    }
                    case MI_UINT16A : {
                        MI_Uint32 j = 0;
                        JSON_Value *json_array_value = json_value_init_array();
                        if(json_array_value == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        JSON_Object *json_array_object = json_value_get_object(json_array_value);
                        if(json_array_object == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        
                        for(j = 0; j < value.uint16a.size; j++)
                        {
                            json_array_append_number(json_array_object, value.uint16a.data[j]);
                        }

                        json_object_set_value(result_root_object, p_instance->classDecl->properties[i]->name, json_array_value);
                        break;
                    }
                    case MI_UINT32A : {
                        MI_Uint32 j = 0;
                        JSON_Value *json_array_value = json_value_init_array();
                        if(json_array_value == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        JSON_Object *json_array_object = json_value_get_object(json_array_value);
                        if(json_array_object == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        
                        for(j = 0; j < value.uint32a.size; j++)
                        {
                            json_array_append_number(json_array_object, value.uint32a.data[j]);
                        }

                        json_object_set_value(result_root_object, p_instance->classDecl->properties[i]->name, json_array_value);
                        break;
                    }
                    case MI_UINT64A : {
                        MI_Uint32 j = 0;
                        JSON_Value *json_array_value = json_value_init_array();
                        if(json_array_value == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        JSON_Object *json_array_object = json_value_get_object(json_array_value);
                        if(json_array_object == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        
                        for(j = 0; j < value.uint64a.size; j++)
                        {
                            json_array_append_number(json_array_object, value.uint64a.data[j]);
                        }

                        json_object_set_value(result_root_object, p_instance->classDecl->properties[i]->name, json_array_value);
                        break;
                    }
                    case MI_SINT8A : {
                        MI_Uint32 j = 0;
                        JSON_Value *json_array_value = json_value_init_array();
                        if(json_array_value == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        JSON_Object *json_array_object = json_value_get_object(json_array_value);
                        if(json_array_object == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        
                        for(j = 0; j < value.sint8a.size; j++)
                        {
                            json_array_append_number(json_array_object, value.sint8a.data[j]);
                        }

                        json_object_set_value(result_root_object, p_instance->classDecl->properties[i]->name, json_array_value);
                        break;
                    }
                    case MI_SINT16A : {
                        MI_Uint32 j = 0;
                        JSON_Value *json_array_value = json_value_init_array();
                        if(json_array_value == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        JSON_Object *json_array_object = json_value_get_object(json_array_value);
                        if(json_array_object == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        
                        for(j = 0; j < value.sint16a.size; j++)
                        {
                            json_array_append_number(json_array_object, value.sint16a.data[j]);
                        }

                        json_object_set_value(result_root_object, p_instance->classDecl->properties[i]->name, json_array_value);
                        break;
                    }
                    case MI_SINT32A : {
                        MI_Uint32 j = 0;
                        JSON_Value *json_array_value = json_value_init_array();
                        if(json_array_value == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        JSON_Object *json_array_object = json_value_get_object(json_array_value);
                        if(json_array_object == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        
                        for(j = 0; j < value.sint32a.size; j++)
                        {
                            json_array_append_number(json_array_object, value.sint32a.data[j]);
                        }

                        json_object_set_value(result_root_object, p_instance->classDecl->properties[i]->name, json_array_value);
                        break;
                    }
                    case MI_SINT64A : {
                        MI_Uint32 j = 0;
                        JSON_Value *json_array_value = json_value_init_array();
                        if(json_array_value == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        JSON_Object *json_array_object = json_value_get_object(json_array_value);
                        if(json_array_object == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        
                        for(j = 0; j < value.sint64a.size; j++)
                        {
                            json_array_append_number(json_array_object, value.sint64a.data[j]);
                        }

                        json_object_set_value(result_root_object, p_instance->classDecl->properties[i]->name, json_array_value);
                        break;
                    }
                    case MI_REAL32A : {
                        MI_Uint32 j = 0;
                        JSON_Value *json_array_value = json_value_init_array();
                        if(json_array_value == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        JSON_Object *json_array_object = json_value_get_object(json_array_value);
                        if(json_array_object == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        
                        for(j = 0; j < value.real32a.size; j++)
                        {
                            json_array_append_number(json_array_object, value.real32a.data[j]);
                        }

                        json_object_set_value(result_root_object, p_instance->classDecl->properties[i]->name, json_array_value);
                        break;
                    }
                    case MI_REAL64A : {
                        MI_Uint32 j = 0;
                        JSON_Value *json_array_value = json_value_init_array();
                        if(json_array_value == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        JSON_Object *json_array_object = json_value_get_object(json_array_value);
                        if(json_array_object == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        
                        for(j = 0; j < value.real64a.size; j++)
                        {
                            json_array_append_number(json_array_object, value.real64a.data[j]);
                        }

                        json_object_set_value(result_root_object, p_instance->classDecl->properties[i]->name, json_array_value);
                        break;
                    }
                    case MI_CHAR16A : {
                        MI_Uint32 j = 0;
                        JSON_Value *json_array_value = json_value_init_array();
                        if(json_array_value == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        JSON_Object *json_array_object = json_value_get_object(json_array_value);
                        if(json_array_object == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        
                        for(j = 0; j < value.real64a.size; j++)
                        {
                            json_array_append_string(json_array_object, value.char16a.data[j]);
                        }

                        json_object_set_value(result_root_object, p_instance->classDecl->properties[i]->name, json_array_value);
                        break;
                    }
                    case MI_DATETIMEA : {
                        MI_Uint32 j = 0;
                        JSON_Value *json_array_value = json_value_init_array();
                        if(json_array_value == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        JSON_Object *json_array_object = json_value_get_object(json_array_value);
                        if(json_array_object == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        
                        for(j = 0; j < value.real64a.size; j++)
                        {
                            MI_Datetime val = value.datetime;
                            char buffer[100]; 
                            if (val.isTimestamp)
                            {
                                sprintf(buffer, "%.4d/%.2d%.2d %.2d:%.2d:%.2d:%.2d - %d",
                                    val.u.timestamp.year, val.u.timestamp.month, val.u.timestamp.day,
                                    val.u.timestamp.hour, val.u.timestamp.minute, val.u.timestamp.second, val.u.timestamp.microseconds,
                                    val.u.timestamp.utc);
                            }
                            else
                            {
                                sprintf(buffer, "%.2d:%.2d:%.2d:%.2d:%.6d",
                                    val.u.interval.days,
                                    val.u.interval.hours,
                                    val.u.interval.minutes,
                                    val.u.interval.seconds,
                                    val.u.interval.microseconds);
                            }
                            json_array_append_string(json_array_object, buffer);
                        }

                        json_object_set_value(result_root_object, p_instance->classDecl->properties[i]->name, json_array_value);
                        break;

                        
                        break;
                    }
                    case MI_STRINGA : {
                        MI_Uint32 j = 0;
                        JSON_Value *json_array_value = json_value_init_array();
                        if(json_array_value == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        JSON_Object *json_array_object = json_value_get_object(json_array_value);
                        if(json_array_object == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        
                        for(j = 0; j < value.real64a.size; j++)
                        {
                            json_array_append_string(json_array_object, value.stringa.data[j]);
                        }

                        json_object_set_value(result_root_object, p_instance->classDecl->properties[i]->name, json_array_value);
                        break;
                    }
                    case MI_INSTANCEA : {
                        MI_Uint32 j = 0;
                        JSON_Value *json_array_value = json_value_init_array();
                        if(json_array_value == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        JSON_Object *json_array_object = json_value_get_object(json_array_value);
                        if(json_array_object == NULL) {
                            return MI_RESULT_FAILED;
                        }
                        
                        for(j = 0; j < value.real64a.size; j++)
                        {
                            JSON_Value *json_val;
                            Convert_MIInstance_JSON(value.instancea.data[j], &json_val);
                            json_array_append_value(json_array_object, json_val);
                        }

                        json_object_set_value(result_root_object, p_instance->classDecl->properties[i]->name, json_array_value);
                        break;
                    }
                }
            }
        }
    }

    return result;
}

MI_Result  Print_JSON_Value (
        JSON_Value** p_root_value
    )
{
    MI_Result result = MI_RESULT_OK;

    char *serialized_string = NULL;
    serialized_string = json_serialize_to_string_pretty(*p_root_value);
    if(serialized_string == NULL) {
        return MI_RESULT_FAILED;
    }
    puts(serialized_string);
    json_free_serialized_string(serialized_string);

    return result;
}

MI_Result  Save_JSON_Value (
        const char* p_file_path,
        JSON_Value** p_root_value
    )
{
    MI_Result result = MI_RESULT_OK;

    char *serialized_string = NULL;
    serialized_string = json_serialize_to_string_pretty(*p_root_value);
    if(serialized_string == NULL) {
        return MI_RESULT_FAILED;
    }
    
    FILE * fd = fopen( p_file_path, "w");
    if (fd == NULL)
    {
        result = errno;
        Tprintf(MI_T("Failed to open file '%T' with errno = %d (%T)\n"), p_file_path, errno, strerror(errno));
    }
    else
    {
        fputs(serialized_string, fd);
        fclose(fd);
        json_free_serialized_string(serialized_string);
    }

    return result;
}

MI_Result  Print_MI_Instance (
        const MI_Instance* p_instance
    )
{
    JSON_Value *value = NULL;
    Convert_MIInstance_JSON(p_instance, &value);
    Print_JSON_Value(&value);
    return MI_RESULT_OK;
}

MI_Result  Print_MI_InstanceA (
        const MI_InstanceA* p_instanceA
    )
{
    MI_Uint32 i = 0;
    MI_Result result = MI_RESULT_OK;

    int type = 0;

    for(i = 0 ; i < p_instanceA->size ; i++)
    {
        const MI_Char* propertyName;
        MI_Value propertyValue;
        MI_Type propertyType;

        result = MI_Instance_GetElementAt(*(p_instanceA->data), i, &propertyName, &propertyValue, &propertyType, 0);

        if( propertyType & MI_ARRAY)
        {
            Print_MI_InstanceA(p_instanceA->data[i]);
        }
        else
        {
            Print_MI_Instance(p_instanceA->data[i]);
        }
    }
    return result;
}

MI_Result  Print_MI_Class (
        const MI_Class* p_class
    )
{
    if (p_class == NULL)
    {
        Tprintf(MI_T("  p_class == NULL\n"));
        return MI_RESULT_OK;
    }

    Tprintf(MI_T("  p_class->namespaceName   = %T\n"), p_class->namespaceName);
    Tprintf(MI_T("  p_class->serverName      = %T\n"), p_class->serverName);

    if (p_class->classDecl != NULL)
    {
        Tprintf(MI_T("  p_class->classDecl->name = %T\n"), p_class->classDecl->name);

        MI_Uint32 i = 0;
        for(i = 0 ; i < p_class->classDecl->numProperties; i++)
        {
            Tprintf(MI_T("  Flags : %8X - Name  : %T\n"), p_class->classDecl->properties[i]->flags, p_class->classDecl->properties[i]->name);
        }

        if (p_class->classDecl->superClass != NULL)
        {
            Tprintf(MI_T("  p_class->classDecl->superClass = %T\n"), p_class->classDecl->superClass);

            if (p_class->classDecl->superClassDecl != NULL)
            {
                MI_Uint32 i = 0;
                for(i = 0 ; i < p_class->classDecl->superClassDecl->numProperties; i++)
                {
                    Tprintf(MI_T("  Flags : %8X - Name  : %T\n"), p_class->classDecl->superClassDecl->properties[i]->flags, p_class->classDecl->superClassDecl->properties[i]->name);
                }
            }
        }
    }
    else
    {
        Tprintf(MI_T("  p_class->classDecl       = NULL\n"));
    }

    return MI_RESULT_OK;
}
