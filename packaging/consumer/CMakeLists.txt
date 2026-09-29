# find_package consumer of an installed KnishIO Crypto Core package, built by
# packaging/check-release-package.sh with -DCMAKE_PREFIX_PATH=<package> -DEXPECTED_VERSION=<ver>.
cmake_minimum_required(VERSION 3.20)
project(kcore_consumer C)

find_package(KnishIOCryptoCore REQUIRED)
if(NOT KnishIOCryptoCore_VERSION VERSION_EQUAL EXPECTED_VERSION)
    message(FATAL_ERROR
        "KnishIOCryptoCore ${KnishIOCryptoCore_VERSION} found at ${KnishIOCryptoCore_DIR}, expected ${EXPECTED_VERSION}")
endif()

add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE KnishIO::kcore)

add_executable(consumer_static main.c)
target_link_libraries(consumer_static PRIVATE KnishIO::kcore-static)
