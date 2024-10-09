/*
   PowerShell Desired State Configuration for Linux

   Copyright (c) Microsoft Corporation

   All rights reserved. 

   MIT License

   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the ""Software""), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED *AS IS*, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
#include <string.h>
#include "EngineHelperInternal.h"
#include "RegistrationManager.h"
#include "RegistrationManagerHelper.h"
//#include "PAL_Extension.h"
#include "EventWrapper.h"
#include "DSC_Systemcalls.h"
#include <MI.h>
#include "EngineHelper.h"
#include "LocalConfigManagerHelper.h"
#include "Resources_LCM.h"

MI_Result RegistrationManager_HandleRequest(
    _In_ RegistrationManager* self,
    _In_ RegistrationRequest *request,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result result = MI_RESULT_OK;

    if (cimErrorDetails)
    {
        *cimErrorDetails = NULL;
    }
    
    if (ShouldDoRegistration(self, request, cimErrorDetails))
    {
        return Register(self, request, cimErrorDetails);
    }

    *cimErrorDetails = NULL;       
    return result;
}

/*Function to return true if registration needs to be done*/
MI_Boolean ShouldDoRegistration(
    _In_ RegistrationManager* self,
    _In_ RegistrationRequest* request,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{   
    MI_Result result = MI_RESULT_OK;
    MI_Uint32 i = 0;
    MI_Value value;
    MI_Char* url = NULL;    
    MI_Type type;
    MI_Uint32 flags;

    if (cimErrorDetails)
    {
        *cimErrorDetails = NULL;
    }

    MI_Instance *registrationDataInstance = request->registrationData;

    result = MI_Instance_GetElement(registrationDataInstance, MSFT_ServerURL_Name, &value, NULL, NULL, NULL);
    EH_CheckResult(result);
    url = value.string;
    
    result = DSC_MI_Instance_GetElement(registrationDataInstance, MSFT_RegistrationKey_Name, &value, &type, &flags, NULL);
    if (result == MI_RESULT_OK && type == MI_STRING && (!(flags & MI_FLAG_NULL)))
    {
        if (Tcscasecmp(value.string, MI_T("")) != 0)
        {
            // This means that a non-empty RegistrationKey is specified. Then, we always do registration. 
            // If RegistrationKey is not specified, then we check the cache and do/skip registration based on ServerURL
            DSC_EventWriteLCMAgentAttemptRegistration(g_ConfigurationDetails.jobGuidString, self->agentId, url);
            return MI_TRUE;
        }
    }

    for (i = 0; i < self->numberOfServerURLs; i++)
    {
        if (Tcscasecmp(self->serverURLs[i], url) == 0)
        {
            DSC_EventWriteLCMServerURLRegistered(g_ConfigurationDetails.jobGuidString, self->agentId, url);
            return MI_FALSE;
        }
    }
    
    EH_UNWIND;

    return MI_TRUE;
}


/*Function to perform registration*/
MI_Result Register(
    _In_ RegistrationManager* self,
    _In_ RegistrationRequest* request,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result result = MI_RESULT_OK;
    MI_Instance* registrationPayload = NULL;
    MI_Uint32 getActionStatusCode;
    MI_Char* resultStatus = NULL;
    MI_Char* thumbprint = NULL;
    MI_Value value;
    MI_Uint32 flags;
    int systemResult = 0;

    if (cimErrorDetails)
    {
        *cimErrorDetails = NULL;
    }


    // Check if RegistrationKey is specified. If not specified, do not attempt to register.
    result = MI_Instance_GetElement(request->registrationData, MSFT_RegistrationKey_Name, &value, NULL, &flags, NULL);
    if (result != MI_RESULT_OK || flags & MI_FLAG_NULL || value.string == NULL || value.string[0] == '\0')
    {
	return MI_RESULT_OK;
    }


    if ( (access(OAAS_KEYPATH, F_OK) == -1) || (access(OAAS_CERTPATH, F_OK) == -1) )
    {
        system("touch "  OAAS_KEYPATH "; chmod 0600 "  OAAS_KEYPATH);
        DSC_LOG_INFO("Executed '%T'\n", "touch "  OAAS_KEYPATH "; chmod 0600 "  OAAS_KEYPATH);
        system("touch "  OAAS_KEYPATH "_old; chmod 0600 "  OAAS_KEYPATH "_old");
        DSC_LOG_INFO("Executed '%T'\n", "touch "  OAAS_KEYPATH "_old; chmod 0600 "  OAAS_KEYPATH "_old");
        systemResult = system("openssl req -subj '/CN=DSC-OaaS' -new -newkey rsa:2048 -days 365 -nodes -x509 -keyout " OAAS_KEYPATH "_old -out " OAAS_CERTPATH " && openssl rsa -in " OAAS_KEYPATH "_old -out " OAAS_KEYPATH " && rm -f " OAAS_KEYPATH "_old");
        DSC_LOG_INFO("Executed '%T', returned %d\n", "openssl req -subj '/CN=DSC-OaaS' -new -newkey rsa:2048 -days 365 -nodes -x509 -keyout " OAAS_KEYPATH "_old -out " OAAS_CERTPATH " && openssl rsa -in " OAAS_KEYPATH "_old -out " OAAS_KEYPATH " && rm -f " OAAS_KEYPATH "_old", systemResult);
        if (systemResult != 0 && errno != 10)
        {
            DSC_EventWriteLCMServerRegCertGenFailed(g_ConfigurationDetails.jobGuidString, self->agentId);
            return GetCimMIError(MI_RESULT_FAILED, cimErrorDetails, ID_PULL_FAILEDTOGENERATECERT);
        }
        
        systemResult = system("openssl x509 -noout -in " OAAS_CERTPATH " -fingerprint | sed 's/^.*=//' > " OAAS_THUMBPRINTPATH);
        DSC_LOG_INFO("Executed '%T', returned %d\n", "openssl x509 -noout -in " OAAS_CERTPATH " -fingerprint | sed 's/^.*=//' > " OAAS_THUMBPRINTPATH, systemResult);
        if (systemResult != 0 && errno != 10)
        {
            DSC_EventWriteLCMServerRegCertGenFailed(g_ConfigurationDetails.jobGuidString, self->agentId);
            return GetCimMIError(MI_RESULT_FAILED, cimErrorDetails, ID_PULL_FAILEDTOGENERATECERT);
        }
        
        {
            long length;
            FILE * fingerprint_file = fopen (OAAS_THUMBPRINTPATH, "r");
            if (fingerprint_file)
            {
                fseek (fingerprint_file, 0, SEEK_END);
                length = ftell (fingerprint_file);
                fseek (fingerprint_file, 0, SEEK_SET);
                thumbprint = DSC_malloc (length * sizeof(MI_Char), NitsHere());
                fread (thumbprint, 1, length, fingerprint_file);
                // There's a newline at the end, so null terminate overwriting it.
                thumbprint[length-1] = '\0';
                fclose (fingerprint_file);
            }
            else
            {
                DSC_EventWriteLCMServerRegCertGenFailed(g_ConfigurationDetails.jobGuidString, self->agentId);
                return MI_RESULT_FAILED;
            }
        }

        // Cache this URL.
        // result = CacheServerURL(self, request->registrationData, thumbprint, cimErrorDetails);
        //EH_CheckResult(result);

        // Write this cache to DSC Cache
        //result = UpdateServerURLsToDSCCache(self, cimErrorDetails);
        //EH_CheckResult(result);
    }

    result = GetAgentInformation(&registrationPayload);
    EH_CheckResult(result);

    result = LCM_Do_Register((MI_Instance *)g_metaConfig, request->registrationData, self->agentId, thumbprint, 
                             registrationPayload, &request->configurationNames, request->typeOfManagerInstance, &resultStatus, &getActionStatusCode, cimErrorDetails);
    EH_CheckResult(result);

    EH_UNWIND;

    if (registrationPayload != NULL)
    {
        MI_Instance_Delete(registrationPayload);
    }
    if (thumbprint != NULL)
    {
        DSC_free(thumbprint);
    }
    DSC_free(resultStatus);
    return result;
}

MI_Char* InhaleTextFile(MI_Char* filePath)
{
    long length;
    MI_Char * result;
    FILE * myfile = fopen (filePath, "r");
    if (myfile)
    {
        if ( fseek (myfile, 0, SEEK_END) == -1 )
        {
            fclose (myfile);
            return NULL;
        }
        if ( (length = ftell (myfile)) == -1 )
        {
            fclose (myfile);
            return NULL;
        }
        if ( fseek (myfile, 0, SEEK_SET) == -1 )
        {
            fclose (myfile);
            return NULL;
        }
        if ( (result = DSC_malloc ((length+1) * sizeof(MI_Char), NitsHere())) == NULL )
        {
            fclose (myfile);
            return NULL;
        }
        
        fread (result, 1, length, myfile);
        result[length] = '\0';
        fclose (myfile);
        return result;
    }
    else
    {
        return NULL;
    }
}                       

/*Function to return agentId value from registry 
If registration has never been attempted on this machine, then this function returns an empty string and returns TRUE for shouldGenerateAgentId*/
MI_Result GetAgentIdFromRegistry(
    _Outptr_result_maybenull_z_ MI_Char** agentId, 
    _Inout_ MI_Boolean *shouldGenerateAgentId)
{    
    if (File_ExistT(AGENTID_FILE_PATH) != 0)
    {
        // We are not able to get the agent Id. 
        // We should generate a new agentId.
        *shouldGenerateAgentId = MI_TRUE ;
        return MI_RESULT_OK;        
    }
    
    /* inhale contents fails into agentId */
    *agentId = InhaleTextFile(AGENTID_FILE_PATH);

    if (Tcscasecmp(MI_T(" "), *agentId) == 0 || Tcscasecmp(MI_T(""), *agentId) == 0)
    {
        // We are not able to get the agent Id. 
        // We should generate a new agentId.
        *shouldGenerateAgentId = MI_TRUE;
        return MI_RESULT_OK;
    } 
    *shouldGenerateAgentId = MI_FALSE;    

    (*agentId)[36] = '\0';

    return MI_RESULT_OK;
}

/*Function to write agentId value to registry */
MI_Result WriteAgentIdToRegistry(
    _In_z_ MI_Char* agentId,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result result = MI_RESULT_OK;
    FILE *fp = fopen(AGENTID_FILE_PATH, "w");
    if (fp != NULL)
    {
        fputs(agentId, fp);
        fclose(fp);
    }
    else
    {
        result = MI_RESULT_FAILED;
    }
    return result;
}

MI_Result CacheServerURL(
    _In_ RegistrationManager* self,
    _In_ MI_Instance* registrationData,
    _In_z_ MI_Char* thumbprint,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Value value;
    MI_Result result = MI_RESULT_OK;

    if (cimErrorDetails)
    {
        *cimErrorDetails = NULL;
    }

    if (self->serverURLs == NULL)
    {
        InitializeServerURLs(self, cimErrorDetails);
    }

    result = MI_Instance_GetElement(registrationData, MSFT_ServerURL_Name, &value, NULL, NULL, NULL);
    EH_CheckResult(result);

    result = WriteServerURLToCache(self, value.string, thumbprint, cimErrorDetails);
    EH_CheckResult(result);    

    EH_UNWIND;

    return result;
}

MI_Result WriteServerURLToCache(
    _In_ RegistrationManager* self,
    _In_z_ MI_Char* serverURL,
    _In_z_ MI_Char* thumbprint,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    size_t dwSize;
    int retValue;
    MI_Result result = MI_RESULT_OK;

    if (cimErrorDetails)
    {
        *cimErrorDetails = NULL;
    }

    if (self->serverURLs == NULL)
    {
        result = InitializeServerURLs(self, cimErrorDetails);
        EH_CheckResult(result);
    }
    
    dwSize = Tcslen(serverURL) + 1;
    self->serverURLs[self->numberOfServerURLs] = (MI_Char*)DSC_malloc(dwSize* sizeof(MI_Char), NitsHere());
    if (self->serverURLs[self->numberOfServerURLs] == NULL)
    {
        EH_Fail_(GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_LCMHELPER_MEMORY_ERROR));
    }
    retValue = Stprintf(self->serverURLs[self->numberOfServerURLs], dwSize, MI_T("%T"), serverURL);
    if (retValue == -1 || NitsShouldFault(NitsHere(), NitsAutomatic))
    {
        DSC_free(self->serverURLs[self->numberOfServerURLs]);
        EH_Fail_(GetCimMIError(MI_RESULT_FAILED, cimErrorDetails, ID_LCMHELPER_PRINTF_ERROR));
    }

    dwSize = Tcslen(thumbprint) + 1;
    self->serverCertificateThumbprints[self->numberOfServerURLs] = (MI_Char*)DSC_malloc(dwSize* sizeof(MI_Char), NitsHere());
    if (self->serverCertificateThumbprints[self->numberOfServerURLs] == NULL)
    {
        EH_Fail_(GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_LCMHELPER_MEMORY_ERROR));
    }
    retValue = Stprintf(self->serverCertificateThumbprints[self->numberOfServerURLs], dwSize, MI_T("%T"), thumbprint);
    if (retValue == -1 || NitsShouldFault(NitsHere(), NitsAutomatic))
    {
        DSC_free(self->serverCertificateThumbprints[self->numberOfServerURLs]);
        EH_Fail_(GetCimMIError(MI_RESULT_FAILED, cimErrorDetails, ID_LCMHELPER_PRINTF_ERROR));
    }

    self->numberOfServerURLs++;

    EH_UNWIND;

        return result;
}

