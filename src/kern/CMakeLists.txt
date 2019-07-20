# See
# https://github.com/pratyushanand/learn-bpf
# <linux>/samples/bpf/

cmake_minimum_required (VERSION 3.8)

set(CMAKE_C_COMPILER "clang") 
execute_process(COMMAND uname -r OUTPUT_VARIABLE KERNEL)
execute_process(COMMAND clang -print-file-name=include OUTPUT_VARIABLE ISYSTEM_PATH)
set(ARCH "x86")

string(REGEX REPLACE "\n$" "" ISYSTEM_PATH "${ISYSTEM_PATH}")
string(REGEX REPLACE "\n$" "" KERNEL "${KERNEL}")
set(KERNEL "/lib/modules/${KERNEL}/build/")

set(CLANG_LLVM_FLAGS -S -nostdinc -isystem ${ISYSTEM_PATH} -I ${PROJECT_SOURCE_DIR}/src/bpf/ )
set(CLANG_LLVM_FLAGS ${CLANG_LLVM_FLAGS} -I${KERNEL}/arch/${ARCH}/include/generated/uapi -I${KERNEL}/arch/${ARCH}/include/generated -I${KERNEL}/arch/${ARCH}/include -I${KERNEL}/arch/${ARCH}/include/uapi -I${KERNEL}/include -I${KERNEL}/include/uapi -include ${KERNEL}/include/linux/kconfig.h -I${KERNEL}/include/generated/uapi)
set(CLANG_LLVM_FLAGS ${CLANG_LLVM_FLAGS} -Werror -D__BPF_TRACING__ -D__KERNEL__ -D__ASM_SYSREG_H -Wno-unused-value -Wno-pointer-sign -Wno-address-of-packed-member -Wno-compare-distinct-pointer-types -Wno-gnu-variable-sized-type-not-at-end -Wno-tautological-compare -O2 -emit-llvm) 

set(SOURCES ${PROJECT_SOURCE_DIR}/src/kern/nutria_mmap.c)

set(OBJS)
foreach(_file ${SOURCES})
    string(REPLACE "${PROJECT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}" obj_file ${_file})
    string(REPLACE ".c" ".o" obj_file ${obj_file})
    string(REPLACE ".o" ".ll" ll_file ${obj_file})

    set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_MAKE_CLEAN_FILES ${ll_file})

    add_custom_command(
        OUTPUT ${obj_file}
        VERBATIM COMMAND ${CMAKE_C_COMPILER} ${CLANG_LLVM_FLAGS} -c ${_file}
        VERBATIM COMMAND llc -march=bpf -filetype=obj -o ${obj_file} ${ll_file}
        DEPENDS ${_file}
        COMMENT "Build ${ll_file}"
    ) 

    list(APPEND OBJS ${obj_file})    
endforeach()


add_library(bpf_modules OBJECT ${OBJS})
set_target_properties(bpf_modules PROPERTIES LINKER_LANGUAGE C)

