#!/bin/bash

set -e

. /etc/os-release
root=$(git rev-parse --show-toplevel)
buildcmd="ninja"
buildtype="debug"
builddir=$root/build/debug
case $ID in
    debian|ubuntu|linuxmint)
        echo "$ID supported"
        buildcmd="ninja"
        ;;
    centos|fedora)
        echo "$ID supported"
        buildcmd="ninja-build"
        ;;
    *)
        echo "$ID not supported"
        exit 1
        ;;
esac

function debug {
    echo "Debug"
    builddir=$root/build/debug
    mkdir -p $builddir
    cd ${builddir}
    cmake -Wdev \
          --debug-output \
          -DCMAKE_VERBOSE_MAKEFILE=ON -G Ninja \
          -DCMAKE_INSTALL_PREFIX=${builddir} \
          -DCMAKE_BUILD_TYPE=Debug ${root}

    # for fmt.py
    ln -sfn "${builddir}/compile_commands.json" "${root}/compile_commands.json"
    ${buildcmd}
}

function tests {
    echo "Testing"
    mkdir -p $builddir
    cd ${builddir}
    ctest --output-on-failure \
          -V -R smf \
          --force-new-ctest-process \
          --schedule-random \
          -j$(nproc) "$@"
}
function release {
    echo "Release"
    builddir=$root/build/release
    mkdir -p $builddir
    cd ${builddir}
    cmake     -Wno-dev \
              -DCMAKE_VERBOSE_MAKEFILE=ON \
              -GNinja \
              -DSEASTAR_ENABLE_DPDK=ON \
              -DCMAKE_INSTALL_PREFIX=${builddir} \
              -DSMF_ENABLE_BENCHMARK_TESTS=ON \
              -DCMAKE_BUILD_TYPE=Release \
              ${root}

    # for fmt.py
    ln -sfn "${builddir}/compile_commands.json" "${root}/compile_commands.json"
    ${buildcmd}
}

function format {
    echo "Format"
    ${root}/tools/fmt.py
}

function package {
    echo "Package"
    mkdir -p $builddir
    cd ${builddir}
    cpack -D CPACK_RPM_PACKAGE_DEBUG=1 \
          -D CPACK_RPM_SPEC_INSTALL_POST="/bin/true" -G RPM;
    cpack -D CPACK_DEBIAN_PACKAGE_DEBUG=1  -G DEB;
}



function usage {
    cat <<EOM
Usage: $(basename "$0") [OPTION]...
  -d          debug build

  -r          release build

  -t          run tests

  -f          format code

  -p          package code

  -h          display help
EOM

    exit 1
}


while getopts ":drtfph" optKey; do
    case $optKey in
        d)
            debug
            ;;
        r)
            release
            ;;
        t)
            tests
            ;;
        f)
            format
            ;;
        p)
            format
            ;;

        h|*)
            usage
            ;;
    esac
done
