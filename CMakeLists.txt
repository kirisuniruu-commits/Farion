cmake_minimum_required(VERSION 3.22)
project(farion LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# Declared first so the preloader SDK reuses these exact versions.
FetchContent_Declare(nlohmann_json GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.11.3)
FetchContent_Declare(boost_pfr GIT_REPOSITORY https://github.com/boostorg/pfr.git GIT_TAG boost-1.88.0)
FetchContent_Declare(magic_enum GIT_REPOSITORY https://github.com/Neargye/magic_enum.git GIT_TAG v0.9.7)
FetchContent_Declare(fmt GIT_REPOSITORY https://github.com/fmtlib/fmt.git GIT_TAG 11.2.0)
set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json boost_pfr magic_enum fmt)

# Pin to a release tag for reproducible builds.
set(PRELOADER_ANDROID_TAG "0.2.3" CACHE STRING "LiteLDev/preloader-android tag")
FetchContent_Declare(preloader_android
    GIT_REPOSITORY https://github.com/LiteLDev/preloader-android.git
    GIT_TAG ${PRELOADER_ANDROID_TAG})
FetchContent_GetProperties(preloader_android)
if(NOT preloader_android_POPULATED)
    FetchContent_Populate(preloader_android)
endif()
set(PRELOADER_ROOT "${preloader_android_SOURCE_DIR}")

function(dh_common target)
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/src" "${PRELOADER_ROOT}/include")
    target_link_libraries(${target} PRIVATE
        nlohmann_json::nlohmann_json Boost::pfr magic_enum::magic_enum fmt::fmt)
    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANG_AND_ID:CXX,Clang,GNU>:-Wall -Wextra>)
endfunction()

if(NOT ANDROID)
    enable_testing()
    add_executable(lod_tests tests/lod_tests.cpp)
    target_compile_options(lod_tests PRIVATE -Wall -Wextra)
    find_package(Threads REQUIRED)
    target_link_libraries(lod_tests PRIVATE Threads::Threads)
    add_test(NAME lod_tests COMMAND lod_tests)

    add_executable(dh_config_gen src/GenerateConfig.cpp)
    dh_common(dh_config_gen)
    target_compile_definitions(dh_config_gen PRIVATE PL_CONFIG_NO_RUNTIME)
else()
    add_subdirectory("${PRELOADER_ROOT}" "${CMAKE_BINARY_DIR}/_preloader_android")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out/${ANDROID_ABI}")
    find_library(LL_LOG_LIB log)

    add_library(farion SHARED src/DistantHorizonsMod.cpp)
    dh_common(farion)
    target_compile_options(farion PRIVATE
        -fvisibility=hidden -fvisibility-inlines-hidden -O2)
    target_link_libraries(farion PRIVATE preloader ${LL_LOG_LIB})
endif()