MI_Result PopulateServerURLs(
    _In_ RegistrationManager* self,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result result = MI_RESULT_OK;
    MI_Char* registeredServerURLs = NULL;   
    MI_Char* token = NULL;
    MI_Char* thumbprint = NULL;
    MI_Char *next_token = NULL;    

    if (cimErrorDetails)
    {
        *cimErrorDetails = NULL;
    }
    
    result = GetRegisteredServerURLsFromCache(&registeredServerURLs, cimErrorDetails);
    // If we are not able to retrieve the URLs from the cache, we just initialize to an empty list. 
    if (result != MI_RESULT_OK)
    {
        result = InitializeServerURLs(self, cimErrorDetails);
        EH_CheckResult(result);
    }
    else
    {
        if ((registeredServerURLs != NULL) || (g_DSCInternalCache != NULL))
        {
            token = Tcstok(registeredServerURLs, ";", &next_token);
            while (token != NULL)
            {
                // thumbprint can be null as in http:\\xxx;;http:\\yyy;xxxxxx
                thumbprint = Tcstok(NULL, ";", &next_token);

                result = WriteServerURLToCache(self, token, thumbprint, cimErrorDetails);
                if (result != MI_RESULT_OK)
                {
                    result = InitializeServerURLs(self, cimErrorDetails);
                    EH_CheckResult(result);
                    // There is no point in proceeding with the other URLs as we could hit the same error that we hit this time
                    // So, initialize to Empty list and break
                    break;
                }
                token = Tcstok(NULL, ";", &next_token);
            }
        }
        else
        {
            // We will be here in the following 2 cases
            // 1) Cache does not exist
            // 2) There were no Server URLs in the cache
            result = InitializeServerURLs(self, cimErrorDetails);
            EH_CheckResult(result);
        }        
    }

    EH_UNWIND;

    if (registeredServerURLs != NULL)
    {
        DSC_free(registeredServerURLs);
    }
    return result;
}

