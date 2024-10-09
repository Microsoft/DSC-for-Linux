/*
   PowerShell Desired State Configuration for Linux

   Copyright (c) Microsoft Corporation

   All rights reserved. 

   MIT License

   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the ""Software""), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED *AS IS*, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "pal/palcommon.h"
#include "pal/file.h"
#include "pal/strings.h"
#include "pal/memory.h"
#include <pal/alloc.h>
#include "PAL_Extension.h"

#if defined(CONFIG_POSIX)
    #include <sys/types.h>
    #include <sys/stat.h>
//    #include <uuid/uuid.h>
    #include <dirent.h>
    #include <unistd.h>
    #include <ctype.h>
    #include <openssl/sha.h>

#endif

#if defined(_MSC_VER)
    #include <objbase.h>
    #include <rpc.h>
    #include <windows.h>
    #include <sys/stat.h>
    #include <io.h>
#if _MSC_VER >= 1200
#pragma warning(push)
#endif
#pragma warning(disable:4201)
#include <sha2.h>
#if _MSC_VER >= 1200
#pragma warning(pop)
#endif    
#endif

#if defined(PAL_HAVE_POSIX)

struct _Internal_Dir
{
    DIR* dir;
    Internal_DirEnt ent;
    TChar dirName[PAL_MAX_PATH_SIZE];
} ;

#else

struct _Internal_Dir
{
    intptr_t handle;
    struct _wfinddata_t fileinfo;
    Internal_DirEnt ent;
    int firstTime;
} ;

#endif


FILE* File_OpenW(
    _In_z_ const wchar_t* path,
    _In_z_ const wchar_t* mode)
{
#if defined(_MSC_VER)
    FILE* fp;
    return _wfopen_s(&fp, path, mode) == 0 ? fp : NULL;
#else
    char *filePath;
    char *fileMode;
    FILE* fp;
    if( !UCS2ToAscii( path, &filePath) )
        return NULL;

    if( !UCS2ToAscii( mode, &fileMode) )
    {
        PAL_Free(filePath);
        return NULL;
    }
    fp = fopen(filePath, fileMode);
    PAL_Free(filePath);
    PAL_Free(fileMode);
    return fp;
#endif    
}

int File_RemoveW(
    _In_z_ const wchar_t* path)
{
#if defined(_MSC_VER)
    return _wunlink(path) == 0 ? 0 : -1;
#else
    char *filePath;
    int result;

    if( !UCS2ToAscii( path, &filePath) )
        return -1;

    result = unlink(filePath) == 0 ? 0 : -1;
    PAL_Free(filePath);
    return result;
#endif    
}


int File_TouchW(
    _In_z_ const wchar_t* path)
{
    FILE* fp = File_OpenW(path, L"w");

    if (!fp)
        return -1;

    File_Close(fp);
    return 0;    
}

int UCS2ToAscii( _In_z_ const wchar_t *input,
                  _Outptr_result_z_ char **output)
{
    size_t inputLen;
    size_t xCount = 0;
    char *incInput = (char*) input;
    char *incOutput;
    inputLen = wcslen(input);
    *output = (char*) PAL_Malloc(inputLen + 1 );
    if( *output == NULL )
    {
        return -1;
    }
    incOutput = *output;
    for( xCount = 0; xCount < inputLen; xCount++)
    {
        if( input[xCount] > 128 )
        {
            PAL_Free(*output);
            return -1;
        }
        incOutput[xCount] = *incInput;
        incInput +=2;
    }
    incOutput[inputLen] = '\0';
    return 0;
    
}


int AsciiToUCS2(_In_z_ char *input, _Outptr_result_z_ wchar_t **output)
{
    size_t inputLen;
    size_t xCount = 0;
    char *incInput = (char*) input;
    char *incOutput;
    inputLen = Strlen(input);
    *output = (wchar_t*) malloc( (inputLen+1) *2 );
    if( *output == NULL )
    {
        return -1;
    }
    memset(*output, 0, (inputLen+1) *2);
    incOutput = (char*)*output;
    for( xCount = 0; xCount < inputLen; xCount++)
    {
        incOutput[xCount*2] = *incInput;
        incInput++;
    }
    return 0;

}

int File_CopyT(_In_z_ const PAL_Char* src, _In_z_ const PAL_Char* dest)
{
    FILE* is = NULL;
    FILE* os = NULL;
    char buf[4096];

    /* Open input file */
    is = File_OpenT(src, PAL_T("rb"));
    if (!is)
        return -1;

