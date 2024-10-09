/*
   PowerShell Desired State Configuration for Linux

   Copyright (c) Microsoft Corporation

   All rights reserved.

   MIT License

   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the ""Software""), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED *AS IS*, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
#include <MI.h>
#include "DSC_Systemcalls.h"
#include "EngineHelper.h"
#include "Resources_LCM.h"
#include <pal/format.h>
#include <pal/file.h>
#include <pal/dir.h>
#include <base/base64.h>
#include "CAEngine.h"
#include "CAEngineInternal.h"
#include "EventWrapper.h"
#include <ctype.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <curl/curl.h>
#include <base/conf.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include "WebPullClient.h"

typedef struct ModuleClassList ModuleClassList;
struct ModuleClassList {
    ModuleClassList* next;
    MI_Char* moduleClass;
};

typedef struct ModuleVersionClassTuple ModuleVersionClassTuple;
struct ModuleVersionClassTuple {
    MI_Char* moduleVersion;
    ModuleClassList* first;
};

typedef struct ModuleTableEntry ModuleTableEntry;
struct ModuleTableEntry {
    ModuleTableEntry* next;
    MI_Char* moduleName;
    ModuleVersionClassTuple* moduleVersionClassTuple;
};

typedef struct ModuleTable ModuleTable;
struct ModuleTable {
    ModuleTableEntry* first;
};

static MI_Result GetModuleNameVersionTable(MI_Char* mofFileLocation,
                                           ModuleTable* table,
                                           _Outptr_result_maybenull_ MI_Instance **extendedError);

struct ReadChunk
{
    char * data;
    size_t last_char;
};

struct Chunk
{
    size_t size;
    char * data;
};

struct HeaderChunk
{
    size_t size;
    char ** headerKeys;
    char ** headerValues;
};

static void InitHeaderChunk(struct HeaderChunk* chunk)
{
    chunk->size = 0;
    chunk->headerKeys = malloc(sizeof(char*));
    chunk->headerValues = malloc(sizeof(char*));

    if (chunk->headerKeys == NULL || chunk->headerValues == NULL)
    {
        fprintf(stderr, "Error: Out of memory in InitHeaderChunk");
        abort();
    }
}

static void CleanupHeaderChunk(struct HeaderChunk * chunk)
{
    size_t i;
    for (i = 0; i < chunk->size; ++i)
    {
        free(chunk->headerKeys[i]);
        free(chunk->headerValues[i]);
    }

    chunk->size = 0;
    free(chunk->headerKeys);
    free(chunk->headerValues);
    chunk->headerKeys = NULL;
    chunk->headerValues = NULL;

}

struct SSLOptions g_sslOptions;
MI_Char* InhaleTextFile(MI_Char* filePath);

static MI_Result GetSSLOptions(_Outptr_result_maybenull_ MI_Instance **extendedError)
{
    Conf* conf = NULL;
    MI_Char* text;

    g_sslOptions.DoNotCheckCertificate = MI_FALSE;
    g_sslOptions.NoSSLv3 = MI_FALSE;
    g_sslOptions.cipherList[0] = '\0';
    g_sslOptions.CABundle[0] = '\0';
    g_sslOptions.Proxy[0] = '\0';

    conf = Conf_Open(OMI_CONF_FILE_PATH);
    if (!conf)
    {
        return GetCimMIError(MI_RESULT_NOT_FOUND, extendedError, ID_PULL_DSCCONF_NOTOPENABLE);
    }

    for (;;)
    {
        const char* key;
        const char* value;
        int r = Conf_Read(conf, &key, &value);
        if (r == -1)
        {
            Conf_Close(conf);
            return GetCimMIError1Param(MI_RESULT_NOT_FOUND, extendedError, ID_PULL_DSCCONF_NOTREADABLE, scs(Conf_Error(conf)));
        }

        if (r == 1)
        {
            break;
        }

        if (strcasecmp(key, "DoNotCheckCertificate") == 0)
        {
            if (strcasecmp(value, "true") == 0)
            {
                g_sslOptions.DoNotCheckCertificate = MI_TRUE;
            }
            else if (strcasecmp(value, "false") == 0)
            {
                g_sslOptions.DoNotCheckCertificate = MI_FALSE;
            }
            else
            {
                Conf_Close(conf);
                return GetCimMIError2Params(MI_RESULT_INVALID_PARAMETER, extendedError, ID_PULL_DSCCONF_INVALIDVALUE, key, value);
            }
        }
        else if (strcasecmp(key, "NoSSLv3") == 0)
        {
            if (strcasecmp(value, "true") == 0)
            {
                g_sslOptions.NoSSLv3 = MI_TRUE;
            }
            else if (strcasecmp(value, "false") == 0)
            {
                g_sslOptions.NoSSLv3 = MI_FALSE;
            }
            else
            {
                Conf_Close(conf);
                return GetCimMIError2Params(MI_RESULT_INVALID_PARAMETER, extendedError, ID_PULL_DSCCONF_INVALIDVALUE, key, value);
            }
        }
        else if (strcasecmp(key, "sslciphersuite") == 0)
        {
            size_t valueLength = strlen(value);
            if (valueLength > MAX_SSLOPTION_STRING_LENGTH)
            {
                Conf_Close(conf);
                return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, extendedError, ID_PULL_SSLCIPHERLISTTOOLONG);
            }
            memcpy(g_sslOptions.cipherList, value, valueLength);
            g_sslOptions.cipherList[valueLength] = '\0';
        }
        else if (strcasecmp(key, "CURL_CA_BUNDLE") == 0)
        {
            size_t valueLength = strlen(value);
            if (valueLength > MAX_SSLOPTION_STRING_LENGTH)
            {
                Conf_Close(conf);
                return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, extendedError, ID_PULL_CABUNDLETOOLONG);
            }
            memcpy(g_sslOptions.CABundle, value, valueLength);
            g_sslOptions.CABundle[valueLength] = '\0';
        }
#if !defined(BUILD_OMS)
        else if (strcasecmp(key, "PROXY") == 0)
        {
            size_t valueLength = strlen(value);
            if (valueLength > MAX_SSLOPTION_STRING_LENGTH)
            {
                Conf_Close(conf);
                return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, extendedError, ID_PULL_PROXYTOOLONG);
            }
            memcpy(g_sslOptions.Proxy, value, valueLength);
            g_sslOptions.Proxy[valueLength] = '\0';
        }
#endif
        else
        {
            continue;
        }
    }

    Conf_Close(conf);

#if defined(BUILD_OMS)
    // TODO: read from OMS's config file to read in the Proxy info
    size_t valueLength;
    // If the user has setup proxy, a conf file will be in one of these two locations
    // If the user has not setup proxy, no conf file will exist; this is valid
    const char* legacyOMSProxyFileLocation = "/etc/opt/microsoft/omsagent/conf/proxy.conf";
    const char* omsProxyFileLocation = "/etc/opt/microsoft/omsagent/proxy.conf";

    char* proxyFileLocationToUse = NULL;

    if (File_ExistT(omsProxyFileLocation) != -1)
    {
        proxyFileLocationToUse = omsProxyFileLocation;
    }
    else if (File_ExistT(legacyOMSProxyFileLocation) != -1)
    {
        proxyFileLocationToUse = legacyOMSProxyFileLocation;
    }

    if (proxyFileLocationToUse != NULL)
    {
	text = InhaleTextFile(proxyFileLocationToUse);
    if(text == NULL) {
        return MI_RESULT_FAILED;
    }
	valueLength = strlen(text);
	if (valueLength > MAX_SSLOPTION_STRING_LENGTH)
	{
	    DSC_free(text);
	    return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, extendedError, ID_PULL_PROXYTOOLONG);
	}
	memcpy(g_sslOptions.Proxy, text, valueLength);
	g_sslOptions.Proxy[valueLength] = '\0';
	DSC_free(text);
    }
#endif

    return MI_RESULT_OK;

}

static size_t HeaderCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
  size_t realsize = size * nmemb;
  struct HeaderChunk *chunk = (struct HeaderChunk *)userp;
  char* colonPointer = NULL;
  long key_length = 0;
  long value_length = 0;
  char *charContents = (char*)calloc(realsize, 1);
  if(charContents == NULL) {
    return 0;
  }

  // We have to do a silly memcpy here because there's no safe version of "strstr", used below.
  memcpy(charContents, contents, realsize);

  // Parse out key+value
  colonPointer = strstr((char*)charContents, ":");
  if (colonPointer == NULL)
  {
      // Not a valid key/value header, ignore and return.
      free(charContents);
      return realsize;
  }

  key_length = (long)(colonPointer - charContents);
  value_length = realsize - key_length - 4;

  if (value_length < 0)
  {
      // Value doesn't exist or has an odd length. Ignore this header line.
      free(charContents);
      return realsize;
  }

  char* tempHeaderKeys = realloc(chunk->headerKeys, (chunk->size + 1) * sizeof(char*) );
  char* tempHeaderValues = realloc(chunk->headerValues, (chunk->size + 1) * sizeof(char*) );
  if(tempHeaderKeys == NULL) {
    free(chunk->headerKeys);
    return 0;
  }
  if(tempHeaderValues == NULL) {
    free(chunk->headerValues);
    return 0;
  }
  chunk->headerKeys = tempHeaderKeys;
  chunk->headerValues = tempHeaderValues;

  chunk->headerKeys[chunk->size] = malloc( key_length + 1 );
  chunk->headerValues[chunk->size] = malloc( value_length + 1);

  memcpy(chunk->headerKeys[chunk->size], charContents, key_length);
  chunk->headerKeys[chunk->size][key_length] = '\0';
  memcpy(chunk->headerValues[chunk->size], colonPointer + 2, value_length);
  chunk->headerValues[chunk->size][value_length] = '\0';

  chunk->size += 1;
  free(charContents);
  return realsize;
}

static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
  size_t realsize = size * nmemb;
  //Handle size overflow due to multiplication
  if( nmemb != 0 && (realsize / nmemb != size || (realsize % nmemb) != 0)) {
    return 0;
  }
  size_t reallocationSize = realsize;
  //Handle size overflow due to addition
  if(reallocationSize + 1 < reallocationSize) {
    return 0;
  }
  reallocationSize = reallocationSize + 1;
  struct Chunk *mem = (struct Chunk *)userp;
  //Handle size overflow due to addition
  if(reallocationSize + mem->size < reallocationSize || reallocationSize + mem->size < mem->size) {
    return 0;
  }
  reallocationSize = reallocationSize + mem->size;

  char* tempMemData = (char *)realloc(mem->data, reallocationSize);
  if(tempMemData == NULL) {
    free(mem->data);
    return 0;
  }
  mem->data = tempMemData;
  memcpy(&(mem->data[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->data[mem->size] = 0;

  return realsize;
}


MI_Char *GetSystemUuid();
MI_Boolean EscapeValue (_In_reads_z_(size) char *tokenValue, int startValue, int size,
                    _Inout_ int *currentTokenStartValue,
                    _Inout_ int *currentTokenEndValue,
                    _Inout_ int *nextTokenValue);

void CleanupModuleVersionClassTuple(ModuleVersionClassTuple* moduleVersionClassTuple)
{
    ModuleClassList* current;
    ModuleClassList* previous;

    if (moduleVersionClassTuple == NULL)
    {
        return;
    }

    current = moduleVersionClassTuple->first;

    DSC_free(moduleVersionClassTuple->moduleVersion);

    while (current != NULL)
    {
        previous = current;
        current = current->next;
        DSC_free(previous->moduleClass);
        DSC_free(previous);
    }
    DSC_free(moduleVersionClassTuple);
}

void CleanupModuleTableEntry(ModuleTableEntry* moduleTableEntry)
{
    if (moduleTableEntry == NULL)
    {
        return;
    }

    CleanupModuleVersionClassTuple(moduleTableEntry->moduleVersionClassTuple);
    DSC_free(moduleTableEntry->moduleName);
    DSC_free(moduleTableEntry);
}

void CleanupModuleTable(ModuleTable moduleTable)
{
    ModuleTableEntry* current = moduleTable.first;
    ModuleTableEntry* previous;
    while (current != NULL)
    {
        previous = current;
        current = current->next;
        CleanupModuleTableEntry(previous);
    }
}

static int IsModuleVersionValidFormat(MI_Char* string)
{
    MI_Char c;

    if (string == NULL)
    {
        return 0;
    }

    // Make sure we don't have some garbage version number here.  It should be just numbers and decimals.
    while ( (c = *string) != '\0')
    {
        if (c == '0' ||
            c == '1' ||
            c == '2' ||
            c == '3' ||
            c == '4' ||
            c == '5' ||
            c == '6' ||
            c == '7' ||
            c == '8' ||
            c == '9' ||
            c == '.')
        {
            ++string;
        }
        else
        {
            return 0;
        }
    }
    return 1;
}

static ModuleTableEntry** ModuleTableContainsKey(ModuleTable* table, MI_Char* moduleNameToFind)
{
    ModuleTableEntry** current;

    if (table == NULL)
    {
        return NULL;
    }

    current = &table->first;
    while (*current != NULL)
    {
        if ( Tcscasecmp((*current)->moduleName, moduleNameToFind) == 0 )
        {
            return current;
        }
        else
        {
            current = &(*current)->next;
        }
    }

    return current;
}

static ModuleClassList** ClassListContainsClass(ModuleClassList** firstClassListElement, MI_Char* classNameToFind)
{
    ModuleClassList** current;

    current = firstClassListElement;
    while (*current != NULL)
    {
        if ( Tcscasecmp((*current)->moduleClass, classNameToFind) == 0 )
        {
            return current;
        }
        else
        {
            current = &(*current)->next;
        }
    }

    return current;
}

MI_Boolean ValidateChecksum(_In_z_ MI_Char *checksum, _In_z_ const MI_Char* path)
{
    MI_Uint8 buffer[BUFFER_SIZE_1KB]; // 1 KB at a time
    MI_Char *computedHash = NULL;
    size_t bytesRead;
    SHA256_CTX Ctx;
    char const alphabet[] = "0123456789ABCDEF";
    unsigned char hashedValue[SHA256TRANSFORM_DIGEST_LEN];
    int iCount = 0;
    FILE * fp = NULL;
    if( checksum == NULL)
        return MI_FALSE;

    fp = File_OpenT(path, MI_T("r"));
    if( fp == NULL )
        return MI_FALSE;

    SHA256_Init(&Ctx);

    do
    {
        bytesRead = fread(buffer , 1, BUFFER_SIZE_1KB, fp);
        if( bytesRead > 0 )
        {
            SHA256_Update
                (
                &Ctx,
                (unsigned char *)buffer,
                (MI_Uint32) bytesRead
                );
        }
    }
    while( bytesRead >= BUFFER_SIZE_1KB );

    SHA256_Final(hashedValue, &Ctx);

    File_Close(fp);
    // validate the checksum.
    // Use the buffer for memory
    computedHash = (MI_Char*) buffer;
    computedHash[SHA256TRANSFORM_DIGEST_LEN*2] = '\0';
    for(iCount=0; iCount < SHA256TRANSFORM_DIGEST_LEN; iCount++)
    {
        computedHash[2*iCount] = alphabet[ hashedValue[iCount]/16];
        computedHash[2*iCount+1] = alphabet[ hashedValue[iCount]%16];
    }

    if( Tcscasecmp(checksum, (const MI_Char*)computedHash) != 0 )
        return MI_FALSE;

    return MI_TRUE;

}

MI_INLINE MI_Boolean IsValidUuid(_In_z_ MI_Char* systemUuid)
{
    MI_Uint32 len = 0;
    MI_Uint32 xCount = 0;
    if( systemUuid == NULL)
    {
        return MI_FALSE;
    }
    len = (MI_Uint32)Tcslen(systemUuid);
    if( len != 36 )
        return MI_FALSE;
    //pattern 8-13-19-24 and all characters are hexadecimal.
    for(xCount = 0; xCount <8; xCount++)
    {
        if(!isxdigit((unsigned char)systemUuid[xCount]))
            return MI_FALSE;
    }
    if(systemUuid[xCount] != MI_T('-'))
        return MI_FALSE;
    for(xCount = 9; xCount <13; xCount++)
    {
        if(!isxdigit((unsigned char)systemUuid[xCount]))
            return MI_FALSE;
    }
    if(systemUuid[xCount] != MI_T('-'))
        return MI_FALSE;
    for(xCount = 14; xCount <18; xCount++)
    {
        if(!isxdigit((unsigned char)systemUuid[xCount]))
            return MI_FALSE;
    }
    if(systemUuid[xCount] != MI_T('-'))
        return MI_FALSE;
    for(xCount = 19; xCount <23; xCount++)
    {
        if(!isxdigit((unsigned char)systemUuid[xCount]))
            return MI_FALSE;
    }
    if(systemUuid[xCount] != MI_T('-'))
        return MI_FALSE;
    for(xCount = 24; xCount <len; xCount++)
    {
        if(!isxdigit((unsigned char)systemUuid[xCount]))
            return MI_FALSE;
    }
    return MI_TRUE;
}

MI_Boolean ConnectionAllowed(_In_ MI_InstanceA *customParam, _In_z_ const MI_Char *serverName)
{
    MI_Result r = MI_RESULT_OK;
    MI_Value value;
    MI_Uint32 flags;
    MI_Uint32 xCount;
    for( xCount = 0; xCount < customParam->size ; xCount++)
    {
        r = MI_Instance_GetElement(customParam->data[xCount], MSFT_KEYVALUEPAIR_CLASS_KEY, &value, NULL, &flags, NULL);
        if( r == MI_RESULT_OK && !(flags & MI_FLAG_NULL) && Tcscasecmp(value.string, AllowUnsecureConnectionParamName) == 0 )
        {
            r = MI_Instance_GetElement(customParam->data[xCount], MSFT_KEYVALUEPAIR_CLASS_VALUE, &value, NULL, &flags, NULL);
            if( r == MI_RESULT_OK && !(flags & MI_FLAG_NULL) && Tcscasecmp(value.string, MI_T("TRUE")) == 0 )
            {
                return MI_TRUE;
            }
        }
    }
    //only https is allowed.
    if( Tcsncasecmp(serverName, MI_T("https"), 5) == 0 )
        return MI_TRUE;

    return MI_FALSE;
}
MI_Result GetMetaConfigParameters(_In_ MI_Instance *metaConfig,
                                  _In_ MI_Char* partialConfigName,
                                  _Outptr_opt_result_maybenull_    MI_Instance **credential,
                                  _Inout_    MI_InstanceA *customParam,
                                  _Outptr_result_maybenull_z_ MI_Char **configurationID,
                                  _Outptr_result_maybenull_z_ MI_Char **certificateID,
                                  _Inout_updates_(URL_SIZE) MI_Char *url,
                                  _Inout_updates_(SUBURL_SIZE) MI_Char *subUrl,
                                  _Out_ MI_Uint32 *port,
                                  _Out_ MI_Boolean *bIsHttps,
                                  _Out_ MI_Uint32* getActionStatusCode,
                                  _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Result r = MI_RESULT_OK;
    MI_Value value;
    MI_Value value2;
    MI_Value value3;
    MI_Char serverURL[MAX_URL_LENGTH];
    MI_Uint32 flags;
    MI_Uint32 xCount;
    MI_Char* configurationSource = NULL;
    MI_Boolean allowUnsecureConnection = MI_FALSE;
    MI_InstanceA customData;
    *credential = NULL;
    *extendedError = NULL;
    *getActionStatusCode = Success;
    *configurationID = NULL;
    *certificateID = NULL;
    *bIsHttps = MI_FALSE;
    *port = DEFAULT_SERVERPORT;
    *getActionStatusCode = Success;
    *extendedError = NULL;
    *serverURL = '\0';

    // 1. Get AgentID
    r = MI_Instance_GetElement(metaConfig, MSFT_DSCMetaConfiguration_AgentId, &value, NULL, &flags, NULL);
    if( r != MI_RESULT_OK || (flags & MI_FLAG_NULL))
    {
        *getActionStatusCode = InvalidConfigurationIdInMetaConfig;
        r = GetCimMIError(r, extendedError, ID_PULL_CONFIGURATIONIDNOTSPECIFIED);
        return r;
    }

    *configurationID = (MI_Char*)DSC_malloc((Tcslen(value.string)+1) * sizeof(MI_Char), NitsHere());
    if( *configurationID == NULL)
    {
        *getActionStatusCode = InvalidConfigurationIdInMetaConfig;
        r = GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, extendedError, ID_ENGINEHELPER_MEMORY_ERROR);
        return r;
    }
    memcpy(*configurationID, value.string, Tcslen(value.string)* sizeof(MI_Char));
    /* Validate the Uuid.*/
