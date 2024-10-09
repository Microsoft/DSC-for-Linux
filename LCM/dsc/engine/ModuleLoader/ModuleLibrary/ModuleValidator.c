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
#include "micodec.h"
#include "EngineHelper.h"
#include "ModuleHandler.h"
#include "ModuleHandlerInternal.h"
#include "ModuleValidator.h"
#include "EventWrapper.h"
#include "Resources_LCM.h"



#define OMI_BaseResource_GetMethodCount 4
#define OMI_BaseResource_TestMethodCount 5
#define OMI_BaseResource_SetMethodCount 4


BaseResourceConfiguration g_MetaConfigurationResourceProperties[] =
{
    { OMI_MetaConfigurationResource_ResourceId, MI_STRING },
    { OMI_MetaConfigurationResource_SourceInfo, MI_STRING },
    { NULL, 0 }
};

BaseResourceConfiguration g_WebDownloadManagerProperties[] =
{
    { "ServerURL", MI_STRING },
    { "CertificateID", MI_STRING },
    { "AllowUnsecureConnection" , MI_BOOLEAN },
    { NULL, 0 }
};


MI_Result  IgnoreValidation(_In_ MI_Class *configDocumentClass,
                            _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    return MI_RESULT_OK;
}

InfraSchemaValidator g_InfraSchemaValidators[] =
{
    {BASE_RESOURCE_CLASSNAME, ValidateBaseResourceConfigurationClass},
    {BASE_DOCUMENT_CLASSNAME, ValidateConfigurationDocumentClass},
    {BASE_REGISTRATION, ValidateBaseResourceRegistrationClass},
    {BASE_REGISTRATION_WMIV2PROVIDER, ValidateCimResourceRegistrationClass},
    {BASE_REGISTRATION_PSPROVIDER, ValidatePSResourceRegistrationClass},
    {METACONFIG_CLASSNAME, ValidateMetaConfigurationClass},
    {MSFT_BASECREDENTIAL_CLASSNAME, ValidateBaseCredClass},
    {MSFT_KEYVALUEPAIR_CLASSNAME, ValidateKeyValuePairClass},
    { MSFT_PARTIALCONFIGURATION_CLASSNAME,            ValidateClassPropertiesForMetaConfResourceChildren },
    { METACONF_RESOURCE_CLASSNAME,                    ValidateMetaConfResourceProperties},
    { OMI_CONFIGURATIONDOWNLOADMANAGER_CLASSNAME,     IgnoreValidation },
    { MSFT_WEBCONFIGURATIONDOWNLOADMANAGER_CLASSNAME, IgnoreValidation },
    { MSFT_FILECONFIGURATIONDOWNLOADMANAGER_CLASSNAME, IgnoreValidation },

    { OMI_RESOURCEMODULEMANAGER_CLASSNAME,     IgnoreValidation },
    { MSFT_WEBRESOURCEMODULEMANAGER_CLASSNAME, IgnoreValidation },
    { MSFT_FILERESOURCEMODULEMANAGER_CLASSNAME, IgnoreValidation },

    { OMI_REPORTMANAGER_CLASSNAME,     IgnoreValidation },
    { MSFT_OAASREPORTMANAGER_CLASSNAME, IgnoreValidation },
    { MSFT_WEBREPORTMANAGER_CLASSNAME, IgnoreValidation },

    {NULL, NULL}
};

BaseResourceConfiguration g_ConfigurationDocumentProperties[] =
{
    { OMI_ConfigurationDocument_Version,        MI_STRING},
    { OMI_ConfigurationDocument_Author,         MI_STRING},
    { OMI_ConfigurationDocument_Copyright,      MI_STRING},
    { OMI_ConfigurationDocument_HelpInfoUri,    MI_STRING},
    { OMI_ConfigurationDocument_ContentType,    MI_STRING},
    { OMI_ConfigurationDocument_GenerationDate, MI_STRING},
    { OMI_ConfigurationDocument_GenerationHost, MI_STRING },
    { OMI_ConfigurationDocument_Name,           MI_STRING },
    { OMI_ConfigurationDocument_MinimumCompatibleVersion,                          MI_STRING },
    { OMI_ConfigurationDocument_CompatibleVersionAdditionalProperties, MI_STRINGA },
    { OMI_ConfigurationDocument_DocumentType, MI_STRING },

    {NULL,                                      0}
};

BaseResourceConfiguration g_ConfigurationDownloadManagerProperties[] =
{
    {NULL,                                                        0}
};

BaseResourceConfiguration g_PSResourceProperties[] =
{
    {MSFT_PSConfigurationProviderRegistration_ModuleName,         MI_STRING},
    {NULL,                                                        0}
};

BaseResourceConfiguration g_CimResourceProperties[] =
{
    {MSFT_CimConfigurationProviderRegistration_Namespace,         MI_STRING},
    {NULL,                                                        0}
};

BaseResourceConfiguration g_BaseRegistrationProperties[] =
{
    {MSFT_BaseConfigurationProviderRegistration_ClassName,                  MI_STRING},
    {MSFT_BaseConfigurationProviderRegistration_DSCEngineCompatVersion,     MI_STRING},
    {MSFT_BaseConfigurationProviderRegistration_DSCModuleVersion,           MI_STRING},
    {NULL,                  0}
};

BaseResourceConfiguration g_BaseResourceProperties[] =
{
    {OMI_BaseResource_ResourceId,         MI_STRING},
    {OMI_BaseResource_SourceInfo,         MI_STRING},
    {OMI_BaseResource_DependsOn,           MI_STRINGA},
    {OMI_BaseResource_ModuleName,         MI_STRING},
    {OMI_BaseResource_ModuleVersion,      MI_STRING},
    {OMI_BaseResource_ConfigurationName,    MI_STRING},
    {NULL,                                              0}
};

PropertyQualifier g_BaseResourceQualifiers[] =
{
    {OMI_BaseResource_ResourceId,           QUALIFIER_REQUIRED},
    {OMI_BaseResource_SourceInfo,           QUALIFIER_WRITE},
    {OMI_BaseResource_DependsOn,            QUALIFIER_WRITE},
    {OMI_BaseResource_ModuleName,           QUALIFIER_REQUIRED},
    {OMI_BaseResource_ModuleVersion,        QUALIFIER_REQUIRED},
    {OMI_BaseResource_ConfigurationName,    QUALIFIER_WRITE},
    {NULL,                                              NULL}
};

BaseResourceConfiguration g_MetaConfigProperties[] =
{
    { MSFT_DSCMetaConfiguration_ConfigurationModeFrequencyMins,             MI_UINT32},
    { MSFT_DSCMetaConfiguration_RebootNodeIfNeeded,                         MI_BOOLEAN},
    { MSFT_DSCMetaConfiguration_Credential,                                 MI_INSTANCE},
    { MSFT_DSCMetaConfiguration_ConfigurationMode,                          MI_STRING},
    { MSFT_DSCMetaConfiguration_RefreshMode,                                MI_STRING},
    { MSFT_DSCMetaConfiguration_CertificateID,                              MI_STRING},
    { MSFT_DSCMetaConfiguration_ConfigurationID,                            MI_STRING},
    { MSFT_DSCMetaConfiguration_DownloadManagerName,                        MI_STRING},
    { MSFT_DSCMetaConfiguration_DownloadManagerCustomData,                  MI_INSTANCEA},
    { MSFT_DSCMetaConfiguration_RefreshFrequencyMins,                       MI_UINT32},
    { MSFT_DSCMetaConfiguration_AllowModuleOverwrite,                       MI_BOOLEAN},
    { MSFT_DSCMetaConfiguration_LocalConfigurationManagerState,             MI_STRING},
    { MSFT_DSCMetaConfiguration_ConfigurationDownloadManagers,              MI_INSTANCEA },
    { MSFT_DSCMetaConfiguration_ResourceModuleManagers,                     MI_INSTANCEA },
    { MSFT_DSCMetaConfiguration_ReportManagers,                             MI_INSTANCEA },
    { MSFT_DSCMetaConfiguration_PartialConfigurations,                      MI_INSTANCEA },
    { MSFT_DSCMetaConfiguration_AgentId,                                    MI_STRING },
    { MSFT_DSCMetaConfiguration_DebugMode,                                  MI_STRINGA },
    { MSFT_DSCMetaConfiguration_LCMVersion,                                 MI_STRING},
    { MSFT_DSCMetaConfiguration_LCMCompatibleVersions,                      MI_STRINGA},
    { MSFT_DSCMetaConfiguration_LCMState,                                   MI_STRING},
    { MSFT_DSCMetaConfiguration_LCMStateDetail,                             MI_STRING},
    { MSFT_DSCMetaConfiguration_StatusRetentionTimeInDays,                  MI_UINT32 },
    { MSFT_DSCMetaConfiguration_ActionAfterReboot,                          MI_STRING },
    { MSFT_DSCMetaConfiguration_EnableSignatureValidation,                  MI_BOOLEAN},
    { MSFT_DSCMetaConfiguration_DisableModuleSignatureValidation,           MI_BOOLEAN},
    {NULL,                                                                  0}
};

BaseResourceConfiguration g_PartialConfigurationProperties[] =
{
    { MSFT_PartialConfiguration_Description,            MI_STRING },
    { MSFT_PartialConfiguration_ExclusiveResources,     MI_STRINGA },
    { MSFT_PartialConfiguration_ConfigurationSource,    MI_STRINGA },
    { MSFT_PartialConfiguration_ResourceModuleSource,   MI_STRINGA },
    { MSFT_PartialConfiguration_DependsOn,              MI_STRINGA },
    { MSFT_PartialConfiguration_RefreshMode,            MI_STRING },
    { NULL, 0 }
};


BaseResourceConfiguration g_BaseCredProperties[] =
{
    {MSFT_Credential_UserName, MI_STRING},
    {MSFT_Credential_Password, MI_STRING},
    {NULL,                                 0}
};

BaseResourceConfiguration g_KeyValuePairProperties[] =
{
    {MSFT_KeyValuePair_Key, MI_STRING},
    {MSFT_KeyValuePair_Value, MI_STRING},
    {NULL,                                  0}
};