#if defined(CONFIG_POSIX)
#ifndef CONFIG_ENABLE_WCHAR

    /* Unlink output file if it exists */
    remove(dest);
#endif
#endif

    /* Open output file */
    os = File_OpenT(dest, PAL_T("wb"));
    if (!os)
    {
        File_Close(is);
        return -1;
    }

    /* Copy file */
    for (;;)
    {
#if defined(_MSC_VER)
        long n = (long)fread(buf, 1, sizeof(buf), is);
        long m;
#else
        ssize_t n = fread(buf, 1, sizeof(buf), is);
        ssize_t m;
#endif

        if (n <= 0)
            break;

#if defined(_MSC_VER)
        m  = (long)fwrite(buf, 1, n, os);
#else
        m  = (ssize_t)fwrite(buf, 1, n, os);
#endif

        if (m != n)
        {
            File_Close(is);
            File_Close(os);
            return -1;
        }
    }

    File_Close(is);
    File_Close(os);
    return 0;
}


int Directory_Exist( 
        _In_z_ const char *path)
{
#if defined(_MSC_VER)
    return _access_s(path, 0);
#else
    return access(path, 0);
#endif
}

int Directory_ExistW(
        _In_z_ const wchar_t *path)
{
#if defined(_MSC_VER)
    return _waccess_s(path, 0);
#else
    char *filePath;
    int result;

    if( !UCS2ToAscii( path, &filePath) )
        return -1;

    result = access(filePath, 0);
    PAL_Free(filePath);
    return result;
#endif        

}

int MkdirW(_In_z_ const wchar_t *path, int mode)
{
#if defined(_MSC_VER)
    PAL_UNUSED(mode);
    return _wmkdir(path);
#else
    char *dirPath;
    int result;

    if( !UCS2ToAscii( path, &dirPath) )
        return -1;

    result = mkdir(dirPath, mode);
    free(dirPath);
    return result;
#endif    
}

int TempnamW(  _Outptr_result_maybenull_z_ wchar_t **output )
{
    *output = NULL;
#if defined(_MSC_VER)
    *output = _wtempnam(NULL,NULL);
#else    
    char *Path = tempnam(NULL,NULL);
    if(Path == NULL )
        return -1;
    
    AsciiToUCS2( Path, output) ;
    free(Path);
#endif
    if( *output == NULL)
        return -1;
    return 0;
}

int Tempnam(  _Outptr_result_maybenull_z_ char **output )
{
    *output = NULL;    
#if defined(_MSC_VER)
    *output = _tempnam(NULL,NULL);
#else
    *output = tempnam(NULL,NULL);
#endif
    if( *output == NULL)
        return -1;
    return 0;
}

int File_Exist( 
        _In_z_ const char *path)
{
    FILE *fp = File_Open(path, "r");
    if( !fp)
        return -1;

    File_Close(fp);
    return 0;
}

int File_ExistW(
        _In_z_ const wchar_t *path)
{
    FILE *fp = File_OpenW(path, L"r");
    if( !fp)
        return -1;

    File_Close(fp);
    return 0;    
}


Internal_Dir* Internal_Dir_Open(_In_z_ const TChar* path, NitsCallSite cs)
{
#if defined(_MSC_VER)

    Internal_Dir* dir;
    wchar_t filespec[PAL_MAX_PATH_SIZE];
    
    /* Allocate and zero-fill struct */
    dir = (Internal_Dir*)PAL_CallocCallsite(cs, 1, sizeof(Internal_Dir));
    if (!dir)
        return NULL;

    /* Build files spec */
    {
        if (Wcslcpy(filespec, path, PAL_MAX_PATH_SIZE) >= PAL_MAX_PATH_SIZE)
            return NULL;

        if (Wcslcat(filespec, L"/*", PAL_MAX_PATH_SIZE) >= PAL_MAX_PATH_SIZE)
            return NULL;
    }

    /* Find first file matching the file spec */
    dir->handle = _wfindfirst(filespec, &dir->fileinfo);
    if (dir->handle == -1)
    {
        PAL_Free(dir);
        return NULL;
    }

    /* Note that readdir() has not been called yet */
    dir->firstTime = 1;

    return dir;

#else

    Internal_Dir* self = (Internal_Dir*)PAL_CallocCallsite(cs, 1, sizeof(Internal_Dir));

    if (!self)
        return NULL;

    self->dir = opendir(path);
    if (!self->dir)
    {
        PAL_Free(self);
        return NULL;
    }
    memcpy(self->dirName, path, (Tcslen(path)+1) * sizeof(TChar));

    return self;

#endif          
}

