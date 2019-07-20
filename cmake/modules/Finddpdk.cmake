# Try to find dpdk 
#
# Once done, this will define
#
# DPDK_FOUND
# DPDK_INCLUDE_DIR
# DPDK_LIBRARIES

find_path(DPDK_INCLUDE_DIR rte_eal.h
  PATHS "${CMAKE_SOURCE_DIR}/lib/3rd/dpdk/include/dpdk/" NO_DEFAULT_PATH)

set(dpdk_components
  acl bbdev bitratestats bus_pci bus_vdev cfgfile cmdline
  cryptodev distributor eal efd ethdev eventdev flow_classify gro gso hash
  ip_frag jobstats kni kvargs latencystats lpm mbuf member mempool 
  mempool_ring mempool_stack meter metrics net pci pdump pipeline pmd_e1000 port power reorder pmd_pcap pmd_tap
  ring sched security table timer vhost pmd_ixgbe pmd_mlx5 )

foreach(c ${dpdk_components})
  find_library(DPDK_rte_${c}_LIBRARY rte_${c}
		PATHS "${CMAKE_SOURCE_DIR}/lib/3rd/dpdk/lib/" NO_DEFAULT_PATH)
endforeach()

foreach(c ${dpdk_components})
  list(APPEND dpdk_check_LIBRARIES "${DPDK_rte_${c}_LIBRARY}")
endforeach()

mark_as_advanced(DPDK_INCLUDE_DIR ${dpdk_check_LIBRARIES})

if (EXISTS ${WITH_DPDK_MLX5})
  find_library(DPDK_rte_pmd_mlx5_LIBRARY rte_pmd_mlx5)
  list(APPEND dpdk_check_LIBRARIES ${DPDK_rte_pmd_mlx5_LIBRARY})
  mark_as_advanced(DPDK_rte_pmd_mlx5_LIBRARY)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(dpdk DEFAULT_MSG
  DPDK_INCLUDE_DIR
  dpdk_check_LIBRARIES)

if(DPDK_FOUND)
  if(EXISTS ${WITH_DPDK_MLX5})
    list(APPEND dpdk_check_LIBRARIES -libverbs)
  endif()
  set(DPDK_LIBRARIES
    -Wl,--whole-archive ${dpdk_check_LIBRARIES}  -Wl,--no-whole-archive)
endif(DPDK_FOUND)