/*
    if( !IsValidUuid(*configurationID))
    {
        *getActionStatusCode = InvalidConfigurationIdFormat;
        r = GetCimMIError1Param(MI_RESULT_FAILED, extendedError, ID_PULL_INVALIDCONFIGURATIONIDFORMAT, *configurationID);
        DSC_free(*configurationID);
        return r;
    }
*/

    // 2. Get Credential.
    r = MI_Instance_GetElement(metaConfig, MetaConfig_Credential, &value, NULL, &flags, NULL);
    if( r == MI_RESULT_OK && !(flags & MI_FLAG_NULL))
    {
        *credential = value.instance;
    }



    // If partialConfigName is defined, get the ConfigurationDownloadManagers, else get the DownloadManagerCustomData
    if ( partialConfigName != NULL )
    {
        // NOTE: Request miType for GetElement calls to aid in debugging schema changes.
        MI_Type miType;
        r = MI_Instance_GetElement(metaConfig, MSFT_DSCMetaConfiguration_PartialConfigurations, &value, &miType, &flags, NULL);
        if (r != MI_RESULT_OK || (flags & MI_FLAG_NULL))
        {
            return GetCimMIError(MI_RESULT_NO_SUCH_PROPERTY, extendedError, ID_PULL_NOPARTIALCONFIGS);
        }

        for (xCount = 0; xCount < value.instancea.size; ++xCount)
        {
            MI_Instance_GetElement(value.instancea.data[xCount], OMI_MetaConfigurationResource_ResourceId, &value2, &miType, &flags, NULL);
            if ( Tcscasecmp(value2.string, partialConfigName) == 0 )
            {
                // Found our partial config! Now let's get the configuration source
                MI_Instance_GetElement(value.instancea.data[xCount], MSFT_PartialConfiguration_ConfigurationSource, &value2, &miType, &flags, NULL);
                if (r != MI_RESULT_OK || (flags & MI_FLAG_NULL))
                {
                    return GetCimMIError1Param(MI_RESULT_NO_SUCH_PROPERTY, extendedError, ID_PULL_NOCONFIGURATIONSOURCE, partialConfigName);
                }

                configurationSource = value2.stringa.data[0];
                break;
            }
        }
        if (configurationSource == NULL)
        {
            // Unable to find partial configuration with this name.
            return GetCimMIError1Param(MI_RESULT_NO_SUCH_PROPERTY, extendedError, ID_PULL_NOCONFIGURATIONSOURCE, partialConfigName);
        }

        r = MI_Instance_GetElement(metaConfig, MSFT_DSCMetaConfiguration_ConfigurationDownloadManagers, &value, &miType, &flags, NULL);
        if (r != MI_RESULT_OK || (flags & MI_FLAG_NULL))
        {
            // Unable to get the config download managers
            return GetCimMIError(MI_RESULT_NO_SUCH_PROPERTY, extendedError, ID_PULL_NOCONFIGDOWNLOADMANAGERS);
        }

        for (xCount = 0; xCount < value.instancea.size; ++xCount)
        {
            MI_Instance_GetElement(value.instancea.data[xCount], OMI_MetaConfigurationResource_ResourceId, &value2, &miType, &flags, NULL);
            if ( Tcscasecmp(value2.string, configurationSource) == 0 )
            {
                // Found the WebDownloadManager with the proper resource ID.
                MI_Instance_GetElement(value.instancea.data[xCount], MI_T("ServerURL"), &value2, &miType, &flags, NULL);
                if (r != MI_RESULT_OK || (flags & MI_FLAG_NULL))
                {
                    return GetCimMIError1Param(MI_RESULT_NO_SUCH_PROPERTY, extendedError, ID_PULL_NOSERVERURL, value3.string);
                }

                memcpy(serverURL, value2.string, (Tcslen(value2.string) + 1) * sizeof(MI_Char));

                MI_Instance_GetElement(value.instancea.data[xCount], MI_T("CertificateID"), &value2, &miType, &flags, NULL);
                if (r == MI_RESULT_OK && !(flags & MI_FLAG_NULL))
                {
                    memcpy(*certificateID, value2.string, Tcslen(value2.string) * sizeof(MI_Char));
                }

                MI_Instance_GetElement(value.instancea.data[xCount], MI_T("AllowUnsecureConnection"), &value2, &miType, &flags, NULL);
                if (r == MI_RESULT_OK && !(flags & MI_FLAG_NULL))
                {
                    allowUnsecureConnection = value2.boolean;
                }

                //Check if connection is secure and if unsecure user explicitly asks for it.
                if( Tcsncasecmp(serverURL, MI_T("https"), 5) != 0 && allowUnsecureConnection == MI_FALSE )
                {
                    *getActionStatusCode = InvalidDownloadManagerCustomDataInMetaConfig;
                    r = GetCimMIError1Param( MI_RESULT_INVALID_PARAMETER, extendedError, ID_PULL_UNSECURECONNECTIONNOTALLOWED, serverURL);
                    return r;
                }
                // Found the WebDownloadManager, stop searching.
                break;
            }
        }
    }
    else
    {
        // 3. Get custom arguments.
        r = MI_Instance_GetElement(metaConfig, MetaConfigDownloadManagerCustomData, &value, NULL, &flags, NULL);
        if( r == MI_RESULT_OK && !(flags & MI_FLAG_NULL))
        {
            customParam->size = value.instancea.size;
            customParam->data = value.instancea.data;
        }


        // 4. Get certificate ID.
        for( xCount =0; xCount < customParam->size; xCount++)
        {
            MI_Value value;
            MI_Uint32 flags;
            r = MI_Instance_GetElement(customParam->data[xCount], MSFT_KEYVALUEPAIR_CLASS_KEY, &value, NULL, &flags, NULL);
            if( r == MI_RESULT_OK && !(flags & MI_FLAG_NULL) && Tcscasecmp(CertificateIdParamName, value.string) == 0 )
            {
                r = MI_Instance_GetElement(customParam->data[xCount], MSFT_KEYVALUEPAIR_CLASS_VALUE, &value, NULL, &flags, NULL);
                if( r == MI_RESULT_OK && !(flags & MI_FLAG_NULL) )
                {
                    *certificateID = (MI_Char*)DSC_malloc((Tcslen(value.string)+1) * sizeof(MI_Char), NitsHere());
                    if( *certificateID == NULL)
                    {
                        *getActionStatusCode = InvalidConfigurationIdInMetaConfig;
                        r = GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, extendedError, ID_ENGINEHELPER_MEMORY_ERROR);
                        if( *configurationID )
                            DSC_free(*configurationID);
                        *configurationID = NULL;
                        return r;
                    }
                    memcpy(*certificateID, value.string, Tcslen(value.string)* sizeof(MI_Char));
                    break;
                }
            }
        }
    }

    if (*serverURL == '\0')
    {
        r = MI_Instance_GetElement(metaConfig, MetaConfigDownloadManagerCustomData, &value, NULL, &flags, NULL);
        if( r == MI_RESULT_OK && !(flags & MI_FLAG_NULL))
        {
            customData.size = value.instancea.size;
            customData.data = value.instancea.data;

            // Get Server Url.
            for( xCount =0; xCount < customData.size; xCount++)
            {
                MI_Value value;
                MI_Uint32 flags;
                r = MI_Instance_GetElement(customData.data[xCount], MSFT_KEYVALUEPAIR_CLASS_KEY, &value, NULL, &flags, NULL);
                if( r == MI_RESULT_OK && !(flags & MI_FLAG_NULL) && Tcscasecmp(ServerUrlParamName, value.string) == 0 )
                {
                    r = MI_Instance_GetElement(customData.data[xCount], MSFT_KEYVALUEPAIR_CLASS_VALUE, &value, NULL, &flags, NULL);
                    if( r == MI_RESULT_OK && !(flags & MI_FLAG_NULL) )
                    {
                        // Found ServerUrl in value.string
                        memcpy(serverURL, value.string, (Tcslen(value.string) + 1) * sizeof(MI_Char));

                        //Check if connection is secure and if unsecure user explicitly asks for it.
                        if( !ConnectionAllowed(&customData, serverURL) )
                        {
                            *getActionStatusCode = InvalidDownloadManagerCustomDataInMetaConfig;
                            r = GetCimMIError1Param( MI_RESULT_INVALID_PARAMETER, extendedError, ID_PULL_UNSECURECONNECTIONNOTALLOWED, serverURL);
                            return r;
                        }

                        break;
                    }
                }
            }
        }
        else
        {
            // There's no DownloadManagerCustomData, AND there's no partial configuration.  We should be able to find WebDownloadManager, and only one!
            r = MI_Instance_GetElement(metaConfig, MSFT_DSCMetaConfiguration_ConfigurationDownloadManagers, &value, NULL, &flags, NULL);
            if (r != MI_RESULT_OK || (flags & MI_FLAG_NULL) || value.instancea.size == 0)
            {
                // Unable to get the config download managers
                return GetCimMIError(MI_RESULT_NO_SUCH_PROPERTY, extendedError, ID_PULL_NOCONFIGDOWNLOADMANAGERS);
            }

            if (value.instancea.size > 1)
            {
                // Too many download managers defined, please set up partial configuration for this.
                return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_PULL_TOOMANYCONFIGDOWNLOADMANAGERS);
            }

            MI_Instance_GetElement(value.instancea.data[0], OMI_MetaConfigurationResource_ResourceId, &value3, NULL, &flags, NULL);

            // Found the WebDownloadManager with the proper resource ID.
            MI_Instance_GetElement(value.instancea.data[0], MI_T("ServerURL"), &value2, NULL, &flags, NULL);
            if (r != MI_RESULT_OK || (flags & MI_FLAG_NULL))
            {
                return GetCimMIError1Param(MI_RESULT_NO_SUCH_PROPERTY, extendedError, ID_PULL_NOSERVERURL, value3.string);
            }

            memcpy(serverURL, value2.string, (Tcslen(value2.string) + 1) * sizeof(MI_Char));

            MI_Instance_GetElement(value.instancea.data[0], MI_T("CertificateID"), &value2, NULL, &flags, NULL);
            if (r == MI_RESULT_OK && !(flags & MI_FLAG_NULL))
            {
                memcpy(*certificateID, value2.string, Tcslen(value2.string) * sizeof(MI_Char));
            }

            MI_Instance_GetElement(value.instancea.data[0], MI_T("AllowUnsecureConnection"), &value2, NULL, &flags, NULL);
            if (r == MI_RESULT_OK && !(flags & MI_FLAG_NULL))
            {
                allowUnsecureConnection = value2.boolean;
            }
        }
    }

    if (*serverURL != '\0')
    {
        if( sscanf(serverURL, MI_T("http://%99[^:]:%d/%199[^\n]"), url, port, subUrl) == 3 ||
            sscanf(serverURL, MI_T("http://%99[^/]/%199[^\n]"), url, subUrl) == 2 )
        {
            //success
        }
        //format is: http://server:port/suburl or http://server/suburl
        else if( sscanf(serverURL, MI_T("https://%99[^:]:%d/%199[^\n]"), url, port, subUrl) == 3 ||
                 sscanf(serverURL, MI_T("https://%99[^/]/%199[^\n]"), url, subUrl) == 2 )
        {
            //success
            *bIsHttps = MI_TRUE;
            if (*port == 80)
            {
                *port = 443;
            }
        }
    }
    else
    {
        // Not found
        *getActionStatusCode = InvalidDownloadManagerCustomDataInMetaConfig;
        return GetCimMIError(r, extendedError, ID_ENGINEHELPER_MEMORY_ERROR);
    }

    return MI_RESULT_OK;

}