int StringEndsWith( _In_z_ TChar *self, _In_opt_z_ TChar *stringEndsWith)
{
    size_t len1 =0, len2 = 0, tempLen = 0;;
    if( stringEndsWith == NULL)
        return 0;
    
    len1 = Tcslen(self);
    len2 = Tcslen(stringEndsWith);
    if( len2 > len1 )
        return -1;

    for(;tempLen <len1 && tempLen < len2; tempLen++)
    {
        if( toupper(self[len1 - 1 - tempLen]) != toupper(stringEndsWith[len2 - 1 - tempLen]) )
        {
            return -1;
        }
    }
    return 0;
}

Internal_DirEnt* Internal_Dir_Read(_In_ Internal_Dir* self, _In_opt_z_ TChar *fileEndsWith)
{
    for(;;)
    {
#if defined(_MSC_VER)

        if (!self->firstTime)
        {
            if (_wfindnext(self->handle, &self->fileinfo) != 0)
                return NULL;
        }
        self->firstTime = 0;
        
        if( StringEndsWith(self->fileinfo.name, fileEndsWith) != 0 )
            continue;
        Wcslcpy(self->ent.name, self->fileinfo.name, PAL_MAX_PATH_SIZE);
        if( self->fileinfo.attrib & _A_SUBDIR )
            self->ent.isDir = 1;
        else
            self->ent.isDir = 0;
        
        return &self->ent;

#else
        struct dirent* p = readdir(self->dir);
        struct stat st;
        TChar filespec[PAL_MAX_PATH_SIZE];
        if (!p)
            return NULL;

        if( StringEndsWith(p->d_name, fileEndsWith) != 0 )
            continue;        

        Strlcpy(self->ent.name, p->d_name, PAL_MAX_PATH_SIZE);
        // Logic to detect Dir may not work on non-linux, non-windows platforms, in which case
        // We would need to use Isdir method.
        //self->ent.isDir = Isdir(self->ent.name);  
        if( Tcslcpy(filespec, self->dirName, PAL_MAX_PATH_SIZE) >= PAL_MAX_PATH_SIZE)
            return NULL;

        if( Tcslcat(filespec, "/", PAL_MAX_PATH_SIZE) >= PAL_MAX_PATH_SIZE)
            return NULL;

        if( Tcslcat(filespec, self->ent.name, PAL_MAX_PATH_SIZE) >= PAL_MAX_PATH_SIZE)
            return NULL;

        if (stat(filespec, &st) != 0)      
            return NULL;
        // self->ent.isDir = (p->d_type & DT_DIR) ? 1 : 0;
        self->ent.isDir = S_ISDIR(st.st_mode);
        return &self->ent;

#endif       
    }
}

int Internal_Dir_Close(Internal_Dir* dir)
{
#if defined(_MSC_VER)    
    _findclose(dir->handle);
    PAL_Free(dir);
    return 0;    
#else
    if (!dir)
        return -1;

    if (closedir(dir->dir) != 0)
    {
        PAL_Free(dir);
        return -1;
    }

    PAL_Free(dir);
    return 0;

#endif

}

int Directory_Remove( _In_z_ TChar *path)
{
#if defined(_MSC_VER) 
    return RemoveDirectory(path) != 0? 0 : -1;
#else
    return unlink(path) == 0 ? 0 : -1;
#endif
}

char * RunCommand(const char* command);

char * Generate_UUID( NitsCallSite cs)
{
    char * uuidString;
    uuidString = (char*)RunCommand("cat /proc/sys/kernel/random/uuid | awk '{print toupper($0)}' | tr -d \"\n\"");

    return uuidString;
}

int GetComputerHostName(_Inout_updates_z_(length) TChar *buffer, unsigned int length)
{
#if defined(_MSC_VER)
    return !GetComputerName(buffer, (LPDWORD)&length);
#else
    return gethostname(buffer, length);
#endif
}

void PAL_SHA256Transform(_In_ void * pInputValue,
    _In_ unsigned int valueLength,
    _Out_ unsigned char hashedValue[SHA256TRANSFORM_DIGEST_LEN])
{
    SHA256_CTX Ctx;    
#if defined(_MSC_VER)

    SHA256Init(&Ctx);

    SHA256Update
        (
        &Ctx,
        (unsigned char *)pInputValue,
        valueLength
        );

    SHA256Final(&Ctx, hashedValue);    
#else

    SHA256_Init(&Ctx);

    SHA256_Update
        (
        &Ctx,
        (unsigned char *)pInputValue,
        valueLength
        );

    SHA256_Final(hashedValue, &Ctx);

#endif
}