MI_Result ValidateInfrastructureSchema(_In_ MI_ClassA *miClassArray,
                                       _Outptr_result_maybenull_ MI_Instance **extendedError )
{
    MI_Result r = MI_RESULT_OK;
    MI_Uint32 xCount = 0, yCount = 0;
    if( extendedError )
        *extendedError = NULL;
    SetJobId();
    // This Event does not produce any useful information and increasing log file size, disabling it. 
    // DSC_EventWriteValidatingInfrastructureSchema(miClassArray->size);
    //Validate various infrastructure classes
    for( xCount = 0 ; xCount < miClassArray->size ; xCount++)
    {
        for (yCount = 0; g_InfraSchemaValidators[yCount].wszClassName != NULL; yCount++)
        {
            if( Tcscasecmp(miClassArray->data[xCount]->classDecl->name, g_InfraSchemaValidators[yCount].wszClassName) == 0 )
            {
                break;
            }
        }

        if( g_InfraSchemaValidators[yCount].wszClassName == NULL || NitsShouldFault(NitsHere(), NitsAutomatic))
        {
            return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError, ID_MODMAN_VALIDATE_INFRASCHEMA);
        }

        r = g_InfraSchemaValidators[yCount].Validator(miClassArray->data[xCount], extendedError);
        if( r != MI_RESULT_OK)
        {
            return r;
        }
    }
    return r;
}


/*Validate Provider registration against schema*/

MI_Result ValidateProviderRegistrationAgainstSchema(_In_ MI_ClassA *miClassArray,
                                                    _In_ MI_InstanceA *miRegistrationArray,
                                                    _Outptr_result_maybenull_ MI_Instance **extendedError)

{
    MI_Result r = MI_RESULT_OK;
    MI_Uint32 xCount=0 , yCount = 0;
    MI_Uint32 registrationFoundCount = 0, schemaFoundCount = 0;
    MI_Value value;

    if (extendedError == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *extendedError = NULL;  // Explicitly set *extendedError to NULL as _Outptr_ requires setting this at least once.

    // This Event does not produce any useful information and increasing log file size, disabling it. 
    // DSC_EventWriteValidatingProviderRegistration(miClassArray->size,miRegistrationArray->size);

    //Test1 : only 1 registration per class
    //Test2 : no class left out without registration(except for meta config), also validates that registration not targeting more than 1 class.
    //Test3: no registration instance left out without class
    //Test4 : no two classes with same name

    for (xCount = 0 ; xCount < miClassArray->size ; xCount++)
    {
        if (miClassArray->data[xCount]->classDecl->superClass &&
            Tcscasecmp(miClassArray->data[xCount]->classDecl->superClass, BASE_RESOURCE_CLASSNAME) == 0)
        {
            schemaFoundCount++;
        }

        //Test4
        for (yCount = xCount + 1 ; yCount <  miClassArray->size ; yCount++)
        {
            if (Tcscasecmp(miClassArray->data[yCount]->classDecl->name, miClassArray->data[xCount]->classDecl->name) == 0  || NitsShouldFault(NitsHere(), NitsAutomatic))
            {
                return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError, ID_MODMAN_VALIDATE_PROVREG_MULTI);
            }
        }
    }

    for (xCount = 0 ; xCount < miRegistrationArray->size ; xCount++)
    {
        for (yCount = 0 ; yCount < miClassArray->size ; yCount++)
        {
            if ((miClassArray->data[yCount]->classDecl->superClass &&
                 Tcscasecmp(miClassArray->data[yCount]->classDecl->superClass, BASE_RESOURCE_CLASSNAME) == 0) ||
                 Tcscasecmp(miClassArray->data[xCount]->classDecl->name, METACONFIG_CLASSNAME) == 0)
            {
                r = DSC_MI_Instance_GetElement(miRegistrationArray->data[xCount], MSFT_BaseConfigurationProviderRegistration_ClassName,
                    &value, NULL, NULL, NULL);
                if (r != MI_RESULT_OK )
                {
                    return GetCimMIError(r, extendedError, ID_MODMAN_VALIDATE_PROVREG_MANDATORY);
                }

                if (Tcscasecmp(value.string, miClassArray->data[yCount]->classDecl->name) == 0 )
                {
                    // Test1
                    registrationFoundCount++;
                    break;
                }
            }
        }

        // Test3
        if (yCount == miClassArray->size  || NitsShouldFault(NitsHere(), NitsAutomatic))
        {
            return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError, ID_MODMAN_VALIDATE_PROVREG_NOCLASS);
        }
    }

    // Test2
    if (registrationFoundCount != schemaFoundCount  || NitsShouldFault(NitsHere(), NitsAutomatic))
    {
        return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError, ID_MODMAN_VALIDATE_PROVREG_NOREG);
    }

    return r;
}


MI_Result ValidateSchema(_In_ MI_ClassA *miClassArray,
                         _In_ MI_Uint32 classIndex,
                         _Inout_updates_(resSize) MI_Boolean *bResourceVisited,
                         _In_ MI_Uint32 resSize,
                         _In_ MI_Boolean bConfigurationResource,
                         _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Result r = MI_RESULT_OK;
    MI_Uint32 xCount = 0, yCount = 0;
    const MI_ClassDecl *classToCheck = NULL;
    MI_Uint32 keyPropertyCount = 0;
    MI_Uint32 propertyBitMask = 0; // 1 = Read, 2 = Write, 4 = Key, 8 = Required
     if( extendedError )
        *extendedError = NULL;

    //PrintClass(miClassArray->data[classIndex]);

    if( classIndex >= miClassArray->size || NitsShouldFault(NitsHere(), NitsAutomatic))
    {
        return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError, ID_MODMAN_VALIDATE_SCHEMA_INVPARAM);
    }

    classToCheck = miClassArray->data[classIndex]->classDecl;
    // This Event does not produce any useful information and increasing log file size, disabling it. 
    // DSC_EventWriteValidatingSchema(classToCheck->name,classIndex,miClassArray->size);

    /*Test7*/
    if( classToCheck->numMethods != 0  || NitsShouldFault(NitsHere(), NitsAutomatic))
    {
        return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError, ID_MODMAN_VALIDATE_SCHEMA_NOMETHOD);
    }

    /*Test 4*/
    // validate class qualifiers
    for( xCount = 0 ; xCount < classToCheck->numQualifiers; xCount++)
    {
        if( Tcscasecmp(QUALIFIER_VERSION, classToCheck->qualifiers[xCount]->name) == 0 )
        {
            break;
        }
    }
    if( xCount == classToCheck->numQualifiers || NitsShouldFault(NitsHere(), NitsAutomatic))
    {
        return GetCimMIError(MI_RESULT_NOT_FOUND, extendedError, ID_MODMAN_VALIDATE_SCHEMA_VERSIONQUAL);
    }

    /*Test 2*/
    /*Rule1: Read and (Key or Required or Write) can't coexist*/
    /*Rule2: Write and (Read) can't coexist*/
    /*Rule3: Key and (Read) can't coexist*/
    /*Rule4: Required and (Read) can't coexist*/
    for( xCount = 0 ; xCount < classToCheck->numProperties; xCount++)
    {
        propertyBitMask = 0;
        /*Test5: Reference types are not supported.*/
        if( classToCheck->properties[xCount]->type == MI_REFERENCE ||
            classToCheck->properties[xCount]->type == MI_REFERENCEA  || NitsShouldFault(NitsHere(), NitsAutomatic))
        {
            return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError, ID_MODMAN_VALIDATE_SCHEMA_NOSUPP_REF);
        }
        if( classToCheck->properties[xCount]->flags & MI_FLAG_READONLY )
        {
            propertyBitMask |= PROPERTY_BITMASK_READ;
        }
        if( classToCheck->properties[xCount]->flags & MI_FLAG_KEY )
        {
            propertyBitMask |= PROPERTY_BITMASK_KEY;
            keyPropertyCount++;
        }
        if( classToCheck->properties[xCount]->flags & MI_FLAG_REQUIRED )
        {
            propertyBitMask |= PROPERTY_BITMASK_REQUIRED;
        }

        for( yCount = 0 ; yCount < classToCheck->properties[xCount]->numQualifiers; yCount++)
        {
            const MI_Qualifier *classQual = classToCheck->properties[xCount]->qualifiers[yCount];
            if( Tcscasecmp(classQual->name, QUALIFIER_WRITE) == 0 )
            {
                propertyBitMask |= PROPERTY_BITMASK_WRITE;
                break;
            }
        }

        // Do actual validation
        if( ((propertyBitMask & PROPERTY_BITMASK_READ) && ( (propertyBitMask & PROPERTY_BITMASK_ALL) & ~(PROPERTY_BITMASK_READ)))  || (NitsShouldFault(NitsHere(), NitsAutomatic)))
        {
            return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError, ID_MODMAN_VALIDATE_SCHEMA_NOMANDATORY_QUALIFIER);
        }

        /*Test6: Embedded object type support*/
        if( classToCheck->properties[xCount]->className != NULL)
        {
            // check for class in existing classes
            for( yCount = 0 ; yCount < miClassArray->size && yCount < resSize; yCount++)
            {
                if( Tcscasecmp(miClassArray->data[yCount]->classDecl->name, classToCheck->properties[xCount]->className) == 0 )
                {
                    if( bResourceVisited[yCount] == MI_TRUE) // Already processed
                    {
                        break;
                    }
                    else
                    {
                        bResourceVisited[yCount] = MI_TRUE;
                        r = ValidateSchema(miClassArray, yCount, bResourceVisited, resSize, MI_FALSE,extendedError);
                        if( r != MI_RESULT_OK)
                        {
                            return r;
                        }
                    }
                }
            }

        }
    }
    if( bConfigurationResource &&
        Tcscasecmp(miClassArray->data[classIndex]->classDecl->name, METACONFIG_CLASSNAME) != 0 &&
        Tcscasecmp(miClassArray->data[classIndex]->classDecl->name, MSFT_LOGRESOURCENAME) != 0)
    {
        if( keyPropertyCount < 1  || NitsShouldFault(NitsHere(), NitsAutomatic))
        {
            return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError, ID_MODMAN_VALIDATE_SCHEMA_NOKEY);
        }
    }

    return r;

}