MI_Char * GetGetActionBodyContent(_In_z_ const MI_Char *checksum, MI_Boolean nodeCompliant,
                                _In_z_ const MI_Char *checksumAlgorithm, MI_Uint32 statusCode)
{
    const MI_Char *checksumName = MI_T("Checksum");
    const MI_Char *complianceName = MI_T("NodeCompliant");
    const MI_Char *algorithmName = MI_T("ChecksumAlgorithm");
    const MI_Char *statusCodeName = MI_T("StatusCode");
    const MI_Char *clientStatusName = MI_T("ClientStatus");

    const MI_Char *complianceStatus;
    MI_Char statusCodeValue[MAX_STATUSCODE_SIZE];
    size_t length;
    MI_Char *content = NULL;

    if( nodeCompliant == MI_TRUE)
    {
        complianceStatus = MI_T("TRUE");
    }
    else
    {
        complianceStatus = MI_T("FALSE");
    }
    Stprintf(statusCodeValue, MAX_STATUSCODE_SIZE, MI_T("%d"), statusCode);

    length = 50 + Tcslen(clientStatusName) + Tcslen(checksumName) + Tcslen(algorithmName) + Tcslen(checksum) + Tcslen(checksumAlgorithm);

    content = (MI_Char *)DSC_malloc(length * sizeof(MI_Char), NitsHere());
    if( content == NULL )
    {
        return NULL;
    }

    // Format should be: {"ClientStatus":[{"Checksum":"%s","ChecksumAlgorithm":"%s"}]}

    Stprintf(content, length, MI_T("{\"%s\":[{\"%s\":\"%s\",\"%s\":\"%s\"}]}"), clientStatusName, checksumName, checksum, algorithmName, checksumAlgorithm);

    return content;
}

int EscapeString(_In_reads_z_(size) char *tokenValue, int startValue, int size)
{
    int xCount = startValue;
    char *start = tokenValue;
    while( xCount < size)
    {
        if( *(start+xCount) == '"')
        {
          return xCount-1;
        }
        if( (*(start+xCount) == '\\') && (xCount+1 < size) && (*(start+xCount+1) == '"'))
        xCount++;

        xCount++;
    }
    return -1;
}

int EscapeArray (_In_reads_z_(size) char *tokenValue, int startValue, int size)
{
    int xCount = startValue;
    char *start = tokenValue;
    int currentTokenStartValue = 0;
    int currentTokenEndValue = 0;
    int nextTokenValue = 0;
    // escape until we get ']'. It will be in format value[,]value*
    while( xCount < size)
    {
        if( *(start+xCount) == ']')
        {
          return xCount-1;
        }
        else if ( *(start+xCount) == ',')
            xCount++;
        else
        {
            if(!EscapeValue(tokenValue, xCount, size, &currentTokenStartValue, &currentTokenEndValue, &nextTokenValue))
            {
                return -1;
            }
            xCount = nextTokenValue;
        }
    }
    return -1;
}

int EscapeObject (_In_reads_z_(size) char *tokenValue, int startValue, int size)
{
    int xCount = startValue;
    char *start = tokenValue;
    int currentTokenStartValue = 0;
    int currentTokenEndValue = 0;
    int nextTokenValue = 0;
    // escape until we get '}'. it will be in format {string:value};
    while( xCount < size)
    {
        if( *(start+xCount) == '}')
        {
          return xCount-1;
        }
        else if ( *(start+xCount) == ',')
            xCount++;
        else
        {
            if(!EscapeValue(tokenValue, xCount, size, &currentTokenStartValue, &currentTokenEndValue, &nextTokenValue))
            {
                return -1;
            }
            xCount = nextTokenValue;
        }
        xCount++;
    }
    return -1;
}

int EscapeNumber (_In_reads_z_(size) char *tokenValue, int startValue, int size)
{
    int xCount = startValue;
    char *start = tokenValue;
    // escape until we get ":",",","}",}]".
    while( xCount < size)
    {
        if( *(start+xCount) == ',' ||
            *(start+xCount) == '}'||
            *(start+xCount) == ']' )
        {
          return xCount-1;
        }
        xCount++;
    }
    return -1;
}

MI_Boolean EscapeValue (_In_reads_z_(size) char *tokenValue, int startValue, int size,
                    _Inout_ int *currentTokenStartValue,
                    _Inout_ int *currentTokenEndValue,
                    _Inout_ int *nextTokenValue)
{
    int xCount = startValue;
    char *start = tokenValue;
    if( xCount < size)
        {
        if( *(start+xCount) == '"' ) // it is a string
        {
            *currentTokenStartValue = xCount +1;
            //TokenEnd
            *currentTokenEndValue = EscapeString(tokenValue, *currentTokenStartValue, size);
            *nextTokenValue = *currentTokenEndValue + 2;
        }
        else if( *(start+xCount) == '[' ) // it is an array
        {
            *currentTokenStartValue = xCount +1;
            *currentTokenEndValue = EscapeArray(tokenValue, *currentTokenStartValue, size);
            *nextTokenValue = *currentTokenEndValue + 2;
        }
        else if( *(start+xCount) == '{' ) // it is an object
        {
            *currentTokenStartValue = xCount +1;
            *currentTokenEndValue = EscapeObject(tokenValue, *currentTokenStartValue, size);
            *nextTokenValue = *currentTokenEndValue + 2;
        }
        else if( Strcasecmp(start+xCount, "true")) // it is value true
        {
            *currentTokenStartValue = xCount;
            *currentTokenEndValue = xCount + 3;
            *nextTokenValue = *currentTokenEndValue + 1;
        }
        else if( Strcasecmp(start+xCount, "false")) // it is value false
        {
            *currentTokenStartValue = xCount;
            *currentTokenEndValue = xCount + 4;
            *nextTokenValue = *currentTokenEndValue + 1;
        }
        else if( Strcasecmp(start+xCount, "null")) // it is value true
        {
            *currentTokenStartValue = xCount;
            *currentTokenEndValue = xCount + 3;
            *nextTokenValue = *currentTokenEndValue + 1;
        }
        else // it is a number
        {
            *currentTokenStartValue = xCount;
            *currentTokenEndValue = EscapeNumber(tokenValue, *currentTokenStartValue, size);
            *nextTokenValue = *currentTokenEndValue + 2;
        }
        if (*currentTokenEndValue == -1 || *currentTokenStartValue == -1)
            return MI_FALSE;
        return MI_TRUE;
    }
    return MI_FALSE;
}



MI_Boolean GetNextToken(_In_reads_z_(size) char *tokenValue, int startValue, int size,
                    _Inout_ int *currentTokenStart,
                    _Inout_ int *currentTokenEnd,
                    _Inout_ int *currentTokenStartValue,
                    _Inout_ int *currentTokenEndValue,
                    _Inout_ int *nextTokenValue)
{
    int xCount = startValue;
    char *start = tokenValue;
    // TokenStart
    while( xCount < size)
    {
        if( *(start+xCount) == '"')
        {
          *currentTokenStart =  xCount+1;
        //TokenEnd
        *currentTokenEnd = EscapeString(tokenValue, *currentTokenStart, size);
        if (*currentTokenEnd == -1)
            return MI_FALSE;

          break;
        }
        if( (*(start+xCount) == '\\') && (xCount+1 < size) && (*(start+xCount+1) == '"'))
        xCount++;

        xCount++;
    }
    if( xCount >= size )
        return MI_FALSE;

    xCount = *currentTokenEnd + 3;
    //TokenStartValue
    if( xCount >= size)
      return MI_FALSE;

    if(!EscapeValue(tokenValue, xCount, size, currentTokenStartValue , currentTokenEndValue, nextTokenValue))
        return MI_FALSE;

    return MI_TRUE;

}


