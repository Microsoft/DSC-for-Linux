/*
   PowerShell Desired State Configuration for Linux

   Copyright (c) Microsoft Corporation

   All rights reserved.

   MIT License

   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the ""Software""), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED *AS IS*, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "DSC_Systemcalls.h"
#include "EngineHelper.h"
#include "ModuleHandler.h"
#include "ModuleHandlerInternal.h"
#include "ModuleValidator.h"
#include "EngineHelper.h"
#include "LocalConfigManagerHelperForCA.h"
#include "MI.h"
#include "CAEngine.h"
#include "CAEngineInternal.h"
#include "ProviderCallbacks.h"
#include "NativeResourceManager.h"
#include "CAValidate.h"
#include <curl/curl.h>

#define _CA_IMPORT_ 1

#include "Resources_LCM.h"
#include "EventWrapper.h"

#if defined(BUILD_OMS)

#include <signal.h>
extern MI_Boolean g_DscHost;

#endif

volatile MI_Operation *g_CurrentWmiv2Operation = NULL;

const MI_Char * GetModuleName( _In_ MI_Instance *inst);
const MI_Char * GetModuleVersion( _In_ MI_Instance *inst);

MI_Result InitResourceErrorList(ResourceErrorList * resourceErrorList)
{
    if (resourceErrorList == NULL)
    {
        return MI_RESULT_FAILED;
    }
    resourceErrorList->first = NULL;
    resourceErrorList->last = NULL;
    return MI_RESULT_OK;
}

MI_Result AddToResourceErrorList(ResourceErrorList * resourceErrorList, const char * resourceID)
{
    ResourceError * current;
    size_t length = 0;
    if (resourceID == NULL || resourceErrorList == NULL)
    {
        return MI_RESULT_FAILED;
    }

    length = strlen(resourceID);

    if (resourceErrorList->first == NULL)
    {
        resourceErrorList->first = (ResourceError*)calloc(1, sizeof(ResourceError));
        current = resourceErrorList->first;
        resourceErrorList->last = current;
    }
    else
    {
        current = (ResourceError*)calloc(1, sizeof(ResourceError));
        resourceErrorList->last->next = current;
        resourceErrorList->last = current;
    }

    if(current == NULL) {
        return MI_RESULT_FAILED;
    }

    current->next = NULL;
    current->resourceID = (char *)calloc(length + 1, sizeof(char));
    
    if(current->resourceID = NULL) {
        free(current);
        return MI_RESULT_FAILED;
    }
    strncpy(current->resourceID, resourceID, length);
    return MI_RESULT_OK;
}

char * BuildStringResourceErrorList(ResourceErrorList * resourceErrorList)
{
    char * outstring;
    const char * separator = ", ";
    const size_t separator_length = strlen(separator);
    size_t current_length;
    size_t added_length;
    ResourceError* current;

    if (resourceErrorList == NULL)
    {
        return NULL;
    }

    outstring = (char *) calloc(1, sizeof(char));
    if(outstring == NULL) {
        return NULL;
    }
    current_length = 0;
    current = resourceErrorList->first;

    while (current != NULL)
    {
        if (current->resourceID == NULL)
        {
            current = current->next;
        }

        added_length = strlen(current->resourceID);
        if (current->next != NULL)
        {
            char* temp = (char *) realloc(outstring, current_length + added_length + separator_length + 1);

            if(NULL == temp) {
                free(outstring);
            }
            outstring = temp;
            strncpy(outstring + current_length, current->resourceID, added_length);
            strncpy(outstring + current_length + added_length, separator, separator_length);
            current_length += added_length + separator_length;
        }
        else
        {
            char* temp = (char *) realloc(outstring, current_length + added_length + 1);

            if(NULL == temp) {
                free(outstring);
            }
            outstring = temp;
            strncpy(outstring + current_length, current->resourceID, added_length);
            current_length += added_length;
        }

        outstring[current_length] = '\0';

        current = current->next;
    }

    return outstring;

}

MI_Result CleanupResourceErrorList(ResourceErrorList * resourceErrorList)
{
    ResourceError * current;
    ResourceError * next;
    if (resourceErrorList == NULL)
    {
        return MI_RESULT_FAILED;
    }

    current = resourceErrorList->first;

    while (current != NULL)
    {
        next = current->next;
        if (current->resourceID)
        {
            free(current->resourceID);
        }
        free(current);
        current = next;
    }

    return MI_RESULT_OK;
}

MI_Result GetDocumentEncryptionSetting( _In_ MI_Instance *documentIns,
                                        _Inout_ MI_Boolean *bEncryptionEnabled,
                                        _Outptr_result_maybenull_z_ MI_Char **certificateid,
                                        _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Value passwordEncryption;
    MI_Value certificateID;

    MI_Result r = MI_RESULT_OK;
    MI_Instance *currentMetaConfigInstance = NULL;

    if (documentIns == NULL || extendedError == NULL || bEncryptionEnabled == NULL || certificateid == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }

    *extendedError = NULL;
    *bEncryptionEnabled = MI_FALSE;
    *certificateid = NULL;

    certificateID.string = NULL;

    r = MI_Instance_GetElement(documentIns, OMI_ConfigurationDocument_ContentType,
        &passwordEncryption, NULL, NULL, NULL);
    if (r == MI_RESULT_OK && passwordEncryption.string != NULL &&
        Tcscasecmp(passwordEncryption.string, OMI_ConfigurationDocument_ContentType_PasswordEncrypted) == 0)
    {
        r = GetMetaConfig((MSFT_DSCMetaConfiguration **)&currentMetaConfigInstance);
        if (r != MI_RESULT_OK)
        {
            return GetCimMIError(r, extendedError, ID_CA_FAILED_TO_GET_METACONFIGURATION);
        }

        r = MI_Instance_GetElement(currentMetaConfigInstance,MSFT_DSCPULL_CertificateID,
            &certificateID, NULL, NULL, NULL);
        if (r == MI_RESULT_OK && certificateID.string != NULL && documentIns != NULL)
        {
            size_t length = Tcslen(certificateID.string)+1;
            *certificateid = (MI_Char*)DSC_malloc(length * sizeof(MI_Char), NitsHere());
            if (*certificateid == NULL)
            {
                return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, extendedError, ID_ENGINEHELPER_MEMORY_ERROR);
            }

            memcpy(*certificateid, certificateID.string, length * sizeof(MI_Char));

            *bEncryptionEnabled = MI_TRUE;
        }

        MI_Instance_Delete(currentMetaConfigInstance);
    }

    // Lack of a value in the document or an invalid setting doesn't stop processing.
    return MI_RESULT_OK;
}

MI_Result InitCAHandler(_Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    g_rnids = NULL;

    if (cimErrorDetails == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once.

    RecursiveLock_Init(&g_cs_CurrentWmiv2Operation);

    if(Sem_Init_Injected(&g_h_ConfigurationStoppedEvent, SEM_USER_ACCESS_DEFAULT, 1, NitsHere()) == -1)
    {
        return GetCimWin32Error(MI_RESULT_FAILED, cimErrorDetails, ID_LCMHELPER_INIT_FILECRITSEC_FAILED);
    }

    return MI_RESULT_OK;
}

MI_Result UnInitCAHandler(_Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    Destroy_StatusReport_RNIDS(g_rnids);
    g_rnids = NULL;

    if (cimErrorDetails == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *cimErrorDetails = NULL;    // Explicitly set *cimErrorDetails to NULL as _Outptr_ requires setting this at least once.

    RecursiveLock_Release(&g_cs_CurrentWmiv2Operation);
    Sem_Destroy(&g_h_ConfigurationStoppedEvent);

    return MI_RESULT_OK;
}

void MI_CALL FreeExecutionOrderContainer(_Inout_ ExecutionOrderContainer *container)
{
    //Free default execution list
    if( container->ExecutionList)
    {
        DSC_free(container->ExecutionList);
    }
    container->ExecutionList = NULL;
    container->executionListSize = 0;
    container->executionListCapacity = 0;
}

/* Dependency resolver will create independent trees. Individual tree is
    sorted and the sort order define the sequence in which the instances need to be executed.
    Exception is resources which do not define any dependency. They all become part of a
    default tree and put in the order they were defined in original document.*/

MI_Result MI_CALL ResolveDependency( _In_ MI_InstanceA *instanceA,
                                  _Inout_ ExecutionOrderContainer *container,
                                  _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Result r = MI_RESULT_OK;
    MI_Uint32 xCount = 0;
    MI_Sint32 *visitedNodes;
    MI_Sint32 *resolvedNodes;
    DSC_EventWriteMessageResolvingDependency();
    if( container == NULL || instanceA == NULL || instanceA->size == 0 || NitsShouldFault(NitsHere(), NitsAutomatic))
    {
        return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError,ID_CAINFRA_DEPENDCY_NULLPARAM);
    }

    if (extendedError == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *extendedError = NULL;  // Explicitly set *extendedError to NULL as _Outptr_ requires setting this at least once.

    /*Algo to find cycles and create trees.
          Create two lists. First one contain information about the nodes that have been visited.
          Second one contain information about the nodes that have been resolved.*/
    visitedNodes = (MI_Sint32*) DSC_malloc(sizeof(MI_Sint32) * instanceA->size, NitsHere());
    if( visitedNodes == NULL)
    {
        return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, extendedError, ID_ENGINEHELPER_MEMORY_ERROR);
    }

    resolvedNodes = (MI_Sint32*) DSC_malloc(sizeof(MI_Sint32) * instanceA->size, NitsHere());
    if( resolvedNodes == NULL)
    {
        DSC_free(visitedNodes);
        return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, extendedError, ID_ENGINEHELPER_MEMORY_ERROR);
    }

    container->ExecutionList = (ResourceExecutionDetails*) DSC_malloc(sizeof(ResourceExecutionDetails) * instanceA->size, NitsHere());
    if( container->ExecutionList == NULL )
    {
        DSC_free(resolvedNodes);
        DSC_free(visitedNodes);
        return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, extendedError, ID_ENGINEHELPER_MEMORY_ERROR);
    }
    container->executionListCapacity = instanceA->size;


    memset(resolvedNodes, -1 , sizeof(MI_Sint32) * instanceA->size);
    memset( container->ExecutionList, -1, sizeof(ResourceExecutionDetails) * instanceA->size);

    for( xCount = 0 ; xCount < instanceA->size; xCount++)
    {
        /*Check if node is already resolved. If yes return success.*/
        if( resolvedNodes[xCount] == NODE_RESOLVED )
        {
            continue;
        }

        memset(visitedNodes, -1 , sizeof(MI_Sint32) * instanceA->size);
        r = ResolveDependencyInternal(xCount, instanceA, container, visitedNodes, resolvedNodes, extendedError);
        if( r != MI_RESULT_OK)
        {
            DSC_free(visitedNodes);
            DSC_free(resolvedNodes);
            FreeExecutionOrderContainer(container);
            return r;
        }
    }
    // All nodes should be visited here by now, if now we have some undefined error.
    for( xCount = 0 ; xCount < instanceA->size; xCount++ )
    {
        if( resolvedNodes[xCount] != NODE_RESOLVED )
        {
            GetCimMIError(MI_RESULT_FAILED, extendedError, ID_CAINFRA_DEPENDCYRESOLVER_OUTOFBOUNDS);
        }
    }

    DSC_free(visitedNodes);
    DSC_free(resolvedNodes);
    return r;
}

/*This method assumes that all the parameters are valid and within range as appropriate. Like index is < instanceA->size etc.
    Also memory for container is freed by the caller.*/
