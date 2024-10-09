SHELL=/bin/bash
include config.mak
-include omi-1.0.8/output/config.mak
UNAME_P := $(shell uname -p)
ifeq ($(UNAME_P),x86_64)
 PF_ARCH := x64
else
 PF_ARCH := x86
endif

current_dir := $(shell pwd)
INSTALLBUILDER_DIR=installbuilder

ifeq ($(BUILD_OMS),BUILD_OMS)
CONFIG_SYSCONFDIR_DSC=omsconfig
DSC_NAMESPACE=root/oms
OAAS_CERTPATH=/etc/opt/microsoft/omsagent/certs/oms.crt
OAAS_KEYPATH=/etc/opt/microsoft/omsagent/certs/oms.key
OAAS_THUMBRPINT=/etc/opt/microsoft/omsagent/certs/oms.thumbprint
PYTHON_PID_DIR=/var/opt/microsoft/omsconfig
BUILD_OMS_VAL=1
else
CONFIG_SYSCONFDIR_DSC=dsc
DSC_NAMESPACE=root/Microsoft/DesiredStateConfiguration
OAAS_CERTPATH=$$CONFIG_CERTSDIR/oaas.crt
OAAS_KEYPATH=$$CONFIG_CERTSDIR/oaas.key
OAAS_THUMBPRINT=$$CONFIG_CERTSDIR/oaas.thumbprint
PYTHON_PID_DIR=/var/opt/omi
BUILD_OMS_VAL=0
endif