MI_Boolean GetGetActionData( _In_reads_z_(size) char *start,
                             int size,
                             _Out_ char** serverResponse,
                             _Outptr_result_maybenull_ OverAllGetActionResponse** serverAssignedConfigurations)
{
    //Search for pattern "value":"<dommmm>", We don't support anything other than simple strings
    int xCount = 1; // escape first character
    int currentTokenStart = 0, currentTokenStartValue = 0;
    int currentTokenEnd = 0, currentTokenEndValue = 0;
    int nextTokenValue = -1;
    char* configName = NULL;
    char* ptr1 = NULL;
    char* ptr1_end = NULL;
    char* ptr2 = NULL;
    char* ptr2_end = NULL;
    *serverAssignedConfigurations = (OverAllGetActionResponse *) DSC_malloc(sizeof(OverAllGetActionResponse), NitsHere());
    *serverResponse = NULL;

    ptr1 = strstr(start, "NodeStatus\":");
    if (ptr1 == NULL)
    {
        return MI_FALSE;
    }

    ptr2 = strstr(start, "ConfigurationName\":");

    ptr1 = ptr1 + 13;
    ptr1_end = strchr(ptr1, '"');
    if (ptr1_end == NULL)
    {
        return MI_FALSE;
    }
    *serverResponse = (char*) DSC_malloc(ptr1_end - ptr1 + 1, NitsHere());
    memcpy(*serverResponse, ptr1, ptr1_end - ptr1);
    (*serverResponse)[ptr1_end - ptr1] = '\0';


    if (ptr2 != NULL)
    {
        ptr2 = ptr2 + 20;
        ptr2_end = strchr(ptr2, '"');
        if (ptr1_end == NULL)
        {
            return MI_FALSE;
        }
        configName = (char*) DSC_malloc(ptr2_end - ptr2 + 1, NitsHere());
        memcpy(configName, ptr2, ptr2_end - ptr2);
        configName[ptr2_end - ptr2] = '\0';
    }
    else
    {
        configName = NULL;
    }

    (*serverAssignedConfigurations)->Details = (ConfigurationStatus*) DSC_malloc(sizeof(ConfigurationStatus), NitsHere());
    (*serverAssignedConfigurations)->Details->ConfigurationName = configName;
    (*serverAssignedConfigurations)->Details->Status = serverResponse;
    (*serverAssignedConfigurations)->NumberOfConfiguration = 1;

    return MI_TRUE;
}

MI_Boolean ValidateContentTypeHeader(_Inout_updates_z_(size)  MI_Char *buffer, MI_Uint32 size, _In_z_ const MI_Char *header)
{
    MI_Uint32 startIndex = 0;
    MI_Char *start = buffer;
    MI_Char *firstOccurancePointer = NULL;
    while( start != NULL && *start != MI_T('\0') && startIndex < size)
    {
        firstOccurancePointer = Tcschr(start + startIndex, ';');
        if( firstOccurancePointer == NULL)
        {
            if( Tcscasecmp(start + startIndex, header) == 0 )
            {
                return MI_TRUE;
            }
            return MI_FALSE;
        }
        if( (firstOccurancePointer - start >= size-1) ||  firstOccurancePointer < start)
        {
            return MI_FALSE;
        }
        start[ firstOccurancePointer - start] = MI_T('\0');
        if( Tcscasecmp(start + startIndex , header) == 0 )
        {
            return MI_TRUE;
        }
        startIndex = (MI_Uint32) (firstOccurancePointer - start + 1);
    }
    return MI_FALSE;
}


/* Caller will validate the content.*/
MI_Char *GetSystemUuid()
{
    MI_Char *systemUUid = NULL;
    FILE * fp = File_OpenT(product_uuid_path, MI_T("r"));
    if( fp == NULL )
        return NULL;

    systemUUid = (MI_Char*)DSC_malloc(JOB_UUID_LENGTH* sizeof(MI_Char), NitsHere());
    if( systemUUid == NULL)
    {
        File_Close(fp);
        return NULL;
    }
    // Caller will validate the contents.
    fread(systemUUid , 1, JOB_UUID_LENGTH, fp);
            File_Close(fp);
    return systemUUid;
        }

MI_Result SetGeneralCurlOptions(CURL* curl,
				_Outptr_result_maybenull_ MI_Instance **extendedError)
{
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30);
    /*
        Default curl operation timeout is infinite.
        If curl operation hangs for some reason, it should not block LCM forever.
        Setting operation timeout to 10 minutes.
    */
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 600);

    if (g_sslOptions.DoNotCheckCertificate == MI_TRUE)
    {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    }
    else
    {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    }

    if (g_sslOptions.CABundle[0] != '\0')
    {
        res = curl_easy_setopt(curl, CURLOPT_CAINFO, g_sslOptions.CABundle);
        if (res != CURLE_OK)
        {
            return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_PULL_CABUNDLENOTSUPPORTED);
        }
    }

    if (g_sslOptions.Proxy[0] != '\0')
    {
	res = curl_easy_setopt(curl, CURLOPT_PROXY, g_sslOptions.Proxy);
        if (res != CURLE_OK)
        {
            return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_PULL_PROXYNOTSUPPORTED);
        }
    }

    return MI_RESULT_OK;
}

MI_Result  IssueGetActionRequest( _In_z_ const MI_Char *configurationID,
                                  _In_z_ const MI_Char *certificateID,
                                  _In_z_ const MI_Char *checkSum,
                                  _In_ MI_Boolean complianceStatus,
                                  _In_ MI_Uint32 lastGetActionStatusCode,
                                  _Outptr_result_maybenull_z_  MI_Char** result,
                                  _Out_ MI_Uint32* getActionStatusCode,
                                  _In_reads_z_(URL_SIZE) const MI_Char *url,
                                  _In_ MI_Uint32 port,
                                  _In_reads_z_(SUBURL_SIZE) const MI_Char *subUrl,
                                  MI_Boolean bIsHttps,
                                  _Outptr_result_maybenull_ OverAllGetActionResponse** serverAssignedConfigurations,
                                  _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Result r = MI_RESULT_OK;
    const char *emptyString = "";
    char *bodyContent = NULL;
    const char *checkSumFinalValue = emptyString;
    RequestContainer requestParam = {0};
    MI_Char actionUrl[MAX_URL_LENGTH];
    char * getActionStatus = NULL;
    long responseCode = 0;

    CURL *curl = NULL;
    CURLcode res = CURLE_OK;
    struct Chunk headerChunk;
    struct Chunk dataChunk;
    struct curl_slist *list = NULL;

    requestParam.serverOperation = OPERATION_GETACTION;
    if( checkSum != NULL)
        checkSumFinalValue = checkSum;

    *result = NULL;

    r = GetSSLOptions(extendedError);
    if( r != MI_RESULT_OK)
    {
        return r;
    }

    DSC_EventWriteWebDownloadManagerDoActionServerUrl(configurationID, url);

    // Get body for GetAction request
    bodyContent = GetGetActionBodyContent(checkSumFinalValue, complianceStatus, AllowedChecksumAlgorithm, lastGetActionStatusCode);

    if( bodyContent == NULL )
    {
        *getActionStatusCode = DownloadManagerInitializationFailure;
        return GetCimMIError(r, extendedError, ID_ENGINEHELPER_MEMORY_ERROR);
    }

    curl = curl_easy_init();
    if (!curl)
    {
        return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_PULL_CURLFAILEDTOINITIALIZE);
    }

    if (bIsHttps)
    {
        Snprintf(actionUrl, MAX_URL_LENGTH, "https://%s:%d/%s/Nodes(AgentId='%s')/GetDscAction", url, port, subUrl, configurationID);

    }
    else
    {
        Snprintf(actionUrl, MAX_URL_LENGTH, "http://%s:%d/%s/Nodes(AgentId='%s')/GetDscAction", url, port, subUrl, configurationID);
    }

    r = SetGeneralCurlOptions(curl, extendedError);
    if (r != MI_RESULT_OK)
    {
	DSC_free(bodyContent);
	curl_easy_cleanup(curl);
	return r;
    }

    curl_easy_setopt(curl, CURLOPT_URL, actionUrl);

    headerChunk.data = (char *)malloc(1);
    headerChunk.size = 0;
    dataChunk.data = (char *)malloc(1);
    dataChunk.size = 0;


    list = curl_slist_append(list, "Accept: application/json");
    list = curl_slist_append(list, "Content-Type: application/json; charset=utf-8");
    list = curl_slist_append(list, "ProtocolVersion: 2.0");

    res = curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    res = curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyContent);
    res = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    res = curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headerChunk);
    res = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &dataChunk);

    res = curl_easy_setopt(curl, CURLOPT_SSLCERT, OAAS_CERTPATH);
    if (res != CURLE_OK)
    {
        curl_slist_free_all(list);
        curl_easy_cleanup(curl);
        return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_PULL_CERTOPTS_NOT_SUPPORTED);
    }
    res = curl_easy_setopt(curl, CURLOPT_SSLKEY, OAAS_KEYPATH);

    if (g_sslOptions.cipherList[0] != '\0')
    {
        res = curl_easy_setopt(curl, CURLOPT_SSL_CIPHER_LIST, g_sslOptions.cipherList);
        if (res != CURLE_OK)
        {
            *getActionStatusCode = GetDscActionCommandFailure;
            DSC_free(bodyContent);
            curl_slist_free_all(list);
            curl_easy_cleanup(curl);
            return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_PULL_CURLFAILEDTOSETCIPHERLIST);
        }
    }

    if (g_sslOptions.NoSSLv3 == MI_TRUE)
    {
        res = curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1);
        if (res != CURLE_OK)
        {
            *getActionStatusCode = GetDscActionCommandFailure;
            DSC_free(bodyContent);
            curl_slist_free_all(list);
            curl_easy_cleanup(curl);
            return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_PULL_CURLFAILEDTOSETNOSSLV3);
        }
    }


    res = curl_easy_perform(curl);
    DSC_free(bodyContent);

    if (res != CURLE_OK)
    {
        *getActionStatusCode = GetDscActionCommandFailure;
        free(headerChunk.data);
        free(dataChunk.data);
        curl_slist_free_all(list);
        curl_easy_cleanup(curl);

        return GetCimMIError2Params(MI_RESULT_FAILED, extendedError, ID_PULL_CURLPERFORMFAILED, url, curl_easy_strerror(res));
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
    curl_slist_free_all(list);
    curl_easy_cleanup(curl);

    if (responseCode != HTTP_SUCCESS_CODE)
    {
        *getActionStatusCode = GetDscActionCommandFailure;
        free(headerChunk.data);
        free(dataChunk.data);
        return GetCimMIError1Param(r, extendedError, ID_PULL_GETACTIONFAILED, webPulginName);
    }

    GetGetActionData(dataChunk.data, dataChunk.size, &getActionStatus, serverAssignedConfigurations);

    free(headerChunk.data);
    free(dataChunk.data);

    if( getActionStatus == NULL)
    {
        *getActionStatusCode = GetDscActionCommandFailure;
        return GetCimMIError1Param(MI_RESULT_FAILED, extendedError, ID_PULL_GETACTIONFAILED, webPulginName);
    }
    if (! (Strcasecmp( getActionStatus, GetActionResultOk) == 0 ||
        Strcasecmp( getActionStatus, GetActionResultGetConfiguration) == 0 ) )
    {
        *getActionStatusCode = GetDscActionCommandFailure;

        r = GetCimMIError2Params(MI_RESULT_INVALID_PARAMETER, extendedError, ID_PULL_GETACTIONUNEXPECTEDRESULT, getActionStatus, url);
        DSC_free(getActionStatus);
        return r;
    }

    *result = getActionStatus;
    *getActionStatusCode = Success;
    DSC_EventWriteWebDownloadManagerDoActionGetCall(configurationID, requestParam.u.action.getActionStatus);

    return MI_RESULT_OK;
}