MI_Result ResolveDependencyInternal( _In_ MI_Uint32 index,
                                   _In_ MI_InstanceA *instanceA,
                                  _Inout_ ExecutionOrderContainer *container,
                                  _Inout_count_(instanceA->size) MI_Sint32 *visitedNodes,
                                  _Inout_count_(instanceA->size) MI_Sint32 *resolvedNodes,
                                  _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Result r = MI_RESULT_OK;
    MI_Instance *currentInstance = NULL;
    MI_Uint32 xCount = 0;
    MI_Uint32 dwIndex = 0;
    MI_Value value;
    const MI_Char *className = NULL;

    if (extendedError == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *extendedError = NULL;  // Explicitly set *extendedError to NULL as _Outptr_ requires setting this at least once.

    if( index >= instanceA->size || NitsShouldFault(NitsHere(), NitsAutomatic))
    {
        return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError,ID_CAINFRA_DEPENDCY_OUTOFBOUNDS);
    }

    // find dependencies
    currentInstance = instanceA->data[index];
    r = MI_Instance_GetClassName(currentInstance, &className);
    if( r != MI_RESULT_OK )
    {
        return GetCimMIError(r, extendedError, ID_MODMAN_VALIDATE_PROVREGINS_CLASSNAME);
    }
    visitedNodes[index] = NODE_VISITED;

    r = MI_Instance_GetElement(currentInstance, OMI_BaseResource_DependsOn, &value, NULL, NULL, NULL);
    if( r == MI_RESULT_NOT_FOUND || r == MI_RESULT_NO_SUCH_PROPERTY || value.string == NULL)
    {
        // no dependency specified add it to the list.
        resolvedNodes[index] = NODE_RESOLVED;
        r = AddToList(container, index, extendedError);
        return r;
    }
    else
    {
        for( xCount = 0 ; xCount < value.stringa.size; xCount++)
        {
            r = GetInstanceIndex(instanceA, value.stringa.data[xCount], index, &dwIndex, extendedError);
            if( r != MI_RESULT_OK)
            {
                return r;
            }
            if( dwIndex >= instanceA->size || NitsShouldFault(NitsHere(), NitsAutomatic))
            {
                return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError,ID_CAINFRA_DEPENDCY_OUTOFBOUNDS);
            }
            /*Check if node is already resolved. If yes return success.*/
            if( resolvedNodes[dwIndex] == NODE_RESOLVED)
            {
                continue;
            }

            /*If the node is visited but not resolved there is a cycle in the graph*/
            if( visitedNodes[dwIndex] == NODE_VISITED || NitsShouldFault(NitsHere(), NitsAutomatic))
            {
                const MI_Char* resourceID;
                const MI_Char* sourceInfo;
                resourceID = GetResourceId(instanceA->data[dwIndex]);
                if(resourceID == NULL) {
                    return MI_RESULT_NOT_FOUND;
                }
                sourceInfo = GetSourceInfo(instanceA->data[dwIndex]);
                if(sourceInfo == NULL)
                {
                    return GetCimMIError1Param(MI_RESULT_FAILED, extendedError, ID_CAINFRA_DEPENDCY_CYCLE, resourceID);
                }
                else
                {
                    return GetCimMIError2Params(MI_RESULT_FAILED, extendedError, ID_CAINFRA_DEPENDCY_CYCLE2, resourceID, sourceInfo);
                }
            }

            r = ResolveDependencyInternal(dwIndex, instanceA, container, visitedNodes, resolvedNodes, extendedError);
            if( r != MI_RESULT_OK)
            {
                return r;
            }
        }
        //Add it to resolved List.
        resolvedNodes[index] = NODE_RESOLVED;
        r = AddToList(container, index, extendedError);
        if( r != MI_RESULT_OK)
        {
            return r;
        }

    }
    return MI_RESULT_OK;
}

MI_Result GetInstanceIndex(_In_ MI_InstanceA *instanceA,
                              _In_z_ MI_Char *resourceId,
                              int currentInstanceIndex,
                              _Out_ MI_Uint32 *resourceIndex,
                              _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Uint32 xCount = 0;
    MI_Result r = MI_RESULT_OK;
    MI_Value value;
    const MI_Char* currentResourceID;
    const MI_Char* currentSourceInfo;

    *resourceIndex = 0;

    if (extendedError == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *extendedError = NULL;  // Explicitly set *extendedError to NULL as _Outptr_ requires setting this at least once.

    for( xCount = 0 ; xCount < instanceA->size; xCount++)
    {
        r = DSC_MI_Instance_GetElement(instanceA->data[xCount], OMI_BaseResource_ResourceId, &value, NULL, NULL, NULL);
        if( r != MI_RESULT_OK )
        {
            return GetCimMIError(r, extendedError, ID_CAINFRA_INSTANCE_RESOURCEID);
        }
        if( Tcscasecmp(value.string, resourceId) == 0 )
        {
            *resourceIndex = xCount;
            return MI_RESULT_OK;
        }
    }
    // If here resource was not found. Throw an error.
    currentResourceID = GetResourceId(instanceA->data[currentInstanceIndex]);
    if(currentResourceID == NULL) {
        return MI_RESULT_NOT_FOUND;
    }
    currentSourceInfo = GetSourceInfo(instanceA->data[currentInstanceIndex]);
    if(currentSourceInfo == NULL)
    {
        return GetCimMIError2Params(MI_RESULT_FAILED, extendedError, ID_CAINFRA_INSTANCE_RESOURCEID_NOTFOUND, resourceId, currentResourceID);
    }
    else
    {
        return GetCimMIError3Params(MI_RESULT_FAILED, extendedError, ID_CAINFRA_INSTANCE_RESOURCEID_NOTFOUND2, resourceId, currentResourceID, currentSourceInfo);
    }
}

MI_Result DependentResourceFailed( _In_ MI_Uint32 index,
                            _In_ ExecutionOrderContainer *container,
                            _In_ MI_InstanceA *instanceA,
                            _Out_ MI_Boolean *bDependentFailed,
                            _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Result r = MI_RESULT_OK;
    MI_Value value;
    MI_Uint32 xCount = 0;
    if (extendedError == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *extendedError = NULL;      // Explicitly set *extendedError to NULL as _Outptr_ requires setting this at least once.
    *bDependentFailed = MI_FALSE;
    if( index >= instanceA->size || NitsShouldFault(NitsHere(), NitsAutomatic))
    {
        return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError,ID_CAINFRA_DEPENDCY_OUTOFBOUNDS);
    }
    /* Step1: get the resources on which resource with location index depends on.*/

    r = MI_Instance_GetElement(instanceA->data[index], OMI_BaseResource_DependsOn, &value, NULL, NULL, NULL);
    if( r == MI_RESULT_NOT_FOUND || r == MI_RESULT_NO_SUCH_PROPERTY || value.string == NULL)
    {
        // no dependencies we are good.
        return MI_RESULT_OK;
    }
    else if ( r != MI_RESULT_OK )
    {
        return GetCimMIError(r, extendedError, ID_MODMAN_GETELEMENT_FAILED );
    }

    /* Step2: For each of dependent resources they should have already been processed. */
    for ( xCount = 0 ; xCount < value.stringa.size && *bDependentFailed == MI_FALSE; xCount++)
    {
        MI_Uint32 resourceIndex = 0;
        r = GetInstanceIndex(instanceA, value.stringa.data[xCount],
            index, &resourceIndex, extendedError);

        if( r != MI_RESULT_OK )
        {
            return r;
        }
        r = DependentResourceProcessed(resourceIndex, container, bDependentFailed, extendedError);
        if( r != MI_RESULT_OK )
        {
            return r;
        }
    }
    return MI_RESULT_OK;
}

MI_Result DependentResourceProcessed (_In_ MI_Uint32 resourceIndex,
                                    _In_ ExecutionOrderContainer *container,
                                    _Inout_ MI_Boolean *bDependentFailed,
                                    _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Uint32 xCount = 0;
    if (extendedError == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *extendedError = NULL;

    for( xCount = 0 ; xCount < container->executionListSize ; xCount++)
    {
        if( container->ExecutionList[xCount].resourceIndex == resourceIndex )
        {
            // status should be processed.
            if( container->ExecutionList[xCount].resourceStatus == ResourceNotProcessed )
            {
                // We have some internal error.
                return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError,ID_CAINFRA_DEPENDCY_OUTOFBOUNDS);
            }
            else if( container->ExecutionList[xCount].resourceStatus == ResourceProcessedAndFailed )
            {
                *bDependentFailed = MI_TRUE;
            }
            return MI_RESULT_OK;
        }
    }
    // Here is impossible, something is messed up which we don't understant.
    return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError,ID_CAINFRA_DEPENDCY_OUTOFBOUNDS);
}

/*Caller will clean up the memory*/
MI_Result AddToList(_Inout_ ExecutionOrderContainer *container,
                            _In_ MI_Uint32 objectIndex,
                            _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Uint32 xCount = 0;

    if (extendedError == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *extendedError = NULL;      // Explicitly set *extendedError to NULL as _Outptr_ requires setting this at least once.

    for( xCount = 0; xCount < container->executionListSize; xCount++)
    {
        if( objectIndex == container->ExecutionList[xCount].resourceIndex)
        {
            // Resource is already in the list, we are good.
            return MI_RESULT_OK;
        }
    }
    if( container->executionListSize == container->executionListCapacity )
    {
        return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, extendedError, ID_ENGINEHELPER_MEMORY_ERROR);
    }
    // Add it to the list at the end.
    container->ExecutionList[ container->executionListSize ].resourceIndex = objectIndex;
    container->ExecutionList[ container->executionListSize ].resourceStatus = ResourceNotProcessed;
    container->executionListSize++;

    return MI_RESULT_OK;
}

MI_Result SetResourcesInOrder(_In_ LCMProviderContext *lcmContext,
                              _In_ ModuleManager *moduleManager,
                              _In_ MI_InstanceA * instanceA,
                              _In_ MI_Session *miSession,
                              _In_ ExecutionOrderContainer *executionOrder,
                              _In_ MI_Uint32 flags,
                              _In_ MI_Instance *documentIns,
                              _Inout_ MI_Uint32 *resultStatus,
                              _Outptr_result_maybenull_ ResourceErrorList *resourceErrorList,
                              _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Uint32 xCount = 0;
    MI_Uint32 index = 0;
    MI_Instance *filteredInstance = NULL;
    MI_Instance *regInstance = NULL;
    ModuleLoaderObject *moduleLoader = NULL;
    MI_Result r = MI_RESULT_OK;
    MI_Result finalr = MI_RESULT_OK;
    ProviderCallbackContext providerContext = {0};
    MI_Char *certificateid = NULL;
    MI_Boolean bEncryptionEnabled = MI_FALSE;
    MI_Boolean canceled;
    DSC_EventWriteMessageSettingResourcesOrder(executionOrder->executionListSize);
    moduleLoader = (ModuleLoaderObject*) moduleManager->reserved2;

    if (extendedError == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }

    *extendedError = NULL;  // Explicitly set *extendedError to NULL as _Outptr_ requires setting this at least once.

    providerContext.lcmProviderContext = lcmContext;

    r = GetDocumentEncryptionSetting(documentIns, &bEncryptionEnabled, &certificateid, extendedError);
    if (r != MI_RESULT_OK)
    {
        return r;
    }

    // Instantiate native resource manager, responsible to load/unload native resource provider.
    r = NativeResourceManager_New(&providerContext, &(providerContext.nativeResourceManager));
    if( r != MI_RESULT_OK)
    {
        if(certificateid != NULL)
        {
            DSC_free(certificateid);
            certificateid = NULL;
        }
        return r;
    }

    /*Assuming the dependencies is implicit (the order in which instances are specified in the list). */
    /*Get the instance compatible with the provider.*/
    for (xCount = 0 ; xCount < executionOrder->executionListSize ; xCount++)
    {
        MI_Boolean bDependentFailed = MI_FALSE;
        if (g_CancelConfiguration == TRUE)
        {
            if(certificateid != NULL)
            {
                DSC_free(certificateid);
                certificateid = NULL;
            }
            DSC_EventWriteConfigurationCancelledInTheMiddle(executionOrder->executionListSize, executionOrder->executionListSize - xCount);
            return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_CA_CANCEL_CONFIGURATION);
        }

        index = executionOrder->ExecutionList[xCount].resourceIndex;
        r = DependentResourceFailed(index, executionOrder, instanceA, &bDependentFailed, extendedError);
        if( r != MI_RESULT_OK )
        {
            if(certificateid != NULL)
            {
                DSC_free(certificateid);
                certificateid = NULL;
            }
            return r;
        }
        if( bDependentFailed == MI_TRUE )
        {
            // mark it as failed and continue;
            executionOrder->ExecutionList[xCount].resourceStatus = ResourceProcessedAndFailed;
            continue;

        }
        providerContext.resourceId = GetResourceId(instanceA->data[index]);

        //metaconfig doesn't have resourceID
        if(providerContext.resourceId == NULL &&
            Tcscasecmp(instanceA->data[index]->classDecl->name, METACONFIG_CLASSNAME) == 0)
        {
            providerContext.resourceId = METACONFIG_CLASSNAME;
        }

        if ( flags & LCM_EXECUTE_TESTONLY )
        {
            LogCAProgress(lcmContext,MI_T("Test"),MI_T("Testing Configuration"), xCount, instanceA->size);
        }
        else
        {
            LogCAProgress(lcmContext,MI_T("Set"),MI_T("Applying Configuration"), xCount, instanceA->size);
        }
        SetMessageInContext(ID_OUTPUT_OPERATION_START,ID_OUTPUT_ITEM_RESOURCE,lcmContext);
        LogCAMessage(lcmContext, ID_OUTPUT_EMPTYSTRING, providerContext.resourceId);

        if (index >= instanceA->size || NitsShouldFault(NitsHere(), NitsAutomatic))
        {
            if(certificateid != NULL)
            {
                DSC_free(certificateid);
                certificateid = NULL;
            }

            return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_CAINFRA_DEPENDCYRESOLVER_OUTOFBOUNDS);
        }

        DSC_EventWriteMessageRegisteringModule(instanceA->data[index]->classDecl->name);

        /* Get Registration Instance to find registration information.*/
        r = moduleManager->ft->GetRegistrationInstance(moduleManager, instanceA->data[index]->classDecl->name, (const MI_Instance **)&regInstance, extendedError);
        if (r != MI_RESULT_OK)
        {
            if(certificateid != NULL)
            {
                DSC_free(certificateid);
                certificateid = NULL;
            }
            return r;
        }

        /*Get provider compatible instance*/
        r = moduleManager->ft->GetProviderCompatibleInstance(moduleManager, instanceA->data[index], &filteredInstance, extendedError);
        if (r != MI_RESULT_OK)
        {
            if(certificateid != NULL)
            {
                DSC_free(certificateid);
                certificateid = NULL;
            }
            return r;
        }

        /* Move the resource to desired state.*/
        canceled = MI_FALSE;
        r = MoveToDesiredState(&providerContext, moduleLoader->application, miSession, filteredInstance, regInstance, flags, resultStatus, &canceled, resourceErrorList, extendedError);
        DSC_LOG_INFO("MoveToDesiredState = %d, *resultStatus = %d\n", r, *resultStatus);
        MI_Instance_Delete(filteredInstance);
        filteredInstance = NULL;

        if (r != MI_RESULT_OK)
        {
            DSC_LOG_INFO("SetResourcesInOrder failed in MoveToDesiredState\n");
            // Failure case, update the resource status
            Intlstr intlstr = Intlstr_Null;
            executionOrder->ExecutionList[xCount].resourceStatus = ResourceProcessedAndFailed;
            if (canceled)
            {
                if(certificateid != NULL)
                {
                    DSC_free(certificateid);
                    certificateid = NULL;
                }

                DSC_EventWriteConfigurationCancelledInTheMiddle(executionOrder->executionListSize, executionOrder->executionListSize - xCount);
                return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_CA_CANCEL_CONFIGURATION);
            }
            if( flags & LCM_EXECUTE_TESTONLY)
            {
                if(certificateid != NULL)
                {
                    DSC_free(certificateid);
                    certificateid = NULL;
                }

                return r;
            }

            GetResourceString(ID_LCMHELPER_SENDCONFIGAPPLY_ERROR, &intlstr);

            DSC_EventWriteLCMSendConfigurationError(CA_ACTIVITY_NAME,
                r,
                intlstr.str,
                providerContext.resourceId,
                GetSourceInfo(instanceA->data[index]),
                (MI_Char*)GetErrorDetail(*extendedError));

            if( intlstr.str)
                Intlstr_Free(intlstr);

            //send the error to WriteError stream.
            LCM_WriteError( lcmContext, *extendedError);

            // we need to continue moving other resources to their desired state.
            finalr = r;
            DSC_LOG_INFO("SetResourcesInOrder finalr = %d\n", finalr);
            if (extendedError)
            {
                if(certificateid != NULL)
                {
                    DSC_free(certificateid);
                    certificateid = NULL;
                }

                MI_Instance_Delete(*extendedError);
                *extendedError = NULL;
            }

            continue;
        }
        // Success Case 
        DSC_LOG_INFO("SetResourcesInOrder succeeded in MoveToDesiredState\n");

        if (resultStatus != NULL && *resultStatus == MI_TRUE)
        {
            Destroy_StatusReport_RNIDS(g_rnids);
            g_rnids = NULL;
        }

        executionOrder->ExecutionList[xCount].resourceStatus = ResourceProcessedAndSucceeded;
        r = finalr;
        SetMessageInContext(ID_OUTPUT_OPERATION_END,ID_OUTPUT_ITEM_RESOURCE,lcmContext);
        LogCAMessage(lcmContext, ID_OUTPUT_EMPTYSTRING, providerContext.resourceId);

        // if resultStatus is 1 then they are in their current state otherwise not.
        if (flags & LCM_EXECUTE_TESTONLY)
        {
            if (*resultStatus == MI_FALSE)
            {
                if(certificateid != NULL)
                {
                    DSC_free(certificateid);
                    certificateid = NULL;
                }

                return r;
            }
        }
        else if (DSC_RESTART_SYSTEM_FLAG & *resultStatus && r == MI_RESULT_OK)
        {
            if(certificateid != NULL)
            {
                DSC_free(certificateid);
                certificateid = NULL;
            }

            return MI_RESULT_OK;
        }
    }

    if(certificateid != NULL)
    {
        DSC_free(certificateid);
        certificateid = NULL;
    }

    // Ignore this call and do not delete the native resource manager instance. It will be cleaned up when the host process goes away.
    // NativeResourceManager_Delete(providerContext.nativeResourceManager);

    return finalr;
}