MI_Result ValidateDSCProviderSchema(_In_ MI_ClassA *miClassArray,
                                    _Outptr_result_maybenull_ MI_Instance **extendedError )
{
    MI_Result r = MI_RESULT_OK;
    MI_Uint32 xCount = 0;
    MI_Uint32 configurationResourceCount = 0;
    MI_Boolean *bResourceVisited = NULL;
    // Test1: Exactly 1 resource per MOF file
    // Test2: All properties should contain qualifiers read, write, required or write and at least 1 key property.
    // Test3: Embedded object and its associated property validation
    // Test4: Version qualifier should be present
    // Test5: Only supported data type is available.
    // Test6: No reduntant class ( all classes should refered from resource class).
    // Test7: Class shouldn't contain any method.

    if (extendedError == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *extendedError = NULL;  // Explicitly set *extendedError to NULL as _Outptr_ requires setting this at least once.

    if (miClassArray == NULL || miClassArray->size == 0  || NitsShouldFault(NitsHere(), NitsAutomatic))
    {
        return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError, ID_MODMAN_VALIDATE_PROVSCHEMA_NORES);
    }

    // This Event does not produce any useful information and increasing log file size, disabling it. 
    // DSC_EventWriteValidatingDSCProviderSchema(miClassArray->size);

    bResourceVisited = (MI_Boolean*)DSC_malloc(sizeof(MI_Boolean) * miClassArray->size, NitsHere());
    if (bResourceVisited == NULL )
    {
        return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, extendedError, ID_ENGINEHELPER_MEMORY_ERROR);
    }

    memset(bResourceVisited, MI_FALSE, sizeof(MI_Boolean) * miClassArray->size );

    /*Start Test*/
    for (xCount = 0 ; xCount < miClassArray->size ; xCount++)
    {
        if ((miClassArray->data[xCount]->classDecl->superClass &&
             Tcscasecmp(BASE_RESOURCE_CLASSNAME, miClassArray->data[xCount]->classDecl->superClass) == 0) ||
             Tcscasecmp(METACONFIG_CLASSNAME, miClassArray->data[xCount]->classDecl->name) == 0)
        {
            bResourceVisited[xCount] = MI_TRUE;
            configurationResourceCount++;
            r = ValidateSchema(miClassArray, xCount, bResourceVisited, miClassArray->size, MI_TRUE, extendedError);
            if (r != MI_RESULT_OK)
            {
                DSC_free(bResourceVisited);
                return r;
            }
        }
    }

    /*Test6*/
    for (xCount = 0 ; xCount < miClassArray->size ; xCount++)
    {
        if(bResourceVisited[xCount] == MI_FALSE || NitsShouldFault(NitsHere(), NitsAutomatic))
        {
            DSC_free(bResourceVisited);
            return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError, ID_MODMAN_VALIDATE_PROVSCHEMA_NOTREFERRED);
        }
    }

    DSC_free(bResourceVisited);

    /*Test1*/
    if (configurationResourceCount != 1  || NitsShouldFault(NitsHere(), NitsAutomatic))
    {
        return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError, ID_MODMAN_VALIDATE_PROVSCHEMA_ONERES);
    }

    return r;
}

MI_Result ValidateDSCDocumentInstance(_In_ MI_InstanceA *miInstanceArray,
                                      _In_ MI_Uint32 flags,
                                      _Outptr_result_maybenull_ MI_Instance **extendedError )
{
    MI_Result r = MI_RESULT_OK;
    MI_Uint32 xCount = 0, yCount = 0;
    MI_Instance *instanceToCheck = NULL;
    MI_Uint32 instanceFlags;
    const MI_Char* resourceID;
    MI_Boolean hasMetaConfig = MI_FALSE;

    // Test1 : should contain all key and required properties.
    if( extendedError )
        *extendedError = NULL;

    if( miInstanceArray == NULL || miInstanceArray->size == 0  || NitsShouldFault(NitsHere(), NitsAutomatic))
    {
        return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError, ID_MODMAN_VALIDATE_PROVSCHEMA_NORES);
    }
    // This Event does not produce any useful information and increasing log file size, disabling it. 
    // DSC_EventWriteValidatingDSCDocInstance(miInstanceArray->size,flags);
    for( xCount = 0; xCount < miInstanceArray->size; xCount++)
    {
        instanceToCheck = miInstanceArray->data[xCount];
        if(Tcscasecmp(instanceToCheck->classDecl->name, METACONFIG_CLASSNAME) == 0 )
        {
            hasMetaConfig = MI_TRUE;
            if (flags & VALIDATE_METACONFIG_INSTANCE)
            {
                //If it has to validate partial configuration definition, do so.
                if (ShouldUsePartialConfigurations(instanceToCheck, MI_FALSE))
                {
                    r = ValidatePartialConfigMetaConfDefinition(instanceToCheck, extendedError);
                    RETURN_RESULT_IF_FAILED(r);
                }
            }
        }
        for( yCount = 0; yCount < instanceToCheck->classDecl->numProperties ; yCount++)
        {
            if( (instanceToCheck->classDecl->properties[yCount]->flags & (MI_FLAG_KEY | MI_FLAG_REQUIRED) ) != 0 )
            {
                //The property must be specified.
                r = DSC_MI_Instance_GetElement(instanceToCheck, instanceToCheck->classDecl->properties[yCount]->name, NULL, NULL, &instanceFlags, NULL);
                if( r != MI_RESULT_OK || (instanceFlags & MI_FLAG_NULL) != 0 )
                {
                    r = MI_RESULT_NOT_FOUND;
                    return GetCimMIError1Param(r, extendedError, ID_MODMAN_VALIDATE_PROVSCHEMA_NOMANDATORY, instanceToCheck->classDecl->properties[yCount]->name);
                }
            }
        }

    }

    //check meta configuration, which should not be merged with other configurations (> 2 because we always have the OMI_ConfigurationDocument)
    if(flags & VALIDATE_METACONFIG_INSTANCE)
    {
        if(hasMetaConfig == MI_FALSE || miInstanceArray->size > 2)
        {
            return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_MODMAN_METACONFIG_NOTFOUND_OR_MORE);
        }
    }
    else
    {
        if(hasMetaConfig == MI_TRUE)
        {
            return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_MODMAN_METACONFIG_MERGED_WITH_OTHERCONFIG);
        }
    }
    //check duplicate resourceid
    for( xCount = 0; xCount < miInstanceArray->size; xCount++)
    {
        instanceToCheck = miInstanceArray->data[xCount];
        resourceID = GetResourceId(instanceToCheck);
        for( yCount = xCount+1; yCount < miInstanceArray->size; yCount++)
        {
            const MI_Char *resourceID2 = GetResourceId(miInstanceArray->data[yCount]);
            if(resourceID && resourceID2 && Tcscasecmp(resourceID, resourceID2) == 0)
            {
                return GetCimMIError1Param(MI_RESULT_FAILED, extendedError, ID_MODMAN_DUPLICATE_RESOURCEID, resourceID);
            }
        }

    }
    return r;
}


MI_Result ValidateDSCProviderRegistrationInstance(_In_ MI_InstanceA *miInstanceArray,
                                                  _Outptr_result_maybenull_ MI_Instance **extendedError )
{
    MI_Result r = MI_RESULT_OK;
    MI_Instance *instanceToCheck = NULL;
    const MI_Char *className = NULL;
    MI_Boolean bWMIv2ProviderRegistration = MI_FALSE;
    // Test1: Exactly 1 registration per MOF file
    // Test2: Should be either PS registration or WMIv2 provider registration
    // Test3: Validate base properties.
    // Test3: WMIv2 provider registration and properties
    // Test4: PS Provider registration and properties.

    if( extendedError )
        *extendedError = NULL;

    /*Start Test*/
    /*Test1*/
    if( miInstanceArray == NULL || miInstanceArray->size != 1  || NitsShouldFault(NitsHere(), NitsAutomatic))
    {
        return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError, ID_MODMAN_VALIDATE_PROVSCHEMA_ONERES);
    }
    instanceToCheck = miInstanceArray->data[0];

    /*Test2*/
    r = DSC_MI_Instance_GetClassName(instanceToCheck, &className);
    if( r != MI_RESULT_OK )
    {
        return GetCimMIError(r, extendedError, ID_MODMAN_VALIDATE_PROVREGINS_CLASSNAME);
    }
    // This Event does not produce any useful information and increasing log file size, disabling it. 
    // DSC_EventWriteValidatingDSCProviderRegistrationInstance(className);
    if( Tcscasecmp(className, BASE_REGISTRATION_WMIV2PROVIDER) == 0 )
    {
        bWMIv2ProviderRegistration = MI_TRUE;
    }
    else if( Tcscasecmp(className, BASE_REGISTRATION_PSPROVIDER) == 0 )
    {
        bWMIv2ProviderRegistration = MI_FALSE;
    }
    else
    {
        return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError, ID_MODMAN_VALIDATE_PROVREGINS_TYPE);
    }

    /*Test3 All Base properties MUST be specified*/
    r = DSC_MI_Instance_GetElement(instanceToCheck, MSFT_BaseConfigurationProviderRegistration_ClassName, NULL, NULL, NULL, NULL);
    if( r != MI_RESULT_OK )
    {
        return GetCimMIError(r, extendedError, ID_MODMAN_VALIDATE_PROVREGINS_PROP_CLASSNAME);
    }
    r = DSC_MI_Instance_GetElement(instanceToCheck, MSFT_BaseConfigurationProviderRegistration_DSCEngineCompatVersion, NULL, NULL, NULL, NULL);
    if( r != MI_RESULT_OK )
    {
        return GetCimMIError(r, extendedError, ID_MODMAN_VALIDATE_PROVREGINS_PROP_COMPATVERSION);
    }
    r = DSC_MI_Instance_GetElement(instanceToCheck, MSFT_BaseConfigurationProviderRegistration_DSCModuleVersion, NULL, NULL, NULL, NULL);
    if( r != MI_RESULT_OK )
    {
        return GetCimMIError(r, extendedError, ID_MODMAN_VALIDATE_PROVREGINS_PROP_MODULEVERSION);
    }

    if( bWMIv2ProviderRegistration)
    {
        /*Test4 All DSC_MI_Instance_GetElement properties*/
        r = DSC_MI_Instance_GetElement(instanceToCheck, MSFT_CimConfigurationProviderRegistration_Namespace, NULL, NULL, NULL, NULL);
        if( r != MI_RESULT_OK)
        {
            return GetCimMIError(r, extendedError, ID_MODMAN_VALIDATE_PROVREGINS_PROP_NAMESPACE);
        }
    }
    else
    {
        /*Test5 All MSFT_PSConfigurationProviderRegistrationn properties*/
        r = DSC_MI_Instance_GetElement(instanceToCheck, MSFT_PSConfigurationProviderRegistration_ModuleName, NULL, NULL, NULL, NULL);
        if( r != MI_RESULT_OK )
        {
            return GetCimMIError(r, extendedError, ID_MODMAN_VALIDATE_PROVREGINS_PROP_MODULENAME);
        }
    }

    return r;
}

