cmake_minimum_required (VERSION 3.9)
project(coypu)
include (CTest)
include (GoogleTest)
include (ExternalProject)

# Enable LTO on Release builds
include(CheckIPOSupported)

check_ipo_supported(RESULT ipo_supported)
if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

list(APPEND CMAKE_MODULE_PATH "/opt/cmake-modules")

set(CMAKE_FIND_LIBRARY_PREFIXES lib ${CMAKE_FIND_LIBRARY_PREFIXES})
set(CMAKE_FIND_LIBRARY_SUFFIXES .a .so ${CMAKE_FIND_LIBRARY_SUFFIXES})

include(GetGitRevisionDescription)
get_git_head_revision(GIT_REFSPEC GIT_SHA1)

configure_file("src/main/version.h.in" "../src/main/version.h")

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(coypu_VERSION_MAJOR 0)
set(coypu_VERSION_MINOR 3)

set(CMAKE_CXX_COMPILER "clang++")
set(CMAKE_C_COMPILER "clang") 
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

SET(CMAKE_CXX_FLAGS         "-nodefaultlibs -stdlib=libc++ -Wall -Wpedantic -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE")
SET(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG=1 -DSPDLOG_DEBUG_ON=0 -DSPDLOG_TRACE_ON=0")
SET(CMAKE_CXX_FLAGS_DEBUG   "-O0 -fno-omit-frame-pointer -g -DSPDLOG_DEBUG_ON=1 -DSPDLOG_TRACE_ON=1")
#SET(CMAKE_EXE_LINKER_FLAGS "-stdlib=libc++")

#add_definitions("-D_GIT_REV=\"${GIT_SHA1}\"")
add_definitions("-DCOYPU_VERSION=\"${coypu_VERSION_MAJOR}.${coypu_VERSION_MINOR}\"")

file(GLOB_RECURSE COYPU_SRC ${PROJECT_SOURCE_DIR}/src/main/*.cpp)
list(FILTER COYPU_SRC EXCLUDE REGEX ".*main.cpp$")
file(GLOB_RECURSE COYPU_TEST_SRC ${PROJECT_SOURCE_DIR}/src/test/*.cpp)


include_directories(${PROJECT_SOURCE_DIR}/src/main)
include_directories(${PROJECT_SOURCE_DIR}/libs/rapidjson/include/)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_BINARY_DIR}/src)

set(BPF_SRC ${PROJECT_SOURCE_DIR}/src/bpf/bpf_load.c ${PROJECT_SOURCE_DIR}/src/bpf/libbpf.c ${PROJECT_SOURCE_DIR}/src/bpf/nutria_bpf.c)
include_directories(${PROJECT_SOURCE_DIR}/src/bpf/)

set(CMAKE_INSTALL_RPATH "/usr/local/lib")

ExternalProject_Add(rust_lib
							DOWNLOAD_COMMAND ""
							CONFIGURE_COMMAND ""
							BUILD_COMMAND cargo build COMMAND cargo build --release
							BINARY_DIR "${CMAKE_SOURCE_DIR}/src/rust-lib"
							INSTALL_COMMAND ""
							LOG_BUILD ON)

link_directories("${CMAKE_SOURCE_DIR}/src/rust-lib/target/release/")

#get_cmake_property(_variableNames VARIABLES)
#list (SORT _variableNames)
#foreach (_variableName ${_variableNames})
#    message(STATUS "${_variableName}=${${_variableName}}")
#endforeach()

add_executable(coypu ${COYPU_SRC} "src/main/main.cpp")
add_dependencies(coypu rust_lib)

target_link_libraries(coypu yaml)
target_link_libraries(coypu pthread)
target_link_libraries(coypu crypto)
target_link_libraries(coypu ssl)
target_link_libraries(coypu numa)
target_link_libraries(coypu bpf)
target_link_libraries(coypu unwind)
target_link_libraries(coypu c++)
target_link_libraries(coypu c++abi)
target_link_libraries(coypu m)
target_link_libraries(coypu c)
target_link_libraries(coypu gcc_s)
target_link_libraries(coypu gcc)
target_link_libraries(coypu coypurust)
target_link_libraries(coypu protobuf)
target_link_libraries(coypu coypuproto)

add_executable(nutra_bpf_load ${BPF_SRC})
target_link_libraries(nutra_bpf_load elf)

add_executable(coyputest ${COYPU_TEST_SRC} ${COYPU_SRC})
target_link_libraries(coyputest yaml)
target_link_libraries(coyputest glog)
target_link_libraries(coyputest gflags)
target_link_libraries(coyputest gtest)
target_link_libraries(coyputest gmock)
target_link_libraries(coyputest pthread)
target_link_libraries(coyputest unwind)
target_link_libraries(coyputest crypto)
target_link_libraries(coyputest ssl)
target_link_libraries(coyputest numa)
target_link_libraries(coyputest bpf)
target_link_libraries(coyputest c++)
target_link_libraries(coyputest c++abi)
target_link_libraries(coyputest m)
target_link_libraries(coyputest c)
target_link_libraries(coyputest gcc_s)
target_link_libraries(coyputest gcc)
target_link_libraries(coyputest protobuf)
target_link_libraries(coyputest coypuproto)

gtest_discover_tests(coyputest)

add_subdirectory("${PROJECT_SOURCE_DIR}/src/kern")
add_subdirectory("${PROJECT_SOURCE_DIR}/src/proto")

add_custom_target(docker 
	COMMAND docker build -q -t coypu .
	DEPENDS coypu rust_lib
	WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
	)