/*Get the current configuration for the desired state objects*/
MI_Result MI_CALL GetConfiguration( _In_ LCMProviderContext *lcmContext,
                                    _In_ MI_Uint32 flags,
                                    _In_ MI_InstanceA *instanceA,
                                    _In_ ModuleManager *moduleManager,
                                    _In_ MI_Instance *documentIns,
                                    _Out_ MI_InstanceA *outInstances,
                                    _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Result r = MI_RESULT_OK;
    MI_Instance *filteredInstance = NULL;
    MI_Instance *regInstance = NULL;
    ModuleLoaderObject *moduleLoader = NULL;
    MI_Instance *getInstanceResult = NULL;
    MI_Session miSession = MI_SESSION_NULL;
    ProviderCallbackContext providerContext = {0};

    MI_Char *certificateid = NULL;
    MI_Boolean bEncryptionEnabled = MI_FALSE;
    MI_Uint32 xCount = 0;
    MI_Uint32 index = 0;

    if( outInstances == NULL  || NitsShouldFault(NitsHere(), NitsAutomatic))
    {
        return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError,ID_CAINFRA_GET_NULLPARAM);
    }

    memset(outInstances, 0, sizeof(MI_InstanceA));
    if( instanceA == 0 || moduleManager == 0 || instanceA->size == 0  || NitsShouldFault(NitsHere(), NitsAutomatic))
    {
        return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError,ID_CAINFRA_DEPENDCY_NULLPARAM);
    }

    if (extendedError == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *extendedError = NULL;  // Explicitly set *extendedError to NULL as _Outptr_ requires setting this at least once.

    providerContext.lcmProviderContext = lcmContext;

    r = GetDocumentEncryptionSetting(documentIns, &bEncryptionEnabled, &certificateid, extendedError);
    if( r != MI_RESULT_OK )
    {
        return r;
    }

    outInstances->data = (MI_Instance **)DSC_malloc( instanceA->size * sizeof(MI_Instance*), NitsHere());
    outInstances->size = 0;

    if( outInstances->data == NULL)
    {
        return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, extendedError, ID_ENGINEHELPER_MEMORY_ERROR);

    }

    moduleLoader = (ModuleLoaderObject*) moduleManager->reserved2;

    /*Create MI session*/
    r = DSC_MI_Application_NewSession(moduleLoader->application, NULL, NULL, NULL, NULL, NULL, &miSession);
    if( r != MI_RESULT_OK)
    {
        CleanUpGetCache(outInstances);
        DSC_free(outInstances->data);
        outInstances->size = 0;
        return GetCimMIError(r, extendedError,ID_CAINFRA_NEWSESSION_FAILED);
    }

    // Instantiate native resource manager, responsible to load/unload native resource provider.
    r = NativeResourceManager_New(&providerContext, &(providerContext.nativeResourceManager));
    if( r != MI_RESULT_OK)
    {
        CleanUpGetCache(outInstances);
        DSC_free(outInstances->data);
        outInstances->size = 0;
        MI_Session_Close(&miSession, NULL, NULL);
        return r;
    }

    /*Assuming the dependencies is implicit (the order in which instances are specified in instance document). */
    /*Get the instance compatible with the provider.*/
    for (xCount = 0 ; xCount < instanceA->size ; xCount++)
    {
        /* Get Registration Instance to find registration information.*/
        r = moduleManager->ft->GetRegistrationInstance(moduleManager, instanceA->data[xCount]->classDecl->name, (const MI_Instance **)&regInstance, extendedError);
        if( r != MI_RESULT_OK)
        {
            CleanUpGetCache(outInstances);
            DSC_free(outInstances->data);
            outInstances->size = 0;
            MI_Session_Close(&miSession, NULL, NULL);
            return r;
        }

        /*Get provider compatible instance*/
        r = moduleManager->ft->GetProviderCompatibleInstance(moduleManager, instanceA->data[xCount], &filteredInstance, extendedError);
        if( r != MI_RESULT_OK)
        {
            CleanUpGetCache(outInstances);
            DSC_free(outInstances->data);
            outInstances->size = 0;
            MI_Session_Close(&miSession, NULL, NULL);
            return r;
        }
        providerContext.resourceId = GetResourceId(instanceA->data[xCount]);

        /* Get the resource.*/
        r = GetCurrentState(&providerContext, moduleLoader->application, &miSession, filteredInstance, regInstance, &getInstanceResult, extendedError);
        MI_Instance_Delete(filteredInstance);
        filteredInstance = NULL;

        if( r != MI_RESULT_OK)
        {
            Intlstr intlstr = Intlstr_Null;
            GetResourceString(ID_LCMHELPER_SENDCONFIGAPPLY_ERROR, &intlstr);

            DSC_EventWriteLCMSendConfigurationError(CA_ACTIVITY_NAME,
                r,
                (MI_Char*)intlstr.str,
                providerContext.resourceId,
                GetSourceInfo(instanceA->data[xCount]),
                (MI_Char*)GetErrorDetail(*extendedError));

            if( intlstr.str)
                Intlstr_Free(intlstr);

            CleanUpGetCache(outInstances);
            DSC_free(outInstances->data);
            outInstances->size = 0;
            MI_Session_Close(&miSession, NULL, NULL);
            return r;
        }

        outInstances->data[xCount] = getInstanceResult;
        outInstances->size += 1;
    }

    MI_Session_Close(&miSession, NULL, NULL);

    // Ignore this call and do not delete the native resource manager instance. It will be cleaned up when the host process goes away.
    // NativeResourceManager_Delete(providerContext.nativeResourceManager);

    return r;
}

/*Apply the configuration for instances whose current state is different than desired state.*/
MI_Result MI_CALL SendConfigurationApply( _In_ LCMProviderContext *lcmContext,
                                  _In_ MI_Uint32 flags,
                                  _In_ MI_InstanceA *instanceA,
                                  _In_ ModuleManager *moduleManager,
                                  _In_ MI_Instance *documentIns,
                                  _Inout_ MI_Uint32 *resultStatus,
                                  _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Result r = MI_RESULT_OK;
    ModuleLoaderObject *moduleLoader = NULL;
    MI_Session miSession = MI_SESSION_NULL;
    ExecutionOrderContainer executionContainer = {0};
    ResourceErrorList resourceErrorList;
    char * resourceErrorString;
    DSC_EventWriteMessageParsingConfiguration();
    DSC_EventWriteEngineMethodParameters(__WFUNCTION__,documentIns->classDecl->name,_STRINGEMPTY_,flags,lcmContext->executionMode,documentIns->nameSpace);

    if( instanceA == 0 || moduleManager == 0 || instanceA->size == 0 || NitsShouldFault(NitsHere(), NitsAutomatic) )
    {
        return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError,ID_CAINFRA_DEPENDCY_NULLPARAM);
    }

    if (extendedError == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *extendedError = NULL;  // Explicitly set *extendedError to NULL as _Outptr_ requires setting this at least once.

    moduleLoader = (ModuleLoaderObject*) moduleManager->reserved2;

    r = ValidateIfDuplicatedInstances(instanceA, extendedError);
    if( r != MI_RESULT_OK)
    {
        FreeExecutionOrderContainer(&executionContainer);
        return r;
    }

    /*Resolving dependencies*/
    r = ResolveDependency(instanceA, &executionContainer, extendedError);
    if( r != MI_RESULT_OK)
    {
        FreeExecutionOrderContainer(&executionContainer);
        return r;
    }

    /*Create MI session*/
    r = DSC_MI_Application_NewSession(moduleLoader->application, NULL, NULL, NULL, NULL, NULL, &miSession);
    if( r != MI_RESULT_OK)
    {
        FreeExecutionOrderContainer(&executionContainer);
        return GetCimMIError(r, extendedError,ID_CAINFRA_NEWSESSION_FAILED);
    }

    InitResourceErrorList(&resourceErrorList);
    /*execute the list in sequence.*/
    r = SetResourcesInOrder(lcmContext, moduleManager, instanceA, &miSession, & executionContainer,
                            flags, documentIns, resultStatus, &resourceErrorList, extendedError);

    if (resourceErrorList.first != NULL)
    {
        resourceErrorString = BuildStringResourceErrorList(&resourceErrorList);
        if (resourceErrorString != NULL)
        {
            if(*extendedError != NULL)
            {
                MI_Instance_Delete(*extendedError);
            }
            r = GetCimMIError1Param(r, extendedError, ID_APPLYCONFIG_RESOURCEERRORLIST, resourceErrorString);
            free(resourceErrorString);
        }
    }

    CleanupResourceErrorList(&resourceErrorList);
    if( r != MI_RESULT_OK )
    {
        FreeExecutionOrderContainer(&executionContainer);
        MI_Session_Close(&miSession, NULL, NULL);
        return r;
    }

    FreeExecutionOrderContainer(&executionContainer);
    MI_Session_Close(&miSession, NULL, NULL);
    return r;
}

