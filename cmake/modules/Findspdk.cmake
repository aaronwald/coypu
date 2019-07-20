# Try to find spdk 
#
# Once done, this will define
#
# SPDK_FOUND
# SPDK_INCLUDE_DIR
# SPDK_LIBRARIES

find_path(SPDK_INCLUDE_DIR spdk/nvme.h
	PATHS "${CMAKE_SOURCE_DIR}/lib/3rd/spdk/include/"
	NO_DEFAULT_PATH)

set(spdk_components spdk_sock_posix spdk_nvme spdk_thread spdk_util spdk_log spdk_sock spdk_env_dpdk spdk_event spdk_json spdk_jsonrpc spdk_conf spdk_rpc spdk_trace spdk_trace_rpc)

foreach(c ${spdk_components})
  find_library(SPDK_spdk_${c}_LIBRARY ${c}
		PATHS "${CMAKE_SOURCE_DIR}/lib/3rd/spdk/lib/" NO_DEFAULT_PATH)
endforeach()

foreach(c ${spdk_components})
  list(APPEND spdk_check_LIBRARIES "${SPDK_spdk_${c}_LIBRARY}")
endforeach()

mark_as_advanced(SPDK_INCLUDE_DIR ${spdk_check_LIBRARIES})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(spdk DEFAULT_MSG
  SPDK_INCLUDE_DIR
  spdk_check_LIBRARIES)

if(SPDK_FOUND)
  set(SPDK_LIBRARIES -Wl,--whole-archive ${spdk_check_LIBRARIES} -Wl,--no-whole-archive)
endif(SPDK_FOUND)