MI_Result GetRegisteredServerURLsFromCache(
    _Outptr_result_maybenull_z_ MI_Char** registeredServerURLs,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Value value;
    MI_Type  type;
    MI_Uint32 flags;
    size_t dwSize;
    int retValue;

    if (cimErrorDetails)
    {
        *cimErrorDetails = NULL;
    }

    memset(registeredServerURLs, 0, sizeof(MI_Char));

    if (g_DSCInternalCache)
    {
        if (DSC_MI_Instance_GetElement(g_DSCInternalCache, DSC_InternalStateCache_RegisteredServerURLs, &value, &type, &flags, NULL) == MI_RESULT_OK)
        {
            dwSize = Tcslen(value.string) + 1;
            *registeredServerURLs = (MI_Char*)DSC_malloc(dwSize*sizeof(MI_Char), NitsHere());
            if (*registeredServerURLs == NULL)
            {
                EH_Fail_(GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_LCMHELPER_MEMORY_ERROR));
            }
            memset(*registeredServerURLs, 0, dwSize);

            retValue = Stprintf(*registeredServerURLs, dwSize, MI_T("%T"), value.string);
            if (retValue == -1 || NitsShouldFault(NitsHere(), NitsAutomatic))
            {
                DSC_free(*registeredServerURLs);
                EH_Fail_(GetCimMIError(MI_RESULT_FAILED, cimErrorDetails, ID_LCMHELPER_PRINTF_ERROR));
            }
        }
    }

    EH_UNWIND;
        return MI_RESULT_OK;
}

