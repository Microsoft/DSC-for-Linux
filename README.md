# PowerShell Desired State Configuration for Linux
[![Build Status](https://travis-ci.org/Microsoft/PowerShell-DSC-for-Linux.svg?branch=master)](https://travis-ci.org/ericgable/PowerShell-DSC-for-Linux)

*Copyright (c) Microsoft Corporation ver. 1.1.1*

*All rights reserved.* 

MIT License

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED *AS IS*, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

# Getting Started
## Latest Release ##
The latest release packages for PowerShell DSC for Linux can be downloaded here:
[Releases](https://github.com/Microsoft/PowerShell-DSC-for-Linux/releases)

## Current status and roadmap for Linux DSC
DSC has shifted to a cross-platform implementation that is developed in the
[PSDesiredStateConfiguration](https://github.com/powershell/psdesiredstateconfiguration)
repo. Unless you have specific needs that require version 1, please navigate to the new location to view the current work on version 3.

For more information, please see the
[doumentation for Desired State Configuration](https://learn.microsoft.com/powershell/dsc/overview?view=dsc-3.0).

**This repo has been archived and will no longer accept new issues or pull requests.**

Three solutions were originally released from this repo:
 
- A stand-alone installation of version 1 for Linux distros and versions
  - UPDATE: The updated DSC resources for Linux are now maintained as the [nxTools module](https://www.powershellgallery.com/packages/nxtools/).
- Microsoft Azure Linux DSC extension for virtual machines
  - UPDATE: [Desired state configuration VM Extension for Linux, and Azure Automation DSC support for Linux, will be retired on 30 September 2023](https://azure.microsoft.com/updates/migrate-from-linux-dsc-extension-to-the-guest-configuration-feature-of-azure-policy-by-may-1-2025/)
- Microsoft Monitoring Agent solutions on Linux virtual machines (aka OMS Linux)
  - UPDATE: [We're retiring the Log Analytics agent in Azure Monitor on 31 August 2024](https://azure.microsoft.com/en-us/updates/were-retiring-the-log-analytics-agent-in-azure-monitor-on-31-august-2024/)

---
 
The codebase in this repo has been updated with fixes based on customer
incidents reported for Microsoft Monitoring Agent.
Therefore, the Microsoft Monitoring Agent already utilizes the latest release.
Unfortunately, it has not been possible to publish new releases
for either standalone installations or DSC extension.
If you encounter challenges with either scenario
and would like to evaluate if the latest code will resolve your issue,
the best option is to download the latest release,
then distribute it using custom tools (such as Azure Custom Script Extension).
This is not intended to expand upon the supported OS coverage.

Please note that
[per the documentation for DSC Linux Extension](https://docs.microsoft.com/en-us/azure/virtual-machines/extensions/dsc-linux),
the extensions for DSC Linux and Monitoring cannot be used
together on the same machine.
 
## Supported Linux operating systems
The following Linux operating system versions are supported by DSC for Linux. 
- CentOS 5, 6, and 7 (x86/x64)
- Debian GNU/Linux 6, 7 and 8 (x86/x64) 
- Oracle Linux 5, 6 and 7 (x86/x64) 
- Red Hat Enterprise Linux Server 5, 6 and 7 (x86/x64) 
- SUSE Linux Enterprise Server 10, 11 and 12 (x86/x64) 
- Ubuntu Server 12.04 LTS, 14.04 LTS, 16.04 LTS (x86/x64) 

## Requirements
The following table describes the required package dependencies for DSC for Linux.

 **Required package**	| **Description**			| **Minimum version**
-----------------------	| -------------------------------------	| -------------------
`glibc`			| GNU C Library				| 2.4 - 31.30
`python`		| Python				| 2.4 - 3.4
`omi`			| Open Management Infrastructure	| 1.0.8-4
`openssl`		| OpenSSL Libraries			| 0.9.8e or 1.0
`python-ctypes`		| Python CTypes library			| Must match Python version
`libcurl`		| cURL http client library		| 7.15.1

OMI Packages can be found at [OMI](https://github.com/Microsoft/omi/releases "OMI Releases").

## Installing DSC Packages
OMI and DSC packages are available in RPM and Debian packages, for x86 and x64 architectures, and for systems with OpenSSL version 0.9.8 and version 1.0.x. To install DSC, select determine the packages that are correct for your operating system, and install them. 

**Examples**
*Red Hat Enterprise Linux, CentOS, or Oracle Linux 7:*
```sh
wget https://github.com/Microsoft/omi/releases/download/v1.1.0-0/omi-1.1.0.ssl_100.x64.rpm
wget https://github.com/Microsoft/PowerShell-DSC-for-Linux/releases/download/v1.1.1-294/dsc-1.1.1-294.ssl_100.x64.rpm

sudo rpm -Uvh omi-1.1.0.ssl_100.x64.rpm dsc-1.1.1-294.ssl_100.x64.rpm
```

*Ubuntu 14.04 LTS, 16.04 LTS, or Debian GNU/Linux 8, x64:*
```sh
wget https://github.com/Microsoft/omi/releases/download/v1.1.0-0/omi-1.1.0.ssl_100.x64.deb
wget https://github.com/Microsoft/PowerShell-DSC-for-Linux/releases/download/v1.1.1-294/dsc-1.1.1-294.ssl_100.x64.deb

sudo dpkg -i omi-1.1.0.ssl_100.x64.deb dsc-1.1.1-294.ssl_100.x64.deb
```

**For more information, review the latest [release notes](https://github.com/Microsoft/PowerShell-DSC-for-Linux/releases/tag/v1.1.1-294) and [product documentation](https://msdn.microsoft.com/en-us/powershell/dsc/lnxgettingstarted).**

## To author DSC MOF configuration for Linux on a Windows computer:

----------


**Prerequisites**

* A Windows computer with:
  * Adminstrative privileges
  * Windows PowerShell (>=4.0)
 
*or*
* A Linux computer with:
  * [PowerShell v6.0.0-alpha.9 or later](https://github.com/PowerShell/PowerShell/releases)
  * DSC v1.1.1 or later


* Install the Linux Resource Provider MOF module:
  * The "nx" module can be installed from the PowerShell Gallery with:
	`install-module nx`
   * In order to compile a Configuration MOF that uses the DSC for Linux resources, use `Import-DscResource -Module nx` inside a DSC Configuration block.
  
* Remotely managing a Linux system with DSC
   * You need a compiled configuration MOF to apply a new configuration to a system.  
        `Start-DscConfiguration -CimSession:$myCimSession -Path:"C:\path_to_compiled_mof_directory\" -Wait -Verbose`
   * You can get the current configuration of the system by running:
        `Get-DscConfiguration -CimSession:$myCimSession`
   * You can test the current configuration of the system by running:
       `Test-DscConfiguration -CimSession:$myCimSession`
   * For more information on creating a CimSession for use with the `-CimSession` parameter, see: http://technet.microsoft.com/en-us/library/jj590760.aspx

* Locally managing a Linux system with DSC
	* See [Performing DSC Operations from the Linux Computer](#performing-dsc-operations-from-the-linux-computer) below for a reference of DSC operations that can be performed on the managed computer


## Building the Desired State Configuration (DSC) Local Configuration Manager and Linux Resource Providers

### Prerequisites

----------

* At least one modern Linux system with:
	* root login capability
	* These build tools:
		* GNU Make
		* `g++`
		* Python version 2.5 or later, the package `python-devel`
	* Open Management Infrastructure (OMI) 1.0.8. http://theopengroup.org/software/omi
		* `pam-devel`
		* `openssl-devel`


### Building and installing the Local Configuration Manager and Linux Resource Providers

----------
1. Extract PSDSC.tar into a directory that you will build it from.
2. Download and extract OMI 1.0.8 into a directory named `omi-1.0.8` in a directory parallel to the LCM and Providers directory.  The directory tree should look something like:
```
   ./configure
   ./LCM
   ./license.txt
   ./omi-1.0.8
   ./omi-1.0.8/agent
   ./omi-1.0.8/base
   ...
```
3. Building
	* Configuring OMI and building
    	* Configure OMI with desired options (refer to OMI documentation for this step).
    		* The default configuration installs to `/opt/omi-1.0.8`
     		* To use the default configuration, run: `cd omi-1.0.8 && ./configure`
      	* Run: `make`
	* Installing OMI:
      *  Run: `cd omi-1.0.8 && sudo ./output/install`
	* Registering the LCM + nxProviders with OMI:
      *  Run: `sudo make reg`

4. Running OMI
	* On the Linux system, run `omiserver` with environment variable `OMI_HOME` set to OMI's installed directory
    	* Run as root: `OMI_HOME=<PATH_TO_INSTALLED_OMI_DIR> $OMI_HOME/bin/omiserver`
      	* The default installation for OMI 1.0.8 is `/opt/omi-1.0.8`.  Thus, for default installations, part 4Ai above becomes:
            `OMI_HOME=/opt/omi-1.0.8 $OMI_HOME/bin/omiserver`
   	> Note: In order to run following reboots, it is recommended to configure OMI as a System-V, Upstart, or SystemD daemon 

## Building and using DSC and OMI from source

DSC and OMI can also be built together entirely from source in a self-contained directory. This is useful primarily for developers.

```sh
# Clone DSC source
git clone https://github.com/Microsoft/PowerShell-DSC-for-Linux.git
cd PowerShell-DSC-for-Linux

# Place the OMI source where DSC expects it
# Alternatively clone from Git and symlink to omi/Unix
wget https://collaboration.opengroup.org/omi/documents/33715/omi-1.0.8.tar.gz
tar xf omi-1.0.8.tar.gz

# Build OMI in developer mode
cd omi-1.0.8
./configure --dev
make -j
cd ..

# Build DSC in developer mode
./configure --no-rpm --no-dpkg --local
make -j
make reg

# Start the OMI server
./omi-1.0.8/output/bin/omiserver
```

### Use Azure Automation as a DSC Pull Server
Note: For more information on Azure Automation’s DSC features, reference the [documentation](https://azure.microsoft.com/en-us/documentation/articles/automation-dsc-overview/).  
Linux computers can be onboarded to Azure Automation DSC, as long as they have outbound access to the internet, via a few simple steps: 

Make sure version 1.1 or later of the DSC Linux agent is installed on the machines you want to onboard to Azure Automation DSC.

**To configure Azure Automation as a DSC Pull Server from the Linux computer:**

- On each Linux machine to onboard to Azure Automation DSC, use Register.py to onboard using the PowerShell DSC Local Configuration Manager defaults:  

#### For python2
```sudo /opt/microsoft/dsc/Scripts/Register.py <Automation account registration key> <Automation account registration URL>  ```
#### For python3
```sudo /opt/microsoft/dsc/Scripts/python3/Register.py <Automation account registration key> <Automation account registration URL>  ```

- To find the registration key and registration URL for your Automation account, see the Secure Registration section below.
- Using the Azure portal or cmdlets, check that the machines to onboard now show up as DSC nodes registered in your Azure Automation account. 

Additional configuration options:
- --ConfigurationName: the name of the configuration to apply
- --RefreshFrequencyMins: Specifies how often (in minutes) LCM attempts to obtain the configuration from the pull server. If configuration on the pull server differs from the current one on the target node, it is copied to the pending store and applied.
- --ConfigurationModeFrequencyMins: Specifies how often (in minutes) LCM ensures that the configuration is in the desired state.
- --ConfigurationMode: Specifies how LCM should apply the configuration. Valid values are: ApplyOnly, ApplyAndMonitor, ApplyAndAutoCorrect, MonitorOnly

**To configure Azure Automation as a DSC Pull Server with a metaconfiguration MOF:**
- Open the PowerShell console or PowerShell ISE as an administrator on a Windows machine in your local environment. This machine must have the latest version of WMF 5 installed
- Connect to Azure Resource Manager using the Azure PowerShell module: 
```
Add-AzureAccount 
Switch-AzureMode AzureResourceManager
```
- Download, from the Automation account you want to onboard nodes to, the PowerShell DSC metaconfigurations for the machines you want to onboard: 
```Get-AzureAutomationDscOnboardingMetaconfig -ResourceGroupName MyResourceGroup AutomationAccountName MyAutomationAccount -ComputerName MyServer1, MyServer2 OutputFolder C:\Users\joe\Desktop```
 
- Optionally, view and update the metaconfigurations in the output folder as needed to match the [PowerShell DSC Local Configuration Manager ](https://technet.microsoft.com/library/dn249922.aspx?f=255&MSPPError=-2147217396)fields and values you want, if the defaults do not match your use case.

- Remotely apply the PowerShell DSC metaconfiguration to the machines you want to onboard: 

```
$SecurePass = ConvertTo -SecureString -string "<root password>" -AsPlainText Force 
$Cred = New-Object System.Management.Automation.PSCredential "root" , $SecurePass 
$Opt = New-CimSessionOption -UseSsl: $true -SkipCACheck: $true -SkipCNCheck: $true -SkipRevocationCheck: $true 
 
$Session = New-CimSession -Credential: $Cred -ComputerName: <your Linux machine > -Port: 5986 -Authentication: basic -SessionOption: $Opt 
Set-DscLocalConfigurationManager -CimSession $Session –Path C:\Users\joe\Desktop\DscMetaConfigs
``` 
- If you cannot apply the PowerShell DSC metaconfigurations remotely, for each Linux machine to onboard, copy the metaconfiguration corresponding to that machine from the folder in step 5 onto the Linux machine. Then call SetDscLocalConfigurationManager.py locally on each Linux machine to onboard to Azure Automation DSC:  

#### For python2
```
/opt/microsoft/dsc/Scripts/SetDscLocalConfigurationManager.py –configurationmof <path to metaconfiguration file> 
```
#### For python3
```
/opt/microsoft/dsc/Scripts/python3/SetDscLocalConfigurationManager.py –configurationmof <path to metaconfiguration file> 
```
- Using the Azure portal or cmdlets, check that the machines to onboard now show up as DSC nodes registered in your Azure Automation account.  

### Importing resource modules to Azure Automation
The supplied resource modules with this release (nxNetworking, nxComputerManagement) can be imported to Azure Automation for distribution with DSC configurations. To import to Azure Automation, rename the .zip files to remove the _X.Y version string from the file name. Such as: nxNetworking.zip and nxComputerManagement.zip. 

## Performing DSC Operations from the Linux Computer 
DSC for Linux includes scripts to work with configuration from the local Linux computer. These scripts are located in `/opt/microsoft/dsc/Scripts` for python2 and `/opt/microsoft/dsc/Scripts/python3` for python3 and include the following:

**GetDscConfiguration.py**
Returns the current configuration applied to the computer. Similar to the Windows PowerShell cmdlet Get-DscConfiguration cmdlet. 
`sudo ./GetDscConfiguration.py` 

**GetDscLocalConfigurationManager.py**
Returns the current meta-configuration applied to the computer. Similar to the Windows PowerShell cmdlet Get-DSCLocalConfigurationManager 
`sudo ./GetDscLocalConfigurationManager.py` 

**PerformRequiredConfigurationChecks.py**
Immediately checks the configuration in accordance with the MetaConfiguration settings and applies the configuration if an update is available. Useful for immediately applying configuration changes on the pull server. 
`sudo ./PerformRequiredConfigurationChecks.py`

**RestoreConfiguration.py** 
Applies the previous configuration known to DSC, a rollback.  
`sudo ./RestoreConfiguration.py`

**SetDscLocalConfigurationManager.py**
Applies a Meta Configuration MOF file to the computer. Similar to the Windows PowerShell cmdlet: Set-DSCLocalConfigurationManager. Requires the path to the Meta Configuration MOF to apply. 
`sudo ./SetDscLocalConfigurationManager.py –configurationmof /tmp/localhost.meta.mof`

**StartDscConfiguration.py** 
Applies a configuration MOF file to the computer. Similar to the Windows PowerShell cmdlet: StartDscConfiguration. Requires the path to the configuration MOF to apply. 
`sudo ./StartDscConfiguration.py –-configurationmof /tmp/localhost.mof`

You can also supply the force parameter to forcibly remove any current pending configuration before applying the new configuration.
`sudo ./StartDscConfiguration.py –-configurationmof /tmp/localhost.mof --force`

**TestDscConfiguration.py** 
Tests the current system configuration for compliance desired state.  Similar to the Windows PowerShell cmdlet: Test-DscConfiguration.  
`sudo ./TestDscConfiguration.py`

**InstallModule.py**
Installs a custom DSC resource module. Requires the path to a .zip file containing the module shared object library and schema MOF files. 
`sudo ./InstallModule.py /tmp/cnx_Resource.zip` 

**RemoveModule.py**
Removes a custom DSC resource module. Requires the name of the module to remove. 
`sudo ./RemoveModule.py cnx_Resource`

## Using PowerShell Desired State Configuration for Linux with a Pull Server 
### Using HTTPS with the Pull Server
Though unencrypted HTTP is supported for communication with the Pull server, HTTPS (SSL/TLS) is recommended. When using HTTPS, the DSC Local Configuration Manager requires that the SSL certificate of the Pull server is verifiable (signed by a trusted authority, has a common name that matches the URL, etc.). 
 
You can modify these HTTPS requirements as needed, by modifying the file /etc/opt/omi/dsc/dsc.conf. The supported properties defined in this file are:  
- **NoSSLv3** set this to true to require the TLS protocol and set this to false to support SSLv3 or TLS. The default is false. 
- **DoNotCheckCertificate** set this to true to ignore SSL certificate verification. The default is false. 
- **CURL_CA_BUNDLE** an optional path to a curl-ca-bundle.crt file containing the CA certificates to trust for SSL/TLS. For more information, see: http://curl.haxx.se/docs/sslcerts.html
- **sslCipherSuite** Optionally set your preferred SSL cipher suite list. Only ciphers matching the rules defined by this list will be supported for HTTPS negotiation. The syntax and available ciphers on your computer depend on whether the cURL package is configured to use OpenSSL or NSS as its SSL library. To determine which SSL library cURL is using, run the following command and look for OpenSSL or NSS in the list of linked libraries: 
```
curl --version |head -n 1 curl 7.29.0 (x86_64-redhat-linux-gnu) libcurl/7.29.0 NSS/3.15.4 zlib/1.2.7 libidn/1.28 libssh2/1.4.3
```  
 *For more information on configuring cipher support, see: http://curl.haxx.se/libcurl/c/CURLOPT_SSL_CIPHER_LIST.html*

## Using an HTTP(s) Proxy Server with DSC ##
DSC for Linux supports the use of an HTTP or HTTPS proxy server when communicating with a Pull Server. To configure a proxy server, edit the file `/etc/opt/omi/conf/dsc/dsc.conf` and add a line starting with `PROXY=`.  The proxy server specification takes the format: The proxy configuration value has the following syntax: `[protocol://][user:password@]proxyhost[:port]`

Property|Description 
---|---- 
Protocol|http or https 
user|Optional username for proxy authentication 
password|Optional password for proxy authentication 
proxyhost|Address or FQDN of the proxy server 
port|Optional port number for the proxy server

**Example**
`PROXY=https://proxyuser:proxypassword@proxyserver01:8080`


## PowerShell Desired State Configuration for Linux Log Files  ##
The following log files are generated for DSC for Linux messages. 

 **Log File** 	| **Directory** 	| **Description**
--------------------- | --------------------- | -------------------
omiserver.log |	/var/opt/omi/log	| Messages relating to the operation of the OMI CIM server. 
dsc.log |	/var/opt/omi/log	| Messages relating to the operation of the Local Configuration Manager and DSC resource operations. 

## Code of Conduct

This project has adopted the [Microsoft Open Source Code of Conduct]
(https://opensource.microsoft.com/codeofconduct/).  For more
information see the [Code of Conduct FAQ]
(https://opensource.microsoft.com/codeofconduct/faq/) or contact
[opencode@microsoft.com](mailto:opencode@microsoft.com) with any
additional questions or comments.