MI_Result  IssueGetConfigurationRequest( _In_z_ const MI_Char *configurationID,
                                         _In_z_ const MI_Char *certificateID,
                                         _In_z_ const MI_Char *directoryPath,
                                         _Outptr_result_maybenull_z_  MI_Char** result,
                                         _Out_ MI_Uint32* getActionStatusCode,
                                         _In_reads_z_(URL_SIZE) const MI_Char *url,
                                         _In_ MI_Uint32 port,
                                         _In_reads_z_(SUBURL_SIZE) const MI_Char *subUrl,
                                         MI_Boolean bIsHttps,
                                         _In_opt_z_ MI_Char *assignedConfiguration,
                                         _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Result r = MI_RESULT_OK;
    MI_Char *outputResult = (MI_Char*)DSC_malloc((Tcslen(MI_T("OK"))+1) * sizeof(MI_Char), NitsHere());

    CURL *curl = NULL;
    CURLcode res = CURLE_OK;
    struct HeaderChunk headerChunk;
    struct Chunk dataChunk;
    char configurationUrl[MAX_URL_LENGTH];
    long responseCode = 0;
    size_t i;
    char* checksumResponse = NULL;
    char* checksumAlgorithmResponse = NULL;

    struct curl_slist *list = NULL;


    *result = NULL;
    if( outputResult == NULL )
    {
        *getActionStatusCode = GetConfigurationCommandFailure;
        return GetCimMIError(r, extendedError, ID_ENGINEHELPER_MEMORY_ERROR);
    }
    DSC_EventWriteGetDscDocumentWebDownloadManagerServerUrl(configurationID, url);
    Stprintf(outputResult,3, MI_T("OK"));

    curl = curl_easy_init();
    if (!curl)
    {
        return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_PULL_CURLFAILEDTOINITIALIZE);
    }

    if (bIsHttps)
    {
        Snprintf(configurationUrl, MAX_URL_LENGTH, "https://%s:%d/%s/Nodes(AgentId='%s')/Configurations(ConfigurationName='%s')/ConfigurationContent", url, port, subUrl, configurationID, assignedConfiguration);
    }
    else
    {
        Snprintf(configurationUrl, MAX_URL_LENGTH, "http://%s:%d/%s/Nodes(AgentId='%s')/Configurations(ConfigurationName='%s')/ConfigurationContent", url, port, subUrl, configurationID, assignedConfiguration);
    }

    r = SetGeneralCurlOptions(curl, extendedError);
    if (r != MI_RESULT_OK)
    {
        curl_easy_cleanup(curl);
        return r;
    }

    curl_easy_setopt(curl, CURLOPT_URL, configurationUrl);

    InitHeaderChunk(&headerChunk);
    dataChunk.data = (char *)malloc(1);
    dataChunk.size = 0;

    list = curl_slist_append(list, "ProtocolVersion: 2.0");


    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headerChunk);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &dataChunk);

    res = curl_easy_setopt(curl, CURLOPT_SSLCERT, OAAS_CERTPATH);
    if (res != CURLE_OK)
    {
        curl_slist_free_all(list);
        curl_easy_cleanup(curl);
        return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_PULL_CERTOPTS_NOT_SUPPORTED);
    }
    curl_easy_setopt(curl, CURLOPT_SSLKEY, OAAS_KEYPATH);


    if (g_sslOptions.cipherList[0] != '\0')
    {
        res = curl_easy_setopt(curl, CURLOPT_SSL_CIPHER_LIST, g_sslOptions.cipherList);
        if (res != CURLE_OK)
        {
            *getActionStatusCode = GetConfigurationCommandFailure;

            curl_slist_free_all(list);
            curl_easy_cleanup(curl);

            return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_PULL_CURLFAILEDTOSETCIPHERLIST);
        }
    }

    if (g_sslOptions.NoSSLv3 == MI_TRUE)
    {
        res = curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1);
        if (res != CURLE_OK)
        {
            *getActionStatusCode = GetConfigurationCommandFailure;

            curl_slist_free_all(list);
            curl_easy_cleanup(curl);
            return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_PULL_CURLFAILEDTOSETNOSSLV3);
        }
    }

    res = curl_easy_perform(curl);

    curl_slist_free_all(list);
    if (res != CURLE_OK)
    {
        *getActionStatusCode = GetConfigurationCommandFailure;
        CleanupHeaderChunk(&headerChunk);
        free(dataChunk.data);
        DSC_free(outputResult);
        curl_easy_cleanup(curl);

        return GetCimMIError2Params(MI_RESULT_FAILED, extendedError, ID_PULL_CURLPERFORMFAILED, url, curl_easy_strerror(res));
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
    curl_easy_cleanup(curl);

    if (responseCode != HTTP_SUCCESS_CODE)
    {
        MI_Char statusCodeValue[MAX_STATUSCODE_SIZE] = {0};
        Stprintf(statusCodeValue, MAX_STATUSCODE_SIZE, MI_T("%d"), responseCode);

        *getActionStatusCode = GetConfigurationCommandFailure;
        CleanupHeaderChunk(&headerChunk);
        free(dataChunk.data);
        DSC_free(outputResult);

        return GetCimMIError2Params(MI_RESULT_FAILED, extendedError, ID_PULL_SERVERHTTPERRORCODE, url, statusCodeValue);
    }

    for (i = 0; i < headerChunk.size; ++i)
    {
        if (checksumResponse != NULL && checksumAlgorithmResponse != NULL)
        {
            break;
        }

        if ( Tcscasecmp(headerChunk.headerKeys[i], "Checksum") == 0 )
        {
            checksumResponse = headerChunk.headerValues[i];
        }
        else if ( Tcscasecmp(headerChunk.headerKeys[i], "ChecksumAlgorithm") == 0 )
        {
            checksumAlgorithmResponse = headerChunk.headerValues[i];
        }
    }
    if (checksumResponse == NULL || checksumAlgorithmResponse == NULL)
    {
        *getActionStatusCode = InvalidChecksumAlgorithm;
        return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_PULL_INVALIDRESPONSEFROMSERVER);
    }

    if( Tcscasecmp(checksumAlgorithmResponse, AllowedChecksumAlgorithm) != 0 )
    {
        *getActionStatusCode = InvalidChecksumAlgorithm;
        return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_PULL_INVALIDCHECKSUMALGORITHM);
    }

    if( dataChunk.size > 0 )
    {
        FILE *fp = File_OpenT(directoryPath, MI_T("a"));
        if(fp == NULL)
        {
            *getActionStatusCode = GetConfigurationCommandFailure;
            return GetCimMIError1Param(MI_RESULT_FAILED, extendedError,
                                       ID_PULL_CONFIGURATIONSAVEFAILED, directoryPath);
        }
        fwrite( dataChunk.data, 1, dataChunk.size, fp);
        File_Close(fp);
    }

    if( !ValidateChecksum(checksumResponse, directoryPath) )
    {
        DSC_EventWriteWebDownloadManagerGetDocChecksumValidation(NULL, NULL);
        *getActionStatusCode = ConfigurationChecksumValidationFailure;
        return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_PULL_CHECKSUMMISMATCH);
    }

    DSC_EventWriteLCMPullConfigurationChecksumValidationResult(configurationID, (MI_Uint32)MI_RESULT_OK);

    //Create checksumFile
    {
        MI_Char checksumFileName[MAX_URL_LENGTH];
        FILE *fp = NULL;
        Stprintf(checksumFileName, MAX_URL_LENGTH,MI_T("%s.checksum"), directoryPath);
        fp = File_OpenT(checksumFileName,MI_T("w"));
        if( fp != NULL )
        {
            fwrite(checksumResponse, 1, Strlen(checksumResponse), fp);
            File_Close(fp);
        }
        else
        {
            *getActionStatusCode = GetConfigurationCommandFailure;
            DSC_free(outputResult);
            return GetCimMIError1Param(MI_RESULT_FAILED, extendedError, ID_PULLGETCONFIGURATION_CHECKSUMSAVEFAILED, checksumFileName);
        }
    }

    CleanupHeaderChunk(&headerChunk);
    free(dataChunk.data);

    *result = outputResult;
    DSC_EventWriteWebDownloadManagerGetDocGetCall(configurationID, *result );

    return MI_RESULT_OK;
}

MI_Result GetUrlParam(_In_ MI_InstanceA *customData,
    _Inout_updates_(URL_SIZE) MI_Char *url,
    _Inout_updates_(SUBURL_SIZE) MI_Char *subUrl,
    _Out_ MI_Uint32 *port,
    _Out_ MI_Boolean *bIsHttps,
    _Out_ MI_Uint32* getActionStatusCode,
    _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Uint32 xCount = 0;
    MI_Result r = MI_RESULT_OK;
    *bIsHttps = MI_FALSE;
    *port = DEFAULT_SERVERPORT;
    *getActionStatusCode = Success;
    *extendedError = NULL;

    // Get Server Url.
    for( xCount =0; xCount < customData->size; xCount++)
    {
        MI_Value value;
        MI_Uint32 flags;
        r = MI_Instance_GetElement(customData->data[xCount], MSFT_KEYVALUEPAIR_CLASS_KEY, &value, NULL, &flags, NULL);
        if( r == MI_RESULT_OK && !(flags & MI_FLAG_NULL) && Tcscasecmp(ServerUrlParamName, value.string) == 0 )
        {
            r = MI_Instance_GetElement(customData->data[xCount], MSFT_KEYVALUEPAIR_CLASS_VALUE, &value, NULL, &flags, NULL);
            if( r == MI_RESULT_OK && !(flags & MI_FLAG_NULL) )
            {
                //Check if connection is secure and if unsecure user explicitly asks for it.
                if( !ConnectionAllowed(customData, value.string) )
                {
                    *getActionStatusCode = InvalidDownloadManagerCustomDataInMetaConfig;
                    r = GetCimMIError1Param( MI_RESULT_INVALID_PARAMETER, extendedError, ID_PULL_UNSECURECONNECTIONNOTALLOWED, value.string);
                    return r;
                }

                if( sscanf(value.string, MI_T("http://%99[^:]:%d/%199[^\n]"), url, port, subUrl) == 3 ||
                    sscanf(value.string, MI_T("http://%99[^/]/%199[^\n]"), url, subUrl) == 2 )
                {
                    //success
                }
                //format is: http://server:port/suburl or http://server/suburl
                else if( sscanf(value.string, MI_T("https://%99[^:]:%d/%199[^\n]"), url, port, subUrl) == 3 ||
                    sscanf(value.string, MI_T("https://%99[^/]/%199[^\n]"), url, subUrl) == 2 )
                {
                    //success
                    *bIsHttps = MI_TRUE;
                }
                break;
            }
        }
    }

    if(  xCount >= customData->size ) // Not found
    {
        *getActionStatusCode = InvalidDownloadManagerCustomDataInMetaConfig;
        return GetCimMIError(r, extendedError, ID_ENGINEHELPER_MEMORY_ERROR);
    }
    return MI_RESULT_OK;
}

MI_Result GetRequestParam(_In_ MI_Instance *metaConfig,
                          _In_ MI_Char * partialConfigName,
                          _Inout_updates_(URL_SIZE) MI_Char *url,
                          _Inout_updates_(SUBURL_SIZE) MI_Char *subUrl,
                          _Out_ MI_Uint32 *port,
                          _Out_ MI_Boolean *bIsHttps,
                          _Outptr_result_maybenull_z_ MI_Char **configurationID,
                          _Outptr_result_maybenull_z_ MI_Char **certificateID,
                          _Out_ MI_Uint32* getActionStatusCode,
                          _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Result r = MI_RESULT_OK;
    MI_Instance *credential = NULL;
    MI_InstanceA customParam = {0};
    *bIsHttps = MI_FALSE;
     //Get meta configuration elements
    r = GetMetaConfigParameters(metaConfig, partialConfigName, &credential, &customParam, configurationID, certificateID, url, subUrl, port, bIsHttps, getActionStatusCode, extendedError);
    return r;
}

MI_Result CreateTmpDirectoryPath(_Outptr_result_maybenull_z_  MI_Char** directoryPath, _Outptr_result_maybenull_z_  MI_Char** fileLocation)
{
    MI_Char *path;
   if( TempnamT(&path) )
       return MI_RESULT_FAILED;

   if( path != NULL)
   {
       int result = MkdirT(path, S_IRWXU);
       if( result == 0 )
       {
            MI_Char *filePath = (MI_Char*) DSC_malloc(MAX_URL_LENGTH * sizeof(MI_Char), NitsHere());
            if( filePath == NULL )
            {
                free(path);
                return MI_RESULT_FAILED;
            }

            Stprintf(filePath, MAX_URL_LENGTH, MI_T("%T/localhost.mof"), path);

            *fileLocation = filePath;
            *directoryPath = path;
            return MI_RESULT_OK;
       }
   }
   return MI_RESULT_FAILED;
}

