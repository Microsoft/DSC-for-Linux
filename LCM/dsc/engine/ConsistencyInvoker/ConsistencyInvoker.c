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
#include <string.h>

#define PYTHON_COMMAND "python"
#define PYTHON2_COMMAND "python2"
#define PYTHON3_COMMAND "python3"
#define PYTHON_SCRIPT_NAME "PerformRequiredConfigurationChecks.py"

char* getPythonProvider();

int main(int argc, char *argv[])
{
    char * pythonCommand = PYTHON2_COMMAND;

    char* dscScriptPath = malloc(strlen(DSC_SCRIPT_PATH) + 1);
    dscScriptPath = strcpy(dscScriptPath, DSC_SCRIPT_PATH);
    
    //if oms config check to be added
    if(strstr(dscScriptPath, "omsconfig")!= NULL)
    {
        pythonCommand = getPythonProvider();

        if(strcmp(pythonCommand, PYTHON3_COMMAND) == 0)
        {
            dscScriptPath = realloc(dscScriptPath, strlen(dscScriptPath) + strlen("/python3") + 1 );
            dscScriptPath = strcat(dscScriptPath, "/python3");
        }
    }


    int fullCommandLength = strlen(pythonCommand) + 1 + strlen(dscScriptPath) + 1 + strlen(PYTHON_SCRIPT_NAME) + 1;
    char fullCommand[fullCommandLength];

    strcpy(fullCommand, pythonCommand);
    strcat(fullCommand, " ");
    strcat(fullCommand, dscScriptPath);
    strcat(fullCommand, "/");
    strcat(fullCommand, PYTHON_SCRIPT_NAME);

    int returnValue = system(fullCommand);
    return returnValue;
}


// I may need to move this method in some file which is accessible to all other files in the project.
char* getPythonProvider()
{
    int buffer_length = 128;
    char buffer[buffer_length]; 
    char* result = malloc(1);
    *result = 0; 

    FILE* pipe = popen("python2 --version 2>&1", "r");   
    if(!pipe) {
        printf("Cant start command.");
    }
    while(fgets(buffer, 128, pipe) != NULL) {
        result = realloc(result, (result ? strlen(result) : 0) + buffer_length );
        strcat(result,buffer);
    }

    // If python2 --version does not contain 'not found' return python2
    if(strstr(result, "not found") == NULL) {
    	return PYTHON2_COMMAND;
    }

    // Look for python3
    result = malloc(1);
    *result = 0;
    pipe = popen("python3 --version 2>&1", "r");
    if(!pipe) {
    	printf("Cant start command.");
    }
    while(fgets(buffer, 128, pipe) != NULL) {
        result = realloc(result, (result ? strlen(result) : 0) + buffer_length );
        strcat(result,buffer);
    }

    // If python3 --version does not contain 'not found' return python3
    if(strstr(result, "not found") == NULL) {
	    return PYTHON3_COMMAND;
    }

    return PYTHON_COMMAND;

}