MI_Result GetCurrentState(_In_ ProviderCallbackContext *provContext,
                           _In_ MI_Application *miApp,
                           _In_ MI_Session *miSession,
                           _In_ MI_Instance *instance,
                           _In_ const MI_Instance *regInstance,
                           _Outptr_result_maybenull_ MI_Instance *outputInstance,
                           _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    if (
#if !defined(BUILD_OMS)
        Tcscasecmp(regInstance->classDecl->name, BASE_REGISTRATION_WMIV2PROVIDER) == 0 ||
#endif
        Tcscasecmp(MSFT_LOGRESOURCENAME, instance->classDecl->name) == 0
        )
    {
        return Get_WMIv2Provider(provContext, miApp, miSession, instance, regInstance, outputInstance, extendedError);
    }
#if defined(BUILD_OMS)
    else
    {
        MI_Result result = MI_RESULT_OK;
        if (provContext->nativeResourceManager == NULL)
        {
            return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError, ID_MODMAN_MODMAN_NULLPARAM);
        }

        // Get ClassName
        MI_Value class_name_value;
        result = MI_Instance_GetElement(regInstance, MI_T("ClassName"), &class_name_value, NULL, NULL, NULL);
        if (result != MI_RESULT_OK)
        {
            return result;
        }
        MI_Char* class_name = class_name_value.string;

        // Get provider .so path for class
        MI_Char resources_so_path[MAX_PATH];
        int ret = Stprintf(resources_so_path, MAX_PATH, MI_T("%T/%T/lib%T.so"), DSC_LIB_PATH, class_name, class_name);
        DSC_LOG_INFO("Looking into shared object file under '%s'\n", resources_so_path);
        if (ret == -1)
        {
            DSC_LOG_ERROR("Stprintf failed (code = %d) for '%s'\n", ret, resources_so_path);
            return result;
        }

        // Get the path to the resource provider module (.so)
        size_t resourceProviderPathLength = (MI_Uint32)(Tcslen(resources_so_path) + 1) ;
        MI_Char* resourceProviderPath = (MI_Char*)DSC_malloc(resourceProviderPathLength * sizeof(MI_Char), NitsHere());
        if (resourceProviderPath == NULL)
        {
            return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, extendedError, ID_LCMHELPER_MEMORY_ERROR);
        }
        result = Stprintf(resourceProviderPath, resourceProviderPathLength, MI_T("%T"), resources_so_path);

        NativeResourceProvider* nativeResourceProvider = NULL;
        result = NativeResourceManager_GetNativeResouceProvider(provContext->nativeResourceManager, resourceProviderPath, instance->classDecl->name, &nativeResourceProvider);
        if (result != MI_RESULT_OK)
        {
            DSC_LOG_ERROR("NativeResourceManager_GetNativeResouceProvider failed (code = %d) for '%s'\n", result, resourceProviderPath);
            goto cleanup;
        }

        result = NativeResourceProvider_GetTargetResource(nativeResourceProvider, miApp, miSession, instance, regInstance,/* flags,*/ outputInstance, extendedError);

cleanup:

        if (!resourceProviderPath)
        {
            DSC_free(resourceProviderPath);
        }

        return result;
    }
#else
    else
    {
        return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError,ID_CAINFRA_UNKNOWN_REGISTRATION);
    }
#endif
}

MI_Result PerformInventoryState(_In_ ProviderCallbackContext *provContext,
                           _In_ MI_Application *miApp,
                           _In_ MI_Session *miSession,
                           _In_ MI_Instance *instance,
                           _In_ const MI_Instance *regInstance,
                           _Outptr_result_maybenull_ MI_InstanceA *outputInstances,
                           _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    if (
#if !defined(BUILD_OMS)
        Tcscasecmp(regInstance->classDecl->name, BASE_REGISTRATION_WMIV2PROVIDER) == 0 ||
#endif
        Tcscasecmp(MSFT_LOGRESOURCENAME, instance->classDecl->name) == 0
        )
    {
        return Inventory_WMIv2Provider(provContext, miApp, miSession, instance, regInstance, outputInstances, extendedError);
    }
#if defined(BUILD_OMS)
    else
    {
        MI_Result result = MI_RESULT_OK;
        if (provContext->nativeResourceManager == NULL)
            return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError, ID_MODMAN_MODMAN_NULLPARAM);

        // Get ClassName
        MI_Value class_name_value;
        result = MI_Instance_GetElement(regInstance, MI_T("ClassName"), &class_name_value, NULL, NULL, NULL);
        if (result != MI_RESULT_OK)
        {
            return result;
        }
        MI_Char* class_name = class_name_value.string;

        // Get provider .so path for class
        MI_Char resources_so_path[MAX_PATH];
        int ret = Stprintf(resources_so_path, MAX_PATH, MI_T("%T/%T/lib%T.so"), DSC_LIB_PATH, class_name, class_name);
        DSC_LOG_INFO("Looking into shared object file under '%s'\n", resources_so_path);
        if (ret == -1)
        {
            DSC_LOG_ERROR("Stprintf failed (code = %d) for '%s'\n", ret, resources_so_path);
            return result;
        }

        // Get the path to the resource provider module (.so)
        size_t resourceProviderPathLength = (MI_Uint32)(Tcslen(resources_so_path) + 1) ;
        MI_Char* resourceProviderPath = (MI_Char*)DSC_malloc(resourceProviderPathLength * sizeof(MI_Char), NitsHere());
        if( resourceProviderPath == NULL)
        {
            return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, extendedError, ID_LCMHELPER_MEMORY_ERROR);
        }
        result = Stprintf(resourceProviderPath, resourceProviderPathLength, MI_T("%T"), resources_so_path);

        NativeResourceProvider* nativeResourceProvider = NULL;
        result = NativeResourceManager_GetNativeResouceProvider(provContext->nativeResourceManager, resourceProviderPath, instance->classDecl->name, &nativeResourceProvider);
        if (result != MI_RESULT_OK)
        {
            DSC_LOG_ERROR("NativeResourceManager_GetNativeResouceProvider failed (code = %d) for '%s'\n", result, resourceProviderPath);
            goto cleanup;
        }

        result = NativeResourceProvider_GetInventory(nativeResourceProvider, miApp, miSession, instance, regInstance,/* flags,*/ outputInstances, extendedError);

cleanup:

        if (!resourceProviderPath)
        {
            DSC_free(resourceProviderPath);
        }

        return result;
    }
#else
    else
    {
        return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError,ID_CAINFRA_UNKNOWN_REGISTRATION);
    }
#endif
}

MI_Result MoveToDesiredState(_In_ ProviderCallbackContext *provContext,
                             _In_ MI_Application *miApp,
                             _In_ MI_Session *miSession,
                             _In_ MI_Instance *instance,
                             _In_ const MI_Instance *regInstance,
                             _In_ MI_Uint32 flags,
                             _Inout_ MI_Uint32 *resultStatus,
                             _Inout_ MI_Boolean *canceled,
                             _Outptr_result_maybenull_ ResourceErrorList *resourceErrorList,
                             _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    DSC_EventWriteMessageMoveResourceToDesired(provContext->resourceId,instance->classDecl->name);

    MI_Result r = MI_RESULT_OK;

    if (
#if defined(BUILD_OMS)
        g_DscHost == MI_FALSE ||
        Tcscasecmp(instance->classDecl->name, METACONFIG_CLASSNAME) == 0 || // put special cases to wmiv2 code
#else
        Tcscasecmp(instance->classDecl->name, METACONFIG_CLASSNAME) == 0 || // put special cases to wmiv2 code
        Tcscasecmp(regInstance->classDecl->name, BASE_REGISTRATION_WMIV2PROVIDER) == 0 ||
#endif
        Tcscasecmp(MSFT_LOGRESOURCENAME, instance->classDecl->name) == 0
        )
    {
        DSC_LOG_INFO("Executing OMI Provider.\n");
        if(instance->classDecl!=NULL)
        {
            SQMLogResourceCountData(instance->classDecl->name,1);
        }

        r = Exec_WMIv2Provider(provContext, miApp, miSession, instance, regInstance, flags, resultStatus, canceled, resourceErrorList, extendedError);
    }
#if defined(BUILD_OMS)
    else
    {
        DSC_LOG_INFO("Executing Native Provider.\n");
        r = Exec_NativeProvider(provContext, miApp, miSession, instance, regInstance, flags, resultStatus, extendedError);
    }
#else
    else
    {
        r = GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError,ID_CAINFRA_UNKNOWN_REGISTRATION);
    }
#endif

    return r;
}