MI_Result  IssueGetModuleRequest( _In_z_ const MI_Char *configurationID,
                                  _In_z_ const MI_Char *moduleName,
                                  _In_z_ const MI_Char *moduleVersion,
                                  _In_z_ const MI_Char *certificateID,
                                  _In_z_ const MI_Char *filePath,
                                  _Outptr_result_maybenull_z_  MI_Char** result,
                                  _Out_ MI_Uint32* getActionStatusCode,
                                  _In_reads_z_(URL_SIZE) const MI_Char *url,
                                  _In_ MI_Uint32 port,
                                  _In_reads_z_(SUBURL_SIZE) const MI_Char *subUrl,
                                  MI_Boolean bIsHttps,
                                  _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Result r = MI_RESULT_OK;
    MI_Char *outputResult = (MI_Char*)DSC_malloc((Tcslen(MI_T("OK"))+1) * sizeof(MI_Char), NitsHere());

    CURL *curl = NULL;
    CURLcode res = CURLE_OK;
    struct HeaderChunk headerChunk;
    struct Chunk dataChunk;
    char configurationUrl[MAX_URL_LENGTH];
    long responseCode = 0;
    size_t i;
    char* checksumResponse = NULL;
    char* checksumAlgorithmResponse = NULL;
    struct curl_slist *list = NULL;
    char agentIdHeader[101];

    *result = NULL;
    if( outputResult == NULL )
    {
        *getActionStatusCode = GetConfigurationCommandFailure;
        return GetCimMIError(r, extendedError, ID_ENGINEHELPER_MEMORY_ERROR);
    }
    DSC_EventWriteGetDscDocumentWebDownloadManagerServerUrl(configurationID, url);
    Stprintf(outputResult,3, MI_T("OK"));

    curl = curl_easy_init();
    if (!curl)
    {
        return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_PULL_CURLFAILEDTOINITIALIZE);
    }


    if (bIsHttps)
    {
        Snprintf(configurationUrl, MAX_URL_LENGTH, "https://%s:%d/%s/Modules(ModuleName='%s',ModuleVersion='%s')/ModuleContent", url, port, subUrl, moduleName, moduleVersion);
    }
    else
    {
        Snprintf(configurationUrl, MAX_URL_LENGTH, "http://%s:%d/%s/Modules(ModuleName='%s',ModuleVersion='%s')/ModuleContent", url, port, subUrl, moduleName, moduleVersion);
    }


    r = SetGeneralCurlOptions(curl, extendedError);
    if (r != MI_RESULT_OK)
    {
	curl_easy_cleanup(curl);
	return r;
    }

    InitHeaderChunk(&headerChunk);
    dataChunk.data = (char *)malloc(1);
    dataChunk.size = 0;

    curl_easy_setopt(curl, CURLOPT_URL, configurationUrl);

    list = curl_slist_append(list, "ProtocolVersion: 2.0");
    Snprintf(agentIdHeader, 100, "AgentId: %s", configurationID);
    agentIdHeader[100] = '\0';
    list = curl_slist_append(list, agentIdHeader);

    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headerChunk);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &dataChunk);

    res = curl_easy_setopt(curl, CURLOPT_SSLCERT, OAAS_CERTPATH);
    if (res != CURLE_OK)
    {
        curl_slist_free_all(list);
        curl_easy_cleanup(curl);
        return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_PULL_CERTOPTS_NOT_SUPPORTED);
    }
    curl_easy_setopt(curl, CURLOPT_SSLKEY, OAAS_KEYPATH);

    if (g_sslOptions.cipherList[0] != '\0')
    {
        res = curl_easy_setopt(curl, CURLOPT_SSL_CIPHER_LIST, g_sslOptions.cipherList);
        if (res != CURLE_OK)
        {
            *getActionStatusCode = GetConfigurationCommandFailure;
            curl_easy_cleanup(curl);
            return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_PULL_CURLFAILEDTOSETCIPHERLIST);
        }
    }

    if (g_sslOptions.NoSSLv3 == MI_TRUE)
    {
        res = curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1);
        if (res != CURLE_OK)
        {
            *getActionStatusCode = GetConfigurationCommandFailure;
            curl_easy_cleanup(curl);
            return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_PULL_CURLFAILEDTOSETNOSSLV3);
        }
    }

    res = curl_easy_perform(curl);

    curl_slist_free_all(list);
    if (res != CURLE_OK)
    {
        *getActionStatusCode = GetConfigurationCommandFailure;
        CleanupHeaderChunk(&headerChunk);
        free(dataChunk.data);
        DSC_free(outputResult);
        curl_easy_cleanup(curl);

        return GetCimMIError2Params(MI_RESULT_FAILED, extendedError, ID_PULL_CURLPERFORMFAILED, url, curl_easy_strerror(res));
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
    curl_easy_cleanup(curl);

    if (responseCode != HTTP_SUCCESS_CODE)
    {
        MI_Char statusCodeValue[MAX_STATUSCODE_SIZE] = {0};
        *getActionStatusCode = GetConfigurationCommandFailure;
        CleanupHeaderChunk(&headerChunk);
        free(dataChunk.data);
        DSC_free(outputResult);
        Stprintf(statusCodeValue, MAX_STATUSCODE_SIZE, MI_T("%d"), responseCode);
        return GetCimMIError4Params(MI_RESULT_FAILED, extendedError, ID_PULL_SERVERHTTPERRORCODEMODULE, url, statusCodeValue, moduleName, moduleVersion);
    }

    for (i = 0; i < headerChunk.size; ++i)
    {
        if (checksumResponse != NULL && checksumAlgorithmResponse != NULL)
        {
            break;
        }

        if ( Tcscasecmp(headerChunk.headerKeys[i], "Checksum") == 0 )
        {
            checksumResponse = headerChunk.headerValues[i];
        }
        else if ( Tcscasecmp(headerChunk.headerKeys[i], "ChecksumAlgorithm") == 0 )
        {
            checksumAlgorithmResponse = headerChunk.headerValues[i];
        }
    }
    if (checksumResponse == NULL || checksumAlgorithmResponse == NULL)
    {
        *getActionStatusCode = InvalidChecksumAlgorithm;
        return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_PULL_INVALIDRESPONSEFROMSERVER);
    }

    if( Tcscasecmp(checksumAlgorithmResponse, AllowedChecksumAlgorithm) != 0 )
    {
        *getActionStatusCode = InvalidChecksumAlgorithm;
        return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_PULL_INVALIDCHECKSUMALGORITHM);
    }

    if( dataChunk.size > 0 )
    {
        FILE *fp = File_OpenT(filePath, MI_T("a"));
        if(fp == NULL)
        {
            *getActionStatusCode = GetConfigurationCommandFailure;
            return GetCimMIError1Param(MI_RESULT_FAILED, extendedError,
                                       ID_PULL_CONFIGURATIONSAVEFAILED, filePath);
        }
        fwrite( dataChunk.data, 1, dataChunk.size, fp);
        File_Close(fp);
    }

    if( !ValidateChecksum(checksumResponse, filePath) )
    {
        DSC_EventWriteWebDownloadManagerGetDocChecksumValidation(NULL, NULL);
        *getActionStatusCode = ConfigurationChecksumValidationFailure;
        return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_PULL_CHECKSUMMISMATCH);
    }



    DSC_EventWriteLCMPullConfigurationChecksumValidationResult(configurationID, (MI_Uint32)MI_RESULT_OK);

    //Create checksumFile
    {
        MI_Char checksumFileName[MAX_URL_LENGTH];
        FILE *fp = NULL;
        Stprintf(checksumFileName, MAX_URL_LENGTH,MI_T("%s.checksum"), filePath);
        fp = File_OpenT(checksumFileName,MI_T("w"));
        if( fp != NULL )
        {
            fwrite(checksumResponse, 1, Strlen(checksumResponse), fp);
            File_Close(fp);
        }
        else
        {
            *getActionStatusCode = GetConfigurationCommandFailure;
            DSC_free(outputResult);
            return GetCimMIError1Param(MI_RESULT_FAILED, extendedError, ID_PULLGETCONFIGURATION_CHECKSUMSAVEFAILED, checksumFileName);
        }
    }

    CleanupHeaderChunk(&headerChunk);
    free(dataChunk.data);

    *result = outputResult;
    DSC_EventWriteWebDownloadManagerGetDocGetCall(configurationID, *result );

    return MI_RESULT_OK;
}

MI_Result MI_CALL Pull_GetModules(_Out_ MI_Uint32 * numModulesInstalled,
                                  const MI_Char *configurationID,
                                  const MI_Char *certificateID,
                                  MI_Char* directoryPath,
                                  MI_Char* fileName,
                                  MI_Char** result,
                                  MI_Uint32* getActionStatusCode,
                                  MI_Boolean bAllowedModuleOverride,
                                  _In_reads_z_(URL_SIZE) const MI_Char *url,
                                  _In_ MI_Uint32 port,
                                  _In_reads_z_(SUBURL_SIZE) const MI_Char *subUrl,
                                  MI_Boolean bIsHttps,
                                  MI_Instance **extendedError)
{
    ModuleTable moduleTable;
    ModuleTableEntry* current;
    MI_Result r;
    MI_Value value;
    int retval = 0;
    char zipPath[MAX_URL_LENGTH];
    char stringBuffer[MAX_URL_LENGTH];
    char * verifyFlag = "1";

    moduleTable.first = NULL;
    r = GetModuleNameVersionTable(fileName, &moduleTable, extendedError);
    if (r != MI_RESULT_OK)
    {
        CleanupModuleTable(moduleTable);
        return r;
    }

    r = MI_Instance_GetElement(g_metaConfig, MSFT_DSCMetaConfiguration_DisableModuleSignatureValidation, &value, NULL, NULL, NULL);

    if (r != MI_RESULT_OK)
    {
#if !defined(BUILD_OMS)
	// default is to not verify
	verifyFlag = "0";
#else
	// default is to verify for oms
	verifyFlag = "1";
#endif
    }
    else
    {
	if (value.boolean == MI_TRUE)
	{
	    verifyFlag = "0";
	}
    }

    // moduleTable now has the modules we need to pull
    current = moduleTable.first;
    while (current != NULL)
    {
        Snprintf(zipPath, MAX_URL_LENGTH, "%s/%s_%s.zip", directoryPath, current->moduleName, current->moduleVersionClassTuple->moduleVersion);
        r = IssueGetModuleRequest(configurationID,
                                  current->moduleName,
                                  current->moduleVersionClassTuple->moduleVersion,
                                  certificateID,
                                  zipPath,
                                  result,
                                  getActionStatusCode,
                                  url,
                                  port,
                                  subUrl,
                                  bIsHttps,
                                  extendedError);

        if (r != MI_RESULT_OK)
        {
            CleanupModuleTable(moduleTable);
            return r;
        }
        // Determine python version
        char data[BUFSIZ];
        int isPython2 = 1;
        DSC_LOG_INFO("Assuming python2 in WebPullClient\n");

	    // Look for python3
        FILE * pipe = popen("python3 -V 2>&1 | grep -Po '(?<=Python )(.+)'", "r");
        fgets(data, BUFSIZ, pipe);
        if (data[0] == '3') {
            DSC_LOG_INFO("Found python3 in WebPullClient.\n");
            isPython2 = 0;
        } else {
            // If python3 does not exist, look for python2
            memset(&data[0], 0, sizeof(data));
            pipe = popen("python2 -V 2>&1 | grep -Po '(?<=Python )(.+)'", "r");
            fgets(data, BUFSIZ, pipe);
            if (data[0] == '2') {
                DSC_LOG_INFO("Found python2 in WebPullClient.\n");
                    isPython2 = 1;
            }
        }

      	if (isPython2 == 1)
      	{
        	DSC_LOG_INFO("Calling InstallModule with python2");
      		Snprintf(stringBuffer, MAX_URL_LENGTH, "%s %s %s", DSC_SCRIPT_PATH "/InstallModule.py", zipPath, verifyFlag);
      	}
      	else
      	{
		DSC_LOG_INFO("Calling InstallModule with python3");
		Snprintf(stringBuffer, MAX_URL_LENGTH, "%s %s %s %s", "/usr/bin/python3 " DSC_SCRIPT_PATH "/python3/InstallModule.py", zipPath, verifyFlag, " 2>&1");
      	}
        DSC_LOG_INFO("executing '%T'\n", stringBuffer);
        retval = system(stringBuffer);
        DSC_LOG_INFO("Executed '%T', returned %d\n", stringBuffer, retval);

        if (retval != 0)
        {
            if (retval == -1 && errno == ECHILD)
            {
                // This is an OK condition.  We weren't able to find the child process to reap after it completes.
            }
            else
            {
                // Attempt to remove the module as a last resort.  If it fails too, a reinstall may be necessary.
                if (isPython2 == 1)
                {
                    DSC_LOG_INFO("Calling RemoveModule with python2");
                    Snprintf(stringBuffer, MAX_URL_LENGTH, "%s %s", DSC_SCRIPT_PATH "/RemoveModule.py", current->moduleName);
                }
                else
                {
                    DSC_LOG_INFO("Calling RemoveModule with python3");
                    Snprintf(stringBuffer, MAX_URL_LENGTH, "%s %s %s", "/usr/bin/python3 " DSC_SCRIPT_PATH "/python3/RemoveModule.py", current->moduleName, " 2>&1");
                }

                retval = system(stringBuffer);
                DSC_LOG_INFO("Executed '%T', returned %d\n", stringBuffer, retval);
                if ( retval == 0 || (retval == -1 && errno == ECHILD) )
                {
                    r = GetCimMIError2Params(MI_RESULT_FAILED, extendedError, ID_PULL_INSTALLMODULEFAILED, current->moduleName, current->moduleVersionClassTuple->moduleVersion);
                }
                else
                {
                    r = GetCimMIError2Params(MI_RESULT_FAILED, extendedError, ID_PULL_INSTALLMODULEANDREMOVEMODULEFAILED, current->moduleName, current->moduleVersionClassTuple->moduleVersion);
                }

                CleanupModuleTable(moduleTable);
                return r;
            }
        }

        *numModulesInstalled = *numModulesInstalled + 1;
        current = current->next;
    }

    CleanupModuleTable(moduleTable);

    return MI_RESULT_OK;

}

MI_Result MI_CALL Pull_GetConfigurationWebDownloadManager(_In_ LCMProviderContext *lcmContext,
                                                          _In_ MI_Instance *metaConfig,
                                                          _In_opt_z_ MI_Char *partialConfigName,
                                                          _In_opt_z_ MI_Char *assignedConfiguration,
                                                          _Outptr_result_maybenull_z_  MI_Char** mofFileName,
                                                          _Outptr_result_maybenull_z_  MI_Char** directoryName,
                                                          _Out_ MI_Uint32 * numModulesInstalled,
                                                          _Outptr_result_maybenull_z_  MI_Char** result,
                                                          _Out_ MI_Uint32* getActionStatusCode,
                                                          _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Result r = MI_RESULT_OK;
    MI_Char *configurationID = NULL;
    MI_Char *certificateID = NULL;
    MI_Char *fileName = NULL;
    MI_Char url[URL_SIZE] = {0};
    MI_Char subUrl[SUBURL_SIZE] = {0};
    MI_Uint32 port = DEFAULT_SERVERPORT;
    MI_Boolean bIsHttps = MI_FALSE;
    MI_Value value;
    MI_Uint32 flags;
    MI_Boolean bAllowedModuleOverride;

    if( metaConfig == NULL )
    {
        return MI_RESULT_INVALID_PARAMETER;
    }

    r = GetRequestParam(metaConfig, partialConfigName, (MI_Char*)url, (MI_Char*)subUrl, &port, &bIsHttps, &configurationID, &certificateID, getActionStatusCode, extendedError);
    //Get meta configuration elements
    if( r != MI_RESULT_OK)
    {
        return r;
    }

    // Validate if URL was correct.
    if( url[0] == MI_T('\0') || subUrl[0] == MI_T('\0') )
    {
        DSC_free(configurationID);
        *getActionStatusCode = InvalidDownloadManagerCustomDataInMetaConfig;
        r = GetCimMIError( MI_RESULT_INVALID_PARAMETER, extendedError, ID_PULL_INVALIDURLINCUSTOMDATA);
        return r;
    }
    r = CreateTmpDirectoryPath(directoryName, &fileName);
    if( r != MI_RESULT_OK)
    {
        DSC_free(configurationID);
        r = GetCimMIError( r, extendedError, ID_LCMHELPER_CREATE_CONFDIR_FAILED);
        return r;
    }

    // Get our supported SSL options if there are any
    r = GetSSLOptions(extendedError);
    if( r != MI_RESULT_OK)
    {
        DSC_free(configurationID);
        DSC_free(fileName);
        return r;
    }

    DSC_EventWriteLCMPullGetConfigAttempt( webPulginName, configurationID);
    // Issue Get Action Request
    r = IssueGetConfigurationRequest( configurationID,  certificateID, fileName, result,
                                      getActionStatusCode, url, port, subUrl, bIsHttps, assignedConfiguration, extendedError);

    if( r != MI_RESULT_OK)
    {
        DSC_free(configurationID);
        DSC_free(fileName);
        return r;
    }

    r = MI_Instance_GetElement(metaConfig, MSFT_DSCMetaConfiguration_AllowModuleOverwrite, &value, NULL, &flags, NULL);
    if( r == MI_RESULT_OK && !(flags & MI_FLAG_NULL) && value.boolean == MI_TRUE)
    {
        bAllowedModuleOverride = MI_TRUE;
    }

    r = Pull_GetModules(numModulesInstalled, configurationID,  certificateID, *directoryName, fileName, result, getActionStatusCode, bAllowedModuleOverride, url, port, subUrl, bIsHttps, extendedError);
    if( r != MI_RESULT_OK)
    {
         DSC_free(configurationID);
         DSC_free(fileName);
         return r;
    }

    DSC_free(configurationID);
    *mofFileName = fileName;
    DSC_EventWriteLCMPullGetConfigSuccess(webPulginName);
    return MI_RESULT_OK;
}