MI_Result UpdateServerURLsToDSCCache(
    _In_ RegistrationManager *self,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Application miApp = MI_APPLICATION_NULL;
    MI_Result result = MI_RESULT_OK;
    MI_Char* serverURLs = NULL;
        
    result = DSC_MI_Application_Initialize(0, NULL, NULL, &miApp);
    if (result != MI_RESULT_OK)
    {
        EH_Fail_(GetCimMIError(result, cimErrorDetails, ID_MODMAN_APPINIT_FAILED));
    }    

    result = FormatServerURLsForDscCache(self, &serverURLs, cimErrorDetails);
    EH_CheckResult(result);

    result = UpdateCurrentStatus(NULL, NULL, NULL, serverURLs, cimErrorDetails);
    EH_CheckResult(result);

    EH_UNWIND;
    if (serverURLs)
    {
        DSC_free(serverURLs);
    }
    MI_Application_Close(&miApp);    
    return result;
}

MI_Result FormatServerURLsForDscCache(
    _In_ RegistrationManager *self,
    _Outptr_result_maybenull_z_ MI_Char** registeredServerURLs,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    MI_Result result = MI_RESULT_OK;
    size_t totalLengthOfServerURLs = 0;
    MI_Uint32 i = 0;

    for (i = 0; i < self->numberOfServerURLs; i++)
    {
        totalLengthOfServerURLs += Tcslen(self->serverURLs[i]);
        // Add 1 for ; between each ServerURL and thumbprint
        totalLengthOfServerURLs += 1;

        totalLengthOfServerURLs += Tcslen(self->serverCertificateThumbprints[i]);
        // Add 1 for ; between each series
        totalLengthOfServerURLs += 1;
    }

    // For '\0'
    totalLengthOfServerURLs += 1;
    
    memset(registeredServerURLs, 0, sizeof(MI_Char));
    *registeredServerURLs = (MI_Char*)DSC_malloc(totalLengthOfServerURLs*sizeof(MI_Char), NitsHere());
    if (*registeredServerURLs == NULL)
    {
        EH_Fail_(GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_LCMHELPER_MEMORY_ERROR));
    }
    memset(*registeredServerURLs, 0, totalLengthOfServerURLs);

    for (i = 0; i < self->numberOfServerURLs; i++)
    {
        *registeredServerURLs = Tcscat(*registeredServerURLs, totalLengthOfServerURLs, self->serverURLs[i]);
        if (*registeredServerURLs == NULL)
        {
            EH_Fail_(GetCimMIError(MI_RESULT_FAILED, cimErrorDetails, ID_ENGINEHELPER_CAT_ERROR));
        }
        *registeredServerURLs = Tcscat(*registeredServerURLs, totalLengthOfServerURLs, ";");
        if (*registeredServerURLs == NULL)
        {
            EH_Fail_(GetCimMIError(MI_RESULT_FAILED, cimErrorDetails, ID_ENGINEHELPER_CAT_ERROR));
        }
        *registeredServerURLs = Tcscat(*registeredServerURLs, totalLengthOfServerURLs, self->serverCertificateThumbprints[i]);
        if (*registeredServerURLs == NULL)
        {
            EH_Fail_(GetCimMIError(MI_RESULT_FAILED, cimErrorDetails, ID_ENGINEHELPER_CAT_ERROR));
        }
        *registeredServerURLs = Tcscat(*registeredServerURLs, totalLengthOfServerURLs, ";");
        if (*registeredServerURLs == NULL)
        {
            EH_Fail_(GetCimMIError(MI_RESULT_FAILED, cimErrorDetails, ID_ENGINEHELPER_CAT_ERROR));
        }
    }

    EH_UNWIND;
        return result;
}