MI_Result Exec_WMIv2Provider(_In_ ProviderCallbackContext *provContext,
                             _In_ MI_Application *miApp,
                             _In_ MI_Session *miSession,
                             _In_ MI_Instance *instance,
                             _In_ const MI_Instance *regInstance,
                             _In_ MI_Uint32 flags,
                             _Inout_ MI_Uint32 *resultStatus,
                             _Inout_ MI_Boolean* canceled,
                             _Outptr_result_maybenull_ ResourceErrorList *resourceErrorList,
                             _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Result r = MI_RESULT_OK;
    MI_Result result = MI_RESULT_OK;
    const MI_Char *provNamespace = NULL;
    MI_Operation operation = MI_OPERATION_NULL;
    MI_Instance *params = NULL;
    MI_Value value;
    MI_Value valueOperationOptions;
    MI_Boolean bTestResult = MI_TRUE;
    MI_Uint64 outProviderContext = 0;
    MI_Uint32 returnValue = 0;
    MI_OperationCallbacks callbacks = MI_OPERATIONCALLBACKS_NULL;
    MI_Real64 duration;
    MI_OperationOptions sessionOptions;
    const MI_Char * instanceNamespace;

    ptrdiff_t start,finish;

    //we don't have reginstance for meta configuration
    if (Tcscasecmp(instance->classDecl->name, METACONFIG_CLASSNAME) == 0)
    {
        instanceNamespace = MI_T("");
    }
    else
    {
        instanceNamespace = regInstance->nameSpace;
    }

    //Debug Log
    DSC_EventWriteEngineMethodParameters(__WFUNCTION__,
                                        instance->classDecl->name,
                                        provContext->resourceId,
                                        flags,
                                        provContext->lcmProviderContext->executionMode,
                                        instanceNamespace);
    DSC_EventWriteMessageExecutingWMI(instance->classDecl->name,provContext->resourceId);

    callbacks.writeMessage = DoWriteMessage;
    /* Sign up for progress only if we are in online mode*/
    if ((LCM_EXECUTIONMODE_ONLINE & provContext->lcmProviderContext->executionMode) == LCM_EXECUTIONMODE_ONLINE)
    {
        callbacks.writeProgress = DoWriteProgress;
    }

    /* Sign up for promptuser only if we are asked to do so.*/
    if ((LCM_SETFLAGS_ENABLEWHATIF & flags) == LCM_SETFLAGS_ENABLEWHATIF)
    {
        callbacks.promptUser = DoPromptUser;
    }

    callbacks.callbackContext = (void *)(provContext);

    if (extendedError == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *extendedError = NULL;  // Explicitly set *extendedError to NULL as _Outptr_ requires setting this at least once.

    *canceled = MI_FALSE;
    if (g_CancelConfiguration)
    {
        *canceled = MI_TRUE;
        return MI_RESULT_FAILED;
    }

    // If the input MI_Instance is a MSFT_LogResource then directly log the message using LCM log API's
    if (Tcscasecmp(instance->classDecl->name, LOGRESOURCE_CLASSNAME) == 0)
    {
        MI_Value value;
        const MI_Char *message = NULL;
        //Start timer for Set
        start=CPU_GetTimeStamp();

        //Starting Test
        SetMessageInContext(ID_OUTPUT_OPERATION_START,ID_OUTPUT_ITEM_TEST,provContext->lcmProviderContext);
        LogCAMessage(provContext->lcmProviderContext, ID_OUTPUT_EMPTYSTRING, provContext->resourceId);

        r = MI_Instance_GetElement(instance, LOGRESOURCE_MESSAGEPROPERTYNAME, &value, NULL, NULL, NULL);
        if (r != MI_RESULT_OK)
        {
            return GetCimMIError(r, extendedError,ID_CAINFRA_GET_LOGRESOURCE_FAILED);
        }

        message = value.string;
        //Stop the timer for test
        finish=CPU_GetTimeStamp();
        duration = (MI_Real64)(finish- start) / TIME_PER_SECONND;
        SetMessageInContext(ID_OUTPUT_OPERATION_END,ID_OUTPUT_ITEM_TEST,provContext->lcmProviderContext);
        LogCAMessageTime(provContext->lcmProviderContext, ID_CA_TEST_TIMEMESSAGE, (const MI_Real64)duration,provContext->resourceId);

        if (!(flags & LCM_EXECUTE_TESTONLY))
        {
            //Start timer for Set
            start=CPU_GetTimeStamp();
            SetMessageInContext(ID_OUTPUT_OPERATION_START,ID_OUTPUT_ITEM_SET,provContext->lcmProviderContext);
            LogCAMessage(provContext->lcmProviderContext, ID_OUTPUT_EMPTYSTRING, provContext->resourceId);

            LCM_WriteMessageFromProvider(provContext->lcmProviderContext, provContext->resourceId, MI_WRITEMESSAGE_CHANNEL_VERBOSE, message);
            //Stop the timer for set
            finish=CPU_GetTimeStamp();
            duration = (MI_Real64)(finish- start) / TIME_PER_SECONND;
            SetMessageInContext(ID_OUTPUT_OPERATION_END,ID_OUTPUT_ITEM_SET,provContext->lcmProviderContext);
            LogCAMessageTime(provContext->lcmProviderContext, ID_CA_SET_TIMEMESSAGE, (const MI_Real64)duration,provContext->resourceId);
        }
    }
    // If the input is a meta configuration then invoke LCM's API directly
    else if (Tcscasecmp(instance->classDecl->name, METACONFIG_CLASSNAME) == 0 && !( flags & LCM_EXECUTE_TESTONLY ))
    {
        //Start timer for Set
        start=CPU_GetTimeStamp();
        //meta configuration is always explicitly get invoked so no need to test before set.
        SetMessageInContext(ID_OUTPUT_OPERATION_START,ID_OUTPUT_ITEM_SET,provContext->lcmProviderContext);
        LogCAMessage(provContext->lcmProviderContext, ID_OUTPUT_EMPTYSTRING, provContext->resourceId);
        r = SetMetaConfig(instance, extendedError);
         //Stop the timer for set
        finish=CPU_GetTimeStamp();
        duration = (MI_Real64)(finish- start) / TIME_PER_SECONND;
        SetMessageInContext(ID_OUTPUT_OPERATION_END,ID_OUTPUT_ITEM_SET,provContext->lcmProviderContext);
        LogCAMessageTime(provContext->lcmProviderContext, ID_CA_SET_TIMEMESSAGE, (const MI_Real64)duration,provContext->resourceId);

        if(r == MI_RESULT_OK)
        {
            result = RegisterStandardTasks(extendedError);
        }
    }
    else
    {
        /*Get target namespace*/
        r = DSC_MI_Instance_GetElement(regInstance, MSFT_CimConfigurationProviderRegistration_Namespace, &value, NULL, NULL, NULL);
        if (r != MI_RESULT_OK)
        {
            return GetCimMIError(r, extendedError,ID_CAINFRA_GET_NAMESPACE_FAILED);
        }

        provNamespace = value.string;

        /*Set input parameters*/
        r = DSC_MI_Application_NewInstance(miApp, MI_T("__Parameters"), NULL, &params);
        if (r != MI_RESULT_OK)
        {
            return GetCimMIError(r, extendedError,ID_CAINFRA_GET_NEWAPPLICATIONINSTANCE_FAILED);
        }

        value.instance = instance;
        r = DSC_MI_Instance_AddElement(params, OMI_BaseResource_Method_InputResource, &value, MI_INSTANCE, 0 );
        if (r != MI_RESULT_OK)
        {
            MI_Instance_Delete(params);
            return GetCimMIError(r, extendedError,ID_CAINFRA_GET_ADDELEM_FAILED);
        }
        r = MI_Application_NewOperationOptions(miApp, MI_FALSE, &sessionOptions);
        if( r != MI_RESULT_OK )
        {
            return GetCimMIError(r, extendedError,ID_CAINFRA_GET_NEWOPERATIONOPTIONS_FAILED);
        }
        valueOperationOptions.string=g_ConfigurationDetails.jobGuidString;
        r =MI_OperationOptions_SetCustomOption(&sessionOptions,DSC_JOBIDSTRING,MI_STRING,&valueOperationOptions,MI_FALSE);
        if( r != MI_RESULT_OK)
        {
            MI_OperationOptions_Delete(&sessionOptions);
            return GetCimMIError(r, extendedError,ID_CAINFRA_GET_SETCUSTOMOPTION_FAILED);
        }
        if(r!=MI_RESULT_OK)
        {
            //INCHTODO: Say setcustomoptionfailed
            return GetCimMIError(r, extendedError,ID_CAINFRA_GET_ADDELEM_FAILED);
        }
        /* Perform Test*/
        //Start timer for test
        start=CPU_GetTimeStamp();
        DSC_EventWriteMessageInvokingSession(provNamespace,instance->classDecl->name,OMI_BaseResource_TestMethodName);
        SetMessageInContext(ID_OUTPUT_OPERATION_START,ID_OUTPUT_ITEM_TEST,provContext->lcmProviderContext);
        LogCAMessage(provContext->lcmProviderContext, ID_OUTPUT_EMPTYSTRING, provContext->resourceId);

        RecursiveLock_Acquire(&g_cs_CurrentWmiv2Operation);
        MI_Session_Invoke(miSession, 0, &sessionOptions, provNamespace,
                             instance->classDecl->name, OMI_BaseResource_TestMethodName,
                             NULL, params, &callbacks,&operation);
        g_CurrentWmiv2Operation = &operation;
        RecursiveLock_Release(&g_cs_CurrentWmiv2Operation);

        r = GetTestMethodResult(&operation, &bTestResult, &outProviderContext, extendedError);

        RecursiveLock_Acquire(&g_cs_CurrentWmiv2Operation);
        g_CurrentWmiv2Operation = NULL;
        MI_Operation_Close(&operation);
        RecursiveLock_Release(&g_cs_CurrentWmiv2Operation);
        if (r != MI_RESULT_OK)
        {
            MI_Instance_Delete(params);
            MI_OperationOptions_Delete(&sessionOptions);
            AddToResourceErrorList(resourceErrorList, provContext->resourceId);
            Destroy_StatusReport_RNIDS(g_rnids);
            g_rnids = Construct_StatusReport_RNIDS(GetSourceInfo(instance), GetModuleName(instance), "0", provContext->resourceId, "0", instance->classDecl->name, GetModuleVersion(instance), "False", provContext->resourceId, "", "False");
            return r;
        }

        //Stop the timer for test
        finish=CPU_GetTimeStamp();
        duration = (MI_Real64)(finish- start) / TIME_PER_SECONND;
        SetMessageInContext(ID_OUTPUT_OPERATION_END,ID_OUTPUT_ITEM_TEST,provContext->lcmProviderContext);
        LogCAMessageTime(provContext->lcmProviderContext, ID_CA_TEST_TIMEMESSAGE, (const MI_Real64)duration,provContext->resourceId);

        /* Skip rest of the operation if we were asked just to test.*/
        if (flags & LCM_EXECUTE_TESTONLY)
        {
            if(bTestResult == MI_TRUE)
            {
                *resultStatus = 1;
            }
            else
            {
                *resultStatus = 0;
                Destroy_StatusReport_RNIDS(g_rnids);
                g_rnids = Construct_StatusReport_RNIDS(GetSourceInfo(instance), GetModuleName(instance), "0", provContext->resourceId, "0", instance->classDecl->name, GetModuleVersion(instance), "False", provContext->resourceId, "", "False");
            }

            MI_Instance_Delete(params);
            MI_OperationOptions_Delete(&sessionOptions);
            return MI_RESULT_OK;
        }

        /* Perform Set if value returned is FALSE*/
        if( bTestResult == MI_TRUE)
        {
            SetMessageInContext(ID_OUTPUT_OPERATION_SKIP,ID_OUTPUT_ITEM_SET,provContext->lcmProviderContext);
            LogCAMessage(provContext->lcmProviderContext, ID_OUTPUT_EMPTYSTRING, provContext->resourceId);
            MI_Instance_Delete(params);
            MI_OperationOptions_Delete(&sessionOptions);
            return MI_RESULT_OK;
        }

        if (g_CancelConfiguration)
        {
            *canceled = MI_TRUE;
            MI_Instance_Delete(params);
            MI_OperationOptions_Delete(&sessionOptions);
            return MI_RESULT_FAILED;
        }

        /* Add outProviderContext to the params*/
        value.uint64 = outProviderContext;
        r = DSC_MI_Instance_AddElement(params, OMI_BaseResource_Method_ProviderContext, &value, MI_UINT64, 0 );
        if (r != MI_RESULT_OK)
        {
            MI_Instance_Delete(params);
            MI_OperationOptions_Delete(&sessionOptions);
            return GetCimMIError(r, extendedError,ID_CAINFRA_GET_ADDELEM_FAILED);
        }

        /* Perform Set*/
        //Start timer for set
        start=CPU_GetTimeStamp();
        SetMessageInContext(ID_OUTPUT_OPERATION_START,ID_OUTPUT_ITEM_SET,provContext->lcmProviderContext);
        LogCAMessage(provContext->lcmProviderContext, ID_OUTPUT_EMPTYSTRING, provContext->resourceId);
        DSC_EventWriteMessageInvokingSession(provNamespace,instance->classDecl->name,OMI_BaseResource_SetMethodName);

        RecursiveLock_Acquire(&g_cs_CurrentWmiv2Operation);
        memset(&operation, 0, sizeof(MI_Operation));
        MI_Session_Invoke(miSession, 0, &sessionOptions, provNamespace,
                             instance->classDecl->name, OMI_BaseResource_SetMethodName,
                             NULL, params, &callbacks,&operation);

        g_CurrentWmiv2Operation = &operation;
        RecursiveLock_Release(&g_cs_CurrentWmiv2Operation);

        r = GetSetMethodResult(&operation, &returnValue, provContext->resourceId, extendedError);
        MI_Instance_Delete(params);

        RecursiveLock_Acquire(&g_cs_CurrentWmiv2Operation);
        g_CurrentWmiv2Operation = NULL;
        MI_Operation_Close(&operation);
        RecursiveLock_Release(&g_cs_CurrentWmiv2Operation);

        if (r != MI_RESULT_OK)
        {
            MI_OperationOptions_Delete(&sessionOptions);
            AddToResourceErrorList(resourceErrorList, provContext->resourceId);
            Destroy_StatusReport_RNIDS(g_rnids);
            g_rnids = Construct_StatusReport_RNIDS(GetSourceInfo(instance), GetModuleName(instance), "0", provContext->resourceId, NULL, instance->classDecl->name, GetModuleVersion(instance), "False", provContext->resourceId, "", "False");
            return r;
        }

        *resultStatus = returnValue;
        if (returnValue != MI_TRUE)
        {
            Destroy_StatusReport_RNIDS(g_rnids);
            g_rnids = Construct_StatusReport_RNIDS(GetSourceInfo(instance), GetModuleName(instance), "0", provContext->resourceId, NULL, instance->classDecl->name, GetModuleVersion(instance), "False", provContext->resourceId, "", "False");
        }
        //Stop the timer for set
        finish=CPU_GetTimeStamp();
        duration = (MI_Real64)(finish- start) / TIME_PER_SECONND;
        SetMessageInContext(ID_OUTPUT_OPERATION_END,ID_OUTPUT_ITEM_SET,provContext->lcmProviderContext);
        LogCAMessageTime(provContext->lcmProviderContext, ID_CA_SET_TIMEMESSAGE, (const MI_Real64)duration,provContext->resourceId);
        MI_OperationOptions_Delete(&sessionOptions);
    }

    //Debug Log
    DSC_EventWriteMethodEnd(__WFUNCTION__);
    return r;
}

MI_Result Exec_NativeProvider(_In_ ProviderCallbackContext *provContext,
                             _In_ MI_Application *miApp,
                             _In_ MI_Session *miSession,
                             _In_ MI_Instance *instance,
                             _In_ const MI_Instance *regInstance,
                             _In_ MI_Uint32 flags,
                             _Inout_ MI_Uint32 *resultStatus,
                             _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Result result = MI_RESULT_OK;
    *resultStatus = 0;
    MI_Uint32 test_operation_result = 0;
    MI_Uint32 set_operation_result = 0;
    MI_Real64 duration;
    ptrdiff_t start,finish;

    if (provContext->nativeResourceManager == NULL)
    {
        return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError, ID_MODMAN_MODMAN_NULLPARAM);
    }

    // Get ClassName
    MI_Value class_name_value;
    result = MI_Instance_GetElement(regInstance, MI_T("ClassName"), &class_name_value, NULL, NULL, NULL);
    if (result != MI_RESULT_OK)
    {
        return result;
    }
    MI_Char* class_name = class_name_value.string;

    // Get provider .so path for class
    MI_Char resources_so_path[MAX_PATH];
    int ret = Stprintf(resources_so_path, MAX_PATH, MI_T("%T/%T/lib%T.so"), DSC_LIB_PATH, class_name, class_name);
    DSC_LOG_INFO("Looking into shared object file under '%s'\n", resources_so_path);
    if (ret == -1)
    {
        DSC_LOG_ERROR("Stprintf failed (code = %d) for '%s'\n", ret, resources_so_path);
        return result;
    }

    // Get the path to the resource provider module (.so)
    size_t resourceProviderPathLength = (MI_Uint32)(Tcslen(resources_so_path) + 1) ;
    MI_Char* resourceProviderPath = (MI_Char*)DSC_malloc(resourceProviderPathLength * sizeof(MI_Char), NitsHere());
    if( resourceProviderPath == NULL)
    {
        return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, extendedError, ID_LCMHELPER_MEMORY_ERROR);
    }
    result = Stprintf(resourceProviderPath, resourceProviderPathLength, MI_T("%T"), resources_so_path);

    NativeResourceProvider* nativeResourceProvider = NULL;
    result = NativeResourceManager_GetNativeResouceProvider(provContext->nativeResourceManager, resourceProviderPath, instance->classDecl->name, &nativeResourceProvider);
    if (result != MI_RESULT_OK)
    {
        DSC_LOG_ERROR("NativeResourceManager_GetNativeResouceProvider failed (code = %d) for '%s'\n", result, resourceProviderPath);
        goto cleanup;
    }

    // Execute Test unless SETONLY was provided
    if (!(flags & LCM_EXECUTE_SETONLY)) {
        //Stop the timer for test
        finish=CPU_GetTimeStamp();
        duration = (MI_Real64)(finish- start) / TIME_PER_SECONND;
        SetMessageInContext(ID_OUTPUT_OPERATION_START,ID_OUTPUT_ITEM_TEST,provContext->lcmProviderContext);

        result = NativeResourceProvider_TestTargetResource(nativeResourceProvider, miApp, miSession, instance, regInstance, &test_operation_result, extendedError);
        DSC_LOG_INFO("NativeResourceProvider_TestTargetResource for '%s' returned %d\n", class_name, test_operation_result);

        if (result != MI_RESULT_OK)
        {
            DSC_LOG_WARNING("NativeResourceProvider_TestTargetResource failed.\n");
        }

        //Stop the timer for test
        finish=CPU_GetTimeStamp();
        duration = (MI_Real64)(finish- start) / TIME_PER_SECONND;
        SetMessageInContext(ID_OUTPUT_OPERATION_END,ID_OUTPUT_ITEM_TEST,provContext->lcmProviderContext);
        LogCAMessageTime(provContext->lcmProviderContext, ID_CA_TEST_TIMEMESSAGE, (const MI_Real64)duration,provContext->resourceId);
    }

    /* Skip rest of the operation if we were asked just to test.*/
    if (flags & LCM_EXECUTE_TESTONLY)
    {
        if(test_operation_result == 1) // TestTargetResource returned TRUE
        {
            *resultStatus = 1;
        }
        else // TestTargetResource returned FALSE
        {
            *resultStatus = 0;
        }

        goto cleanup;
    }

    /* Perform Set if value returned is FALSE*/
    if(test_operation_result == 1) // TestTargetResource returned TRUE, so we are skipping SetTargetResource
    {
        SetMessageInContext(ID_OUTPUT_OPERATION_SKIP,ID_OUTPUT_ITEM_SET,provContext->lcmProviderContext);
        LogCAMessage(provContext->lcmProviderContext, ID_OUTPUT_EMPTYSTRING, provContext->resourceId);
        DSC_LOG_INFO("TestTargetResource returned TRUE, so we are skipping SetTargetResource...\n");
        goto cleanup;
    }

    /* Perform Set*/
    //Start timer for set
    start=CPU_GetTimeStamp();
    SetMessageInContext(ID_OUTPUT_OPERATION_START,ID_OUTPUT_ITEM_SET,provContext->lcmProviderContext);

    result = NativeResourceProvider_SetTargetResource(nativeResourceProvider, miApp, miSession, instance, regInstance, &set_operation_result, extendedError);
    if (result != MI_RESULT_OK)
    {
        result = GetCimMIError(result, extendedError, ID_NATIVE_PROVIDER_MANAGER_SET_OPERATION_FAILED);
        DSC_LOG_ERROR("NativeResourceProvider_SetTargetResource failed.\n");
        goto cleanup;
    }

    //Stop the timer for set
    finish=CPU_GetTimeStamp();
    duration = (MI_Real64)(finish- start) / TIME_PER_SECONND;
    SetMessageInContext(ID_OUTPUT_OPERATION_END,ID_OUTPUT_ITEM_SET,provContext->lcmProviderContext);
    LogCAMessageTime(provContext->lcmProviderContext, ID_CA_SET_TIMEMESSAGE, (const MI_Real64)duration,provContext->resourceId);

    // The result of SET operation indicates the final result for this call.
    if(set_operation_result == 0) // SetTargetResource returned OK
    {
        result = MI_RESULT_OK;
    }
    else // SetTargetResource reported a failure from the resource.
    {
        result = MI_RESULT_FAILED;
    }
    DSC_LOG_INFO("NativeResourceProvider_SetTargetResource for '%s' returned %d\n", class_name, set_operation_result);