MI_Result MI_CALL Pull_GetActionWebDownloadManager(_In_ LCMProviderContext *lcmContext,
                                                   _In_ MI_Instance *metaConfig,
                                                   _In_opt_z_ MI_Char *partialConfigName,
                                                   _In_z_ const MI_Char *checkSum,
                                                   _In_ MI_Boolean complianceStatus,
                                                   _In_ MI_Uint32 lastGetActionStatusCode,
                                                   _Outptr_result_maybenull_z_  MI_Char** result,
                                                   _Out_ MI_Uint32* getActionStatusCode,
                                                   _Outptr_result_maybenull_ OverAllGetActionResponse** serverAssignedConfigurations,
                                                   _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Result r = MI_RESULT_OK;
    MI_Char *configurationID = NULL;
    MI_Char *certificateID = NULL;
    MI_Char url[URL_SIZE] = {0};
    MI_Char subUrl[SUBURL_SIZE] = {0};
    MI_Uint32 port = DEFAULT_SERVERPORT;
    MI_Boolean bIsHttps = MI_FALSE;
    MI_Char *tmpChecksum = NULL;

    if( metaConfig == NULL)
    {
        return MI_RESULT_INVALID_PARAMETER;
    }
    tmpChecksum = (MI_Char*)DSC_malloc( (Tcslen(checkSum) +1) * sizeof(MI_Char), NitsHere());
    if( tmpChecksum == NULL)
    {
        return GetCimMIError(MI_RESULT_SERVER_LIMITS_EXCEEDED, extendedError, ID_ENGINEHELPER_MEMORY_ERROR);
    }
    memcpy(tmpChecksum, checkSum, (Tcslen(checkSum) + 1)* sizeof(MI_Char));

    r = GetRequestParam(metaConfig, partialConfigName, (MI_Char*)url, (MI_Char*)subUrl, &port, &bIsHttps, &configurationID, &certificateID, getActionStatusCode, extendedError);
    if( r != MI_RESULT_OK)
    {
        DSC_free(tmpChecksum);
        return r;
    }

    // Validate if URL was correct.
    if( url[0] == MI_T('\0') || subUrl[0] == MI_T('\0') )
    {
        DSC_free(configurationID);
        *getActionStatusCode = InvalidDownloadManagerCustomDataInMetaConfig;
        r = GetCimMIError( MI_RESULT_INVALID_PARAMETER, extendedError, ID_PULL_INVALIDURLINCUSTOMDATA);
        DSC_free(tmpChecksum);
        return r;
    }

    // Get our supported SSL options if there are any
    r = GetSSLOptions(extendedError);
    if( r != MI_RESULT_OK)
    {
        DSC_free(configurationID);
        DSC_free(tmpChecksum);
        return r;
    }

    DSC_EventWriteLCMPullGetActionAttempt( webPulginName, configurationID, tmpChecksum, complianceStatus);
    // Issue Get Action Request
    r = IssueGetActionRequest( configurationID, certificateID, tmpChecksum, complianceStatus, lastGetActionStatusCode, result, getActionStatusCode, url, port, subUrl, bIsHttps, serverAssignedConfigurations, extendedError);

    DSC_free(configurationID);
    DSC_free(tmpChecksum);
    if( r != MI_RESULT_OK)
    {
        return r;
    }
    DSC_EventWriteLCMPullGetActionSuccess(*result, webPulginName);

    return MI_RESULT_OK;
}

static int CompareVersions(MI_Char* a, MI_Char* b)
{
    MI_Char* a_token;
    MI_Char* a_next_token;
    long a_val = 0;

    MI_Char* b_token;
    MI_Char* b_next_token;
    long b_val = 0;

    a_token = Strtok(a, ".", &a_next_token);
    b_token = Strtok(b, ".", &b_next_token);

    if (a_token == NULL || b_token == NULL)
    {
        if (a_token == NULL && b_token == NULL)
        {
            return 0;
        }
        else if (a_token == NULL)
        {
            return -1;
        }
        else
        {
            return 1;
        }
    }

    a_val = Strtol(a,0,10);
    b_val = Strtol(b,0,10);
    if ( a_val != b_val )
    {
        return a_val - b_val;
    }
    else
    {
        return CompareVersions(a_next_token, b_next_token);
    }
}

static int IsModuleInstalled(MI_Char* moduleName, MI_Char* moduleVersion)
{
    int bytesRead = 0;
    size_t tmpLength;
    MI_Char buffer[MAX_URL_LENGTH];
    MI_Char buffer2[MAX_URL_LENGTH];
    FILE * fp = NULL;

    Snprintf(buffer, MAX_URL_LENGTH, DSC_MODULES_PATH "/%s/VERSION", moduleName);

    fp = File_OpenT(buffer, MI_T("r"));
    if( fp == NULL )
    {
        return 0;
    }

    bytesRead = fread(buffer, 1, MAX_URL_LENGTH, fp);
    buffer[bytesRead] = '\0';

    File_Close(fp);

    if (bytesRead == 0)
    {
        // Unable to read VERSION.
        return 0;
    }

    if (bytesRead == MAX_URL_LENGTH)
    {
        // Invalid version size.  It shouldn't be this long!
        return 0;
    }

    // Since we're going to be tokenizing moduleVersion, we should make a copy of it.
    tmpLength = Tcslen(moduleVersion) + 1;
    memcpy(buffer2, moduleVersion, tmpLength);

    // If the installed version (buffer) is less than the parsed moduleVersion, we need the latest version.
    if (CompareVersions(buffer, buffer2) < 0)
    {
        return 0;
    }

    return 1;
}

// Remove from the table modules that are installed with a version greater than or equal to the requested version
static MI_Result FilterUsingCachedModules(ModuleTable* table)
{
    ModuleTableEntry** pointsToCurrent = &table->first;
    ModuleTableEntry* current;
    ModuleTableEntry* next;

    while (*pointsToCurrent != NULL)
    {
        current = *pointsToCurrent;
        next = current->next;

        // if module is installed, remove it from list
        if ( IsModuleInstalled( current->moduleName, current->moduleVersionClassTuple->moduleVersion ) == 1 )
        {
            *pointsToCurrent = next;
            CleanupModuleTableEntry(current);
        }
        else
        {
            pointsToCurrent = &current->next;
        }
    }

    return MI_RESULT_OK;
}

static MI_Result GetModuleNameVersionTable(MI_Char* mofFileLocation,
                                           ModuleTable* table,
                                           _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Result r = MI_RESULT_OK;
    MI_InstanceA *miInstanceArray = NULL;
    MI_Uint8 *pbuffer = NULL;
    MI_Uint32 contentSize;
    MI_Uint32 readBytes;
    MI_ClassA miClassArray = {0};
    MI_Application *miApp = NULL;
    MI_Deserializer deserializer;
    MI_OperationOptions options;
    MI_Value moduleName;
    MI_Value moduleVersion;
    int i = 0;
    size_t tmpLength;
    ModuleTableEntry* current;
    ModuleTableEntry** foundEntry;
    ModuleClassList** foundClassEntry;

    miApp = (MI_Application *) DSC_malloc( sizeof(MI_Application), NitsHere());
    memset(miApp,  0, sizeof(MI_Application));
    r = DSC_MI_Application_Initialize(0, NULL, NULL, miApp);
    if( r != MI_RESULT_OK)
    {
        DSC_free(miApp);
        return GetCimMIError1Param(MI_RESULT_FAILED, extendedError, ID_PULL_INITIALIZEMODULETABLEFAILED, "App Init failed");
    }

    memset(&deserializer, 0, sizeof(MI_Deserializer));
    memset(&options, 0, sizeof(MI_OperationOptions));

    r = DSC_MI_Application_NewOperationOptions(miApp, MI_FALSE, &options);
    if (r != MI_RESULT_OK)
    {
        MI_Application_Close(miApp);
        DSC_free(miApp);
        return GetCimMIError1Param(MI_RESULT_FAILED, extendedError, ID_PULL_INITIALIZEMODULETABLEFAILED, "Setting NewOperationOptions failed");
    }

    r = DSC_MI_OperationOptions_SetString(&options, MOFCODEC_SCHEMA_VALIDATION_OPTION_NAME, MOFCODEC_SCHEMA_VALIDATION_IGNORE, 0);
    if (r!=MI_RESULT_OK )
    {
        MI_Application_Close(miApp);
        DSC_free(miApp);
        return GetCimMIError1Param(MI_RESULT_FAILED, extendedError, ID_PULL_INITIALIZEMODULETABLEFAILED, "Setting OperationOptions string Failed");
    }

    r = DSC_MI_Application_NewDeserializer_Mof(miApp, 0, MOFCODEC_FORMAT, &deserializer);
    if (r!=MI_RESULT_OK )
    {
        MI_Application_Close(miApp);
        DSC_free(miApp);
        return GetCimMIError1Param(MI_RESULT_FAILED, extendedError, ID_PULL_INITIALIZEMODULETABLEFAILED, "Creating New Deserializer Failed");
    }

    r = ReadFileContent(mofFileLocation, &pbuffer, &contentSize, extendedError);
    if(r != MI_RESULT_OK)
    {
        MI_Application_Close(miApp);
        DSC_free(miApp);
        return r;
    }
    miClassArray.size = 0;
    miClassArray.data = NULL;

    r = MI_Deserializer_DeserializeInstanceArray(&deserializer, 0, &options, 0, pbuffer, contentSize, &miClassArray, &readBytes, &miInstanceArray, extendedError);
    if (r != MI_RESULT_OK)
    {
        MI_Application_Close(miApp);
        DSC_free(miApp);
        return r;
    }

    if(pbuffer)
    {
        DSC_free(pbuffer);
        pbuffer = NULL;
    }

    for (i = 0; i < miInstanceArray->size; ++i)
    {
        // compare classname with BASE_DOCUMENT_CLASSNAME.  Skip if same
        if ( Tcscasecmp( miInstanceArray->data[i]->classDecl->name , BASE_DOCUMENT_CLASSNAME) != 0 )
        {

            r = DSC_MI_Instance_GetElement(miInstanceArray->data[i], MI_T("ModuleName"), &moduleName, NULL, NULL, NULL);
            if (r != MI_RESULT_OK )
            {
                continue;
            }

            r = DSC_MI_Instance_GetElement(miInstanceArray->data[i], MI_T("ModuleVersion"), &moduleVersion, NULL, NULL, NULL);
            if (r != MI_RESULT_OK )
            {
                continue;
            }

            if ( IsModuleVersionValidFormat(moduleVersion.string) == 1 )
            {

                // foundEntry will be a pointer to the "next" or "first" pointer that points to the ModuleTableEntry we want
                foundEntry = ModuleTableContainsKey(table, moduleName.string);
                if(foundEntry == NULL) {
                    return MI_RESULT_FAILED;
                }
                if ( *foundEntry != NULL )
                {
                    // Find VersionClass tuple, append new class if necessary, update version if necessary

                    // compare version? Windows doesn't.
                    // current->moduleVersionClassTuple->moduleVersion

                    // See if class is in the class list.  If it isn't, add it. if it is, do nothing
                    foundClassEntry = ClassListContainsClass(&current->moduleVersionClassTuple->first, miInstanceArray->data[i]->classDecl->name );
                    if (*foundClassEntry == NULL)
                    {
                        *foundClassEntry = (ModuleClassList*) DSC_malloc(sizeof(ModuleClassList), NitsHere());
                        (*foundClassEntry)->next = NULL;

                        // Copy class name
                        tmpLength = Tcslen(miInstanceArray->data[i]->classDecl->name) + 1;
                        (*foundClassEntry)->moduleClass = (MI_Char*)DSC_malloc(sizeof(MI_Char) * tmpLength, NitsHere());
                        memcpy((*foundClassEntry)->moduleClass, miInstanceArray->data[i]->classDecl->name, tmpLength);
                    }
                }
                else
                {
                    *foundEntry = (ModuleTableEntry*) DSC_malloc(sizeof(ModuleTableEntry), NitsHere());
                    current = *foundEntry;
                    current->next = NULL;

                    // Copy module name
                    tmpLength = Tcslen(moduleName.string) + 1;
                    current->moduleName = (MI_Char*)DSC_malloc(sizeof(MI_Char) * tmpLength, NitsHere());
                    memcpy(current->moduleName, moduleName.string, tmpLength);

                    current->moduleVersionClassTuple = (ModuleVersionClassTuple*) DSC_malloc(sizeof(ModuleVersionClassTuple), NitsHere());

                    // Copy module version
                    tmpLength = Tcslen(moduleVersion.string) + 1;
                    current->moduleVersionClassTuple->moduleVersion = (MI_Char*)DSC_malloc(sizeof(MI_Char) * tmpLength, NitsHere());
                    memcpy(current->moduleVersionClassTuple->moduleVersion, moduleVersion.string, tmpLength);

                    current->moduleVersionClassTuple->first = (ModuleClassList*) DSC_malloc(sizeof(ModuleClassList), NitsHere());
                    current->moduleVersionClassTuple->first->next = NULL;

                    // Copy class name
                    tmpLength = Tcslen(miInstanceArray->data[i]->classDecl->name) + 1;
                    current->moduleVersionClassTuple->first->moduleClass = (MI_Char*)DSC_malloc(sizeof(MI_Char) * tmpLength, NitsHere());
                    memcpy(current->moduleVersionClassTuple->first->moduleClass, miInstanceArray->data[i]->classDecl->name, tmpLength);

                }
            }
            else
            {
                // Error: Invalid version for module.
                MI_Deserializer_Close(&deserializer);
                MI_OperationOptions_Delete(&options);
                CleanUpDeserializerInstanceCache(miInstanceArray);
                MI_Application_Close(miApp);
                DSC_free(miApp);
                return GetCimMIError2Params(MI_RESULT_FAILED, extendedError, ID_PULL_INVALIDMODULEVERSION, moduleVersion.string, moduleName.string);
            }
        }
    }

    r = FilterUsingCachedModules(table);

    MI_Deserializer_Close(&deserializer);
    MI_OperationOptions_Delete(&options);

    CleanUpDeserializerInstanceCache(miInstanceArray);
    MI_Application_Close(miApp);
    DSC_free(miApp);

    return MI_RESULT_OK;
}