MI_Result InitializeServerURLs(
    _In_ RegistrationManager *self,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    self->serverURLs = (MI_Char**)DSC_malloc(NUMBEROFSERVERURLS * sizeof(MI_Char*), NitsHere());
    if (self->serverURLs == NULL)
    {
        EH_Fail_(GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_LCMHELPER_MEMORY_ERROR));
    }
    self->serverCertificateThumbprints = (MI_Char**)DSC_malloc(NUMBEROFSERVERURLS * sizeof(MI_Char*), NitsHere());
    if (self->serverCertificateThumbprints == NULL)
    {
        EH_Fail_(GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_LCMHELPER_MEMORY_ERROR));
    }
    self->numberOfServerURLs = 0;

    EH_UNWIND;
        return MI_RESULT_OK;
}

MI_Result GetThumbprintForRegisteredServerURL(
    _In_ RegistrationManager* self,
    _In_ MI_Instance* registrationData,
    _Outptr_result_maybenull_z_ MI_Char** thumbprint,
    _Outptr_result_maybenull_ MI_Instance **cimErrorDetails)
{
    size_t dwSize;
    MI_Value value;
    int retValue;
    MI_Uint32 i = 0;
    MI_Result result = MI_RESULT_OK;

    if (cimErrorDetails)
    {
        *cimErrorDetails = NULL;
    }

    *thumbprint = 0;

    if (self->serverURLs == NULL)
    {
        InitializeServerURLs(self, cimErrorDetails);
    }

    if (self->serverURLs != NULL)
    {
        result = MI_Instance_GetElement(registrationData, MSFT_ServerURL_Name, &value, NULL, NULL, NULL);
        EH_CheckResult(result);

        for (i = 0; i < self->numberOfServerURLs; i++)
        {
            if (self->serverURLs[i] != NULL && Tcscasecmp(self->serverURLs[i], value.string) == 0)
            {
                dwSize = Tcslen(self->serverCertificateThumbprints[i]) + 1;
                *thumbprint = (MI_Char*)DSC_malloc(dwSize*sizeof(MI_Char), NitsHere());
                if (*thumbprint == NULL)
                {
                    EH_Fail_(GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, cimErrorDetails, ID_LCMHELPER_MEMORY_ERROR));
                }

                retValue = Stprintf(*thumbprint, dwSize, MI_T("%T"), self->serverCertificateThumbprints[i]);
                if (retValue == -1 || NitsShouldFault(NitsHere(), NitsAutomatic))
                {
                    DSC_free(*thumbprint);
                    EH_Fail_(GetCimMIError(MI_RESULT_FAILED, cimErrorDetails, ID_LCMHELPER_PRINTF_ERROR));
                }

                result = MI_RESULT_OK;
                break;
            }
        }
    }

EH_UNWIND;

    return result;
}