cleanup:

    if (!resourceProviderPath)
    {
        DSC_free(resourceProviderPath);
    }

    return result;
}

MI_Result GetSetMethodResult(_In_ MI_Operation *operation,
                              _Out_opt_ MI_Uint32 *returnValue,
                             _In_z_ const MI_Char * resourceId,
                              _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Result r = MI_RESULT_OK;
    MI_Result innerR = MI_RESULT_OK;
    const MI_Instance* outInstance;
    MI_Boolean moreResults;
    MI_Result result = MI_RESULT_INVALID_PARAMETER;
    const MI_Char *errorMessage;
    const MI_Instance *completionDetails = NULL;
    MI_Value value;

    *returnValue = 0;

    if (extendedError == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *extendedError = NULL;  // Explicitly set *extendedError to NULL as _Outptr_ requires setting this at least once.

    /*Get the operation result*/
    r = MI_Operation_GetInstance(operation, &outInstance, &moreResults, &result, &errorMessage, &completionDetails);
    if( result != MI_RESULT_OK)
    {
        r = result;
    }
    if( r != MI_RESULT_OK)
    {
        if( completionDetails != NULL)
        {
            innerR = DSC_MI_Instance_Clone( completionDetails, extendedError);
        }
        if( innerR != MI_RESULT_OK || completionDetails == NULL)
        {
            r = GetCimMIError(r, extendedError,ID_CAINFRA_GETINSTANCE_FAILED);
        }
        /* append resourceid to error message*/
        else
        {
            AppendWMIErrorWithResourceID(*extendedError, resourceId);
        }
        return r;
    }

    /*Get ReturnValue*/
    r = DSC_MI_Instance_GetElement(outInstance, OMI_BaseResource_Method_ReturnValue, &value, NULL, NULL, NULL);
    if( r != MI_RESULT_OK )
    {
        return GetCimMIError(r, extendedError,ID_CAINFRA_GET_RETURNVALUE_FAILED);
    }
    *returnValue = value.uint32;

    return r;
}

MI_Result GetTestMethodResult(_In_ MI_Operation *operation,
                              _Out_opt_ MI_Boolean *bTestResult,
                              _Out_opt_ MI_Uint64 *outProviderContext,
                              _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Result r = MI_RESULT_OK;
    MI_Result innerR = MI_RESULT_OK;
    const MI_Instance* outInstance;
    MI_Boolean moreResults;
    MI_Result result = MI_RESULT_INVALID_PARAMETER;
    const MI_Char *errorMessage;
    const MI_Instance *completionDetails = NULL;
    MI_Value value;

    *bTestResult = MI_TRUE;
    *outProviderContext = 0;

    if (extendedError == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *extendedError = NULL;  // Explicitly set *extendedError to NULL as _Outptr_ requires setting this at least once.

    /*Get the operation result*/
    r = MI_Operation_GetInstance(operation, &outInstance, &moreResults, &result, &errorMessage, &completionDetails);
    if( result != MI_RESULT_OK)
    {
        r = result;
    }
    if( r != MI_RESULT_OK)
    {
        if( completionDetails != NULL)
        {
            innerR = DSC_MI_Instance_Clone( completionDetails, extendedError);
        }
        if( innerR != MI_RESULT_OK || completionDetails == NULL)
        {
            r = GetCimMIError(r, extendedError,ID_CAINFRA_GETINSTANCE_FAILED);
        }
        return r;
    }

    /*Get Result  property*/
    r = DSC_MI_Instance_GetElement(outInstance, OMI_BaseResource_Method_Result, &value, NULL, NULL, NULL);
    if( r != MI_RESULT_OK )
    {
        return GetCimMIError(r, extendedError,ID_CAINFRA_GET_RESULT_FAILED);
    }
    *bTestResult = value.boolean;

    /*Get ProviderContext  property*/
    r = DSC_MI_Instance_GetElement(outInstance, OMI_BaseResource_Method_ProviderContext, &value, NULL, NULL, NULL);
    if( r != MI_RESULT_OK )
    {
        return GetCimMIError(r, extendedError,ID_CAINFRA_GET_PROVCONTEXT_FAILED);
    }
    *outProviderContext = value.uint64;

    return r;
}

MI_Result GetGetMethodResult(_In_ MI_Operation *operation,
                              _Outptr_result_maybenull_ MI_Instance **outputInstance,
                              _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Result r = MI_RESULT_OK;
    MI_Result innerR = MI_RESULT_OK;
    const MI_Instance* outInstance;
    MI_Boolean moreResults;
    MI_Result result = MI_RESULT_INVALID_PARAMETER;
    const MI_Char *errorMessage;
    const MI_Instance *completionDetails = NULL;
    MI_Value value;

    if (extendedError == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *extendedError = NULL;  // Explicitly set *extendedError to NULL as _Outptr_ requires setting this at least once.

    *outputInstance = NULL;
    /*Get the operation result*/
    r = MI_Operation_GetInstance(operation, &outInstance, &moreResults, &result, &errorMessage, &completionDetails);
    if( result != MI_RESULT_OK)
    {
        DSC_LOG_INFO("MI_Operation_GetInstance inside GetGetMethodResult failed result not ok");
        r = result;
    }
    if( r != MI_RESULT_OK)
    {
        DSC_LOG_INFO("MI_Operation_GetInstance inside GetGetMethodResult failed r not ok");
        if( completionDetails != NULL)
        {
            innerR = DSC_MI_Instance_Clone( completionDetails, extendedError);
        }
        if( innerR != MI_RESULT_OK || completionDetails == NULL)
        {
            r = GetCimMIError(r, extendedError,ID_CAINFRA_GETINSTANCE_FAILED);
        }

        return r;
    }

    /*Get configurations  property*/
    r = DSC_MI_Instance_GetElement(outInstance, OMI_BaseResource_Method_OutputResource, &value, NULL, NULL, NULL);
    if( r != MI_RESULT_OK )
    {
        return GetCimMIError(r, extendedError,ID_CAINFRA_GET_OUTPUTRES_FAILED);
    }
    /*Clone the object*/

    r = DSC_MI_Instance_Clone(value.instance, outputInstance);
    if( r != MI_RESULT_OK )
    {
        return GetCimMIError(r, extendedError,ID_CAINFRA_CLONE_FAILED);
    }
    return r;
}

MI_Result Get_WMIv2Provider(_In_ ProviderCallbackContext *provContext,
                               _In_ MI_Application *miApp,
                               _In_ MI_Session *miSession,
                               _In_ MI_Instance *instance,
                               _In_ const MI_Instance *regInstance,
                               _Outptr_result_maybenull_ MI_Instance **outputInstance,
                                _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Result r = MI_RESULT_OK;
    const MI_Char *provNamespace = NULL;
    MI_Operation operation = MI_OPERATION_NULL;
    MI_Instance *params = NULL;
    MI_Value value;
    MI_Value valueOperationOptions;
    MI_OperationCallbacks callbacks = MI_OPERATIONCALLBACKS_NULL;
    MI_Real64 duration;
    MI_OperationOptions sessionOptions ;

    ptrdiff_t finish,start;
    //Debug Log
    DSC_EventWriteEngineMethodParameters(__WFUNCTION__,
                                        instance->classDecl->name,
                                        provContext->resourceId,
                                        0,
                                        provContext->lcmProviderContext->executionMode,
                                        regInstance->nameSpace);
    DSC_EventWriteMessageWmiGet(instance->classDecl->name,provContext->resourceId);

    callbacks.writeMessage = DoWriteMessage;
    /* Sign up for progress only if we are in online mode*/
    if( (LCM_EXECUTIONMODE_ONLINE & provContext->lcmProviderContext->executionMode) == LCM_EXECUTIONMODE_ONLINE)
    {
        callbacks.writeProgress = DoWriteProgress;
    }

    callbacks.callbackContext = (void *)(provContext);

    if (extendedError == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *extendedError = NULL;  // Explicitly set *extendedError to NULL as _Outptr_ requires setting this at least once.

    *outputInstance = NULL;

    start=CPU_GetTimeStamp();
    SetMessageInContext(ID_OUTPUT_OPERATION_START,ID_OUTPUT_ITEM_GET,provContext->lcmProviderContext);
    LogCAMessage(provContext->lcmProviderContext, ID_OUTPUT_EMPTYSTRING, provContext->resourceId);
    DSC_EventWriteMessageInvokingSession(provNamespace,instance->classDecl->name,OMI_BaseResource_GetMethodName);

    if( Tcscasecmp(MSFT_LOGRESOURCENAME, instance->classDecl->name) == 0 )
    {
        r = DSC_MI_Instance_Clone(instance, outputInstance);
        if( r != MI_RESULT_OK)
        {
            return GetCimMIError(r, extendedError, ID_CAINFRA_CLONE_FAILED);
        }
    }
    else
    {
        /*Get target namespace*/
        r = DSC_MI_Instance_GetElement(regInstance, MSFT_CimConfigurationProviderRegistration_Namespace, &value, NULL, NULL, NULL);
        if( r != MI_RESULT_OK )
        {
            return GetCimMIError(r, extendedError,ID_CAINFRA_GET_NAMESPACE_FAILED);
        }
        provNamespace = value.string;

        /*Set input parameters*/
        r = DSC_MI_Application_NewInstance(miApp, MI_T("__Parameters"), NULL, &params);
        if( r != MI_RESULT_OK )
        {
            return GetCimMIError(r, extendedError,ID_CAINFRA_GET_NEWAPPLICATIONINSTANCE_FAILED);
        }
        value.instance = instance;
        
        r = DSC_MI_Instance_AddElement(params, OMI_BaseResource_Method_InputResource, &value, MI_INSTANCE, 0 );
        if( r != MI_RESULT_OK)
        {
            MI_Instance_Delete(params);
            return GetCimMIError(r, extendedError,ID_CAINFRA_GET_ADDELEM_FAILED);
        }
        
        r = MI_Application_NewOperationOptions(miApp, MI_FALSE, &sessionOptions);
        if( r != MI_RESULT_OK )
        {
            return GetCimMIError(r, extendedError,ID_CAINFRA_GET_NEWOPERATIONOPTIONS_FAILED);
        }
        valueOperationOptions.string=g_ConfigurationDetails.jobGuidString;
        
        r =MI_OperationOptions_SetCustomOption(&sessionOptions,DSC_JOBIDSTRING,MI_STRING,&valueOperationOptions,MI_FALSE);
        if( r != MI_RESULT_OK)
        {
            MI_OperationOptions_Delete(&sessionOptions);
            return GetCimMIError(r, extendedError,ID_CAINFRA_GET_SETCUSTOMOPTION_FAILED);
        }

        /* Perform Get*/
        MI_Session_Invoke(miSession, 0, &sessionOptions, provNamespace,
                             instance->classDecl->name, OMI_BaseResource_GetMethodName,
                             NULL, params, &callbacks,&operation);
        r = GetGetMethodResult(&operation, outputInstance , extendedError);
        MI_Instance_Delete(params);
        MI_OperationOptions_Delete(&sessionOptions);
        MI_Operation_Close(&operation);
        if( r != MI_RESULT_OK)
        {
            DSC_LOG_INFO("GetGetMethodResult r is not ok");
            return r;
        }
    }
    //Stop timer for get
    finish=CPU_GetTimeStamp();
    duration = (MI_Real64)(finish- start) / TIME_PER_SECONND;
    SetMessageInContext(ID_OUTPUT_OPERATION_END,ID_OUTPUT_ITEM_GET,provContext->lcmProviderContext);
    LogCAMessageTime(provContext->lcmProviderContext, ID_CA_GET_TIMEMESSAGE, (const MI_Real64)duration,provContext->resourceId);
    //Debug Log
    DSC_EventWriteMethodEnd(__WFUNCTION__);
    return r;
}

MI_Result PerformInventoryMethodResult(_In_ MI_Operation *operation,
                              _Outptr_result_maybenull_ MI_InstanceA *outputInstances,
                              _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Result r = MI_RESULT_OK;
    MI_Result innerR = MI_RESULT_OK;
    const MI_Instance* outInstance;
    MI_Instance * tempInstance;
    MI_Instance ** outInstanceArray;
    MI_Boolean moreResults;
    MI_Result result = MI_RESULT_INVALID_PARAMETER;
    const MI_Char *errorMessage;
    const MI_Instance *completionDetails = NULL;
    MI_Value value;
    int i;

    if (extendedError == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *extendedError = NULL;  // Explicitly set *extendedError to NULL as _Outptr_ requires setting this at least once.

    /*Get the operation result*/
    r = MI_Operation_GetInstance(operation, &outInstance, &moreResults, &result, &errorMessage, &completionDetails);
    if( result != MI_RESULT_OK)
    {
        r = result;
    }
    if( r != MI_RESULT_OK)
    {
        if( completionDetails != NULL)
        {
            innerR = DSC_MI_Instance_Clone( completionDetails, extendedError);
        }
        if( innerR != MI_RESULT_OK || completionDetails == NULL)
        {
            r = GetCimMIError(r, extendedError,ID_CAINFRA_GETINSTANCE_FAILED);
        }
        return r;
    }

    /*Get configurations  property*/
    r = DSC_MI_Instance_GetElement(outInstance, "inventory", &value, NULL, NULL, NULL);
    if( r != MI_RESULT_OK )
    {
        return GetCimMIError(r, extendedError,ID_CAINFRA_GET_OUTPUTRES_FAILED);
    }
    /*Clone the instances*/

    if (value.instancea.size > 0)
    {
    outputInstances->data = (MI_Instance**)DSC_malloc(value.instancea.size * sizeof(MI_Instance*), NitsHere());
    outputInstances->size = value.instancea.size;
    for (i = 0; i < value.instancea.size; ++i)
    {
        r = DSC_MI_Instance_Clone(value.instancea.data[i], &tempInstance);
        if( r != MI_RESULT_OK )
        {
        return GetCimMIError(r, extendedError,ID_CAINFRA_CLONE_FAILED);
        }
        outputInstances->data[i] = tempInstance;
    }
    }
    else
    {
    outputInstances->data = NULL;
    outputInstances->size = 0;
    }

    return r;
}

const MI_Char * GetResourceId( _In_ MI_Instance *inst)
{
    MI_Result r = MI_RESULT_OK;
    MI_Value value;
    r = MI_Instance_GetElement(inst, OMI_BaseResource_ResourceId, &value, NULL, NULL, NULL);
    if( r != MI_RESULT_OK)
    {
        return NULL;
    }
    return (const MI_Char*)value.string;
}

const MI_Char * GetSourceInfo( _In_ MI_Instance *inst)
{
    MI_Result r = MI_RESULT_OK;
    MI_Value value;
    // Not using DSC version as caller handles the failures as success.
    r = MI_Instance_GetElement(inst, OMI_BaseResource_SourceInfo, &value, NULL, NULL, NULL);
    if( r != MI_RESULT_OK)
    {
        return NULL;
    }
    return (const MI_Char*)value.string;
}

const MI_Char * GetModuleName( _In_ MI_Instance *inst)
{
    MI_Result r = MI_RESULT_OK;
    MI_Value value;
    // Not using DSC version as caller handles the failures as success.
    r = MI_Instance_GetElement(inst, OMI_BaseResource_ModuleName, &value, NULL, NULL, NULL);
    if( r != MI_RESULT_OK)
    {
        return NULL;
    }
    return (const MI_Char*)value.string;
}

const MI_Char * GetModuleVersion( _In_ MI_Instance *inst)
{
    MI_Result r = MI_RESULT_OK;
    MI_Value value;
    // Not using DSC version as caller handles the failures as success.
    r = MI_Instance_GetElement(inst, OMI_BaseResource_ModuleVersion, &value, NULL, NULL, NULL);
    if( r != MI_RESULT_OK)
    {
        return NULL;
    }
    return (const MI_Char*)value.string;
}


void LogCAMessage(_In_ LCMProviderContext *lcmContext,
                  _In_ MI_Uint32 messageIndex,
                  _In_z_ const MI_Char *resourceId
                  )
{
    MI_Uint32 lcmChannel = MI_WRITEMESSAGE_CHANNEL_VERBOSE;
    MI_Char wcMessage[MAX_PATH] ;
    Intlstr intlstr = Intlstr_Null;
    GetResourceString(messageIndex, &intlstr);
    //merge message and resourceId.
    if( intlstr.str && Stprintf(wcMessage, MAX_PATH, MI_T("%T"), intlstr.str) > 0 )
    {
        LCM_WriteMessage(lcmContext, resourceId, lcmChannel, wcMessage);
    }

    if( intlstr.str)
        Intlstr_Free(intlstr);
}

void LogCAMessageTime(_In_ LCMProviderContext *lcmContext,
                  _In_ MI_Uint32 messageIndex,
                  _In_ const MI_Real64 duration,
                  _In_z_ const MI_Char *resourceId
                  )
{
    MI_Uint32 lcmChannel = MI_WRITEMESSAGE_CHANNEL_VERBOSE;
    MI_Char wcTime[DURATION_SIZE] ;
    Intlstr intlstr = Intlstr_Null;
    //merge message and resourceId.Stprintf(durationMessage, durationMsgLen, msgStr, resourceId, duration)
    if( Stprintf(wcTime, DURATION_SIZE, MI_T("%0.4f"), duration) > 0 )
    {
        GetResourceString1Param(messageIndex, wcTime, &intlstr);
        if( intlstr.str )
        {
            LCM_WriteMessage(lcmContext, resourceId, lcmChannel, intlstr.str);
            Intlstr_Free(intlstr);
        }
    }
}

void LogCAProgress( _In_ LCMProviderContext *lcmContext,
                         _In_z_ const MI_Char * currentOperation,
                         _In_z_ const MI_Char * statusDescription,
                         _In_ MI_Uint32 currentResourceIndex,
                         _In_ MI_Uint32 totalResource)
{
    MI_Uint32 percentComplete = 100;
    MI_Uint32 secondsRemaining = totalResource - currentResourceIndex;

    if( currentResourceIndex+1 != totalResource)
    {
        percentComplete = (MI_Uint32) (100/totalResource) *( currentResourceIndex+1);
    }
    LCM_WriteProgress(lcmContext, CA_ACTIVITY_NAME, currentOperation, statusDescription, percentComplete, secondsRemaining);
}

void SetMessageInContext(MI_Uint32 msgOperation,MI_Uint32 msgItem,LCMProviderContext *lcmContext)
{
    if(lcmContext!=NULL)
    {
        lcmContext->messageItem=msgItem;
        lcmContext->messageOperation=msgOperation;
    }
}

//Stop the current configuration
//if force is specified then we try to stop the current operation
//otherwise we will wait until the current operation finishes, and then cancel the remaining
MI_Result  MI_CALL StopCurrentConfiguration(_Outptr_result_maybenull_ MI_Instance **extendedError, BOOL force)
{
    MI_Result r = MI_RESULT_OK;
    DWORD dwWaitResult;

    if (extendedError == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *extendedError = NULL;  // Explicitly set *extendedError to NULL as _Outptr_ requires setting this at least once.

    g_CancelConfiguration = TRUE;
    if (force == TRUE)
    {
        DSC_EventWriteMessageWaitCurrentConfig();
        RecursiveLock_Acquire(&g_cs_CurrentWmiv2Operation);

        if (g_CurrentWmiv2Operation != NULL)
        {
            r = MI_Operation_Cancel((MI_Operation*)g_CurrentWmiv2Operation, MI_REASON_NONE);
            if( r == MI_RESULT_OK)
            {
                DSC_EventWriteConfigurationForcelyStopped();
            }
        }

        RecursiveLock_Release(&g_cs_CurrentWmiv2Operation);

        if (r != MI_RESULT_OK)
        {
            g_CancelConfiguration = FALSE;
            return GetCimMIError(r, extendedError,ID_CA_CANCELWMIV2_FAILED);
        }
    }

    dwWaitResult = Sem_TimedWait(&g_h_ConfigurationStoppedEvent, STOP_CONFIGURATIONT_TIMEOUT);
    g_CancelConfiguration = FALSE;
    if (dwWaitResult != 0)
    {
        return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_CA_FAILED_TO_WAIT_EVENT);
    }

    return MI_RESULT_OK;
}

MI_Char* RunCommand(const MI_Char* command)
{
    MI_Char* result = NULL;
    FILE * fp;
    const int bufferSize = 5000;
    char buffer[bufferSize * 10 + 1];
    char curBuffer[bufferSize + 1];
    size_t cur_loc = 0;
    size_t count = 0;
    int status;

    curBuffer[bufferSize] = '\0';

    fp = popen(command, "r");
    if (fp == NULL)
    {
        return NULL;
    }

    while (fgets(curBuffer, 5000, fp) != NULL)
    {
        count = strlen(curBuffer);
        if (count + cur_loc > bufferSize * 10)
          {
            // Too much data printed to stdout of the command.  Let's just cut it short here.
            // Since this isn't a PAL function, and we know that we won't get anywhere near this limit
            // in any reasonable normal or error situation, this should suffice.
            break;
          }
        memcpy(buffer + cur_loc, curBuffer, count);
        cur_loc += count;

    }

    buffer[cur_loc] = '\0';

    pclose(fp);
    result = (MI_Char*)DSC_malloc((cur_loc + 1) * sizeof(MI_Char*), NitsHere());
    memcpy(result, buffer, cur_loc + 1);

    return result;
}


extern MI_Result Pull_Register(MI_Char* serverURL,
                               MI_Char* agentId,
                               MI_Char* x_ms_header,
                               MI_Char* auth_header,
                               MI_Char* requestBody,
                               _Outptr_result_maybenull_ MI_Instance **extendedError);

static const char * const s_ManagerInstanceNames[] = { "",
                                                       "ConfigurationRepository",
                                                       "ResourceRepository",
                                                       "ReportServer" };


MI_Result MI_CALL Do_Register(
    _In_ MI_Instance *metaConfig,
    _In_ MI_Instance *managerInstance,
    _In_z_ MI_Char *agentId,
    _In_z_ MI_Char *thumbprint,
    _In_ MI_Instance *registrationPayload,
    _In_ MI_StringA *configurationNames,
    _In_ MI_Uint32 typeOfManagerInstance,
    _Outptr_result_maybenull_z_  MI_Char** result,
    _Out_ MI_Uint32* getActionStatusCode,
    _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Result r;
    const size_t c_bufferSize = 2048;
    MI_Char buffer[c_bufferSize];
    MI_Char* requestBody;
    MI_Char* header;
    MI_Char* x_ms_header;
    MI_Char* auth_header;
    char * saveptr;
    MI_Value val;

    r = MI_Instance_GetElement(managerInstance, "RegistrationKey", &val, NULL, NULL, NULL);

    if (typeOfManagerInstance == 1)
    {
        char* configName = "";
        if (configurationNames->size != 0)
        {
            configName = configurationNames->data[0];
        }
        const char * bodyFormatString = "%s/RegisterHelper.sh body %s %s %s";
        const char * headerFormatString = "%s/RegisterHelper.sh header %s %s %s";
        snprintf(buffer, c_bufferSize, bodyFormatString, DSC_SCRIPT_PATH, s_ManagerInstanceNames[typeOfManagerInstance], val.string, configName);
        requestBody = RunCommand(buffer);
        snprintf(buffer, c_bufferSize, headerFormatString, DSC_SCRIPT_PATH, s_ManagerInstanceNames[typeOfManagerInstance], val.string, configName);
        header = RunCommand(buffer);
    }
    else
    {
        const char * bodyFormatString = "%s/RegisterHelper.sh body %s %s";
        const char * headerFormatString = "%s/RegisterHelper.sh header %s %s";
        snprintf(buffer, c_bufferSize, bodyFormatString, DSC_SCRIPT_PATH, s_ManagerInstanceNames[typeOfManagerInstance], val.string);
        requestBody = RunCommand(buffer);
        snprintf(buffer, c_bufferSize, headerFormatString, DSC_SCRIPT_PATH, s_ManagerInstanceNames[typeOfManagerInstance], val.string);
        header = RunCommand(buffer);
    }
    if(requestBody == NULL) {
        return MI_RESULT_FAILED;
    }
    if(header == NULL) {
        DSC_free(requestBody);
        return MI_RESULT_FAILED;
    }

    x_ms_header = strtok_r(header, "\n", &saveptr);
    auth_header = strtok_r(NULL, "\n", &saveptr);

    r = MI_Instance_GetElement(managerInstance, "ServerURL", &val, NULL, NULL, NULL);

    r = Pull_Register(val.string, agentId, x_ms_header, auth_header, requestBody, extendedError);
    DSC_free(requestBody);
    DSC_free(header);
    if (r != MI_RESULT_OK)
    {
        *result = DSC_strdup("FailedToRegister");
        *getActionStatusCode = 32;
        return r;
    }

    *result = DSC_strdup(REGISTER_STATUSCODE_CREATED);
    *getActionStatusCode = 0;
    return MI_RESULT_OK;
}

MI_Result Inventory_WMIv2Provider(_In_ ProviderCallbackContext *provContext,
                               _In_ MI_Application *miApp,
                               _In_ MI_Session *miSession,
                               _In_ MI_Instance *instance,
                               _In_ const MI_Instance *regInstance,
                               _Outptr_result_maybenull_ MI_InstanceA *outputInstances,
                                _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Result r = MI_RESULT_OK;
    const MI_Char *provNamespace = NULL;
    MI_Operation operation = MI_OPERATION_NULL;
    MI_Instance *params = NULL;
    MI_Value value;
    MI_Value valueOperationOptions;
    MI_OperationCallbacks callbacks = MI_OPERATIONCALLBACKS_NULL;
    MI_Real64 duration;
    MI_OperationOptions sessionOptions ;

    ptrdiff_t finish,start;
    //Debug Log
    DSC_EventWriteEngineMethodParameters(__WFUNCTION__,
                                        instance->classDecl->name,
                                        provContext->resourceId,
                                        0,
                                        provContext->lcmProviderContext->executionMode,
                                        regInstance->nameSpace);
    DSC_EventWriteMessageWmiGet(instance->classDecl->name,provContext->resourceId);

    callbacks.writeMessage = DoWriteMessage;
    /* Sign up for progress only if we are in online mode*/
    if( (LCM_EXECUTIONMODE_ONLINE & provContext->lcmProviderContext->executionMode) == LCM_EXECUTIONMODE_ONLINE)
    {
        callbacks.writeProgress = DoWriteProgress;
    }

    callbacks.callbackContext = (void *)(provContext);

    if (extendedError == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *extendedError = NULL;  // Explicitly set *extendedError to NULL as _Outptr_ requires setting this at least once.

    outputInstances->data = NULL;
    outputInstances->size = 0;

    start=CPU_GetTimeStamp();
    SetMessageInContext(ID_OUTPUT_OPERATION_START,ID_OUTPUT_ITEM_INVENTORY,provContext->lcmProviderContext);
    LogCAMessage(provContext->lcmProviderContext, ID_OUTPUT_EMPTYSTRING, provContext->resourceId);
    DSC_EventWriteMessageInvokingSession(provNamespace,instance->classDecl->name,OMI_BaseResource_InventoryMethodName);

    /*Get target namespace*/
    r = DSC_MI_Instance_GetElement(regInstance, MSFT_CimConfigurationProviderRegistration_Namespace, &value, NULL, NULL, NULL);
    if( r != MI_RESULT_OK )
    {
    return GetCimMIError(r, extendedError,ID_CAINFRA_INVENTORY_NAMESPACE_FAILED);
    }
    provNamespace = value.string;

    /*Set input parameters*/
    r = DSC_MI_Application_NewInstance(miApp, MI_T("__Parameters"), NULL, &params);
    if( r != MI_RESULT_OK )
    {
    return GetCimMIError(r, extendedError,ID_CAINFRA_INVENTORY_NEWAPPLICATIONINSTANCE_FAILED);
    }
    value.instance = instance;
    r = DSC_MI_Instance_AddElement(params, OMI_BaseResource_Method_InputResource, &value, MI_INSTANCE, 0 );
    if( r != MI_RESULT_OK)
    {
    MI_Instance_Delete(params);
    return GetCimMIError(r, extendedError,ID_CAINFRA_INVENTORY_ADDELEM_FAILED);
    }
    r = MI_Application_NewOperationOptions(miApp, MI_FALSE, &sessionOptions);
    if( r != MI_RESULT_OK )
    {
    return GetCimMIError(r, extendedError,ID_CAINFRA_INVENTORY_NEWOPERATIONOPTIONS_FAILED);
    }
    valueOperationOptions.string=g_ConfigurationDetails.jobGuidString;
    r =MI_OperationOptions_SetCustomOption(&sessionOptions,DSC_JOBIDSTRING,MI_STRING,&valueOperationOptions,MI_FALSE);
    if( r != MI_RESULT_OK)
    {
    MI_OperationOptions_Delete(&sessionOptions);
    return GetCimMIError(r, extendedError,ID_CAINFRA_INVENTORY_SETCUSTOMOPTION_FAILED);
    }

    /* Perform Inventory*/

    MI_Session_Invoke(miSession, 0, &sessionOptions, provNamespace,
              instance->classDecl->name, OMI_BaseResource_InventoryMethodName,
              NULL, params, &callbacks,&operation);

    r = PerformInventoryMethodResult(&operation, outputInstances, extendedError);
    MI_Instance_Delete(params);
    MI_OperationOptions_Delete(&sessionOptions);
    MI_Operation_Close(&operation);
    if( r != MI_RESULT_OK)
    {
    return r;
    }

    //Stop timer for get
    finish=CPU_GetTimeStamp();
    duration = (MI_Real64)(finish- start) / TIME_PER_SECONND;
    SetMessageInContext(ID_OUTPUT_OPERATION_END,ID_OUTPUT_ITEM_INVENTORY,provContext->lcmProviderContext);
    LogCAMessageTime(provContext->lcmProviderContext, ID_CA_INVENTORY_TIMEMESSAGE, (const MI_Real64)duration,provContext->resourceId);
    //Debug Log
    DSC_EventWriteMethodEnd(__WFUNCTION__);
    return r;
}

/*Get the current configuration for the desired state objects*/
MI_Result MI_CALL PerformInventory( _In_ LCMProviderContext *lcmContext,
                                    _In_ MI_Uint32 flags,
                                    _In_ MI_InstanceA *instanceA,
                                    _In_ ModuleManager *moduleManager,
                                    _In_ MI_Instance *documentIns,
                                    _Out_ MI_InstanceA *outInstances,
                                    _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Result r = MI_RESULT_OK;
    MI_Instance *filteredInstance = NULL;
    MI_Instance *regInstance = NULL;
    ModuleLoaderObject *moduleLoader = NULL;
    MI_InstanceA inventoryInstancesResult;
    MI_Session miSession = MI_SESSION_NULL;
    ProviderCallbackContext providerContext = {0};
    MI_InstanceA * inventoryInstancesResultArray = NULL;

    MI_Char *certificateid = NULL;
    MI_Boolean bEncryptionEnabled = MI_FALSE;
    MI_Uint32 xCount = 0;
    MI_Uint32 index = 0;
    MI_Instance * tempInstance;
    MI_Instance ** tempInstanceArray;
    MI_Uint32 i = 0;
    MI_Uint32 j = 0;
    MI_Uint32 totalInstanceCount = 0;

    if( outInstances == NULL  || NitsShouldFault(NitsHere(), NitsAutomatic))
    {
        return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError,ID_CAINFRA_INVENTORY_NULLPARAM);
    }

    memset(outInstances, 0, sizeof(MI_InstanceA));
    if( instanceA == 0 || moduleManager == 0 || instanceA->size == 0  || NitsShouldFault(NitsHere(), NitsAutomatic))
    {
        return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError,ID_CAINFRA_DEPENDCY_NULLPARAM);
    }

    if (extendedError == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *extendedError = NULL;  // Explicitly set *extendedError to NULL as _Outptr_ requires setting this at least once.

    providerContext.lcmProviderContext = lcmContext;

    r = GetDocumentEncryptionSetting(documentIns, &bEncryptionEnabled, &certificateid, extendedError);
    if( r != MI_RESULT_OK )
    {
        return r;
    }

    outInstances->data = NULL;
    outInstances->size = 0;
    inventoryInstancesResult.data = NULL;
    inventoryInstancesResult.size = 0;

    inventoryInstancesResultArray = (MI_InstanceA*)DSC_malloc(sizeof(MI_InstanceA) * instanceA->size, NitsHere());

    moduleLoader = (ModuleLoaderObject*) moduleManager->reserved2;

    /*Create MI session*/
    r = DSC_MI_Application_NewSession(moduleLoader->application, NULL, NULL, NULL, NULL, NULL, &miSession);
    if( r != MI_RESULT_OK)
    {
        return GetCimMIError(r, extendedError,ID_CAINFRA_NEWSESSION_FAILED);
    }

    // Instantiate native resource manager, responsible to load/unload native resource provider.
    r = NativeResourceManager_New(&providerContext, &(providerContext.nativeResourceManager));
    if( r != MI_RESULT_OK)
    {
        return r;
    }

    /*Assuming the dependencies is implicit (the order in which instances are specified in instance document). */
    /*Get the instance compatible with the provider.*/
    for (xCount = 0 ; xCount < instanceA->size ; xCount++)
    {
        /* Get Registration Instance to find registration information.*/
        r = moduleManager->ft->GetRegistrationInstance(moduleManager, instanceA->data[xCount]->classDecl->name, (const MI_Instance **)&regInstance, extendedError);
        if( r != MI_RESULT_OK)
        {
            MI_Session_Close(&miSession, NULL, NULL);
            return r;
        }

        /*Get provider compatible instance*/
        r = moduleManager->ft->GetProviderCompatibleInstance(moduleManager, instanceA->data[xCount], &filteredInstance, extendedError);
        if( r != MI_RESULT_OK)
        {
            MI_Session_Close(&miSession, NULL, NULL);
            return r;
        }
        providerContext.resourceId = GetResourceId(instanceA->data[xCount]);

        /* Get the inventory.*/
        r = PerformInventoryState(&providerContext, moduleLoader->application, &miSession, filteredInstance, regInstance, &inventoryInstancesResult, extendedError);
        MI_Instance_Delete(filteredInstance);
        filteredInstance = NULL;

        if( r != MI_RESULT_OK)
        {
            Intlstr intlstr = Intlstr_Null;
            GetResourceString(ID_LCMHELPER_GETINVENTORY_ERROR, &intlstr);

            DSC_EventWriteLCMSendConfigurationError(CA_ACTIVITY_NAME,
                r,
                (MI_Char*)intlstr.str,
                providerContext.resourceId,
                GetSourceInfo(instanceA->data[xCount]),
                (MI_Char*)GetErrorDetail(*extendedError));

            if( intlstr.str)
                Intlstr_Free(intlstr);

            MI_Session_Close(&miSession, NULL, NULL);
            return r;
        }

        inventoryInstancesResultArray[xCount].data = inventoryInstancesResult.data;
        inventoryInstancesResultArray[xCount].size = inventoryInstancesResult.size;
        totalInstanceCount += inventoryInstancesResult.size;
    }

    if (totalInstanceCount > 0)
    {
    outInstances->data = (MI_Instance**)DSC_malloc(sizeof(MI_Instance*) * totalInstanceCount, NitsHere());
    outInstances->size = totalInstanceCount;

    xCount = 0;
    for (i = 0; i < instanceA->size; ++i)
    {
        for (j = 0; j < inventoryInstancesResultArray[i].size; ++j)
        {
        outInstances->data[xCount] = inventoryInstancesResultArray[i].data[j];
        xCount += 1;
        }
    }
    }
    else
    {
        outInstances->data = NULL;
        outInstances->size = 0;
    }

    MI_Session_Close(&miSession, NULL, NULL);

    // Ignore this call and do not delete the native resource manager instance. It will be cleaned up when the host process goes away.
    NativeResourceManager_Delete(providerContext.nativeResourceManager);

#if defined(BUILD_OMS)
    struct sigaction sa;
    sa.sa_handler = &handleSIGCHLDSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, 0);
    DSC_EventWriteMessageRegisterProcessHandler();
#endif

    return r;
}


#if defined(BUILD_OMS)
void handleSIGCHLDSignal(int sig)
{
    int saved_errorno = errno;

    // TODO: Maybe addressed later.
    DSC_EventWriteMessageWaitForChildProcess();

    // OMS providers registers the SIGINT handler but may not have
    // an opportunity to clean up before getting unloaded.
    // This code is to ensure that OMSConfig picks up the work left off by
    // the OMS providers of cleaning up zombie processes.
    // Only one instance of SIGCHLD can be queued, so it becomes necessary to reap
    // several zombie processes during one invocation of the handler function.
    while (waitpid((pid_t)(-1), 0, WNOHANG) > 0) { }

    errno = saved_errorno;
}
#endif