size_t read_callback(char *buffer, size_t size, size_t nitems, void *instream)
{
    struct ReadChunk * readChunk = (struct ReadChunk *) instream;
    size_t len;

    if (readChunk == NULL)
    {
        return 0;
    }

    len = strlen(&readChunk->data[readChunk->last_char]);

    if (len <= (size * nitems))
    {
        memcpy(buffer, &readChunk->data[readChunk->last_char], len);
        readChunk->last_char += len;
        return len;
    }
    else
    {
        memcpy(buffer, &readChunk->data[readChunk->last_char], size * nitems);
        readChunk->last_char += size * nitems;
        return size * nitems;
    }
}

MI_Result Pull_Register(MI_Char* serverURL,
                        MI_Char* agentId,
                        MI_Char* x_ms_header,
                        MI_Char* auth_header,
                        MI_Char* requestBody,
                        _Outptr_result_maybenull_ MI_Instance **extendedError)
{
    MI_Result r = MI_RESULT_OK;
    const char *emptyString = "";
    MI_Char actionUrl[MAX_URL_LENGTH];
    char * getActionStatus = NULL;
    long responseCode = 0;

    CURL *curl = NULL;
    CURLcode res = CURLE_OK;
    struct Chunk headerChunk;
    struct Chunk dataChunk;
    struct ReadChunk readChunk;
    struct curl_slist *list = NULL;

    r = GetSSLOptions(extendedError);
    if ( r != MI_RESULT_OK)
    {
        return r;
    }

    curl = curl_easy_init();
    if (!curl)
    {
        return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_PULL_CURLFAILEDTOINITIALIZE);
    }

    Snprintf(actionUrl, MAX_URL_LENGTH, "%s/Nodes(AgentId='%s')", serverURL, agentId);
    curl_easy_setopt(curl, CURLOPT_URL, actionUrl);

    headerChunk.data = (char *)malloc(1);
    headerChunk.size = 0;
    dataChunk.data = (char *)malloc(1);
    dataChunk.size = 0;
    readChunk.data = requestBody;
    readChunk.last_char = 0;

    list = curl_slist_append(list, x_ms_header);
    list = curl_slist_append(list, auth_header);
    list = curl_slist_append(list, "Accept: application/json");
    list = curl_slist_append(list, "Content-Type: application/json; charset=utf-8");
    list = curl_slist_append(list, "ProtocolVersion: 2.0");

    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &dataChunk);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1);
    curl_easy_setopt(curl, CURLOPT_PUT, 1);
    curl_easy_setopt(curl, CURLOPT_READDATA, &readChunk);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)strlen(requestBody));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    res = curl_easy_setopt(curl, CURLOPT_SSLCERTTYPE, "PEM");
    if (res != CURLE_OK)
      {
        curl_slist_free_all(list);
        curl_easy_cleanup(curl);
        return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_PULL_CERTOPTS_NOT_SUPPORTED);
      }
    res = curl_easy_setopt(curl, CURLOPT_SSLCERT, OAAS_CERTPATH);
    if (res != CURLE_OK)
      {
        curl_slist_free_all(list);
        curl_easy_cleanup(curl);
        return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_PULL_CERTOPTS_NOT_SUPPORTED);
      };
    res = curl_easy_setopt(curl, CURLOPT_SSLKEY, OAAS_KEYPATH);
    if (res != CURLE_OK)
      {
        curl_slist_free_all(list);
        curl_easy_cleanup(curl);
        return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_PULL_CERTOPTS_NOT_SUPPORTED);
      }

    r = SetGeneralCurlOptions(curl, extendedError);
    if (r != MI_RESULT_OK)
    {
	curl_easy_cleanup(curl);
	return r;
    }

    res = curl_easy_perform(curl);

    if (res != CURLE_OK)
    {
        // Error on communication

        curl_slist_free_all(list);
        curl_easy_cleanup(curl);
        free(headerChunk.data);
        free(dataChunk.data);

        return GetCimMIError2Params(MI_RESULT_FAILED, extendedError, ID_PULL_CURLPERFORMFAILED, actionUrl, curl_easy_strerror(res));
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);

    if (responseCode != 200 && responseCode != 201 && responseCode != 204)
    {
        // Error on register
        MI_Char statusCodeValue[MAX_STATUSCODE_SIZE] = {0};
        Stprintf(statusCodeValue, MAX_STATUSCODE_SIZE, MI_T("%d"), responseCode);

        curl_slist_free_all(list);
        curl_easy_cleanup(curl);
        free(headerChunk.data);
        free(dataChunk.data);

        return GetCimMIError2Params(MI_RESULT_FAILED, extendedError, ID_PULL_SERVERHTTPERRORCODEREGISTER, actionUrl, statusCodeValue);
    }

    curl_slist_free_all(list);
    curl_easy_cleanup(curl);

    free(headerChunk.data);
    free(dataChunk.data);

    return MI_RESULT_OK;
}

extern MI_Char* RunCommand(const MI_Char* command);

MI_Result MI_CALL Pull_SendStatusReport(_In_ LCMProviderContext *lcmContext,
                                                      _In_ MI_Instance *metaConfig,
                                                      _In_ MI_Instance *statusReport,
                                                      _In_ MI_Uint32 isStatusReport,
                                                      _Out_ MI_Uint32* getActionStatusCode,
                                                      _Outptr_result_maybenull_ MI_Instance **extendedError)
{

    MI_Result r = MI_RESULT_OK;
    const char *emptyString = "";
    MI_Char actionUrl[MAX_URL_LENGTH];
    char dataBuffer[10000];

    char * getActionStatus = NULL;
    long responseCode = 0;

    CURL *curl = NULL;
    CURLcode res = CURLE_OK;
    struct Chunk headerChunk;
    struct Chunk dataChunk;
    struct curl_slist *list = NULL;
    MI_Value managerInstances;
    MI_Value currentReportServer;
    MI_Value agentId;
    MI_Value serverURL;
    MI_Value endTime;
    MI_Uint32 flags;
    int i = 0;
    const char* commandFormat = NULL;
    const char* reportText = NULL;

    int bAtLeastOneReportSuccess = 0;

    r = DSC_MI_Instance_GetElement(metaConfig, MSFT_DSCMetaConfiguration_ReportManagers, &managerInstances, NULL, &flags, NULL);
    if (r != MI_RESULT_OK || (flags & MI_FLAG_NULL))
    {
        // Unable to find report managers, don't report
        return MI_RESULT_OK;
    }

    r = GetSSLOptions(extendedError);
    if( r != MI_RESULT_OK)
    {
        return r;
    }

    r = DSC_MI_Instance_GetElement(metaConfig, "AgentId", &agentId, NULL, NULL, NULL);

    list = curl_slist_append(list, "Accept: application/json");
    list = curl_slist_append(list, "Content-Type: application/json; charset=utf-8");
    list = curl_slist_append(list, "ProtocolVersion: 2.0");

    for (i = 0; i < managerInstances.stringa.size; ++i)
    {
        r = MI_Instance_GetElement((MI_Instance*)managerInstances.stringa.data[i], MSFT_ServerURL_Name, &serverURL, NULL, NULL, NULL);

        r = MI_Instance_GetElement(statusReport, REPORTING_ENDTIME, &endTime, NULL, &flags, 0);
        if (r != MI_RESULT_OK || (flags & MI_FLAG_NULL) || (endTime.datetime.u.timestamp.year == 0))
        {
            // not the End report
            commandFormat =  DSC_SCRIPT_PATH "/StatusReport.sh %s StartTime";
            snprintf(dataBuffer, 10000, commandFormat, g_ConfigurationDetails.jobGuidString);
        }
        else
        {
            if (g_currentError[0] == '\0')
            {
                commandFormat =  DSC_SCRIPT_PATH "/StatusReport.sh %s EndTime";
                snprintf(dataBuffer, 10000, commandFormat, g_ConfigurationDetails.jobGuidString);
            }
            else
            {
                if (g_rnids == NULL)
                {
                    commandFormat =  DSC_SCRIPT_PATH "/StatusReport.sh %s EndTime \"%s\"";
                    snprintf(dataBuffer, 10000, commandFormat, g_ConfigurationDetails.jobGuidString, g_currentError);
                }
                else
                {
                    commandFormat =  DSC_SCRIPT_PATH "/StatusReport.sh %s EndTime \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" \"%s\" ";
                    snprintf(dataBuffer, 10000, commandFormat, g_ConfigurationDetails.jobGuidString, g_currentError, g_rnids->SourceInfo,
                             g_rnids->ModuleName, g_rnids->DurationInSeconds, g_rnids->InstanceName, g_rnids->StartDate, g_rnids->ResourceName,
                             g_rnids->ModuleVersion, g_rnids->RebootRequested, g_rnids->ResourceId, g_rnids->ConfigurationName, g_rnids->InDesiredState);
                    Destroy_StatusReport_RNIDS(g_rnids);
                    g_rnids = NULL;

                }
            }
        }

        reportText = RunCommand(dataBuffer);
        if(reportText == NULL) {
            return MI_RESULT_FAILED;
        }

        curl = curl_easy_init();
        if (!curl)
        {
            return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_PULL_CURLFAILEDTOINITIALIZE);
        }

        r = SetGeneralCurlOptions(curl, extendedError);
        if (r != MI_RESULT_OK)
        {
            DSC_free(reportText);
            curl_easy_cleanup(curl);
            return r;
        }

        Snprintf(actionUrl, MAX_URL_LENGTH, "%s/Nodes(AgentId='%s')/SendReport", serverURL.string, agentId.string);
        curl_easy_setopt(curl, CURLOPT_URL, actionUrl);

        headerChunk.data = (char *)malloc(1);
        headerChunk.size = 0;
        dataChunk.data = (char *)malloc(1);
        dataChunk.size = 0;

        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, reportText);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &headerChunk);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &dataChunk);

        res = curl_easy_setopt(curl, CURLOPT_SSLCERT, OAAS_CERTPATH);
        if (res != CURLE_OK)
        {
            curl_slist_free_all(list);
            curl_easy_cleanup(curl);
            return GetCimMIError(MI_RESULT_FAILED, extendedError, ID_PULL_CERTOPTS_NOT_SUPPORTED);
        }
        curl_easy_setopt(curl, CURLOPT_SSLKEY, OAAS_KEYPATH);

        res = curl_easy_perform(curl);

        if (res != CURLE_OK)
        {
            // Error on communication.  Go to next report.
            GetCimMIError2Params(MI_RESULT_FAILED, extendedError, ID_PULL_CURLPERFORMFAILED, actionUrl, curl_easy_strerror(res));

            curl_easy_cleanup(curl);
            DSC_free(reportText);
            free(headerChunk.data);
            free(dataChunk.data);
            continue;
        }

        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);

        if (responseCode != 200 && responseCode != 201 && responseCode != 204)
        {
            // Error on register
            MI_Char statusCodeValue[MAX_STATUSCODE_SIZE] = {0};
            Stprintf(statusCodeValue, MAX_STATUSCODE_SIZE, MI_T("%d"), responseCode);
            GetCimMIError2Params(MI_RESULT_FAILED, extendedError, ID_PULL_SERVERHTTPERRORCODEREGISTER, actionUrl, statusCodeValue);

            curl_easy_cleanup(curl);
            DSC_free(reportText);
            free(headerChunk.data);
            free(dataChunk.data);
            continue;
        }

        bAtLeastOneReportSuccess = 1;

        curl_easy_cleanup(curl);

        DSC_free(reportText);
        free(headerChunk.data);
        free(dataChunk.data);

    }

    curl_slist_free_all(list);

    if (bAtLeastOneReportSuccess == 1)
    {
        return MI_RESULT_OK;
    }
    else
    {
        MI_Char statusCodeValue[MAX_STATUSCODE_SIZE] = {0};
        Stprintf(statusCodeValue, MAX_STATUSCODE_SIZE, MI_T("%d"), responseCode);
        return GetCimMIError1Param(MI_RESULT_FAILED, extendedError, ID_PULL_REPORTINGFAILED, statusCodeValue);
    }
}
