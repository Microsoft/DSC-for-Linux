/*
   PowerShell Desired State Configuration for Linux

   Copyright (c) Microsoft Corporation

   All rights reserved.

   MIT License

   Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the ""Software""), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED *AS IS*, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
#include "PythonProvider.hpp"
#include "debug_tags.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <errno.h>
#include <fcntl.h>
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
//#include <array>
#include <cstring>
namespace
{


typedef util::unique_ptr<char[]> char_array;


char const OMI_PYTHON_VERSION_STR[] = "OMI_PYTHON_VERSION";
char const DEFAULT_PYTHON_VERSION[] = "python";
char const DEFAULT_OMI_PATH[] = "/opt/omi/";
char const SCRIPT_PATH_EXTENSION[] = "/lib/Scripts/";
char const DEFAULT_DSC_SCRIPT[] = "client";
char const PY_EXTENSION[] = ".py";
char const PYTHON3_COMMAND[] =  "python3";

std::string determinePythonVersion(){
    
    int buffer_length = 128;
    char buffer[buffer_length]; 
    char* result = (char*)malloc(1);
    if(result == NULL) {
        return "python";
    }
    *result = 0; 

    // Check for python3
    FILE* pipe = popen("python3 -V 2>&1 | grep -Po '(?<=Python )(.+)'", "r");
    if(!pipe) {
        std::cout << "Couldn't start command." << std::endl;
    }

    while(fgets(buffer, 128, pipe) != NULL) {
        char* temp = (char*)realloc(result, (result ? strlen(result) : 0) + buffer_length );
        
        if(NULL == temp) {
            free(result);
        }
        
        result = temp;
        strcat(result,buffer);
    }

    // If python3 --version exists
    if(*result != '\0' && result[0] == '3') {
        std::cout << "Found python3." << std::endl;
    	return "python3";
    }

    // Look for python2
    result = (char*)malloc(1);
    if(result == NULL) {
        return "python";
    }
    *result = 0;
    pipe = popen("python2 -V 2>&1 | grep -Po '(?<=Python )(.+)'", "r");
    if(!pipe) {
    	std::cout << "Couldn't start command." << std::endl;
    }
    while(fgets(buffer, 128, pipe) != NULL) {
        char* temp = (char*)realloc(result, (result ? strlen(result) : 0) + buffer_length );

        if(NULL == temp) {
            free(result);
        }
        
        result = temp;
        strcat(result,buffer);
    }

    // If python2 --version exists
    if(*result != '\0' && result[0] == '2') {
        std::cout << "Found python2." << std::endl;
	    return "python2";
    }
    return "python";
}

char_array::move_type
get_python_version ()
{
    char_array pyV;
    std::cout << "In PythonProvider." << std::endl;
    std::string version = determinePythonVersion();
    pyV.reset (strcpy (new char[1 + version.length()], version.c_str()));
    return pyV.move();
}


char_array::move_type
get_script_path ()
{
    size_t len = strlen(DSC_SCRIPT_PATH) + 1;
    char_array fullPath (strcpy (new char[len], DSC_SCRIPT_PATH));
    char const* fileName = DEFAULT_DSC_SCRIPT;
    len += strlen ("/");
    len += strlen (fileName);
    len += strlen (PY_EXTENSION);
    fullPath.reset (strcpy (new char[len], fullPath.get ()));
    strcat (fullPath.get (), "/");
    if(strcmp( determinePythonVersion().c_str(), PYTHON3_COMMAND) == 0)	
    {	
        len +=  strlen (PYTHON3_COMMAND) + strlen ("/");
        fullPath.reset (strcpy (new char[len], fullPath.get ()));
        strcat(fullPath.get (), PYTHON3_COMMAND);
        strcat(fullPath.get (), "/");
    }    
    strcat (fullPath.get (), fileName);
    strcat (fullPath.get (), PY_EXTENSION);
    std::cout << "Script path: "<< std::endl;
    std::cout << fullPath.get() << std::endl;
    return fullPath.move ();
}

class PropertyFinder
{
public:
    /*ctor*/ PropertyFinder (std::string const& name)
        : m_Name (name)
    {
        // empty
    }

    bool operator () (MI_PropertyDecl const* const pProperty) const
    {
        return pProperty->name ? m_Name == pProperty->name : false;
    }

private:
    std::string const m_Name;
};


struct MI_Deleter
{
    void operator () (
        MI_Instance*& pInstance)
    {
        if (0 != pInstance)
        {
            MI_Instance_Delete (pInstance);
            pInstance = 0;
        }
    }
};


typedef util::unique_ptr<MI_Instance, MI_Deleter> MI_InstancePtr;


int
allocate_MI_Instance (
    MI_Context* pContext,
    MI_Instance* const pInstance,
    std::string const name,
    MI_InstancePtr* ppInstanceOut)
{


    //SCX_BOOKEND ("allocate_MI_Instance");
    int rval = EXIT_FAILURE;
    // find the property by name
    MI_PropertyDecl const* const* const ppBegin =
        pInstance->classDecl->properties;
    MI_PropertyDecl const* const* const ppEnd =
        ppBegin + pInstance->classDecl->numProperties;
    MI_PropertyDecl const* const* const ppProperty =
        std::find_if (ppBegin, ppEnd, PropertyFinder (name));

    if (name.compare("__Inventory") == 0)
    {
        MI_Instance* pNewInstance = 0;
        if (MI_RESULT_OK == MI_NewDynamicInstance (
                pContext, pInstance->classDecl->name,
                NULL, &pNewInstance))
        {
            ppInstanceOut->reset (pNewInstance);
            rval = EXIT_SUCCESS;
        }
        else
        {
            std::ostringstream strm;
            strm << __FILE__ << '[' << __LINE__ << ']'
                 << "MI_NewDynamicInstance failed";
            SCX_BOOKEND_PRINT (strm.str ());
            std::cerr << strm.str () << std::endl;
        }
	return rval;
    }

    if (ppEnd != ppProperty)
    {
        MI_Instance* pNewInstance = 0;
        if (MI_RESULT_OK == MI_NewDynamicInstance (
                pContext, (*ppProperty)->className,
                (*ppProperty)->flags, &pNewInstance))
        {
            ppInstanceOut->reset (pNewInstance);
            rval = EXIT_SUCCESS;
        }
        else
        {
            std::ostringstream strm;
            strm << __FILE__ << '[' << __LINE__ << ']'
                 << "MI_NewDynamicInstance failed";
            SCX_BOOKEND_PRINT (strm.str ());
            std::cerr << strm.str () << std::endl;
        }
    }
    else
    {
        std::ostringstream strm;
        strm << __FILE__ << '[' << __LINE__ << ']' << "encountered a member: "
             << name << " that is not in the ClassDecl.";
        SCX_BOOKEND_PRINT (strm.str ());
        std::cerr << strm.str () << std::endl;
    }
    return rval;
}


}