all:
	rm -rf release/*.rpm release/*.deb
	mkdir -p intermediate/Scripts
	mkdir -p intermediate/Scripts/python3
	mkdir -p intermediate/Modules
ifeq ($(BUILD_LOCAL),1)
	$(MAKE) local
else
 ifeq ($(PF_ARCH),x64)
	ln -fs $(current_dir)/ext/curl/x64 $(current_dir)/ext/curl/current_platform
 else
	ln -fs $(current_dir)/ext/curl/x86 $(current_dir)/ext/curl/current_platform
 endif
	cd ../pal/build; ./configure --enable-ulinux
 ifeq ($(BUILD_SSL_098),1)
	rm -rf omi-1.0.8/output_openssl_0.9.8/lib/libdsccore.so
	$(MAKE) omi098
	$(MAKE) dsc098
	$(MAKE) dsckit098
 endif
 ifeq ($(BUILD_SSL_100),1)
	rm -rf omi-1.0.8/output_openssl_1.0.0/lib/libdsccore.so
	$(MAKE) omi100
	$(MAKE) dsc100
	$(MAKE) dsckit100
 endif
 ifeq ($(BUILD_SSL_110),1)
	rm -rf omi-1.0.8/output_openssl_1.1.0/lib/libdsccore.so
	$(MAKE) omi110
	$(MAKE) dsc110
	$(MAKE) dsckit110
 endif
 ifeq ($(BUILD_SSL_300),1)
	rm -rf omi-1.0.8/output_openssl_3.0.0/lib/libdsccore.so
	$(MAKE) omi300
	$(MAKE) dsc300
	$(MAKE) dsckit300
 endif

endif


ifeq ($(BUILD_OMS),BUILD_OMS)
dsckit098: nx nxOMSPerfCounter nxOMSSyslog nxOMSKeyMgmt nxOMSPlugin nxOMSCustomLog nxOMSSudoCustomLog nxFileInventory nxOMSGenerateInventoryMof nxOMSAgentNPMConfig nxOMSAutomationWorker nxOMSAuditdPlugin nxOMSContainers nxOMSWLI
else
dsckit098: nx nxNetworking nxComputerManagement nxMySQL
endif
	$(MAKE) -C $(INSTALLBUILDER_DIR) SSL_VERSION=098 BUILD_RPM=$(BUILD_RPM) BUILD_DPKG=$(BUILD_DPKG) BUILD_OMS_VAL=$(BUILD_OMS_VAL)

	-mkdir -p release; \
	cp omi-1.0.8/output_openssl_0.9.8/release/*.{rpm,deb} output/release/*.{rpm,deb} release/

ifeq ($(BUILD_OMS),BUILD_OMS)
dsckit100: nx nxOMSPerfCounter nxOMSSyslog nxOMSKeyMgmt nxOMSPlugin nxOMSCustomLog nxOMSSudoCustomLog nxFileInventory nxOMSGenerateInventoryMof nxOMSAgentNPMConfig nxOMSAutomationWorker nxOMSAuditdPlugin nxOMSContainers nxOMSWLI
else
dsckit100: nx nxNetworking nxComputerManagement nxMySQL
endif
	$(MAKE) -C $(INSTALLBUILDER_DIR) SSL_VERSION=100 BUILD_RPM=$(BUILD_RPM) BUILD_DPKG=$(BUILD_DPKG) BUILD_OMS_VAL=$(BUILD_OMS_VAL)

	-mkdir -p release; \
	cp omi-1.0.8/output_openssl_1.0.0/release/*.{rpm,deb} output/release/*.{rpm,deb} release/

ifeq ($(BUILD_OMS),BUILD_OMS)
dsckit110: nx nxOMSPerfCounter nxOMSSyslog nxOMSKeyMgmt nxOMSPlugin nxOMSCustomLog nxOMSSudoCustomLog nxFileInventory nxOMSGenerateInventoryMof nxOMSAgentNPMConfig nxOMSAutomationWorker nxOMSAuditdPlugin
else
dsckit110: nx nxNetworking nxComputerManagement nxMySQL
endif
	$(MAKE) -C $(INSTALLBUILDER_DIR) SSL_VERSION=110 BUILD_RPM=$(BUILD_RPM) BUILD_DPKG=$(BUILD_DPKG) BUILD_OMS_VAL=$(BUILD_OMS_VAL)

	-mkdir -p release; \
	cp omi-1.0.8/output_openssl_1.1.0/release/*.{rpm,deb} output/release/*.{rpm,deb} release/

ifeq ($(BUILD_OMS),BUILD_OMS)
dsckit300: nx nxOMSPerfCounter nxOMSSyslog nxOMSKeyMgmt nxOMSPlugin nxOMSCustomLog nxOMSSudoCustomLog nxFileInventory nxOMSGenerateInventoryMof nxOMSAgentNPMConfig nxOMSAutomationWorker nxOMSAuditdPlugin
else
dsckit300: nx nxNetworking nxComputerManagement nxMySQL
endif
	$(MAKE) -C $(INSTALLBUILDER_DIR) SSL_VERSION=300 BUILD_RPM=$(BUILD_RPM) BUILD_DPKG=$(BUILD_DPKG) BUILD_OMS_VAL=$(BUILD_OMS_VAL)

	-mkdir -p release; \
	cp omi-1.0.8/output_openssl_3.0.0/release/*.{rpm,deb} output/release/*.{rpm,deb} release/

dsc098: lcm098 providers
	mkdir -p intermediate/Scripts
	mkdir -p intermediate/Scripts/python3
	mkdir -p intermediate/Modules
	.  omi-1.0.8/output_openssl_0.9.8/config.mak; \
	for f in LCM/scripts/*.py LCM/scripts/*.sh Providers/Scripts/*.py Providers/Scripts/*.sh; do \
	  cat $$f | \
	  sed "s@<CONFIG_BINDIR>@$$CONFIG_BINDIR@" | \
	  sed "s@<CONFIG_LIBDIR>@$$CONFIG_LIBDIR@" | \
	  sed "s@<CONFIG_LOCALSTATEDIR>@$$CONFIG_LOCALSTATEDIR@" | \
	  sed "s@<CONFIG_SYSCONFDIR>@$$CONFIG_SYSCONFDIR@" | \
	  sed "s@<CONFIG_SYSCONFDIR_DSC>@$(CONFIG_SYSCONFDIR_DSC)@" | \
	  sed "s@<OAAS_CERTPATH>@$(OAAS_CERTPATH)@" | \
	  sed "s@<OAAS_KEYPATH>@$(OAAS_KEYPATH)@" | \
	  sed "s@<OAAS_THUMBPRINT>@$(OAAS_THUMBPRINT)@" | \
	  sed "s@<OMI_LIB_SCRIPTS>@$$CONFIG_LIBDIR/Scripts@" | \
	  sed "s@<PYTHON_PID_DIR>@$(PYTHON_PID_DIR)@" | \
	  sed "s@<DSC_NAMESPACE>@$(DSC_NAMESPACE)@" | \
	  sed "s@<DSC_SCRIPT_PATH>@$(DSC_SCRIPT_PATH)@" | \
	  sed "s@<DSC_HOST_BASE_PATH>@$(DSC_HOST_BASE_PATH)@" | \
	  sed "s@<DSC_MODULES_PATH>@$(DSC_MODULES_PATH)@" > intermediate/Scripts/`basename $$f`; \
	  chmod a+x intermediate/Scripts/`basename $$f`; \
	done
	for f in LCM/scripts/python3/*.py LCM/scripts/python3/*.sh; do \
	  cat $$f | \
	  sed "s@<CONFIG_BINDIR>@$$CONFIG_BINDIR@" | \
	  sed "s@<CONFIG_LIBDIR>@$$CONFIG_LIBDIR@" | \
	  sed "s@<CONFIG_LOCALSTATEDIR>@$$CONFIG_LOCALSTATEDIR@" | \
	  sed "s@<CONFIG_SYSCONFDIR>@$$CONFIG_SYSCONFDIR@" | \
	  sed "s@<CONFIG_SYSCONFDIR_DSC>@$(CONFIG_SYSCONFDIR_DSC)@" | \
	  sed "s@<OAAS_CERTPATH>@$(OAAS_CERTPATH)@" | \
	  sed "s@<OAAS_KEYPATH>@$(OAAS_KEYPATH)@" | \
	  sed "s@<OAAS_THUMBPRINT>@$(OAAS_THUMBPRINT)@" | \
	  sed "s@<OMI_LIB_SCRIPTS>@$$CONFIG_LIBDIR/Scripts@" | \
	  sed "s@<PYTHON_PID_DIR>@$(PYTHON_PID_DIR)@" | \
	  sed "s@<DSC_NAMESPACE>@$(DSC_NAMESPACE)@" | \
	  sed "s@<DSC_SCRIPT_PATH>@$(DSC_SCRIPT_PATH)@" | \
	  sed "s@<DSC_HOST_BASE_PATH>@$(DSC_HOST_BASE_PATH)@" | \
	  sed "s@<DSC_MODULES_PATH>@$(DSC_MODULES_PATH)@" > intermediate/Scripts/python3/`basename $$f`; \
	  chmod a+x intermediate/Scripts/python3/`basename $$f`; \
	done
	if [ -f ../dsc.version ]; then cp -f ../dsc.version build/dsc.version; else cp -f build/Makefile.version build/dsc.version; fi


dsc100: lcm100 providers
	mkdir -p intermediate/Scripts
	mkdir -p intermediate/Scripts/python3
	mkdir -p intermediate/Modules
	.  omi-1.0.8/output_openssl_1.0.0/config.mak; \
	for f in LCM/scripts/*.py LCM/scripts/*.sh Providers/Scripts/*.py Providers/Scripts/*.sh; do \
	  cat $$f | \
	  sed "s@<CONFIG_BINDIR>@$$CONFIG_BINDIR@" | \
	  sed "s@<CONFIG_LIBDIR>@$$CONFIG_LIBDIR@" | \
	  sed "s@<CONFIG_LOCALSTATEDIR>@$$CONFIG_LOCALSTATEDIR@" | \
	  sed "s@<CONFIG_SYSCONFDIR>@$$CONFIG_SYSCONFDIR@" | \
	  sed "s@<CONFIG_SYSCONFDIR_DSC>@$(CONFIG_SYSCONFDIR_DSC)@" | \
	  sed "s@<OAAS_CERTPATH>@$(OAAS_CERTPATH)@" | \
	  sed "s@<OAAS_KEYPATH>@$(OAAS_KEYPATH)@" | \
	  sed "s@<OAAS_THUMBPRINT>@$(OAAS_THUMBPRINT)@" | \
	  sed "s@<OMI_LIB_SCRIPTS>@$$CONFIG_LIBDIR/Scripts@" | \
	  sed "s@<PYTHON_PID_DIR>@$(PYTHON_PID_DIR)@" | \
	  sed "s@<DSC_NAMESPACE>@$(DSC_NAMESPACE)@" | \
	  sed "s@<DSC_SCRIPT_PATH>@$(DSC_SCRIPT_PATH)@" | \
	  sed "s@<DSC_HOST_BASE_PATH>@$(DSC_HOST_BASE_PATH)@" | \
	  sed "s@<DSC_MODULES_PATH>@$(DSC_MODULES_PATH)@" > intermediate/Scripts/`basename $$f`; \
	  chmod a+x intermediate/Scripts/`basename $$f`; \
	done
	for f in LCM/scripts/python3/*.py LCM/scripts/python3/*.sh; do \
	  cat $$f | \
	  sed "s@<CONFIG_BINDIR>@$$CONFIG_BINDIR@" | \
	  sed "s@<CONFIG_LIBDIR>@$$CONFIG_LIBDIR@" | \
	  sed "s@<CONFIG_LOCALSTATEDIR>@$$CONFIG_LOCALSTATEDIR@" | \
	  sed "s@<CONFIG_SYSCONFDIR>@$$CONFIG_SYSCONFDIR@" | \
	  sed "s@<CONFIG_SYSCONFDIR_DSC>@$(CONFIG_SYSCONFDIR_DSC)@" | \
	  sed "s@<OAAS_CERTPATH>@$(OAAS_CERTPATH)@" | \
	  sed "s@<OAAS_KEYPATH>@$(OAAS_KEYPATH)@" | \
	  sed "s@<OAAS_THUMBPRINT>@$(OAAS_THUMBPRINT)@" | \
	  sed "s@<OMI_LIB_SCRIPTS>@$$CONFIG_LIBDIR/Scripts@" | \
	  sed "s@<PYTHON_PID_DIR>@$(PYTHON_PID_DIR)@" | \
	  sed "s@<DSC_NAMESPACE>@$(DSC_NAMESPACE)@" | \
	  sed "s@<DSC_SCRIPT_PATH>@$(DSC_SCRIPT_PATH)@" | \
	  sed "s@<DSC_HOST_BASE_PATH>@$(DSC_HOST_BASE_PATH)@" | \
	  sed "s@<DSC_MODULES_PATH>@$(DSC_MODULES_PATH)@" > intermediate/Scripts/python3/`basename $$f`; \
	  chmod a+x intermediate/Scripts/python3/`basename $$f`; \
	done
	if [ -f ../dsc.version ]; then cp -f ../dsc.version build/dsc.version; else cp -f build/Makefile.version build/dsc.version; fi

dsc110: lcm110 providers
	mkdir -p intermediate/Scripts
	mkdir -p intermediate/Scripts/python3
	mkdir -p intermediate/Modules
	.  omi-1.0.8/output_openssl_1.1.0/config.mak; \
	for f in LCM/scripts/*.py LCM/scripts/*.sh Providers/Scripts/*.py Providers/Scripts/*.sh; do \
	  cat $$f | \
	  sed "s@<CONFIG_BINDIR>@$$CONFIG_BINDIR@" | \
	  sed "s@<CONFIG_LIBDIR>@$$CONFIG_LIBDIR@" | \
	  sed "s@<CONFIG_LOCALSTATEDIR>@$$CONFIG_LOCALSTATEDIR@" | \
	  sed "s@<CONFIG_SYSCONFDIR>@$$CONFIG_SYSCONFDIR@" | \
	  sed "s@<CONFIG_SYSCONFDIR_DSC>@$(CONFIG_SYSCONFDIR_DSC)@" | \
	  sed "s@<OAAS_CERTPATH>@$(OAAS_CERTPATH)@" | \
	  sed "s@<OAAS_KEYPATH>@$(OAAS_KEYPATH)@" | \
	  sed "s@<OAAS_THUMBPRINT>@$(OAAS_THUMBPRINT)@" | \
	  sed "s@<OMI_LIB_SCRIPTS>@$$CONFIG_LIBDIR/Scripts@" | \
	  sed "s@<PYTHON_PID_DIR>@$(PYTHON_PID_DIR)@" | \
	  sed "s@<DSC_NAMESPACE>@$(DSC_NAMESPACE)@" | \
	  sed "s@<DSC_SCRIPT_PATH>@$(DSC_SCRIPT_PATH)@" | \
	  sed "s@<DSC_HOST_BASE_PATH>@$(DSC_HOST_BASE_PATH)@" | \
	  sed "s@<DSC_MODULES_PATH>@$(DSC_MODULES_PATH)@" > intermediate/Scripts/`basename $$f`; \
	  chmod a+x intermediate/Scripts/`basename $$f`; \
	done

	for f in LCM/scripts/python3/*.py LCM/scripts/python3/*.sh; do \
	  cat $$f | \
	  sed "s@<CONFIG_BINDIR>@$$CONFIG_BINDIR@" | \
	  sed "s@<CONFIG_LIBDIR>@$$CONFIG_LIBDIR@" | \
	  sed "s@<CONFIG_LOCALSTATEDIR>@$$CONFIG_LOCALSTATEDIR@" | \
	  sed "s@<CONFIG_SYSCONFDIR>@$$CONFIG_SYSCONFDIR@" | \
	  sed "s@<CONFIG_SYSCONFDIR_DSC>@$(CONFIG_SYSCONFDIR_DSC)@" | \
	  sed "s@<OAAS_CERTPATH>@$(OAAS_CERTPATH)@" | \
	  sed "s@<OAAS_KEYPATH>@$(OAAS_KEYPATH)@" | \
	  sed "s@<OAAS_THUMBPRINT>@$(OAAS_THUMBPRINT)@" | \
	  sed "s@<OMI_LIB_SCRIPTS>@$$CONFIG_LIBDIR/Scripts@" | \
	  sed "s@<PYTHON_PID_DIR>@$(PYTHON_PID_DIR)@" | \
	  sed "s@<DSC_NAMESPACE>@$(DSC_NAMESPACE)@" | \
	  sed "s@<DSC_SCRIPT_PATH>@$(DSC_SCRIPT_PATH)@" | \
	  sed "s@<DSC_HOST_BASE_PATH>@$(DSC_HOST_BASE_PATH)@" | \
	  sed "s@<DSC_MODULES_PATH>@$(DSC_MODULES_PATH)@" > intermediate/Scripts/python3/`basename $$f`; \
	  chmod a+x intermediate/Scripts/python3/`basename $$f`; \
	done
	if [ -f ../dsc.version ]; then cp -f ../dsc.version build/dsc.version; else cp -f build/Makefile.version build/dsc.version; fi

dsc300: lcm300 providers
	mkdir -p intermediate/Scripts
	mkdir -p intermediate/Scripts/python3
	mkdir -p intermediate/Modules
	.  omi-1.0.8/output_openssl_3.0.0/config.mak; \
	for f in LCM/scripts/*.py LCM/scripts/*.sh Providers/Scripts/*.py Providers/Scripts/*.sh; do \
	  cat $$f | \
	  sed "s@<CONFIG_BINDIR>@$$CONFIG_BINDIR@" | \
	  sed "s@<CONFIG_LIBDIR>@$$CONFIG_LIBDIR@" | \
	  sed "s@<CONFIG_LOCALSTATEDIR>@$$CONFIG_LOCALSTATEDIR@" | \
	  sed "s@<CONFIG_SYSCONFDIR>@$$CONFIG_SYSCONFDIR@" | \
	  sed "s@<CONFIG_SYSCONFDIR_DSC>@$(CONFIG_SYSCONFDIR_DSC)@" | \
	  sed "s@<OAAS_CERTPATH>@$(OAAS_CERTPATH)@" | \
	  sed "s@<OAAS_KEYPATH>@$(OAAS_KEYPATH)@" | \
	  sed "s@<OAAS_THUMBPRINT>@$(OAAS_THUMBPRINT)@" | \
	  sed "s@<OMI_LIB_SCRIPTS>@$$CONFIG_LIBDIR/Scripts@" | \
	  sed "s@<PYTHON_PID_DIR>@$(PYTHON_PID_DIR)@" | \
	  sed "s@<DSC_NAMESPACE>@$(DSC_NAMESPACE)@" | \
	  sed "s@<DSC_SCRIPT_PATH>@$(DSC_SCRIPT_PATH)@" | \
	  sed "s@<DSC_HOST_BASE_PATH>@$(DSC_HOST_BASE_PATH)@" | \
	  sed "s@<DSC_MODULES_PATH>@$(DSC_MODULES_PATH)@" > intermediate/Scripts/`basename $$f`; \
	  chmod a+x intermediate/Scripts/`basename $$f`; \
	done

	for f in LCM/scripts/python3/*.py LCM/scripts/python3/*.sh; do \
	  cat $$f | \
	  sed "s@<CONFIG_BINDIR>@$$CONFIG_BINDIR@" | \
	  sed "s@<CONFIG_LIBDIR>@$$CONFIG_LIBDIR@" | \
	  sed "s@<CONFIG_LOCALSTATEDIR>@$$CONFIG_LOCALSTATEDIR@" | \
	  sed "s@<CONFIG_SYSCONFDIR>@$$CONFIG_SYSCONFDIR@" | \
	  sed "s@<CONFIG_SYSCONFDIR_DSC>@$(CONFIG_SYSCONFDIR_DSC)@" | \
	  sed "s@<OAAS_CERTPATH>@$(OAAS_CERTPATH)@" | \
	  sed "s@<OAAS_KEYPATH>@$(OAAS_KEYPATH)@" | \
	  sed "s@<OAAS_THUMBPRINT>@$(OAAS_THUMBPRINT)@" | \
	  sed "s@<OMI_LIB_SCRIPTS>@$$CONFIG_LIBDIR/Scripts@" | \
	  sed "s@<PYTHON_PID_DIR>@$(PYTHON_PID_DIR)@" | \
	  sed "s@<DSC_NAMESPACE>@$(DSC_NAMESPACE)@" | \
	  sed "s@<DSC_SCRIPT_PATH>@$(DSC_SCRIPT_PATH)@" | \
	  sed "s@<DSC_HOST_BASE_PATH>@$(DSC_HOST_BASE_PATH)@" | \
	  sed "s@<DSC_MODULES_PATH>@$(DSC_MODULES_PATH)@" > intermediate/Scripts/python3/`basename $$f`; \
	  chmod a+x intermediate/Scripts/python3/`basename $$f`; \
	done
	if [ -f ../dsc.version ]; then cp -f ../dsc.version build/dsc.version; else cp -f build/Makefile.version build/dsc.version; fi

omi098:
	$(MAKE) configureomi098
	rm -rf omi-1.0.8/output
	ln -s output_openssl_0.9.8 omi-1.0.8/output
	$(MAKE) -C omi-1.0.8
	$(MAKE) -C omi-1.0.8/installbuilder SSL_VERSION=098 BUILD_RPM=$(BUILD_RPM) BUILD_DPKG=$(BUILD_DPKG) SSL_BUILD=0.9.8

omi100:
	$(MAKE) configureomi100
	rm -rf omi-1.0.8/output
	ln -s output_openssl_1.0.0 omi-1.0.8/output
	$(MAKE) -C omi-1.0.8
	$(MAKE) -C omi-1.0.8/installbuilder SSL_VERSION=100 BUILD_RPM=$(BUILD_RPM) BUILD_DPKG=$(BUILD_DPKG) SSL_BUILD=1.0.0

omi110:
	$(MAKE) configureomi110
	rm -rf omi-1.0.8/output
	ln -s output_openssl_1.1.0 omi-1.0.8/output
	$(MAKE) -C omi-1.0.8
	$(MAKE) -C omi-1.0.8/installbuilder SSL_VERSION=110 BUILD_RPM=$(BUILD_RPM) BUILD_DPKG=$(BUILD_DPKG) SSL_BUILD=1.1.0

omi300:
	$(MAKE) configureomi300
	rm -rf omi-1.0.8/output
	ln -s output_openssl_3.0.0 omi-1.0.8/output
	$(MAKE) -C omi-1.0.8
	$(MAKE) -C omi-1.0.8/installbuilder SSL_VERSION=300 BUILD_RPM=$(BUILD_RPM) BUILD_DPKG=$(BUILD_DPKG) SSL_BUILD=3.0.0

configureomi098:
	(cd omi-1.0.8; ./configure $(DEBUG_FLAGS) --enable-preexec --prefix=/opt/omi --outputdirname=output_openssl_0.9.8 --localstatedir=/var/opt/omi --sysconfdir=/etc/opt/omi/conf --certsdir=/etc/opt/omi/ssl --opensslcflags="$(openssl098_cflags)" --openssllibs="-L$(current_dir)/ext/curl/current_platform/lib $(openssl098_libs)" --openssllibdir="$(openssl098_libdir)")

configureomi100:
	(cd omi-1.0.8; ./configure $(DEBUG_FLAGS) --enable-preexec --prefix=/opt/omi --outputdirname=output_openssl_1.0.0 --localstatedir=/var/opt/omi --sysconfdir=/etc/opt/omi/conf --certsdir=/etc/opt/omi/ssl --opensslcflags="$(openssl100_cflags)" --openssllibs="-L$(current_dir)/ext/curl/current_platform/lib $(openssl100_libs)" --openssllibdir="$(openssl100_libdir)")

configureomi110:
	(cd omi-1.0.8; ./configure $(DEBUG_FLAGS) --enable-preexec --prefix=/opt/omi --outputdirname=output_openssl_1.1.0 --localstatedir=/var/opt/omi --sysconfdir=/etc/opt/omi/conf --certsdir=/etc/opt/omi/ssl --opensslcflags="$(openssl110_cflags)" --openssllibs="-L$(current_dir)/ext/curl/current_platform/lib $(openssl110_libs)" --openssllibdir="$(openssl110_libdir)")

configureomi300:
	(cd omi-1.0.8; ./configure $(DEBUG_FLAGS) --enable-preexec --prefix=/opt/omi --outputdirname=output_openssl_3.0.0 --localstatedir=/var/opt/omi --sysconfdir=/etc/opt/omi/conf --certsdir=/etc/opt/omi/ssl --opensslcflags="$(openssl300_cflags)" --openssllibs="-L$(current_dir)/ext/curl/current_platform/lib $(openssl300_libs)" --openssllibdir="$(openssl300_libdir)")

lcm098:
	$(MAKE) -C LCM

lcm100:
	$(MAKE) -C LCM

lcm110:
	$(MAKE) -C LCM

lcm300:
	$(MAKE) -C LCM

providers:
	$(MAKE) -C Providers

ifeq ($(BUILD_OMS),BUILD_OMS)
nx:
	rm -rf output/staging; \
	VERSION="1.5"; \
	PROVIDERS="nxService nxPackage nxUser nxGroup nxAvailableUpdates"; \
	STAGINGDIR="output/staging/$@/DSCResources"; \
	cat Providers/Modules/$@.psd1 | sed "s@<MODULE_VERSION>@$${VERSION}@" > intermediate/Modules/$@.psd1; \
	for current in $$PROVIDERS; do \
		mkdir -p $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/{2.4x-2.5x,2.6x-2.7x,3.x}/Scripts; \
		cp intermediate/Modules/$@.psd1 output/staging/$@/; \
		cp Providers/$${current}/MSFT_$${current}Resource.schema.mof $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp Providers/$${current}/MSFT_$${current}Resource.reg $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp Providers/bin/libMSFT_$${current}Resource.so $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH); \
		cp Providers/Scripts/2.4x-2.5x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.4x-2.5x/Scripts; \
		cp Providers/Scripts/2.6x-2.7x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.6x-2.7x/Scripts; \
		cp Providers/Scripts/3.x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/3.x/Scripts; \
	done;\
	cd output/staging; \
	cd $@; find . -type f -print0 | xargs -0 sha256sum |grep -v sha256sums > ./$@.sha256sums; cd ..;\
	zip -r $@_$${VERSION}.zip $@; \
	mkdir -p ../../release; \
	mv $@_$${VERSION}.zip ../../release/
else
nx:
	rm -rf output/staging; \
	VERSION="1.5"; \
	PROVIDERS="nxFile nxScript nxUser nxGroup nxService nxPackage nxEnvironment nxSshAuthorizedKeys nxArchive nxFileLine"; \
	STAGINGDIR="output/staging/$@/DSCResources"; \
	cat Providers/Modules/$@.psd1 | sed "s@<MODULE_VERSION>@$${VERSION}@" > intermediate/Modules/$@.psd1; \
	for current in $$PROVIDERS; do \
		mkdir -p $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/{2.4x-2.5x,2.6x-2.7x,3.x}/Scripts; \
		cp intermediate/Modules/$@.psd1 output/staging/$@/; \
		cp Providers/$${current}/MSFT_$${current}Resource.schema.mof $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp Providers/$${current}/MSFT_$${current}Resource.reg $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp Providers/bin/libMSFT_$${current}Resource.so $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH); \
		cp Providers/Scripts/2.4x-2.5x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.4x-2.5x/Scripts; \
		cp Providers/Scripts/2.6x-2.7x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.6x-2.7x/Scripts; \
		cp Providers/Scripts/3.x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/3.x/Scripts; \
	done;\
	cd output/staging; \
	zip -r $@_$${VERSION}.zip $@; \
	mkdir -p ../../release; \
	mv $@_$${VERSION}.zip ../../release/
endif

nxOMSAutomationWorker:
	rm -rf output/staging; \
	VERSION="1.10.0.0"; \
	PROVIDERS="nxOMSAutomationWorker"; \
	STAGINGDIR="output/staging/$@/DSCResources"; \
	cat Providers/Modules/$@.psd1 | sed "s@<MODULE_VERSION>@$${VERSION}@" > intermediate/Modules/$@.psd1; \
	for current in $$PROVIDERS; do \
		mkdir -p $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/{2.4x-2.5x,2.6x-2.7x,3.x}/Scripts; \
		mkdir -p $$STAGINGDIR/MSFT_$${current}Resource/automationworker; \
		cp intermediate/Modules/$@.psd1 output/staging/$@/; \
		cp Providers/$${current}/MSFT_$${current}Resource.schema.mof $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp Providers/$${current}/MSFT_$${current}Resource.reg $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp -r Providers/$${current}/automationworker/* $$STAGINGDIR/MSFT_$${current}Resource/automationworker; \
		cp Providers/bin/libMSFT_$${current}Resource.so $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH); \
		cp Providers/Scripts/2.4x-2.5x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.4x-2.5x/Scripts; \
		cp Providers/Scripts/2.6x-2.7x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.6x-2.7x/Scripts; \
		cp Providers/Scripts/3.x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/3.x/Scripts; \
	done; \
	cd output/staging; \
	cd $@; find . -type f -print0 | xargs -0 sha256sum |grep -v sha256sums > ./$@.sha256sums; cd ..;\
	zip -r $@_$${VERSION}.zip $@; \
	mkdir -p ../../release; \
	mv $@_$${VERSION}.zip ../../release/

nxComputerManagement:
	rm -rf output/staging; \
	VERSION="1.0"; \
	PROVIDERS="nxComputer"; \
	STAGINGDIR="output/staging/$@/DSCResources"; \
	cat Providers/Modules/$@.psd1 | sed "s@<MODULE_VERSION>@$${VERSION}@" > intermediate/Modules/$@.psd1; \
	for current in $$PROVIDERS; do \
		mkdir -p $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/{2.4x-2.5x,2.6x-2.7x,3.x}/Scripts; \
		cp intermediate/Modules/$@.psd1 output/staging/$@/; \
		cp Providers/$${current}/MSFT_$${current}Resource.schema.mof $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp Providers/$${current}/MSFT_$${current}Resource.reg $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp Providers/bin/libMSFT_$${current}Resource.so $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH); \
		cp Providers/Scripts/2.4x-2.5x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.4x-2.5x/Scripts; \
		cp Providers/Scripts/2.6x-2.7x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.6x-2.7x/Scripts; \
		cp Providers/Scripts/3.x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/3.x/Scripts; \
	done;\
	cd output/staging; \
	cd $@; find . -type f -print0 | xargs -0 sha256sum |grep -v sha256sums > ./$@.sha256sums; cd ..;\
	zip -r $@_$${VERSION}.zip $@; \
	mkdir -p ../../release; \
	mv $@_$${VERSION}.zip ../../release/

nxNetworking:
	rm -rf output/staging; \
	VERSION="1.0"; \
	PROVIDERS="nxDNSServerAddress nxIPAddress nxFirewall"; \
	STAGINGDIR="output/staging/$@/DSCResources"; \
	cat Providers/Modules/$@.psd1 | sed "s@<MODULE_VERSION>@$${VERSION}@" > intermediate/Modules/$@.psd1; \
	for current in $$PROVIDERS; do \
		mkdir -p $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/{2.4x-2.5x,2.6x-2.7x,3.x}/Scripts; \
		cp intermediate/Modules/$@.psd1 output/staging/$@/; \
		cp Providers/$${current}/MSFT_$${current}Resource.schema.mof $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp Providers/$${current}/MSFT_$${current}Resource.reg $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp Providers/bin/libMSFT_$${current}Resource.so $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH); \
		cp Providers/Scripts/2.4x-2.5x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.4x-2.5x/Scripts; \
		cp Providers/Scripts/2.6x-2.7x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.6x-2.7x/Scripts; \
		cp Providers/Scripts/3.x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/3.x/Scripts; \
	done;\
	cd output/staging; \
	cd $@; find . -type f -print0 | xargs -0 sha256sum |grep -v sha256sums > ./$@.sha256sums; cd ..;\
	zip -r $@_$${VERSION}.zip $@; \
	mkdir -p ../../release; \
	mv $@_$${VERSION}.zip ../../release/

nxMySQL:
	rm -rf output/staging; \
	VERSION="1.0"; \
	PROVIDERS="nxMySqlDatabase nxMySqlGrant nxMySqlUser"; \
	STAGINGDIR="output/staging/$@/DSCResources"; \
	cat Providers/Modules/$@.psd1 | sed "s@<MODULE_VERSION>@$${VERSION}@" > intermediate/Modules/$@.psd1; \
	for current in $$PROVIDERS; do \
		mkdir -p $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/{2.4x-2.5x,2.6x-2.7x,3.x}/Scripts; \
		cp intermediate/Modules/$@.psd1 output/staging/$@/; \
		cp Providers/$${current}/MSFT_$${current}Resource.schema.mof $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp Providers/$${current}/MSFT_$${current}Resource.reg $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp Providers/bin/libMSFT_$${current}Resource.so $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH); \
		cp Providers/Scripts/2.4x-2.5x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.4x-2.5x/Scripts; \
		cp Providers/Scripts/2.6x-2.7x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.6x-2.7x/Scripts; \
		cp Providers/Scripts/3.x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/3.x/Scripts; \
	done;\
	cd output/staging; \
	cd $@; find . -type f -print0 | xargs -0 sha256sum |grep -v sha256sums > ./$@.sha256sums; cd ..;\
	zip -r $@_$${VERSION}.zip $@; \
	mkdir -p ../../release; \
	mv $@_$${VERSION}.zip ../../release/

nxOMSPerfCounter:
	rm -rf output/staging; \
        VERSION="2.3"; \
        PROVIDERS="nxOMSPerfCounter"; \
        STAGINGDIR="output/staging/$@/DSCResources"; \
        cat Providers/Modules/$@.psd1 | sed "s@<MODULE_VERSION>@$${VERSION}@" > intermediate/Modules/$@.psd1; \
        for current in $$PROVIDERS; do \
                mkdir -p $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/{2.4x-2.5x,2.6x-2.7x,3.x}/Scripts; \
                cp intermediate/Modules/$@.psd1 output/staging/$@/; \
                cp Providers/$${current}/MSFT_$${current}Resource.schema.mof $$STAGINGDIR/MSFT_$${current}Resource/; \
                cp Providers/$${current}/MSFT_$${current}Resource.reg $$STAGINGDIR/MSFT_$${current}Resource/; \
                cp Providers/bin/libMSFT_$${current}Resource.so $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH); \
                cp Providers/Scripts/2.4x-2.5x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.4x-2.5x/Scripts; \
                cp Providers/Scripts/2.6x-2.7x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.6x-2.7x/Scripts; \
                cp Providers/Scripts/3.x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/3.x/Scripts; \
        done;\
        cd output/staging; \
		cd $@; find . -type f -print0 | xargs -0 sha256sum |grep -v sha256sums > ./$@.sha256sums; cd ..;\
        zip -r $@_$${VERSION}.zip $@; \
        mkdir -p ../../release; \
        mv $@_$${VERSION}.zip ../../release/

nxOMSSyslog:
	rm -rf output/staging; \
        VERSION="2.5"; \
        PROVIDERS="nxOMSSyslog"; \
        STAGINGDIR="output/staging/$@/DSCResources"; \
        cat Providers/Modules/$@.psd1 | sed "s@<MODULE_VERSION>@$${VERSION}@" > intermediate/Modules/$@.psd1; \
        for current in $$PROVIDERS; do \
                mkdir -p $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/{2.4x-2.5x,2.6x-2.7x,3.x}/Scripts; \
                cp intermediate/Modules/$@.psd1 output/staging/$@/; \
                cp Providers/$${current}/MSFT_$${current}Resource.schema.mof $$STAGINGDIR/MSFT_$${current}Resource/; \
                cp Providers/$${current}/MSFT_$${current}Resource.reg $$STAGINGDIR/MSFT_$${current}Resource/; \
                cp Providers/bin/libMSFT_$${current}Resource.so $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH); \
                cp Providers/Scripts/2.4x-2.5x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.4x-2.5x/Scripts; \
                cp Providers/Scripts/2.6x-2.7x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.6x-2.7x/Scripts; \
                cp Providers/Scripts/3.x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/3.x/Scripts; \
        done;\
        cd output/staging; \
		cd $@; find . -type f -print0 | xargs -0 sha256sum |grep -v sha256sums > ./$@.sha256sums; cd ..;\
        zip -r $@_$${VERSION}.zip $@; \
        mkdir -p ../../release; \
        mv $@_$${VERSION}.zip ../../release/

nxOMSKeyMgmt:
	rm -rf output/staging; \
        VERSION="1.0"; \
        PROVIDERS="nxOMSKeyMgmt"; \
        STAGINGDIR="output/staging/$@/DSCResources"; \
        cat Providers/Modules/$@.psd1 | sed "s@<MODULE_VERSION>@$${VERSION}@" > intermediate/Modules/$@.psd1; \
        for current in $$PROVIDERS; do \
                mkdir -p $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/{2.4x-2.5x,2.6x-2.7x,3.x}/Scripts; \
                cp intermediate/Modules/$@.psd1 output/staging/$@/; \
                cp Providers/$${current}/MSFT_$${current}Resource.schema.mof $$STAGINGDIR/MSFT_$${current}Resource/; \
                cp Providers/$${current}/MSFT_$${current}Resource.reg $$STAGINGDIR/MSFT_$${current}Resource/; \
                cp Providers/bin/libMSFT_$${current}Resource.so $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH); \
                cp Providers/Scripts/2.4x-2.5x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.4x-2.5x/Scripts; \
                cp Providers/Scripts/2.6x-2.7x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.6x-2.7x/Scripts; \
                cp Providers/Scripts/3.x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/3.x/Scripts; \
        done;\
        cd output/staging; \
		cd $@; find . -type f -print0 | xargs -0 sha256sum |grep -v sha256sums > ./$@.sha256sums; cd ..;\
        zip -r $@_$${VERSION}.zip $@; \
        mkdir -p ../../release; \
        mv $@_$${VERSION}.zip ../../release/


nxOMSCustomLog:
	rm -rf output/staging; \
	VERSION="1.0"; \
	PROVIDERS="nxOMSCustomLog"; \
	STAGINGDIR="output/staging/$@/DSCResources"; \
	cat Providers/Modules/$@.psd1 | sed "s@<MODULE_VERSION>@$${VERSION}@" > intermediate/Modules/$@.psd1; \
	for current in $$PROVIDERS; do \
		mkdir -p $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/{2.4x-2.5x,2.6x-2.7x,3.x}/Scripts; \
		cp intermediate/Modules/$@.psd1 output/staging/$@/; \
		cp Providers/$${current}/MSFT_$${current}Resource.schema.mof $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp Providers/$${current}/MSFT_$${current}Resource.reg $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp Providers/bin/libMSFT_$${current}Resource.so $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH); \
		cp Providers/Scripts/2.4x-2.5x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.4x-2.5x/Scripts; \
		cp Providers/Scripts/2.6x-2.7x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.6x-2.7x/Scripts; \
		cp Providers/Scripts/3.x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/3.x/Scripts; \
	done;\
	cd output/staging; \
	cd $@; find . -type f -print0 | xargs -0 sha256sum |grep -v sha256sums > ./$@.sha256sums; cd ..;\
	zip -r $@_$${VERSION}.zip $@; \
	mkdir -p ../../release; \
	mv $@_$${VERSION}.zip ../../release/

nxOMSSudoCustomLog:
	rm -rf output/staging; \
	VERSION="2.8"; \
	PROVIDERS="nxOMSSudoCustomLog"; \
	STAGINGDIR="output/staging/$@/DSCResources"; \
	cat Providers/Modules/$@.psd1 | sed "s@<MODULE_VERSION>@$${VERSION}@" > intermediate/Modules/$@.psd1; \
	for current in $$PROVIDERS; do \
		mkdir -p $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/{2.4x-2.5x,2.6x-2.7x,3.x}/Scripts; \
		cp intermediate/Modules/$@.psd1 output/staging/$@/; \
		cp -r Providers/Modules/CustomLog/ $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp Providers/$${current}/MSFT_$${current}Resource.schema.mof $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp Providers/$${current}/MSFT_$${current}Resource.reg $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp Providers/bin/libMSFT_$${current}Resource.so $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH); \
		cp Providers/Scripts/2.4x-2.5x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.4x-2.5x/Scripts; \
		cp Providers/Scripts/2.6x-2.7x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.6x-2.7x/Scripts; \
		cp Providers/Scripts/3.x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/3.x/Scripts; \
	done;\
	cd output/staging; \
	cd $@; find . -type f -print0 | xargs -0 sha256sum |grep -v sha256sums > ./$@.sha256sums; cd ..;\
	zip -r $@_$${VERSION}.zip $@; \
	mkdir -p ../../release; \
	mv $@_$${VERSION}.zip ../../release/


nxOMSGenerateInventoryMof:
	rm -rf output/staging; \
	VERSION="1.5"; \
	PROVIDERS="nxOMSGenerateInventoryMof"; \
	STAGINGDIR="output/staging/$@/DSCResources"; \
	cat Providers/Modules/$@.psd1 | sed "s@<MODULE_VERSION>@$${VERSION}@" > intermediate/Modules/$@.psd1; \
	for current in $$PROVIDERS; do \
		mkdir -p $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/{2.4x-2.5x,2.6x-2.7x,3.x}/Scripts; \
		cp intermediate/Modules/$@.psd1 output/staging/$@/; \
		cp Providers/$${current}/MSFT_$${current}Resource.schema.mof $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp Providers/$${current}/MSFT_$${current}Resource.reg $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp Providers/bin/libMSFT_$${current}Resource.so $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH); \
		cp Providers/Scripts/2.4x-2.5x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.4x-2.5x/Scripts; \
		cp Providers/Scripts/2.6x-2.7x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.6x-2.7x/Scripts; \
		cp Providers/Scripts/3.x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/3.x/Scripts; \
	done;\
	cd output/staging; \
	cd $@; find . -type f -print0 | xargs -0 sha256sum |grep -v sha256sums > ./$@.sha256sums; cd ..;\
	zip -r $@_$${VERSION}.zip $@; \
	mkdir -p ../../release; \
	mv $@_$${VERSION}.zip ../../release/

nxOMSPlugin:
	rm -rf output/staging; \
	VERSION="3.74"; \
	PROVIDERS="nxOMSPlugin"; \
	STAGINGDIR="output/staging/$@/DSCResources"; \
	cat Providers/Modules/$@.psd1 | sed "s@<MODULE_VERSION>@$${VERSION}@" > intermediate/Modules/$@.psd1; \
	for current in $$PROVIDERS; do \
		mkdir -p $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/{2.4x-2.5x,2.6x-2.7x,3.x}/Scripts; \
		cp intermediate/Modules/$@.psd1 output/staging/$@/; \
		cp -r Providers/Modules/Plugins/ $$STAGINGDIR/MSFT_$${current}Resource; \
		mkdir -p $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Plugins; \
		cp -r Providers/Modules/Plugins_$(PF_ARCH)/* $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Plugins; \
		cp Providers/$${current}/MSFT_$${current}Resource.schema.mof $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp Providers/$${current}/MSFT_$${current}Resource.reg $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp Providers/bin/libMSFT_$${current}Resource.so $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH); \
		cp Providers/Scripts/2.4x-2.5x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.4x-2.5x/Scripts; \
		cp Providers/Scripts/2.6x-2.7x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.6x-2.7x/Scripts; \
		cp Providers/Scripts/3.x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/3.x/Scripts; \
	done;\
	cd output/staging; \
	cd $@; find . -type f -print0 | xargs -0 sha256sum |grep -v sha256sums > ./$@.sha256sums; cd ..;\
	zip -r $@_$${VERSION}.zip $@; \
	mkdir -p ../../release; \
	mv $@_$${VERSION}.zip ../../release/

nxFileInventory:
	rm -rf output/staging; \
	VERSION="1.4"; \
	PROVIDERS="nxFileInventory"; \
	STAGINGDIR="output/staging/$@/DSCResources"; \
	cat Providers/Modules/$@.psd1 | sed "s@<MODULE_VERSION>@$${VERSION}@" > intermediate/Modules/$@.psd1; \
	for current in $$PROVIDERS; do \
		mkdir -p $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/{2.4x-2.5x,2.6x-2.7x,3.x}/Scripts; \
		cp intermediate/Modules/$@.psd1 output/staging/$@/; \
		cp Providers/$${current}/MSFT_$${current}Resource.schema.mof $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp Providers/$${current}/MSFT_$${current}Resource.reg $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp Providers/bin/libMSFT_$${current}Resource.so $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH); \
		cp Providers/Scripts/2.4x-2.5x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.4x-2.5x/Scripts; \
		cp Providers/Scripts/2.6x-2.7x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.6x-2.7x/Scripts; \
		cp Providers/Scripts/3.x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/3.x/Scripts; \
	done;\
	cd output/staging; \
	cd $@; find . -type f -print0 | xargs -0 sha256sum |grep -v sha256sums > ./$@.sha256sums; cd ..;\
	zip -r $@_$${VERSION}.zip $@; \
	mkdir -p ../../release; \
	mv $@_$${VERSION}.zip ../../release/

nxOMSAgentNPMConfig:
	rm -rf output/staging; \
	VERSION="4.6"; \
	PROVIDERS="nxOMSAgentNPMConfig"; \
	STAGINGDIR="output/staging/$@/DSCResources"; \
	cat Providers/Modules/$@.psd1 | sed "s@<MODULE_VERSION>@$${VERSION}@" > intermediate/Modules/$@.psd1; \
	for current in $$PROVIDERS; do \
		mkdir -p $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/{2.4x-2.5x,2.6x-2.7x,3.x}/Scripts; \
		cp intermediate/Modules/$@.psd1 output/staging/$@/; \
		cp -r Providers/Modules/NPM/ $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp Providers/$${current}/MSFT_$${current}Resource.schema.mof $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp Providers/$${current}/MSFT_$${current}Resource.reg $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp Providers/bin/libMSFT_$${current}Resource.so $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH); \
		cp Providers/Scripts/2.4x-2.5x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.4x-2.5x/Scripts; \
		cp Providers/Scripts/2.6x-2.7x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.6x-2.7x/Scripts; \
		cp Providers/Scripts/3.x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/3.x/Scripts; \
	done;\
	cd output/staging; \
	cd $@; find . -type f -print0 | xargs -0 sha256sum |grep -v sha256sums > ./$@.sha256sums; cd ..;\
	zip -r $@_$${VERSION}.zip $@; \
	mkdir -p ../../release; \
	mv $@_$${VERSION}.zip ../../release/

nxOMSAuditdPlugin:
	rm -rf output/staging; \
	VERSION="1.10"; \
	PROVIDERS="nxOMSAuditdPlugin"; \
	STAGINGDIR="output/staging/$@/DSCResources"; \
	cat Providers/Modules/$@.psd1 | sed "s@<MODULE_VERSION>@$${VERSION}@" > intermediate/Modules/$@.psd1; \
	for current in $$PROVIDERS; do \
		mkdir -p $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/{2.4x-2.5x,2.6x-2.7x,3.x}/Scripts; \
		cp intermediate/Modules/$@.psd1 output/staging/$@/; \
		cp -r Providers/Modules/nxOMSAuditdPlugin/* $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp Providers/$${current}/MSFT_$${current}Resource.schema.mof $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp Providers/$${current}/MSFT_$${current}Resource.reg $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp Providers/bin/libMSFT_$${current}Resource.so $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH); \
		cp Providers/Scripts/2.4x-2.5x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.4x-2.5x/Scripts; \
		cp Providers/Scripts/2.6x-2.7x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.6x-2.7x/Scripts; \
		cp Providers/Scripts/3.x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/3.x/Scripts; \
	done;\
	cd output/staging; \
	cd $@; find . -type f -print0 | xargs -0 sha256sum |grep -v sha256sums > ./$@.sha256sums; cd ..;\
	zip -r $@_$${VERSION}.zip $@; \
	mkdir -p ../../release; \
	mv $@_$${VERSION}.zip ../../release/

nxOMSContainers:
	rm -rf output/staging; \
	VERSION="1.0"; \
	PROVIDERS="nxOMSContainers"; \
	STAGINGDIR="output/staging/$@/DSCResources"; \
	cat Providers/Modules/$@.psd1 | sed "s@<MODULE_VERSION>@$${VERSION}@" > intermediate/Modules/$@.psd1; \
	for current in $$PROVIDERS; do \
		mkdir -p $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/{2.4x-2.5x,2.6x-2.7x,3.x}/Scripts; \
		mkdir -p $$STAGINGDIR/MSFT_$${current}Resource/containers; \
		cp intermediate/Modules/$@.psd1 output/staging/$@/; \
		cp Providers/$${current}/MSFT_$${current}Resource.schema.mof $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp Providers/$${current}/MSFT_$${current}Resource.reg $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp -r Providers/$${current}/containers/* $$STAGINGDIR/MSFT_$${current}Resource/containers; \
		cp Providers/bin/libMSFT_$${current}Resource.so $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH); \
		cp Providers/Scripts/2.4x-2.5x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.4x-2.5x/Scripts; \
		cp Providers/Scripts/2.6x-2.7x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.6x-2.7x/Scripts; \
		cp Providers/Scripts/3.x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/3.x/Scripts; \
	done; \
	cd output/staging; \
	cd $@; find . -type f -print0 | xargs -0 sha256sum |grep -v sha256sums > ./$@.sha256sums; cd ..;\
	zip -r $@_$${VERSION}.zip $@; \
	mkdir -p ../../release; \
	mv $@_$${VERSION}.zip ../../release/

nxOMSWLI:
	rm -rf output/staging; \
	VERSION="1.46"; \
	PROVIDERS="nxOMSWLI"; \
	STAGINGDIR="output/staging/$@/DSCResources"; \
	cat Providers/Modules/$@.psd1 | sed "s@<MODULE_VERSION>@$${VERSION}@" > intermediate/Modules/$@.psd1; \
	for current in $$PROVIDERS; do \
		mkdir -p $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/{2.4x-2.5x,2.6x-2.7x,3.x}/Scripts; \
		cp intermediate/Modules/$@.psd1 output/staging/$@/; \
                cp -r Providers/Modules/WLI $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp Providers/$${current}/MSFT_$${current}Resource.schema.mof $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp Providers/$${current}/MSFT_$${current}Resource.reg $$STAGINGDIR/MSFT_$${current}Resource/; \
		cp Providers/bin/libMSFT_$${current}Resource.so $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH); \
		cp Providers/Scripts/2.4x-2.5x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.4x-2.5x/Scripts; \
		cp Providers/Scripts/2.6x-2.7x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/2.6x-2.7x/Scripts; \
		cp Providers/Scripts/3.x/Scripts/$${current}.py $$STAGINGDIR/MSFT_$${current}Resource/$(PF_ARCH)/Scripts/3.x/Scripts; \
	done; \
	cd output/staging; \
	cd $@; find . -type f -print0 | xargs -0 sha256sum |grep -v sha256sums > ./$@.sha256sums; cd ..;\
	zip -r $@_$${VERSION}.zip $@; \
	mkdir -p ../../release; \
	mv $@_$${VERSION}.zip ../../release/

distclean: clean
	rm -rf omi-1.0.8/output
	rm -rf omi-1.0.8/output_openssl_0.9.8
	rm -rf omi-1.0.8/output_openssl_1.0.0
	rm -rf omi-1.0.8/output_openssl_1.1.0
	rm -rf omi-1.0.8/output_openssl_3.0.0

clean:
ifeq ($(BUILD_LOCAL),1)
	$(MAKE) -C LCM clean
	$(MAKE) -C Providers clean
	$(MAKE) -C omi-1.0.8 distclean
	rm -rf omi-1.0.8/output
	rm -rf output
	rm -rf release
	rm -rf intermediate
else
	$(MAKE) -C LCM clean
	$(MAKE) -C Providers clean
	rm -rf output
	rm -rf release
	rm -rf intermediate
endif


# To build DSC without making kits (i.e. the old style), run 'make local'
local:
	rm -rf release/*.rpm release/*.deb
	mkdir -p intermediate/Scripts
	mkdir -p intermediate/Modules
	$(MAKE) lcm providers
lcm:
	$(MAKE) -C omi-1.0.8
	$(MAKE) -C LCM

reg: lcmreg providersreg

lcmreg:
	$(MAKE) -C LCM deploydsc

providersreg:
	.  omi-1.0.8/output/config.mak; \
	for f in LCM/scripts/*.py LCM/scripts/*.sh Providers/Scripts/*.py Providers/Scripts/*.sh; do \
	  cat $$f | \
	  sed "s@<CONFIG_BINDIR>@$(CONFIG_BINDIR)@" | \
	  sed "s@<CONFIG_LIBDIR>@$(CONFIG_LIBDIR)@" | \
	  sed "s@<CONFIG_LOCALSTATEDIR>@$$CONFIG_LOCALSTATEDIR@" | \
	  sed "s@<CONFIG_SYSCONFDIR>@$(CONFIG_SYSCONFDIR)@" | \
	  sed "s@<CONFIG_SYSCONFDIR_DSC>@$(CONFIG_SYSCONFDIR_DSC)@" | \
	  sed "s@<OAAS_CERTPATH>@$(OAAS_CERTPATH)@" | \
	  sed "s@<OAAS_KEYPATH>@$(OAAS_KEYPATH)@" | \
	  sed "s@<OAAS_THUMBPRINT>@$(OAAS_THUMBPRINT)@" | \
	  sed "s@<OMI_LIB_SCRIPTS>@$(CONFIG_LIBDIR)/Scripts@" | \
	  sed "s@<PYTHON_PID_DIR>@$(PYTHON_PID_DIR)@" | \
	  sed "s@<DSC_NAMESPACE>@$(DSC_NAMESPACE)@" | \
	  sed "s@<DSC_SCRIPT_PATH>@$(DSC_SCRIPT_PATH)@" | \
	  sed "s@<DSC_HOST_BASE_PATH>@$(DSC_HOST_BASE_PATH)@" | \
	  sed "s@<DSC_MODULES_PATH>@$(CONFIG_DATADIR)/dsc/modules@" > intermediate/Scripts/`basename $$f`; \
	  chmod a+x intermediate/Scripts/`basename $$f`; \
	done
	for f in LCM/scripts/python3/*.py LCM/scripts/python3/*.sh; do \
	  cat $$f | \
	  sed "s@<CONFIG_BINDIR>@$$CONFIG_BINDIR@" | \
	  sed "s@<CONFIG_LIBDIR>@$$CONFIG_LIBDIR@" | \
	  sed "s@<CONFIG_LOCALSTATEDIR>@$$CONFIG_LOCALSTATEDIR@" | \
	  sed "s@<CONFIG_SYSCONFDIR>@$$CONFIG_SYSCONFDIR@" | \
	  sed "s@<CONFIG_SYSCONFDIR_DSC>@$(CONFIG_SYSCONFDIR_DSC)@" | \
	  sed "s@<OAAS_CERTPATH>@$(OAAS_CERTPATH)@" | \
	  sed "s@<OAAS_KEYPATH>@$(OAAS_KEYPATH)@" | \
	  sed "s@<OAAS_THUMBPRINT>@$(OAAS_THUMBPRINT)@" | \
	  sed "s@<OMI_LIB_SCRIPTS>@$$CONFIG_LIBDIR/Scripts@" | \
	  sed "s@<PYTHON_PID_DIR>@$(PYTHON_PID_DIR)@" | \
	  sed "s@<DSC_NAMESPACE>@$(DSC_NAMESPACE)@" | \
	  sed "s@<DSC_SCRIPT_PATH>@$(DSC_SCRIPT_PATH)@" | \
	  sed "s@<DSC_HOST_BASE_PATH>@$(DSC_HOST_BASE_PATH)@" | \
	  sed "s@<DSC_MODULES_PATH>@$(DSC_MODULES_PATH)@" > intermediate/Scripts/python3/`basename $$f`; \
	  chmod a+x intermediate/Scripts/python3/`basename $$f`; \
	done
	$(MAKE) -C Providers reg