MI_Result ValidateClassProperties(_In_ MI_Class *classInfo,
                                  _In_ BaseResourceConfiguration *properties,
                                  _In_ MI_Uint32 basePropertyCount,
                                  _Outptr_result_maybenull_ MI_Instance **extendedError )
{
    MI_Result r = MI_RESULT_OK;
    MI_Type type;
    MI_Uint32 count = 0;
    MI_Uint32 proCount = 0;
    if( extendedError )
        *extendedError = NULL;


    /*Start Test*/
    for (count = 0; properties[count].wszPropName != NULL; count++)
    {
        const TChar *propName = properties[count].wszPropName;
        const TChar *className = classInfo->classDecl->name;
        // This Event does not produce any useful information and increasing log file size, disabling it. 
        // DSC_EventWriteValidatingClassProperty(propName,className);
        r = DSC_MI_Class_GetElement(classInfo, propName, NULL, NULL, &type, NULL, NULL, NULL, NULL);
        if( r != MI_RESULT_OK )
        {
            return GetCimMIError2Params(r, extendedError, ID_MODMAN_VALIDATE_CLASSPROP_MAND_NAME, propName, className);
        }

        if(type != properties[count].propType || NitsShouldFault(NitsHere(), NitsAutomatic))
        {
            return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError, ID_MODMAN_VALIDATE_CLASSPROP_MAND_TYPE);
        }
    }

    /*validate property counts*/
    r = DSC_MI_Class_GetElementCount(classInfo, &proCount);
    if(r != MI_RESULT_OK)
    {
        return GetCimMIError(r, extendedError, ID_MODMAN_VALIDATE_CLASSPROP_MAND_GETCLASS);
    }

    if((count + basePropertyCount) != proCount || NitsShouldFault(NitsHere(), NitsAutomatic))
    {
        return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError, ID_MODMAN_VALIDATE_CLASSPROP_MAND_NOMATCH);
    }

    return r;
}

MI_Result ValidateBaseResourceConfigurationClass(_In_ MI_Class *baseResourceClass,
                                                 _Outptr_result_maybenull_ MI_Instance **extendedError )
{
    MI_Result r;
    MI_QualifierSet qualifierSet;
    MI_Uint32 xCount = 0;
    MI_Type qualifierType;
    MI_Uint32 qualifierFlags;
    MI_Value qualifierValue;
    MI_Uint32 index;

    r = ValidateClassProperties(baseResourceClass, g_BaseResourceProperties, 0, extendedError);
    if( r!= MI_RESULT_OK)
    {
        return r;
    }

    /* validate qualifier */
    for (xCount = 0; g_BaseResourceQualifiers[xCount].wszPropName != NULL; xCount++)
    {
        const TChar * propName = g_BaseResourceQualifiers[xCount].wszPropName;
        r = DSC_MI_Class_GetElement(baseResourceClass, g_BaseResourceQualifiers[xCount].wszPropName, NULL, NULL, NULL, NULL, &qualifierSet, NULL, NULL);
        if( r != MI_RESULT_OK )
        {
            return GetCimMIError2Params(r, extendedError, ID_MODMAN_VALIDATE_CLASSPROP_MAND_NAME, propName, baseResourceClass->classDecl->name);            
        }

        r = DSC_MI_QualifierSet_GetQualifier(&qualifierSet, g_BaseResourceQualifiers[xCount].wszQualifierName, &qualifierType, &qualifierFlags, &qualifierValue, &index);
        if( r != MI_RESULT_OK )
        {
            return GetCimMIError(r, extendedError, ID_MODMAN_VALIDATE_BASERES_NOQUAL);
        }
    }

    return r;
}

MI_Result ValidateBaseResourceRegistrationClass(_In_ MI_Class *baseRegistrationClass,
                                                _Outptr_result_maybenull_ MI_Instance **extendedError )
{
    MI_Result r;
    MI_Uint32 flags;

    r = ValidateClassProperties(baseRegistrationClass, g_BaseRegistrationProperties, 0, extendedError);
    if( r!= MI_RESULT_OK)
    {
        return r;
    }

    /* Validate key for className */
    r = DSC_MI_Class_GetElement(baseRegistrationClass, MSFT_BaseConfigurationProviderRegistration_ClassName, NULL, NULL, NULL, NULL, NULL, &flags, NULL);
    if( r != MI_RESULT_OK )
    {
        return GetCimMIError2Params(r, extendedError, ID_MODMAN_VALIDATE_CLASSPROP_MAND_NAME, MSFT_BaseConfigurationProviderRegistration_ClassName, baseRegistrationClass->classDecl->name);
    }

    if( !(flags & MI_FLAG_KEY) || NitsShouldFault(NitsHere(), NitsAutomatic))
    {
        return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError, ID_MODMAN_VALIDATE_BASERES_NOQUAL);
    }

    return r;
}

MI_Result ValidateCimResourceRegistrationClass(_In_ MI_Class *cimRegistrationClass,
                                               _Outptr_result_maybenull_ MI_Instance **extendedError )
{
    MI_Result r;

    r = ValidateClassProperties(cimRegistrationClass, g_CimResourceProperties, MSFT_CimConfigurationProviderRegistration_BaseProperty_Count, extendedError);
    if( r!= MI_RESULT_OK)
    {
        return r;
    }

    /* validate parent class */
    if(Tcscasecmp(cimRegistrationClass->classDecl->superClass, BASE_REGISTRATION) != 0 || NitsShouldFault(NitsHere(), NitsAutomatic))
    {
        return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError, ID_MODMAN_VALIDATE_BASERES_V2DERIVATEION);
    }

    return r;
}

MI_Result ValidatePSResourceRegistrationClass(_In_ MI_Class *psRegistrationClass,
                                              _Outptr_result_maybenull_ MI_Instance **extendedError )
{
    MI_Result r;

    r = ValidateClassProperties(psRegistrationClass, g_PSResourceProperties, MSFT_PSConfigurationProviderRegistration_BaseProperty_Count, extendedError);
    if( r!= MI_RESULT_OK)
    {
        return r;
    }

    /* validate parent */
    if(Tcscasecmp(psRegistrationClass->classDecl->superClass, BASE_REGISTRATION) != 0 || NitsShouldFault(NitsHere(), NitsAutomatic))
    {
        return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError, ID_MODMAN_VALIDATE_BASERES_PSDERIVATEION);
    }

    return r;
}

MI_Result ValidateConfigurationDocumentClass(_In_ MI_Class *configDocumentClass,
                                             _Outptr_result_maybenull_ MI_Instance **extendedError )
{
    MI_Result r;
    r = ValidateClassProperties(configDocumentClass, g_ConfigurationDocumentProperties, 0, extendedError);
    if( r!= MI_RESULT_OK)
    {
        return r;
    }
    return r;
}

MI_Result ValidateMetaConfigurationClass(_In_ MI_Class *configDocumentClass,
                                         _Outptr_result_maybenull_ MI_Instance **extendedError )
{
    MI_Result r;
    r = ValidateClassProperties(configDocumentClass, g_MetaConfigProperties, MSFT_DSCMetaConfiguration_BaseProperty_Count, extendedError);
    if( r!= MI_RESULT_OK)
    {
        return r;
    }
    return r;
}

MI_Result ValidateBaseCredClass(_In_ MI_Class *configDocumentClass,
                                _Outptr_result_maybenull_ MI_Instance **extendedError )
{
    MI_Result r;
    r = ValidateClassProperties(configDocumentClass, g_BaseCredProperties, 0, extendedError);
    if( r!= MI_RESULT_OK)
    {
        return r;
    }
    return r;
}

MI_Result ValidateKeyValuePairClass(_In_ MI_Class *configDocumentClass,
                                   _Outptr_result_maybenull_ MI_Instance **extendedError )
{
    MI_Result r;
    r = ValidateClassProperties(configDocumentClass, g_KeyValuePairProperties, 0, extendedError);
    if( r!= MI_RESULT_OK)
    {
        return r;
    }
    return r;
}