namespace scx
{


/*static*/ unsigned char const PythonProvider::MI_NULL_FLAG = 64;
/*static*/ MI_Boolean PythonProvider::CHILD_SIGNAL_REGISTERED = MI_FALSE;

template<typename T>
/*static*/ int
PythonProvider::TypeHelper<T>::recv (
    PythonProvider* const pProvider,
    T* const pValueOut)
{
    //SCX_BOOKEND ("TypeHelper::recv");
    T temp;
    int rval = pProvider->recv (&temp);
    if (EXIT_SUCCESS == rval)
    {
        *pValueOut = temp;
    }
    return rval;
}


template<typename T>
template<typename Array_t>
/*static*/ int
PythonProvider::TypeHelper<T>::recv_array (
    PythonProvider* const pProvider,
    Array_t* const pValueOut,
    util::unique_ptr<char[]>* const pStorage)
{
    //SCX_BOOKEND ("TypeHelper::recv_array");
    int length;
    int rval = pProvider->recv (&length);
    if (EXIT_SUCCESS == rval)
    {
        pStorage->reset (new char[length * sizeof (T)]);
        T* const pArray = reinterpret_cast<T* const> (pStorage->get ());
        for (int i = 0;
             EXIT_SUCCESS == rval && length > i;
             ++i)
        {
            rval = pProvider->recv (pArray + i);
        }
        if (EXIT_SUCCESS == rval)
        {
            pValueOut->data = pArray;
            pValueOut->size = static_cast<MI_Uint32> (length);
        }
    }
    return rval;
}


/*dtor*/
PythonProvider::~PythonProvider ()
{
#if (PRINT_BOOKENDS)
    std::ostringstream strm;
    strm << "name: \"" << m_Name << '\"';
    SCX_BOOKEND_EX ("PythonProvider::dtor", strm.str ());
#endif
    if (INVALID_SOCKET != m_FD)
    {
        close (m_FD);
    }
    if( m_pid > 0 )
    {
        waitpid(m_pid, NULL, 0);
    }
    for(size_t xCount = 0 ; xCount < m_PreviousPid.size(); xCount++)
    {
        waitpid(m_PreviousPid[xCount] , NULL, WNOHANG);
    }
    m_PreviousPid.clear();
}

int
PythonProvider::init ()
{
    int rval = EXIT_SUCCESS;

#if (PRINT_BOOKENDS)
    std::ostringstream strm;
    strm << "name: \"" << m_Name << '\"';
    SCX_BOOKEND_EX ("PythonProvider::init", strm.str ());
#endif
    if (INVALID_SOCKET == m_FD)
    {
        rval = forkExec ();
    }
    return rval;
}


MI_Result
PythonProvider::test (
    MI_Instance const& instance,
    MI_Boolean* const pTestResultOut)
{
#if PRINT_BOOKENDS
    std::ostringstream strm;
    strm << "name: \"" << m_Name << '\"';
    SCX_BOOKEND_EX ("PythonProvider::test", strm.str ());
#endif
    MI_Result rval = MI_RESULT_FAILED;
    int result = sendRequest (TEST, instance);
    if (EXIT_SUCCESS == result)
    {
	SCX_BOOKEND_PRINT ("send succeeded");
        result = recvResult (pTestResultOut);
        if (EXIT_SUCCESS == result)
        {
            SCX_BOOKEND_PRINT ("recv succeeded");
            rval = MI_RESULT_OK;
        }
        else 
        {
            SCX_BOOKEND_PRINT ("recv failed");
        }
    }
    else 
    {
        SCX_BOOKEND_PRINT ("send failed");
    }
    return rval;
}


MI_Result
PythonProvider::set (
    MI_Instance const& instance,
    MI_Result* const pSetResultOut)
{
#if PRINT_BOOKENDS
    std::ostringstream strm;
    strm << "name: \"" << m_Name << '\"';
    SCX_BOOKEND_EX ("PythonProvider::set", strm.str ());
#endif
    MI_Result rval = MI_RESULT_FAILED;
    int result = sendRequest (SET, instance);
    if (EXIT_SUCCESS == result)
    {
        SCX_BOOKEND_PRINT ("send succeeded");
        MI_Boolean boolResult = MI_FALSE;
        result = recvResult (&boolResult);
        if (EXIT_SUCCESS == result)
        {
            SCX_BOOKEND_PRINT ("recv succeeded");
            rval = MI_RESULT_OK;
            *pSetResultOut = boolResult ? MI_RESULT_OK : MI_RESULT_FAILED;
        }
        else
        {
            SCX_BOOKEND_PRINT ("recv failed");
        }
    }
    else
    {
        SCX_BOOKEND_PRINT ("send failed");
    }
    return rval;
}


MI_Result
PythonProvider::get (
    MI_Instance const& instance,
    MI_Context* const pContext,
    MI_Instance* const pInstanceOut)
{
    std::ostringstream strm;
#if PRINT_BOOKENDS
    strm << "name: \"" << m_Name << '\"';
    SCX_BOOKEND_EX ("PythonProvider::get", strm.str ());
    strm.str ("");
    strm.clear ();
#endif
    // 1: send the request
    // 2: read (int) RESULT
    //     A: RESULT is affirmative (0)
    //         i: read (int) ARG_COUNT
    //         ii: read ARG
    //         iii: read ARG_TYPE
    //         iv: read ARG_VALUE
    //         v: add ARG to new instance
    //         iv: goto ii
    //     B: RESULT is negative (non-0)
    //         i: read (string) error msg
    //         ii: output error msg
    MI_Result rval = MI_RESULT_FAILED;
    int getResult = -1;
    int result = sendRequest (GET, instance);
    if (EXIT_SUCCESS == result)
    {
        SCX_BOOKEND_PRINT ("send succeeded");
        result = recv (&getResult);
        if (EXIT_SUCCESS == result)
        {
            if (0 == getResult)
            {
                SCX_BOOKEND_PRINT ("recv'd POSITIVE");
                result = recv (pContext, pInstanceOut);
                rval = EXIT_SUCCESS == result ? MI_RESULT_OK : MI_RESULT_FAILED;
            }
            else
            {
                SCX_BOOKEND_PRINT ("recv'd NEGATIVE");
                std::string errorMsg;
                result = recv (&errorMsg);
                if (EXIT_SUCCESS == result)
                {
                    if (0 != errorMsg.length ())
                    {
                        strm << ": error msg: \"" << errorMsg << '\"';
                        SCX_BOOKEND_PRINT (strm.str ());
                        std::cerr << strm.str () << std::endl;
                        strm.str ("");
                        strm.clear ();
                    }
                    else
                    {
                        SCX_BOOKEND_PRINT ("no error msg");
                    }
                }
                else
                {
                    SCX_BOOKEND_PRINT ("failed to receive error msg");
                }
            }
        }
    }
    else
    {
        SCX_BOOKEND_PRINT ("send failed");
    }
    return rval;
}


MI_Result
PythonProvider::inventory (
    MI_Instance const& instance,
    MI_Context* const pContext,
    MI_Instance* const pInstanceOut)
{
    std::ostringstream strm;
#if PRINT_BOOKENDS
    strm << "name: \"" << m_Name << '\"';
    SCX_BOOKEND_EX ("PythonProvider::inventory", strm.str ());
    strm.str ("");
    strm.clear ();
#endif
    // 1: send the request
    // 2: read (int) RESULT
    //     A: RESULT is affirmative (0)
    //         i: read (int) ARG_COUNT
    //         ii: read ARG
    //         iii: read ARG_TYPE
    //         iv: read ARG_VALUE
    //         v: add ARG to new instance
    //         iv: goto ii
    //     B: RESULT is negative (non-0)
    //         i: read (string) error msg
    //         ii: output error msg
    MI_Result rval = MI_RESULT_FAILED;
    int inventoryResult = -1;
    int result = sendRequest (INVENTORY, instance);
    if (EXIT_SUCCESS == result)
    {
        SCX_BOOKEND_PRINT ("send succeeded");
        result = recv (&inventoryResult);
        if (EXIT_SUCCESS == result)
        {
            if (0 == inventoryResult)
            {
                SCX_BOOKEND_PRINT ("recv'd POSITIVE");
                result = recv (pContext, pInstanceOut);
                rval = EXIT_SUCCESS == result ? MI_RESULT_OK : MI_RESULT_FAILED;
            }
            else
            {
                SCX_BOOKEND_PRINT ("recv'd NEGATIVE");
                std::string errorMsg;
                result = recv (&errorMsg);
                if (EXIT_SUCCESS == result)
                {
                    if (0 != errorMsg.length ())
                    {
                        strm << ": error msg: \"" << errorMsg << '\"';
                        SCX_BOOKEND_PRINT (strm.str ());
                        std::cerr << strm.str () << std::endl;
                        strm.str ("");
                        strm.clear ();
                    }
                    else
                    {
                        SCX_BOOKEND_PRINT ("no error msg");
                    }
                }
                else
                {
                    SCX_BOOKEND_PRINT ("failed to receive error msg");
                }
            }
        }
    }
    else
    {
        SCX_BOOKEND_PRINT ("send failed");
    }
    return rval;
}

int
PythonProvider::forkExec ()
{
    int rval = EXIT_SUCCESS;
    std::ostringstream strm;
#if (PRINT_BOOKENDS)
    strm << "name: \"" << m_Name << '\"';
    SCX_BOOKEND_EX ("PythonProvider::forkExec", strm.str ());
    strm.str ("");
    strm.clear ();
#endif
    if (INVALID_SOCKET == m_FD)
    {
        int sockets[2];
	int pid;
        int result = socketpair (AF_UNIX, SOCK_STREAM, 0, sockets);
        if (-1 != result)
        {
            // socketpair succeeded
            SCX_BOOKEND_PRINT ("socketpair - succeeded");
            pid = fork ();
            if (0 == pid)
            {
                // fork succeded, this is the child process
                SCX_BOOKEND_PRINT ("fork - succeeded: this is the child");
                // close the parent socket
                close (sockets[1]);
                // create the argument list including the child socket name as a
                // command line arg
                size_t const SOCK_ID_BUF_LEN = 32;
                char socketID[SOCK_ID_BUF_LEN];
                snprintf (socketID, SOCK_ID_BUF_LEN, "%d", sockets[0]);
                char_array pyV (get_python_version ());

                char_array fullName (get_script_path ());
                SCX_BOOKEND_PRINT (fullName);

                char* args[] = { pyV.get (), fullName.get (), socketID, 0 };

                SCX_BOOKEND_PRINT ("In PythonProvider, using:");
                SCX_BOOKEND_PRINT (args[0]);

                // exec
                execvp (args[0], args);
                SCX_BOOKEND_PRINT ("execvp - failed");
                // if we got here, exec failed!
                // check errno { EACCES, ENOEXEC }
                strm << "PythonProvider::forkExec - exec failed: " << errno
                     << ": \"" << errnoText << '\"';
                SCX_BOOKEND_PRINT (strm.str ());
                std::cerr << strm.str () << std::endl;
                strm.str ("");
                strm.clear ();
                rval = EXIT_FAILURE;
            }
            else if (-1 != pid)
            {
	    	m_pid=pid;
                // fork succeeded, this is the parent process
                SCX_BOOKEND_PRINT ("fork - succeeded: this is the parent");
                close (sockets[0]);
                m_FD = sockets[1];
                rval = EXIT_SUCCESS;
            }
            else
            {
                // fork failed
                // error (check errno { EAGAIN, ENOMEM })
                strm << "PythonProvider::forkExec - fork failed: " << errno
                     << ": \"" << errnoText << '\"';
                SCX_BOOKEND_PRINT (strm.str ());
                std::cerr << strm.str () << std::endl;
                strm.str ("");
                strm.clear ();
                rval = EXIT_FAILURE;
            }
        }
        else
        {
            // socketpair failed
            // error (check errno { EAFNOSUPPORT, EMFILE, EOPNOTSUPP,
            //    EPROTONOSUPPORT, EPROTOTYPE, EACCES, ENOBUFS, ENOMEM })
            strm << "PythonProvider::forkExec - socketpair_failed: " << errno
                 << ": \"" << errnoText << '\"';
            SCX_BOOKEND_PRINT (strm.str ());
            std::cerr << strm.str () << std::endl;
            strm.str ("");
            strm.clear ();
            rval = EXIT_FAILURE;
        }
    }
    else
    {
        SCX_BOOKEND_PRINT ("already initialized");
    }
    return rval;
}


int
PythonProvider::verifySocketState ()
{
    //SCX_BOOKEND ("PythonProvider::verifySocketState");
    int result = EXIT_SUCCESS;
    // test for socket
    // if there is a socket...
    //     test that the socket has no readable data
    //     if there is data...
    //         close the socket
    // if there is no socket..
    //     attempt to open a socket
    if (INVALID_SOCKET != m_FD)
    {
        // there should be no data on the socket
        // set the file to non-blocking and attempt to read
        // if there is data to read, the socket stream is in a bad state
        int flags = fcntl (m_FD, F_GETFL, 0);
        char buf[4];
        if (-1 == flags ||
            -1 == fcntl (m_FD, F_SETFL, flags | O_NONBLOCK) ||
            -1 != read (m_FD, buf, 1) ||
            EAGAIN != errno ||
            -1 == fcntl (m_FD, F_SETFL, flags))
        {
            // reset the socket
            handleSocketClosed ();
        }
    }
    if (INVALID_SOCKET == m_FD)
    {
        //Release previous disconnected child process if any
        if( m_pid > 0 )
        {
            // It is possible that disconnected process is still running, in that case
            // try to do cleanup when the provider is unloaded.
            if( waitpid(m_pid , NULL, WNOHANG) == 0 )
            {
                //If process isn't done, cleanup will be done when the provider is unloaded
                m_PreviousPid.push_back(m_pid);
            }
            m_pid = -2;
        }
        result = init ();
    }
    return result;
}


void
PythonProvider::handleSocketClosed ()
{
    close (m_FD);
    m_FD = INVALID_SOCKET;
}


int
PythonProvider::send (
    char const* const str)
{
    //SCX_BOOKEND ("PythonProvider::send (str)");
    ssize_t const nStrLen = (0 == str) ? 0 : strlen (str);
    int rval = send (static_cast<int>(nStrLen));
    ssize_t nCharsSent = 0;
    while (EXIT_SUCCESS == rval &&
           nStrLen > nCharsSent)
    {
        ssize_t nSent = write (m_FD, str + nCharsSent, nStrLen - nCharsSent);
        if (-1 != nSent)
        {
            nCharsSent += nSent;
        }
        else if (EINTR != errno)
        {
            // error (check errno { EACCESS, EAGAIN, EWOULDBLOCK, EBADF,
            //                      ECONNRESET, EDESTADDRREQ, EFAULT, EINVAL,
            //                      EISCONN, EMSGSIZE, ENOBUFS, ENOMEM,
            //                      ENOTCONN, ENOTSOCK, EOPNOTSUPP, EPIPE })
            handleSocketClosed ();
            rval = EXIT_FAILURE;
            std::ostringstream strm;
            strm << "error on socket: (" << errno << ") \"" << errnoText
                 << '\"';
            SCX_BOOKEND_PRINT (strm.str ());
            std::cerr << strm.str () << std::endl;
        }
    }
    return rval;
}


int
PythonProvider::send (
    MI_Datetime const& datetime)
{
    //SCX_BOOKEND ("PythonProvider::send (MI_Datetime)");
    int rval = send (static_cast<unsigned char>(datetime.isTimestamp));
    if (EXIT_SUCCESS == rval)
    {
        if (datetime.isTimestamp)
        {
            if (EXIT_SUCCESS != send (datetime.u.timestamp.year) ||
                EXIT_SUCCESS != send (datetime.u.timestamp.month) ||
                EXIT_SUCCESS != send (datetime.u.timestamp.day) ||
                EXIT_SUCCESS != send (datetime.u.timestamp.hour) ||
                EXIT_SUCCESS != send (datetime.u.timestamp.minute) ||
                EXIT_SUCCESS != send (datetime.u.timestamp.second) ||
                EXIT_SUCCESS != send (datetime.u.timestamp.microseconds) ||
                EXIT_SUCCESS != send (datetime.u.timestamp.utc))
            {
                rval = EXIT_FAILURE;
            }
        }
        else
        {
            if (EXIT_SUCCESS != send (datetime.u.interval.days) ||
                EXIT_SUCCESS != send (datetime.u.interval.hours) ||
                EXIT_SUCCESS != send (datetime.u.interval.minutes) ||
                EXIT_SUCCESS != send (datetime.u.interval.seconds) ||
                EXIT_SUCCESS != send (datetime.u.interval.microseconds))
            {
                rval = EXIT_FAILURE;
            }
        }
    }
    return rval;
}


int
PythonProvider::send (
    MI_Value const& value,
    MI_Type const type)
{
    //SCX_BOOKEND ("PythonProvider::send (value, type)");
    std::ostringstream strm;
    int rval = EXIT_SUCCESS;
    switch (type)
    {
    case MI_BOOLEAN:
        rval = send<unsigned char> (value.boolean);
        break;
    case MI_UINT8:
        rval = send (value.uint8);
        break;
    case MI_SINT8:
        rval = send (value.sint8);
        break;
    case MI_UINT16:
        rval = send (value.uint16);
        break;
    case MI_SINT16:
        rval = send (value.sint16);
        break;
    case MI_UINT32:
        rval = send (value.uint32);
        break;
    case MI_SINT32:
        rval = send (value.sint32);
        break;
    case MI_UINT64:
        rval = send (value.uint64);
        break;
    case MI_SINT64:
        rval = send (value.sint64);
        break;
    case MI_REAL32:
        rval = send (value.real32);
        break;
    case MI_REAL64:
        rval = send (value.real64);
        break;
    case MI_CHAR16:
        rval = send (value.char16);
        break;
    case MI_DATETIME:
        rval = send (value.datetime);
        break;
    case MI_STRING:
        rval = send (static_cast<char const* const> (value.string));
        break;
    case MI_INSTANCE:
        rval = send (*(value.instance));
        break;
    case MI_BOOLEANA:
        {
            //SCX_BOOKEND ("PythonProvider::send_array (bool)");
            rval = send<int> (value.booleana.size);
            for (MI_Uint32 n = 0;
                 EXIT_SUCCESS == rval && n < value.booleana.size;
                 ++n)
            {
                rval = send<unsigned char> (value.booleana.data[n]);
            }
        }
        break;
    case MI_UINT8A:
        rval = send_array (value.uint8a);
        break;
    case MI_SINT8A:
        rval = send_array (value.sint8a);
        break;
    case MI_UINT16A:
        rval = send_array (value.uint16a);
        break;
    case MI_SINT16A:
        rval = send_array (value.sint16a);
        break;
    case MI_UINT32A:
        rval = send_array (value.uint32a);
        break;
    case MI_SINT32A:
        rval = send_array (value.sint32a);
        break;
    case MI_UINT64A:
        rval = send_array (value.uint64a);
        break;
    case MI_SINT64A:
        rval = send_array (value.sint64a);
        break;
    case MI_REAL32A:
        rval = send_array (value.real32a);
        break;
    case MI_REAL64A:
        rval = send_array (value.real64a);
        break;
    case MI_CHAR16A:
        rval = send_array (value.char16a);
        break;
    case MI_DATETIMEA:
        rval = send_array (value.datetimea);
        break;
    case MI_STRINGA:
        rval = send_str_array (value.stringa);
        break;
    case MI_INSTANCEA:
        rval = send_array (value.instancea);
        break;
    default:
        strm << __FILE__ << '[' << __LINE__ << ']'
             << "encountered an unhandled param type: " << type;
        SCX_BOOKEND_PRINT (strm.str ());
        std::cerr << strm.str () << std::endl;
        rval = EXIT_FAILURE;
        break;
    }
    return rval;
}


int
PythonProvider::send_str_array (
    MI_StringA const& strArray)
{
    //SCX_BOOKEND ("PythonProvider::send_str_array");
    int rval = send<int> (strArray.size);
    for (MI_Uint32 n = 0;
         EXIT_SUCCESS == rval && n < strArray.size;
         ++n)
    {
        rval = send (static_cast<char const* const> (strArray.data[n]));
    }
    return rval;
}


int
PythonProvider::send (
    MI_Value const& value,
    MI_Type const type,
    MI_Uint32 const flags)
{
    //SCX_BOOKEND ("PythonProvider::send (value, type, flags)");
    int rval = EXIT_SUCCESS;
    std::ostringstream strm;

    if (MI_REFERENCE != type &&
        MI_REFERENCEA != type)
    {
        if (0 == (MI_FLAG_NULL & flags))
        {
            rval = send<unsigned char> (type);
            if (EXIT_SUCCESS == rval)
            {
                rval = send (value, type);
            }
        }
        else
        {
            rval = send<unsigned char> (type | MI_NULL_FLAG);
        }
    }
    else
    {
        strm << __FILE__ << '[' << __LINE__ << ']'
             << "encountered an unhandled param type: " << type;
        SCX_BOOKEND_PRINT (strm.str ());
        std::cerr << strm.str () << std::endl;
        rval = EXIT_FAILURE;
    }
    return rval;
}


int
PythonProvider::send (
    MI_Instance const& instance)
{
    //SCX_BOOKEND ("PythonProvider::send (MI_Instance)");
    std::ostringstream strm;
    int rval = EXIT_SUCCESS;
    MI_Uint32 elementCount;
    if (EXIT_SUCCESS == rval &&
        MI_RESULT_OK != MI_Instance_GetElementCount (&instance, &elementCount))
    {
        SCX_BOOKEND_PRINT ("GetElementCount failed");
        rval = EXIT_FAILURE;
    }
    int nArgs = 0;
    for (MI_Uint32 n = 0;
         EXIT_SUCCESS == rval && n < elementCount;
         ++n)
    {
        MI_Char const* elementName;
        MI_Value value;
        MI_Type type;
        MI_Uint32 flags;
        if (MI_RESULT_OK == MI_Instance_GetElementAt (
                &instance, n, &elementName, &value, &type, &flags))
        {
            if (!(MI_FLAG_READONLY == (MI_FLAG_READONLY & flags) &&
                  MI_FLAG_KEY != (MI_FLAG_KEY & flags)))
            {
                ++nArgs;
            }
        }
        else
        {
            SCX_BOOKEND_PRINT ("GetElementAt - failed");
            rval = EXIT_FAILURE;
        }
    }
    if (EXIT_SUCCESS == rval)
    {
        rval = send (nArgs);
    }
    for (MI_Uint32 n = 0;
         EXIT_SUCCESS == rval && n < elementCount;
         ++n)
    {
        MI_Char const* elementName;
        MI_Value value;
        MI_Type type;
        MI_Uint32 flags;
        if (MI_RESULT_OK == MI_Instance_GetElementAt (
                &instance, n, &elementName, &value, &type, &flags))
        {
            if (!(MI_FLAG_READONLY == (MI_FLAG_READONLY & flags) &&
                  MI_FLAG_KEY != (MI_FLAG_KEY & flags)))
            {
                send (elementName);
                send (value, type, flags);
            }
        }
        else
        {
            SCX_BOOKEND_PRINT ("GetElementAt - failed");
            rval = EXIT_FAILURE;
        }
    }
    return rval;
}


int
PythonProvider::send (
    MI_Instance* const pInstance)
{
    return send (*pInstance);
}


int
PythonProvider::sendRequest (
    unsigned char const opType,
    MI_Instance const& instance)
{
    std::ostringstream strm;
#if PRINT_BOOKENDS
    strm << "name: \"" << m_Name << '\"';
    SCX_BOOKEND_EX ("PythonProvider::sendRequest", strm.str ());
#endif
    int rval = verifySocketState ();
    // 1: write the header
    //     a: (unsigned char) OP_TYPE
    //     b: (string) OP_NAME
    //     c: (int) ARG_COUNT
    // 2: write each ARG
    if (EXIT_SUCCESS == rval)
    {
        rval = send (opType);
    }
    if (EXIT_SUCCESS == rval)
    {
        rval = send (m_Name);
    }
    if (EXIT_SUCCESS == rval)
    {
        rval = send (instance);
    }
    return rval;
}


int
PythonProvider::recv (
    std::string* const pStrOut)
{
    //SCX_BOOKEND ("PythonProvider::recv (string)");
    int nStrLen = 0;
    int rval = recv (&nStrLen);
    if (EXIT_SUCCESS == rval)
    {
        if (0 != nStrLen)
        {
            ssize_t nCharsRead = 0;
            char_array pBuffer (new char[nStrLen]);
            while (EXIT_SUCCESS == rval &&
                   nStrLen > nCharsRead)
            {
                ssize_t nRead = read (m_FD, pBuffer.get () + nCharsRead,
                                      nStrLen - nCharsRead);
                if (0 < nRead)
                {
                    nCharsRead += nRead;
                }
                else if (0 == nRead)
                {
                    // socket closed
                    handleSocketClosed ();
                    rval = EXIT_FAILURE;
                    SCX_BOOKEND_PRINT ("socket closed unexpectedly");
                    std::cerr << "socket closed unexpectedly" << std::endl;
                }
                else if (EINTR != errno)
                {
                    // Error - check errno { EAGAIN, EBADF, EFAULT, EINVAL, EIO,
                    //                       EISDIR }
                    handleSocketClosed ();
                    rval = EXIT_FAILURE;
                    std::ostringstream strm;
                    strm << "error on socket: (" << errno << ") \"" << errnoText
                         << '\"';
                    SCX_BOOKEND_PRINT (strm.str ());
                    std::cerr << strm.str () << std::endl;
                }
            }
            if (EXIT_SUCCESS == rval)
            {
                pStrOut->assign (pBuffer.get (), nStrLen);
            }
            else
            {
                SCX_BOOKEND_PRINT ("unable to read string text");
                std::cerr << "unable to read string text" << std::endl;
            }
        }
        else
        {
            pStrOut->erase ();
        }
    }
    else
    {
        SCX_BOOKEND_PRINT ("unable to read string size");
        std::cerr << "unable to read string size" << std::endl;
    }
    return rval;
}


int
PythonProvider::recv (
    MI_Datetime* const pDatetimeOut)
{
    //SCX_BOOKEND ("PythonProvider::recv (MI_Datetime)");
    MI_Datetime tempVal;
    unsigned char isTimestamp;
    int rval = recv (&isTimestamp);
    if (EXIT_SUCCESS == rval)
    {
        if (isTimestamp)
        {
            tempVal.isTimestamp = MI_TRUE;
            if (EXIT_SUCCESS != recv (&(tempVal.u.timestamp.year)) ||
                EXIT_SUCCESS != recv (&(tempVal.u.timestamp.month)) ||
                EXIT_SUCCESS != recv (&(tempVal.u.timestamp.day)) ||
                EXIT_SUCCESS != recv (&(tempVal.u.timestamp.hour)) ||
                EXIT_SUCCESS != recv (&(tempVal.u.timestamp.minute)) ||
                EXIT_SUCCESS != recv (&(tempVal.u.timestamp.second)) ||
                EXIT_SUCCESS != recv (&(tempVal.u.timestamp.microseconds)) ||
                EXIT_SUCCESS != recv (&(tempVal.u.timestamp.utc)))
            {
                rval = EXIT_FAILURE;
            }
        }
        else
        {
            tempVal.isTimestamp = MI_FALSE;
            if (EXIT_SUCCESS != recv (&(tempVal.u.interval.days)) ||
                EXIT_SUCCESS != recv (&(tempVal.u.interval.hours)) ||
                EXIT_SUCCESS != recv (&(tempVal.u.interval.minutes)) ||
                EXIT_SUCCESS != recv (&(tempVal.u.interval.seconds)) ||
                EXIT_SUCCESS != recv (&(tempVal.u.interval.microseconds)))
            {
                rval = EXIT_FAILURE;
            }
        }
    }
    if (EXIT_SUCCESS == rval)
    {
        *pDatetimeOut = tempVal;
    }
    return rval;
}


int
PythonProvider::recvResult (
    MI_Boolean* const pResultOut)
{
    std::ostringstream strm;
    *pResultOut = MI_FALSE;
#if (PRINT_BOOKENDS)
    strm << "name: \"" << m_Name << '\"';
    SCX_BOOKEND_EX ("PythonProvider::recvResult", strm.str ());
    strm.str ("");
    strm.clear ();
#endif
    int result = -1;
    int rval = recv (&result);
    if (EXIT_SUCCESS == rval)
    {
        if (0 == result)
        {
            SCX_BOOKEND_PRINT ("recv'd POSITIVE");
            *pResultOut = MI_TRUE;
        }
        else
        {
            SCX_BOOKEND_PRINT ("recv'd NEGATIVE");
            std::string errorMsg;
            rval = recv (&errorMsg);
            if (EXIT_SUCCESS == rval)
            {
                if (0 != errorMsg.length ())
                {
                    strm << ": error msg: \"" << errorMsg << '\"';
                    SCX_BOOKEND_PRINT (strm.str ());
                    std::cerr << strm.str () << std::endl;
                    strm.str ("");
                    strm.clear ();
                }
                else
                {
                    SCX_BOOKEND_PRINT ("no error msg");
                }
            }
            else
            {
                SCX_BOOKEND_PRINT ("failed to receive error msg");
            }
        }
    }
    else
    {
        SCX_BOOKEND_PRINT ("failed to receive result");
    }
    return rval;
}


int
PythonProvider::recv_MI_Value (
    MI_Context* const pContext,
    MI_Instance* const pInstanceOut)
{
    SCX_BOOKEND ("PythonProvider::recv_MI_Value");
    std::string name;
    int rval = recv (&name);
    if (EXIT_SUCCESS == rval)
    {
        std::ostringstream strm;
        unsigned char type;
        rval = recv (&type);
        if (EXIT_SUCCESS == rval)
        {
            if (0 == (MI_NULL_FLAG & type))
            {
                std::string tempStr;
                char_array pArray;
                util::unique_ptr<std::string[]> pStrArray;
                util::unique_ptr<MI_InstancePtr[]> ppInstanceArray;
                MI_Value val;
                switch (type)
                {
                case MI_BOOLEAN:
                    {
                        //SCX_BOOKEND ("recv_MI_Value (BOOLEAN)");
                        unsigned char temp;
                        rval = recv (&temp);
                        if (EXIT_SUCCESS == rval)
                        {
                            val.boolean = temp ? MI_TRUE : MI_FALSE;
                        }
                    }
                    break;
                case MI_UINT8:
                    rval = TypeHelper<MI_Uint8>::recv (this, &(val.uint8));
                    break;
                case MI_SINT8:
                    rval = TypeHelper<MI_Sint8>::recv (this, &(val.sint8));
                    break;
                case MI_UINT16:
                    rval = TypeHelper<MI_Uint16>::recv (this, &(val.uint16));
                    break;
                case MI_SINT16:
                    rval = TypeHelper<MI_Sint16>::recv (this, &(val.sint16));
                    break;
                case MI_UINT32:
                    rval = TypeHelper<MI_Uint32>::recv (this, &(val.uint32));
                    break;
                case MI_SINT32:
                    rval = TypeHelper<MI_Sint32>::recv (this, &(val.sint32));
                    break;
                case MI_UINT64:
                    rval = TypeHelper<MI_Uint64>::recv (this, &(val.uint64));
                    break;
                case MI_SINT64:
                    rval = TypeHelper<MI_Sint64>::recv (this, &(val.sint64));
                    break;
                case MI_REAL32:
                    rval = TypeHelper<MI_Real32>::recv (this, &(val.real32));
                    break;
                case MI_REAL64:
                    rval = TypeHelper<MI_Real64>::recv (this, &(val.real64));
                    break;
                case MI_CHAR16:
                    rval = TypeHelper<MI_Char16>::recv (this, &(val.char16));
                    break;
                case MI_DATETIME:
                    rval = TypeHelper<MI_Datetime>::recv (this, &(val.datetime));
                    break;
                case MI_STRING:
                    rval = recv (&tempStr);
                    val.string = const_cast<MI_Char*>(tempStr.c_str ());
                    break;
                case MI_INSTANCE:
                    {
                        SCX_BOOKEND ("recv_MI_Value (MI_INSTANCE)");
                        ppInstanceArray.reset (new MI_InstancePtr[1]);
                        if (EXIT_SUCCESS == (rval = allocate_MI_Instance (
                                                 pContext, pInstanceOut, name,
                                                 &ppInstanceArray[0])) &&
                            EXIT_SUCCESS == (rval = recv (
                                                 ppInstanceArray[0].get ())))
                        {
                            val.instance = ppInstanceArray[0].get ();
                        }
                    }
                    break;
                case MI_BOOLEANA:
                    {
                        //SCX_BOOKEND ("recv_MI_Value (BOOLEANA)");
                        int length;
                        rval = recv (&length);
                        if (EXIT_SUCCESS == rval)
                        {
                            pArray.reset (
                                new char[length * sizeof (MI_Boolean)]);
                            MI_Boolean* const pTemp =
                                reinterpret_cast<MI_Boolean* const> (
                                    pArray.get ());
                            for (int i = 0;
                                 EXIT_SUCCESS == rval && length > i;
                                 ++i)
                            {
                                unsigned char temp;
                                rval = recv (&temp);
                                if (EXIT_SUCCESS == rval)
                                {
                                    pTemp[i] = temp ? MI_TRUE : MI_FALSE;
                                }
                            }
                            if (EXIT_SUCCESS == rval)
                            {
                                val.booleana.data = pTemp;
                                val.booleana.size =
                                    static_cast<MI_Uint32> (length);
                            }
                        }
                    }
                    break;
                case MI_UINT8A:
                    rval = TypeHelper<MI_Uint8>::recv_array (
                        this, &(val.uint8a), &pArray);
                    break;
                case MI_SINT8A:
                    rval = TypeHelper<MI_Sint8>::recv_array (
                        this, &(val.sint8a), &pArray);
                    break;
                case MI_UINT16A:
                    rval = TypeHelper<MI_Uint16>::recv_array (
                            this, &(val.uint16a), &pArray);
                    break;
                case MI_SINT16A:
                    rval = TypeHelper<MI_Sint16>::recv_array (
                            this, &(val.sint16a), &pArray);
                    break;
                case MI_UINT32A:
                    rval = TypeHelper<MI_Uint32>::recv_array (
                            this, &(val.uint32a), &pArray);
                    break;
                case MI_SINT32A:
                    rval = TypeHelper<MI_Sint32>::recv_array (
                            this, &(val.sint32a), &pArray);
                    break;
                case MI_UINT64A:
                    rval = TypeHelper<MI_Uint64>::recv_array (
                            this, &(val.uint64a), &pArray);
                    break;
                case MI_SINT64A:
                    rval = TypeHelper<MI_Sint64>::recv_array (
                            this, &(val.sint64a), &pArray);
                    break;
                case MI_REAL32A:
                    rval = TypeHelper<MI_Real32>::recv_array (
                            this, &(val.real32a), &pArray);
                    break;
                case MI_REAL64A:
                    rval = TypeHelper<MI_Real64>::recv_array (
                            this, &(val.real64a), &pArray);
                    break;
                case MI_CHAR16A:
                    rval = TypeHelper<MI_Char16>::recv_array (
                            this, &(val.char16a), &pArray);
                    break;
                case MI_DATETIMEA:
                    rval = TypeHelper<MI_Datetime>::recv_array (
                            this, &(val.datetimea), &pArray);
                    break;
                case MI_STRINGA:
                    {
                        //SCX_BOOKEND ("recv_MI_Value (STRINGA)");
                        int length;
                        int rval = recv (&length);
                        if (EXIT_SUCCESS == rval)
                        {
                            MI_Char** pTemp = 0;
                            if (0 < length)
                            {
                                pArray.reset (
                                    new char[length * sizeof (MI_Char*)]);
                                pTemp = reinterpret_cast<MI_Char** const> (
                                        pArray.get ());
                                pStrArray.reset (new std::string[length]);
                                for (int i = 0;
                                     EXIT_SUCCESS == rval && length > i;
                                     ++i)
                                {
                                    rval = recv (pStrArray.get () + i);
                                    if (EXIT_SUCCESS == rval)
                                    {
                                        pTemp[i] = const_cast<MI_Char*>(
                                            pStrArray[i].c_str ());
                                    }
                                }
                            }
                            if (EXIT_SUCCESS == rval)
                            {
                                val.stringa.data = pTemp;
                                val.stringa.size =
                                    static_cast<MI_Uint32> (length);
                            }
                        }
                    }
                    break;
                case MI_INSTANCEA:
                    // read the size
                    // allocate the pointer array
                    // for each instance
                    //     allocate an instance
                    //     read the instance
                    // set the array
                    // cleanup the memory
                    {
                        SCX_BOOKEND ("recv_MI_Value (INSTANCEA)");
                        int length;
                        int rval = recv (&length);
                        if (EXIT_SUCCESS == rval)
                        {
                            MI_Instance** pTemp = 0;
                            if (0 < length)
                            {
                                pArray.reset (
                                    new char[length * sizeof (MI_Instance*)]);
                                pTemp = reinterpret_cast<MI_Instance** const> (
                                    pArray.get ());
                                ppInstanceArray.reset (
                                    new MI_InstancePtr[length]);
                                for (int i = 0;
                                     EXIT_SUCCESS == rval && i < length;
                                     ++i)
                                {
                                    rval = allocate_MI_Instance (
                                        pContext, pInstanceOut, name,
                                        &ppInstanceArray[i]);
                                    if (EXIT_SUCCESS == rval)
                                    {
                                        rval = recv (pContext,
                                                     ppInstanceArray[i].get ());
                                    }
                                    if (EXIT_SUCCESS == rval)
                                    {
                                        pTemp[i] = ppInstanceArray[i].get ();
                                    }
                                }
                            }
                            if (EXIT_SUCCESS == rval)
                            {
                                val.instancea.data = pTemp;
                                val.instancea.size =
                                    static_cast<MI_Uint32> (length);
                            }
                        }
                    }
                    break;
                case MI_REFERENCE:
                case MI_REFERENCEA:
                    strm << __FILE__ << '[' << __LINE__ << ']'
                         << "encountered a non-standard param type: " << type;
                    SCX_BOOKEND_PRINT (strm.str ());
                    std::cerr << strm.str () << std::endl;
                    strm.str ("");
                    strm.clear ();
                    rval = EXIT_FAILURE;
                    break;
                default:
                    strm << __FILE__ << '[' << __LINE__ << ']'
                         << "encountered an unknown param type: " << type;
                    SCX_BOOKEND_PRINT (strm.str ());
                    std::cerr << strm.str () << std::endl;
                    strm.str ("");
                    strm.clear ();
                    rval = EXIT_FAILURE;
                    break;
                }
                if (EXIT_SUCCESS == rval)
                {
                    MI_Value tmpVal;
                    MI_Type tmpType;
                    MI_Result result = MI_Instance_GetElement (
                        pInstanceOut, name.c_str (), &tmpVal, &tmpType, NULL,
                        NULL);
                    if (MI_RESULT_NO_SUCH_PROPERTY == result)
                    {
                        result = MI_Instance_AddElement (
                            pInstanceOut, name.c_str (), &val,
                            static_cast<MI_Type>(type), 0);
                    }
                    else
                    {
                        result = MI_Instance_SetElement (
                            pInstanceOut, name.c_str (), &val,
                            static_cast<MI_Type>(type), 0);
                    }
                    if (MI_RESULT_OK == result)
                    {
                        strm << "value added - name: \"" << name
                             << "\" - type: " << static_cast<int>(type);
                    }
                    else
                    {
                        strm << "Failed to add value: \"" << name << "\" type: "
                             << static_cast<int>(type) << " to instance";
                        rval = EXIT_FAILURE;
                    }
                    SCX_BOOKEND_PRINT (strm.str ());
                    strm.str ("");
                    strm.clear ();
                }
                else
                {
                    SCX_BOOKEND_PRINT ("Failed to read value");
                }
            }
        }
        else
        {
            SCX_BOOKEND_PRINT ("Failed to read type");
        }
    }
    else
    {
        SCX_BOOKEND_PRINT ("Failed to read name");
    }
    return rval;
}


int
PythonProvider::recv (
    MI_Context* const pContext,
    MI_Instance* const pInstanceOut)
{
    SCX_BOOKEND ("PythonProvider::recv (MI_Instance)");
    int nItems;
    int rval = recv (&nItems);
    for (int i = 0; EXIT_SUCCESS == rval && i < nItems; ++i)
    {
        rval = recv_MI_Value (pContext, pInstanceOut);
    }
    return rval;
}


} // namespace scx
