#!/bin/bash

export PATH=/sbin:/bin:/usr/sbin:/usr/bin

CACHE_DIR=/var/opt/microsoft/omsconfig_secure_cache
DSC_PGP_KEY=/opt/microsoft/omsconfig/keys/dscgpgkey.asc
DEST_MODE=755

umask 022

ShowUsage()
{
  cat << EOF
  Error: $1

  Usage: $0 -n <DSC name> -s <source file> -o <dest file> [-m <dest mode>]

    -n <name of DSC module>
    -s <source file, path relative to root of DSC module>
    -o <dest file>
    -m <destination mode>
EOF
    exit 1
}

Errf()
{
    (>&2 echo "[ERROR] $1")
    exit 1
}

if [ $(id -u) -ne 0 ]; then
    ShowUsage "Must be run as root"
fi

while (( "$#" )); do
  if [ "$1" == "-n" ]; then
    shift
    DSC_NAME=$1
  elif [ "$1" == "-s" ] ; then
    shift
    SRC_DSC_PATH=$1
  elif [ "$1" == "-o" ] ; then
    shift
    DEST_PATH=$1
  elif [ "$1" == "-m" ] ; then
    shift
    DEST_MODE=$1
  else
    ShowUsage "unsupported argument '$1'"
  fi
    shift
done

if [ -z "$DSC_NAME" ]; then
    ShowUsage "Missing DSC module name"
fi

if [ -z "$SRC_DSC_PATH" ]; then
    ShowUsage "Missing source DSC file"
fi

if [ -z "$DEST_PATH" ]; then
    ShowUsage "Missing Destination File"
fi

DSC_ROOT="/opt/microsoft/omsconfig/modules/${DSC_NAME}"

if [ ! -e $DSC_ROOT ]; then
    ShowUsage "Invalid DSC name"
fi

SRC_PATH=${DSC_ROOT}/${SRC_DSC_PATH}
SRC_NAME=$(basename $SRC_PATH)

if [ ! -e $SRC_PATH ]; then
    ShowUsage "Invalid DSC source file"
fi

DSC_SIGNATURE_FILE=${DSC_ROOT}/${DSC_NAME}.asc
DSC_HASH_FILE=${DSC_ROOT}/${DSC_NAME}.sha256sums

if [ ! -e $DSC_SIGNATURE_FILE ]; then
    Errf "DSC signature file is missing"
fi

if [ ! -e $DSC_HASH_FILE ]; then
    Errf "DSC hash file is missing"
fi

DEST_DIR=$(dirname $DEST_PATH)
if [ $DEST_DIR != "/opt/microsoft/omsconfig/Scripts" -a $DEST_DIR != "/opt/microsoft/omsagent/plugin" ]; then
    Errf "Invalid destination dir, only /opt/microsoft/omsconfig/Scripts and /opt/microsoft/omsagent/plugin are allowed"
fi

DSC_CACHE_DIR=$CACHE_DIR/$DSC_NAME

# Make the cache dir if it doesn't exist
if [ ! -e $DSC_CACHE_DIR ]; then
    mkdir -p -m 755 $DSC_CACHE_DIR
    if [ $? -ne 0 ]; then
        Errf "Failed to create cache dir '$DSC_CACHE_DIR'"
    fi
fi

# Copy DSC module signature file into the root owned cache
cp $DSC_SIGNATURE_FILE $DSC_CACHE_DIR
if [ $? -ne 0 ]; then
    Errf "Failed to copy $DSC_SIGNATURE_FILE to $DSC_CACHE_DIR"
fi

# Copy DSC module sha256sums file into the root owned the cache
cp $DSC_HASH_FILE $DSC_CACHE_DIR
if [ $? -ne 0 ]; then
    Errf "Failed to copy $DSC_HASH_FILE to $DSC_CACHE_DIR"
fi

# Copy DSC module source file into the the root owned cache
cp $SRC_PATH $DSC_CACHE_DIR
if [ $? -ne 0 ]; then
    Errf "Failed to copy $SRC_PATH to $DSC_CACHE_DIR"
fi

# Get the hash of the source file as recorded in the DSC module .sha256sums file
DSC_SRC_HASH=$(grep $SRC_DSC_PATH $DSC_CACHE_DIR/${DSC_NAME}.sha256sums | cut -d' ' -f 1)

# Get the actual has of the source file
SRC_HASH=$(sha256sum $DSC_CACHE_DIR/$SRC_NAME | cut -d' ' -f 1)

# Compare the actual hash with the expected hash
if [ "$DSC_SRC_HASH" != "$SRC_HASH" ]; then
    Errf "Invalid source file hash"
fi

KEYRING=$CACHE_DIR/keyring.gpg

# Create the keyring if it doesn't exist
if [ ! -e $KEYRING ]; then
    gpg_out=$(gpg --homedir $CACHE_DIR --no-default-keyring --keyring $KEYRING --import $DSC_PGP_KEY 2>&1)
    if [ $? -ne 0 ]; then
        echo "$gpg_out"
        Errf "Failed to import $DSC_PGP_KEY"
    fi
fi

# Validate the DSC module signature
gpgv_out=$(gpg --homedir $CACHE_DIR --no-default-keyring --keyring $KEYRING --verify $DSC_CACHE_DIR/${DSC_NAME}.asc $DSC_CACHE_DIR/${DSC_NAME}.sha256sums 2>&1)
if [ $? -ne 0 ]; then
    echo "$gpgv_out"
    Errf "Failed to validate signature"
fi

# Remove any tmp dest file if it exist
rm -f $DEST_PATH.tmp

# Crete the temp dest file
touch $DEST_PATH.tmp
if [ $? -ne 0 ]; then
    rm -rf $TMP_DIR
    Errf "Failed to create $DEST_PATH.tmp"
fi

# Set the temporary perms on the dest temp file
chmod 600 $DEST_PATH.tmp
if [ $? -ne 0 ]; then
    Errf "Failed to chmod $DEST_PATH.tmp"
fi

# Make sure the dest temp file is owned by root
# Since the dest dir can be modified by non-root, it is possible for an attacker to race this script to insert their own contents
# If the dest temp file is not owned by root, then a attack occured
STAT=$(stat --format="0.0" $DEST_PATH.tmp)
if [ "$STAT" != "0.0" ]; then
    Errf "Invalid ownership of  $DEST_PATH.tmp"
fi

# Copy validated contents into the dest temp file
cp $DSC_CACHE_DIR/$SRC_NAME $DEST_PATH.tmp
if [ $? -ne 0 ]; then
    Errf "Failed to create $DEST_PATH.tmp"
fi

# Change perms to match desired mode
chmod $DEST_MODE $DEST_PATH.tmp
if [ $? -ne 0 ]; then
    Errf "Failed to chmod $DEST_PATH.tmp"
fi

# Replace the old dest file with the new dest file
# "mv -f" used rename(2) which is atomic
mv -f $DEST_PATH.tmp $DEST_PATH
if [ $? -ne 0 ]; then
    Errf "Failed to move $DEST_PATH.tmp to $DEST_PATH"
fi

exit 0