MI_Result ValidateDSCProviderMapping(_In_ MI_Instance *regInstance,
                                     _In_ MI_Class *regClass,
                                     _In_ MI_Session *miSession,
                                     _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Result r = MI_RESULT_OK;
    MI_Value namespaceName;

    if (extendedError == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *extendedError = NULL;  // Explicitly set *extendedError to NULL as _Outptr_ requires setting this at least once.

    /*Validation only for configuration classes which are registered as wmiv2 provider.
           MSFT_LogResource is a special class.*/
    if ( (Tcscasecmp(regClass->classDecl->name, MSFT_LOGRESOURCENAME) != 0) &&
        ((regClass->classDecl->superClass &&
         Tcscasecmp(regClass->classDecl->superClass, BASE_RESOURCE_CLASSNAME) == 0) ||
         Tcscasecmp(regClass->classDecl->name, METACONFIG_CLASSNAME) == 0))
    {
        r = DSC_MI_Instance_GetElement(regInstance, MI_T("Namespace"), &namespaceName, NULL, NULL, NULL);
        if (r == MI_RESULT_OK )
        {
            r = ValidateDSCProviderMappingForWMIV2Provider(namespaceName.string, regClass, miSession, extendedError);
        }
        else if (r == MI_RESULT_NOT_FOUND || r == MI_RESULT_NO_SUCH_PROPERTY)
        {
            // It is not wmiv2 provider registration, PS provider validation should happen here
            return MI_RESULT_OK;
        }
        else
        {
            return GetCimMIError(r, extendedError, ID_MODMAN_VALIDATE_MAPPING_NAMESPACE );
        }
    }

    return r;
}

MI_Result ValidateDSCProviderMappingForWMIV2Provider( _In_z_ const MI_Char *namespaceName,
                                                     _In_ MI_Class *regClass,
                                                     _In_ MI_Session *miSession,
                                                     _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Result r = MI_RESULT_OK;
    MI_Result innerR = MI_RESULT_OK;
    MI_Boolean moreResults;
    MI_Result result = MI_RESULT_INVALID_PARAMETER;
    const MI_Char *errorMessage;
    const MI_Instance *completionDetails = NULL;
    MI_Operation operation = MI_OPERATION_NULL;
    MI_Uint32 xCount, usefulMethodCount =0 ;
    const MI_Class *resultClass;
    const MI_MethodDecl *methodDecl = NULL;
    if( extendedError )
        *extendedError = NULL;

    MI_Session_GetClass(miSession, 0, 0, namespaceName, regClass->classDecl->name, NULL, &operation);

    r = MI_Operation_GetClass(&operation, &resultClass, &moreResults, &result, &errorMessage, &completionDetails);
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
        MI_Operation_Close(&operation);
        return r;
    }
    // check class methods and its name
    for( xCount = 0 ; xCount < resultClass->classDecl->numMethods; xCount++)
    {
        methodDecl = resultClass->classDecl->methods[xCount];

        if( Tcscasecmp(methodDecl->name, OMI_BaseResource_GetMethodName) == 0 )
        {
            if( methodDecl->numParameters != OMI_BaseResource_GetMethodCount  || NitsShouldFault(NitsHere(), NitsAutomatic))
            {
                MI_Operation_Close(&operation);
                return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError,ID_MODMAN_VALIDATE_MAPPING_PARAM_GET);
            }
            usefulMethodCount++;
        }
        else if(Tcscasecmp(methodDecl->name, OMI_BaseResource_TestMethodName) == 0 )
        {
            if( methodDecl->numParameters != OMI_BaseResource_TestMethodCount  || NitsShouldFault(NitsHere(), NitsAutomatic))
            {
                MI_Operation_Close(&operation);
                return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError,ID_MODMAN_VALIDATE_MAPPING_PARAM_TEST);
            }
            usefulMethodCount++;
        }
        else if (Tcscasecmp(methodDecl->name, OMI_BaseResource_SetMethodName) == 0)
        {
            if( methodDecl->numParameters != OMI_BaseResource_SetMethodCount  || NitsShouldFault(NitsHere(), NitsAutomatic) )
            {
                MI_Operation_Close(&operation);
                return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError,ID_MODMAN_VALIDATE_MAPPING_PARAM_SET);
            }
            usefulMethodCount++;
        }
    }

    MI_Operation_Close(&operation);

    if( usefulMethodCount != 3  || NitsShouldFault(NitsHere(), NitsAutomatic))
    {
        // at least one method is missing
        return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError,ID_MODMAN_VALIDATE_MAPPING_PARAM_ALLFAILED );
    }
    return MI_RESULT_OK;
}

MI_Boolean ValidateABCFormatVersion(_In_z_ const MI_Char *str)
{
    MI_Boolean bValidFormat = MI_TRUE;
    MI_Uint32 dwDots = 0;
    while( *str != '\0')
    {
        if( !(*str >= MI_T('0') && *str <= MI_T('9') ) )
        {
            if( *str == '.' )
            {
                dwDots++;
                if( dwDots > 2 )
                {
                    break;
                }
            }
            else
            {
                bValidFormat = MI_FALSE;
                break;
            }
        }
        str++;
    }
    if( bValidFormat && dwDots == 2 )
    {
        return MI_TRUE;
    }
    return MI_FALSE;
}

MI_Result ValidateDocumentInstance(_In_ MI_Instance *docInstance,
                                    _Outptr_result_maybenull_ MI_Instance **extendedError )
{

    MI_Result r = MI_RESULT_OK;
    MI_Uint32 yCount = 0;
    MI_Uint32 Flags;
    MI_Value value;
    DSC_EventWriteValidatingDocInstance();

    // Test1 : should have 'Version' property with correct format.
    if( extendedError )
        *extendedError = NULL;

    for( yCount = 0; yCount < docInstance->classDecl->numProperties ; yCount++)
    {
        r = DSC_MI_Instance_GetElement(docInstance, OMI_ConfigurationDocument_Version, &value, NULL, &Flags, NULL);
        if( r != MI_RESULT_OK || (Flags & MI_FLAG_NULL) != 0 )
        {
            r = MI_RESULT_NOT_FOUND;
            return GetCimMIError1Param(r, extendedError, ID_MODMAN_VALIDATE_PROVSCHEMA_NOMANDATORY, OMI_ConfigurationDocument_Version);
        }
        if( !ValidateABCFormatVersion(value.string))
        {
            return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError, ID_MODMAN_VALIDATE_DOCINS_VERSION);
        }

	r = DSC_MI_Instance_GetElement(docInstance, OMI_ConfigurationDocument_DocumentType, &value, NULL, &Flags, NULL);
	if( r == MI_RESULT_OK && (Flags & MI_FLAG_NULL) == 0 && Tcscasecmp(value.string, "inventory") == 0 )
        {
	    r = MI_RESULT_INVALID_PARAMETER;
	    return GetCimMIError(r, extendedError, ID_MODMAN_VALIDATE_PROVSCHEMA_SHOULD_NOT_BE_INVENTORY);
        }
    }
    return r;
}


MI_Result ValidateInventoryDocumentInstance(_In_ MI_Instance *docInstance,
                                    _Outptr_result_maybenull_ MI_Instance **extendedError )
{

    MI_Result r = MI_RESULT_OK;
    MI_Uint32 yCount = 0;
    MI_Uint32 Flags;
    MI_Value value;
    DSC_EventWriteValidatingDocInstance();

    // Test1 : should have 'Version' property with correct format.
    if( extendedError )
        *extendedError = NULL;

    for( yCount = 0; yCount < docInstance->classDecl->numProperties ; yCount++)
    {
        r = DSC_MI_Instance_GetElement(docInstance, OMI_ConfigurationDocument_Version, &value, NULL, &Flags, NULL);
        if( r != MI_RESULT_OK || (Flags & MI_FLAG_NULL) != 0 )
        {
            r = MI_RESULT_NOT_FOUND;
            return GetCimMIError1Param(r, extendedError, ID_MODMAN_VALIDATE_PROVSCHEMA_NOMANDATORY, OMI_ConfigurationDocument_Version);
        }
        if( !ValidateABCFormatVersion(value.string))
        {
            return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError, ID_MODMAN_VALIDATE_DOCINS_VERSION);
        }

	r = DSC_MI_Instance_GetElement(docInstance, OMI_ConfigurationDocument_DocumentType, &value, NULL, &Flags, NULL);
	if( !(r == MI_RESULT_OK && (Flags & MI_FLAG_NULL) == 0) )
        {
	    r = MI_RESULT_INVALID_PARAMETER;
	    return GetCimMIError(r, extendedError, ID_MODMAN_VALIDATE_PROVSCHEMA_SHOULD_BE_INVENTORY_1);
        }
	if ( Tcscasecmp(value.string, "inventory") != 0 )
	{
	    r = MI_RESULT_INVALID_PARAMETER;
	    return GetCimMIError(r, extendedError, ID_MODMAN_VALIDATE_PROVSCHEMA_SHOULD_BE_INVENTORY_2);
	}
    }
    return r;
}

/*Function that validates if the version of the metaconfiguration is in accordance with the properties that it supports*/
/* For now, we're just going to verify that it has some valid version number.  For the V2 port, we'll validate everything else */
MI_Result ValidateVersionNumbersCompatibility(_In_ MI_InstanceA *instanceA,
                                            _In_z_ const MI_Char* versionNumber,
                                            _In_ LCMProviderContext *lcmContext,
                                            _Outptr_result_maybenull_ MI_Instance **extendedError )
{

    MI_Result r = MI_RESULT_OK;
    MI_Uint32 Flags;
    MI_Value value;
    MI_Uint32 i=0;
    MSFT_DSCMetaConfiguration *metaConfigInstance=NULL;
    MI_Instance *tempInstance;
    Intlstr pTempStr = Intlstr_Null;
    if (instanceA == 0 || instanceA->size == 0 || NitsShouldFault(NitsHere(), NitsAutomatic))
    {
        return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError, ID_CAINFRA_DEPENDCY_NULLPARAM);
    }

    if (extendedError == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *extendedError = NULL;  // Explicitly set *extendedError to NULL as _Outptr_ requires setting this at least once.
    //get the metaconfiguration instance first

    for (i = 0; i < instanceA->size; i++)
    {
        tempInstance = instanceA->data[i];
        if (tempInstance->classDecl != NULL &&Tcscasecmp(tempInstance->classDecl->name, METACONFIG_CLASSNAME) == 0)
        {
            metaConfigInstance=(MSFT_DSCMetaConfiguration*)tempInstance;
            break;
        }
    }
    if(metaConfigInstance==NULL)
    {
        //No metaconfiguration object was found - this isn't supported
        return GetCimMIError(MI_RESULT_NOT_SUPPORTED, extendedError, ID_LCM_FAILED_TO_GET_METACONFIGURATION);
    }

    if (Tcscasecmp(versionNumber, OMI_ConfigurationDocument_VersionV1) == 0)
    {
        // We're okay here
    }
    else if(Tcscasecmp(versionNumber, OMI_ConfigurationDocument_VersionV2) == 0)
    {
        // We're okay here
    }
    else
    {
        //It isn't in the right version format
        return GetCimMIError(MI_RESULT_INVALID_PARAMETER, extendedError, ID_MODMAN_VALIDATE_VERSIONNUMBER);
    }
    return r;
}


