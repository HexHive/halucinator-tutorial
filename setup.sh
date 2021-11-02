#!/bin/bash

# You only need to run this script if it has not been run for you in the demo 
# environment. If it has, there is nothing to do here.

UBUNTU_DEPS="gcc build-essential pkg-config gdb-multiarch python3-pip python3-virtualenv libcap-dev libpixman-1-dev libglib2.0-dev"

# locate this script:
SCRIPT="${BASH_SOURCE[0]}"
SCRIPTDIR=""
if [ -z "$SCRIPT" ]; then
    SCRIPT=$(readlink -f $0)
    SCRIPTDIR=$(dirname $SCRIPT)
else
    SCRIPTDIR="$( cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 ; pwd -P )"
fi

# Identify Distro

if ! command -v lsb_release >/dev/null 2>&1; then
    echo "No lsb_release command installed.";
    exit -1
else
    DISTRO=$(lsb_release -is | tr '[:upper:]' '[:lower:]')
fi

if [[ "$DISTRO" == "ubuntu" ]]; then
    sudo apt-get install $UBUNTU_DEPS
elif [[ "$DISTRO" == "fedora" ]]; then
    echo "Fedora!"
    echo "Not implemented yet, this is todo."
    exit(1)
fi

git submodule update --init

pushd $SCRIPTDIR/sw/
mkdir -p avatar-qemu-build

# This builds avatar-qemu
../avatar-qemu/configure --disable-sdl --target-list=arm-softmmu,avr-softmmu --enable-debug --prefix=$HOME/qemu/
make -j $(nproc)
make install
popd

# make a virtualenv
virtualenv -p python3 halenv
source halenv/bin/activate

# copy the right config file for global halucinator use
cp sw/halucinator/configs/...

pushd sw/avatar2
pip install .
popd

pushd sw/halucinator
pip install -r requirements
pip install -e .
popd