/*Function to check if the partial config name passed is present among the partial configuraions defined in metaconfig*/
MI_Result ValidatePartialConfigurationName(_In_z_ const MI_Char* partialConfigName,
    _In_ MI_Instance* metaConfigInstance,
    _Outptr_result_maybenull_ MI_Instance** partialConfigMetaInstance,
    _Inout_ MI_StringA* otherExclusiveResources,//These are exclusive resources of other partial configurations which is returned
    _Outptr_result_maybenull_ MI_Instance** cimErrorDetails)
{
    //TODO : Get all metaconfiguration partial config instances
    MI_Uint32 count = 0;
    MI_Uint32 innerCount = 0;
    MI_Value value;
    MI_Char* qualifiedPartialConfName = NULL;
    MI_Boolean isValidPartialConfig = MI_FALSE;
    MI_Value valuePartialConfig;
    MI_Result result = MI_RESULT_OK;
    MI_Uint32 flags;
    MI_Char** tempArray = NULL;
    MI_Uint32 tempNewSize = 0;
    MI_Uint32 tempOldSize = 0;
    MI_Char** tempOtherExclusiveResources = NULL;
    MI_Boolean flagProcessExclusiveResources = MI_TRUE;
    if (metaConfigInstance == NULL || partialConfigName == NULL || cimErrorDetails == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *cimErrorDetails = NULL;
    *partialConfigMetaInstance = NULL;
    DSC_EventWriteLCMValidatingPartialConfigName(partialConfigName);
    result = MI_Instance_GetElement(metaConfigInstance, MSFT_DSCMetaConfiguration_PartialConfigurations, &value, NULL, NULL, NULL);
    GOTO_CLEANUP_IF_FAILED(result, Cleanup);
    if (value.instancea.data == NULL)
    {
        result = GetCimMIError(MI_RESULT_NOT_FOUND, cimErrorDetails, ID_LCMHELPER_ERROR_PARTIALCONFIG_UNDEFINED);
        goto Cleanup;
    }
    if (otherExclusiveResources == NULL)
    {
        flagProcessExclusiveResources = MI_FALSE;//In this case you only want to validate the partial config name
    }
    //Iterate through all metaconfig partial config definitions and find the matching partial config name
    for (count = 0; count < value.instancea.size; count++)
    {
        result = MI_Instance_GetElement(value.instancea.data[count], OMI_MetaConfigurationResource_ResourceId, &valuePartialConfig, NULL,NULL,NULL);
        GOTO_CLEANUP_IF_FAILED(result, Cleanup);
        //The string would be [PartialConfiguration]SomeName - So we need to concatenate the suffix first.
        result = DSCConcatStrings(&qualifiedPartialConfName, 3, MSFT_PartialConfiguration_FriendlyName, partialConfigName, MI_T("[%T]%T")); //Padding=3 2 for [] and 1 for \0
        GOTO_CLEANUP_IF_FAILED(result, Cleanup);
        if (valuePartialConfig.string != NULL && Tcscasecmp(valuePartialConfig.string, qualifiedPartialConfName) == 0)
        {
            //This means there was a match between metaconfig and partial config name passed
            isValidPartialConfig = MI_TRUE;
            *partialConfigMetaInstance = value.instancea.data[count];
            DSCFREE_IF_NOT_NULL(qualifiedPartialConfName);
            continue;//So that you don't count the exclusive resources of its own instance in themeta config.
        }
        //Reset qualified partialconfname back
        DSCFREE_IF_NOT_NULL(qualifiedPartialConfName);
        if (flagProcessExclusiveResources)
        {
            //Get exclusive resources of all other partial configs
            result = MI_Instance_GetElement(value.instancea.data[count], MSFT_PartialConfiguration_ExclusiveResources, &valuePartialConfig, NULL, &flags, NULL);
            GOTO_CLEANUP_IF_FAILED(result, Cleanup);
            //Check the exclusive resources, if they're present, add it to the ongoing array
            if (!(flags & MI_FLAG_NULL) && valuePartialConfig.stringa.data != NULL && valuePartialConfig.stringa.size > 0)
            {
                //Copy both arrays into a tempArray
                tempNewSize = valuePartialConfig.stringa.size + tempOldSize;
                tempArray = (MI_Char**)DSC_malloc(tempNewSize*sizeof(MI_Char*), NitsHere());
                for (innerCount = 0; innerCount < tempOldSize && innerCount < tempNewSize; innerCount++)
                {
                    tempArray[innerCount] = tempOtherExclusiveResources[innerCount];
                }
                for (innerCount = 0; innerCount < valuePartialConfig.stringa.size && innerCount < tempNewSize; innerCount++)
                {
                    tempArray[innerCount + tempOldSize] = valuePartialConfig.stringa.data[innerCount];
                }
                //Delete the tempOtherExclusive Array and point it to the new temparray
                DSCFREE_IF_NOT_NULL(tempOtherExclusiveResources);
                tempOtherExclusiveResources = tempArray;
                tempOldSize = tempNewSize;
            }
        }
    }
    if (!isValidPartialConfig)
    {
        result = GetCimMIError1Param(MI_RESULT_NOT_FOUND, cimErrorDetails, ID_LCMHELPER_INVALID_PARTIALCONFIG_NOTINMETACONF, partialConfigName);
    }
    if (flagProcessExclusiveResources)
    {
        otherExclusiveResources->data = tempOtherExclusiveResources;
        otherExclusiveResources->size = tempNewSize;
    }
    tempOtherExclusiveResources = NULL;
    tempArray = NULL;

Cleanup:
    DSCFREE_IF_NOT_NULL(qualifiedPartialConfName);
    DSCFREE_IF_NOT_NULL(tempArray);
    DSCFREE_IF_NOT_NULL(tempOtherExclusiveResources);
    return result;
}



/*Function to check if the partial config name passed is present among the partial configuraions defined in metaconfig*/
MI_Result ValidatePartialConfigMetaConfDefinition(_In_ MI_Instance* metaConfigInstance,
    _Outptr_result_maybenull_ MI_Instance** cimErrorDetails)
{
    //TODO : Get all metaconfiguration partial config instances
    MI_Uint32 count = 0;
    MI_Value value;
    MI_Result result = MI_RESULT_OK;
    MI_StringA arrayOfAllExclusiveResources={ 0 };
    if (metaConfigInstance == NULL || cimErrorDetails == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *cimErrorDetails = NULL;
    result = MI_Instance_GetElement(metaConfigInstance, MSFT_DSCMetaConfiguration_PartialConfigurations, &value, NULL, NULL, NULL);
    GOTO_CLEANUP_IF_FAILED(result, Cleanup);
    if (value.instancea.data == NULL)
    {
        result = GetCimMIError(MI_RESULT_NOT_FOUND, cimErrorDetails, ID_LCMHELPER_ERROR_PARTIALCONFIG_UNDEFINED);
        return result;
    }
    DSC_EventWriteLCMValidatingMetaConfPartial();
    //Iterate through all metaconfig partial config definitions and find if their properties are correctly defined
    for (count = 0; count < value.instancea.size; count++)
    {
        //Check if configuration download manager is properly named in ConfigurationSource
        result = ValidatePartialMeta_ConfigDownloadMgr(MI_TRUE, value.instancea.data[count], metaConfigInstance, cimErrorDetails);
        GOTO_CLEANUP_IF_FAILED(result, Cleanup);//Result and cimerror is set by the function above

        //Check if configuration download manager is properly named in ResourceModuleSource
        result = ValidatePartialMeta_ConfigDownloadMgr(MI_FALSE, value.instancea.data[count], metaConfigInstance, cimErrorDetails);
        GOTO_CLEANUP_IF_FAILED(result, Cleanup);//Result and cimerror is set by the function above

        result = ValidatePartialMeta_ExclusiveResources(value.instancea.data[count], &arrayOfAllExclusiveResources, cimErrorDetails);
        GOTO_CLEANUP_IF_FAILED(result, Cleanup);//Result and cimerror is set by the function above

    }
Cleanup:
    if (arrayOfAllExclusiveResources.size > 0)
    {
        DSC_free(arrayOfAllExclusiveResources.data);
        arrayOfAllExclusiveResources.data = NULL;
    }
    return result;
}

/*Function to check the following :
  - Each resource in the partial config document has the same configuration document name
  - Each resource does not disobey the exclusive resource rule */
MI_Result ValidatePartialConfigFile(_In_z_ const MI_Char* partialConfigName,
    _In_ MI_InstanceA resourceInstanceArray,
    _In_ MI_Instance * partialConfigIntance,
    _In_ MI_StringA *ExcludedResourcesList,
    _Outptr_result_maybenull_ MI_Instance** cimErrorDetails)
{
    //TODO : Get all metaconfiguration partial config instances
    MI_Uint32 count = 0;
    MI_Value valuePartialConfig;
    MI_Result result = MI_RESULT_OK;
    MI_Uint32 innerCount = 0;
    MI_Boolean obeysExclusiveResourceRule = MI_FALSE;
    if (partialConfigName == NULL || cimErrorDetails== NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *cimErrorDetails = NULL;
    //Iterate through all metaconfig partial config definitions and find the matching partial config name
    for (count = 0; count < resourceInstanceArray.size; count++)
    {
        result = MI_Instance_GetElement(resourceInstanceArray.data[count], OMI_BaseResource_ConfigurationName, &valuePartialConfig, NULL, NULL, NULL);
        if (result != MI_RESULT_OK)
        {
            result = GetCimMIError1Param(MI_RESULT_NOT_FOUND, cimErrorDetails, ID_LCM_PARTIALCONFIG_CONFIGURATIONNAME_MISSING, resourceInstanceArray.data[count]->classDecl->name);
            return result;
        }
        if (valuePartialConfig.string == NULL || Tcscasecmp(valuePartialConfig.string, partialConfigName) != 0)
        {
            //If even one of the configuration name values doesn't match the partial configuration name, throw error
            result = GetCimMIError1Param(MI_RESULT_NOT_FOUND, cimErrorDetails, ID_LCMHELPER_INVALID_PARTIALCONFIG_INCONSISTENCY, partialConfigName);
            return result;
        }
        //Check that the resource name in the array is not present inside any of the other partial configurations' exclusive resources.
        if (ExcludedResourcesList != NULL && ExcludedResourcesList->data != NULL)
        {

            //For each exluded resource, check if there's a match with this resources class name
            for (innerCount = 0; innerCount < ExcludedResourcesList->size; innerCount++)
            {
                result = ValidateResourceObeysExclusiveResourcesRule(ExcludedResourcesList->data[innerCount], resourceInstanceArray.data[count], &obeysExclusiveResourceRule, cimErrorDetails);
                RETURN_RESULT_IF_FAILED(result);
                if (!obeysExclusiveResourceRule)
                {
                    result = GetCimMIError2Params(MI_RESULT_INVALID_PARAMETER, cimErrorDetails, ID_MODMAN_PARTIALCONFIG_EXCLUSIVERESOURCE_DISOBEYED, partialConfigName, ExcludedResourcesList->data[innerCount]);
                    return result;
                }

            }
        }
    }
    return result;
}


/*Function to check if the configurationsource exists as a defined download manager

varifyConfigSource 
    MI_TRUE to verify ConfigurationSource
    MI_FALSE to verify ResourceModuleSource

*/
MI_Result ValidatePartialMeta_ConfigDownloadMgr(_In_ MI_Boolean verifyConfigSource,
    _In_ MI_Instance* partialInstance,
    _In_ MI_Instance* metaConfInstance,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result result = MI_RESULT_OK;
    MI_Uint32 flags = 0;
    MI_Uint32 count = 0;
    MI_Value valueSource;
    MI_Type miType;
    MI_Value valueDownloadMgrs;
    MI_Value valueConfigDownloadMgrName;
    MI_Boolean isValidPartialConfig = MI_FALSE;
    MI_Char* sourcePropertyValue;
    const MI_Char* sourcePropertyName;
    const MI_Char* downloadManagerName;


    if (cimErrorDetails == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *cimErrorDetails = NULL;

    if (verifyConfigSource)
    { 
        sourcePropertyName = MSFT_PartialConfiguration_ConfigurationSource;
        downloadManagerName = MSFT_DSCMetaConfiguration_ConfigurationDownloadManagers;
    }
    else
    {
        sourcePropertyName = MSFT_PartialConfiguration_ResourceModuleSource;
        downloadManagerName = MSFT_DSCMetaConfiguration_ResourceModuleManagers;
    }

    DSC_EventWriteLCMValidatingPartialConfigMetaConfDownloadMgr();

    //Get the property to validate from the instance
    // either 
    result = MI_Instance_GetElement(partialInstance, sourcePropertyName, &valueSource, &miType, &flags, 0);
    if (result != MI_RESULT_OK)
    {
        result = GetCimMIError2Params(MI_RESULT_NOT_FOUND, cimErrorDetails, ID_ENGINEHELPER_GET_PROPERTY_X_FROM_Y_FAILED, sourcePropertyName, MSFT_PARTIALCONFIGURATION_CLASSNAME);
        return result;
    }
    if (flags & MI_FLAG_NULL)
    {
        return MI_RESULT_OK;//Since we don't have to bother if its not defined
    }

    //Get the download managers
    result = MI_Instance_GetElement(metaConfInstance, downloadManagerName, &valueDownloadMgrs, &miType, &flags, 0);
    if (result != MI_RESULT_OK)
    {
        result = GetCimMIError2Params(MI_RESULT_NOT_FOUND, cimErrorDetails, ID_ENGINEHELPER_GET_PROPERTY_X_FROM_Y_FAILED, downloadManagerName, METACONFIG_CLASSNAME);
        return result;
    }

    if (valueSource.stringa.size == 0 || valueSource.stringa.data == NULL || valueSource.stringa.data[0] == NULL)
    {
        return MI_RESULT_OK;
    }

    if (!verifyConfigSource)
    {
        // ResourceModuleSource not supported on Linux
        return GetCimMIError(MI_RESULT_NOT_SUPPORTED, cimErrorDetails, ID_MODMAN_PARTIALCONFIG_RESOURCEMODULESOURCE_NOTSUPPORTED);
    }

    // NOTE: MSFT_PartialConfiguration_ConfigurationSource is declared as an array but only allows a single entry.
    if (valueSource.stringa.size > 1)
    {
        sourcePropertyValue = valueSource.stringa.data[1];
        // NOTE: Update the error message when/if ResouceModuleSource is supported.
        return GetCimMIError1Param(MI_RESULT_NOT_SUPPORTED, cimErrorDetails, ID_MODMAN_PARTIALCONFIG_CONFIGSOURCE_MUSTBEONE, sourcePropertyValue);
    }

    sourcePropertyValue = valueSource.stringa.data[0];

    if (valueDownloadMgrs.instancea.size != 0 && valueDownloadMgrs.instancea.data != NULL)
    {
        for (count = 0; count < valueDownloadMgrs.instancea.size; count++)
        {           
            result = MI_Instance_GetElement(valueDownloadMgrs.instancea.data[count], OMI_MetaConfigurationResource_ResourceId, &valueConfigDownloadMgrName, &miType, &flags, NULL);
            RETURN_RESULT_IF_FAILED(result);

            if (valueConfigDownloadMgrName.string != NULL 
                && 
                Tcscasecmp(sourcePropertyValue, valueConfigDownloadMgrName.string) == 0)
            {
                // This means there was a match between a metaconfig config download manager's resource id and the
                // partial config's download manager reference.
                isValidPartialConfig = MI_TRUE;
                break;
            }
        }
        if (!isValidPartialConfig)
        {
            result = GetCimMIError1Param(MI_RESULT_NOT_FOUND, cimErrorDetails, ID_MODMAN_INVALID_PARTIALCONFIG_CONFIGSOURCE, sourcePropertyValue);
        }
    }
    else
    {
        result = GetCimMIError1Param(MI_RESULT_NOT_FOUND, cimErrorDetails, ID_MODMAN_INVALID_PARTIALCONFIG_CONFIGSOURCE, sourcePropertyValue);
    }
    return result;
}

/*Function to check if exclusive resource definitions are not overlapp[ing on another in the metaconfig instance*/
MI_Result ValidatePartialMeta_ExclusiveResources(_In_ MI_Instance* partialInstance,
    _Inout_ MI_StringA* arrayOutput,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result result = MI_RESULT_OK;
    MI_Uint32 flags = 0;
    MI_Uint32 count = 0;
    MI_Uint32 innerCount = 0;
    size_t newSize = 0;
    MI_Value value;
    MI_Uint32 newIndexOfArrayOutput = 0;
    MI_Char** tempArrayOutput = NULL;
    MI_Boolean alreadyCopiedArrayOutputIntoTemp = MI_TRUE;
    if (cimErrorDetails == NULL || arrayOutput == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *cimErrorDetails = NULL;
    DSC_EventWriteLCMValidatingPartialConfigMetaConfExclusiveResources();
    //Get the property from the instance
    result = MI_Instance_GetElement(partialInstance, MSFT_PartialConfiguration_ExclusiveResources, &value, NULL, &flags, 0);
    GOTO_CLEANUP_AND_THROW_ERROR2PARAMS_IF_FAILED(result, ID_ENGINEHELPER_GET_PROPERTY_X_FROM_Y_FAILED, cimErrorDetails, MSFT_PartialConfiguration_ExclusiveResources, MSFT_PARTIALCONFIGURATION_CLASSNAME, Cleanup);
    if (!(flags & MI_FLAG_NULL) && value.stringa.data != NULL )
    {
        //Assign size of new array and allocate it
        newSize = arrayOutput->size + value.stringa.size;
        // arrayOutput->size is a Uint32, Fail now if newSize would be truncated later on
        if (newSize > 0xFFFFFFFF)
        {
            return MI_RESULT_SERVER_LIMITS_EXCEEDED;
        }

        tempArrayOutput = DSC_malloc(newSize*sizeof(MI_Char*), NitsHere());
        if (!tempArrayOutput)
        {
            return MI_RESULT_SERVER_LIMITS_EXCEEDED;
        }

        for (count = 0; count < value.stringa.size; count++)
        {
            for (innerCount = 0; innerCount < arrayOutput->size && innerCount < newSize; innerCount++)
            {
                if (Tcscasecmp(value.stringa.data[count], arrayOutput->data[innerCount]) == 0)
                {
                    //This means there was a match for exclusive resources between two partial config definitions
                    result = GetCimMIError1Param(MI_RESULT_INVALID_PARAMETER, cimErrorDetails, ID_MODMAN_PARTIALCONFIG_DUPLICATE_EXCLUSIVERESOURCE, value.stringa.data[count]);
                    break;
                }
                //Add this element into temparrayOutput only if not already copied
                if (!alreadyCopiedArrayOutputIntoTemp)
                {
                    tempArrayOutput[innerCount] = arrayOutput->data[innerCount];
                }

            }
            GOTO_CLEANUP_IF_FAILED(result, Cleanup);
            //Mark that you've already copied the arrayOutput fully into temporary array
            alreadyCopiedArrayOutputIntoTemp = MI_TRUE;
            //Have to copy each element from value.stringa as well
            newIndexOfArrayOutput = arrayOutput->size + count;
            tempArrayOutput[newIndexOfArrayOutput] = value.stringa.data[count];
        }
        //Now make arrayOutput point to tempArrayOutput which is the new combined array
        if (arrayOutput->size != 0)
        {//Avoiding DSCFree when its not initialized at all.
            DSC_free(arrayOutput->data);
        }
        arrayOutput->data = tempArrayOutput;
        arrayOutput->size = newSize;

    }
    return result;
    //Check the property if empty - if not, then save each string into stringA
Cleanup:
    DSCFREE_IF_NOT_NULL(tempArrayOutput);
    return result;
}
/*Function that calls all other validation functions for partial configuration*/
MI_Result ValidatePartialConfiguration(_In_ ModuleManager* moduleManager,
    _In_z_ const MI_Char* partialConfigLocation,
    _In_ MI_Instance* metaConfigInstance,
    _Outptr_result_maybenull_ MI_Instance ** cimErrorDetails)
{
    MI_Result result = MI_RESULT_OK;
    MI_Instance* partialConfigMetaInstance = NULL;
    MI_StringA* OthersExclusiveResourceList = NULL;
    MI_InstanceA resourceInstances = { 0 };
    MI_InstanceA miInstanceArray = { 0 };
    MI_Instance* documentIns = NULL;
    MI_Uint32 flags = 0;
    MI_Value value;
    if (metaConfigInstance == NULL || partialConfigLocation == NULL || cimErrorDetails == NULL || moduleManager==NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *cimErrorDetails = NULL;
    //Get the partial configuration's name
    result = GetArrayInstancesFromSingleMof(moduleManager, flags | VALIDATE_DOCUMENT_INSTANCE,
        partialConfigLocation, &miInstanceArray, cimErrorDetails, MI_FALSE);
    if (result != MI_RESULT_OK)
    {
        return result;
    }
    result = FilterForConfigurationResource(&miInstanceArray, &resourceInstances, &documentIns, cimErrorDetails);
    GOTO_CLEANUP_IF_FAILED(result, Cleanup);
    result = DSC_MI_Instance_GetElement(documentIns, OMI_ConfigurationDocument_Name, &value, NULL, &flags, NULL);
    if (result != MI_RESULT_OK || (flags & MI_FLAG_NULL))
    {
        //Output error that there is no name present
        result = GetCimMIError2Params(MI_RESULT_INVALID_CLASS, cimErrorDetails, ID_ENGINEHELPER_GET_PROPERTY_X_FROM_Y_FAILED, OMI_ConfigurationDocument_Name, BASE_DOCUMENT_CLASSNAME);
        goto Cleanup;
    }
    DSC_EventWriteLCMValidatingPartialConfiguration(value.string);
    OthersExclusiveResourceList = (MI_StringA*)DSC_malloc(sizeof(MI_StringA), NitsHere());
    OthersExclusiveResourceList->data = NULL;
    OthersExclusiveResourceList->size = 0;
    //First check if this is a valid partial config name, and get back the partial config metaconfig instance
    result = ValidatePartialConfigurationName(value.string, metaConfigInstance, &partialConfigMetaInstance, OthersExclusiveResourceList, cimErrorDetails);
    GOTO_CLEANUP_IF_FAILED(result,Cleanup);

    //Validate if the file was written correctly
    result = ValidatePartialConfigFile(value.string, resourceInstances, partialConfigMetaInstance, OthersExclusiveResourceList, cimErrorDetails);
    GOTO_CLEANUP_IF_FAILED(result, Cleanup);

 Cleanup:
    if (OthersExclusiveResourceList != NULL)
    {
        DSCFREE_IF_NOT_NULL(OthersExclusiveResourceList->data);
        DSC_free(OthersExclusiveResourceList);
    }
    CleanUpInstanceCache(&resourceInstances);
    INSTANCE_DELETE_IF_NOT_NULL(documentIns);

    return result;
}

/*Function to validate a mof instance based on the flags passed in*/
MI_Result ValidatePartialConfigMergedFile(_In_ ModuleManager *moduleManager,
    _In_z_ const MI_Char * mofFilePath,
    _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Result r = MI_RESULT_OK;
    MI_Uint32 flags = 0;
    /*Form full path to mof file*/
    MI_InstanceA *miTempInstanceArray = NULL;
    MI_Uint8 *pbuffer = NULL;
    MI_Uint32 contentSize=0;
    MI_Uint32 readBytes;
    MI_ClassA miClassArray = { 0 };
    ModuleLoaderObject *moduleLoader = NULL;

    if (extendedError == NULL || moduleManager==NULL || mofFilePath == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    *extendedError = NULL;  // Explicitly set *extendedError to NULL as _Outptr_ requires setting this at least once.
    //If the provided moffile path is not null, only then read the file's contents, else check the

    /*Load the manager*/
    DSC_EventWriteLCMValidatingMergedPartial();
    r = LoadModuleManager(moduleManager, extendedError);
    if (r != MI_RESULT_OK)
    {
        return r;
    }
    r = ReadFileContent(mofFilePath, &pbuffer, &contentSize, extendedError);
    if (r != MI_RESULT_OK)
    {
        return r;
    }

    moduleLoader = (ModuleLoaderObject*)moduleManager->reserved2;
    miClassArray.size = moduleLoader->schemaCount;
    miClassArray.data = moduleLoader->providerSchema;
    r = MI_Deserializer_DeserializeInstanceArray(moduleLoader->deserializer, 0, moduleLoader->strictOptions, 0, pbuffer, contentSize, &miClassArray, &readBytes, &miTempInstanceArray, extendedError);
    GOTO_CLEANUP_IF_FAILED(r, Exit);

    r = ValidateDSCDocumentInstance(miTempInstanceArray, flags | VALIDATE_DOCUMENT_INSTANCE, extendedError);
    if (r != MI_RESULT_OK)
    {
        //Appending the underlying errors to this error ID
        AppendWMIError1ParamID(*extendedError, ID_LCM_PARTIALCONFIG_MERGED_VALIDATION_FAILED);
    }
    GOTO_CLEANUP_IF_FAILED(r, Exit);

Exit:
    DSCFREE_IF_NOT_NULL(pbuffer);
    if (miTempInstanceArray != NULL)
    {
        CleanUpDeserializerInstanceCache(miTempInstanceArray);
    }
    return r;


}

MI_Result ValidateClassPropertiesForMetaConfResourceChildren(_In_ MI_Class *configDocumentClass,
                                                             _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Result r;
    r = ValidateClassProperties(configDocumentClass, g_PartialConfigurationProperties, OMI_MetaConfigurationResource_Property_Count, extendedError);

    return r;
}


//Function to validate if the given module and resource name match as per the convention to the given partial configuration file. If matchFound = true,its bad.
MI_Result ValidateResourceObeysExclusiveResourcesRule(_In_z_ MI_Char* exclusiveResourceString,
    _In_ MI_Instance* resourceInstance,
    _Inout_ MI_Boolean * obeysExclusiveResources,
    _Outptr_result_maybenull_ MI_Instance** cimErrorDetails)
{
    /*
    Three ways in which we can represent module/resource
    1. Module\*
    2. Modulename\resourcename
    3. ResourceName
    */
    MI_Uint32 numCharactersBeforeDelimiter = 0;
    MI_Boolean gotDelimiter = MI_FALSE;
    MI_Boolean matchFound = MI_FALSE;
    MI_Char* resourceName = NULL;
    //MI_Char* token = NULL;
    MI_Char* moduleName = NULL;
    MI_Value valueModuleName;
    MI_Char* travelPtr = NULL;
    //MI_Char* delimiter = L"\\";
    MI_Result result = MI_RESULT_OK;
    if (cimErrorDetails == NULL || exclusiveResourceString==NULL)
    {
        return MI_RESULT_NOT_FOUND;
    }
    *cimErrorDetails = NULL;
    if (resourceInstance == NULL || resourceInstance->classDecl == NULL || resourceInstance->classDecl->name == NULL)
    {
        result = GetCimMIError(MI_RESULT_INVALID_CLASS, cimErrorDetails, ID_LCM_INVALID_RESOURCEINSTANCE);
        return result;
    }
    DSC_EventWriteLCMValidatingPartialExclusiveResourceStringDefintion(exclusiveResourceString);
    //Get the module name from the resource instance
    result = MI_Instance_GetElement(resourceInstance, OMI_BaseResource_ModuleName, &valueModuleName, NULL, NULL, NULL);
    if (result != MI_RESULT_OK)
    {
        result = GetCimMIError1Param(MI_RESULT_INVALID_CLASS, cimErrorDetails, ID_LCM_MODULENAME_NOTFOUND, resourceInstance->classDecl->name);
        return result;
    }
    moduleName = exclusiveResourceString;

    travelPtr = exclusiveResourceString;
    while (*travelPtr)
    {
        while (*travelPtr == '\\')
        {
            travelPtr++;//Just don't count delimiter
            gotDelimiter = MI_TRUE;
        }
        if (gotDelimiter)
        {
            //This means module is correctly identified, but resource name is yet to be assigned.
            resourceName = travelPtr;
            break;
        }
        travelPtr++;
        numCharactersBeforeDelimiter++;
    }
    //Check for error cases
    if ((gotDelimiter && (resourceName == NULL  || numCharactersBeforeDelimiter == 0 ))) //Case:  \ResourceName , \ and ModuleName
    {
        result = GetCimMIError2Params(MI_RESULT_INVALID_PARAMETER, cimErrorDetails, ID_PARTIALCONFIG_INVALID_EXCLUSIVERESOURCESTRING, exclusiveResourceString, resourceInstance->classDecl->name);
        return result;
    }

    //Case by Case basis.Case1: Module
    if (moduleName != NULL && Tcscasecmp(resourceName, L"*") == 0)
    {
        if (Tcsncasecmp(valueModuleName.string, moduleName, numCharactersBeforeDelimiter) == 0)
        {
            matchFound = MI_TRUE;
        }
    }

    //Case 2 ModuleName/ResourceName,
    else if (resourceName!=NULL)
    {
        if (Tcscasecmp(resourceInstance->classDecl->name, resourceName) == 0)
        {
            matchFound = MI_TRUE;
        }
    }
    else if (resourceName == NULL) //Case 3 : ResourceName
    {
        if (Tcscasecmp(resourceInstance->classDecl->name, exclusiveResourceString) == 0)
        {
            matchFound = MI_TRUE;
        }
    }
    //It obeys exclusive resource rule as long as match isn't found.
    *obeysExclusiveResources = !matchFound;
    return result;
}


MI_Result ValidateMetaConfResourceProperties(_In_ MI_Class *configDocumentClass,
                                             _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Result r;
    r = ValidateClassProperties(configDocumentClass, g_MetaConfigurationResourceProperties, 0, extendedError);

    return r;
}

