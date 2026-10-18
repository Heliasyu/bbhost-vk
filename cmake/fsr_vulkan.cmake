# bbhost-vk: FireBurn/FSR-Vulkan (MIT, third_party/FSR-Vulkan, a git
# submodule pinned in .gitmodules) - AMD's FidelityFX FSR 3 upscaler on native
# Vulkan with its SPIR-V shader tables checked in (generated/). Phase 0 only
# builds and links it: src/host/upscaler.cpp references the FSR 3.1.5 and the
# 3.1.4 upscalers and counts the embedded 3.1.5 shader modules, and nothing
# records an FSR 3 dispatch yet (docs/vk/PLAN.md, Phase 3).
#
# Only what bbhost links is built (EXCLUDE_FROM_ALL): the portable contract,
# the 3.1.4 Vulkan backend and the 3.1.5 upscaler. Its tests, the capability
# probe, the experimental source-v07 "FSR4" provider (which is not AMD FSR
# 4.1.1; Phase 4 builds that from the player's own DLL instead) and its install
# rules are off. BBHOST_FSR_VULKAN=OFF leaves it out; the Windows cross build
# leaves it out by default until it is tried there.
if(WIN32)
  set(_bbhost_fsr_vulkan_default OFF)
else()
  set(_bbhost_fsr_vulkan_default ON)
endif()
option(BBHOST_FSR_VULKAN "link FireBurn/FSR-Vulkan (FSR 3.1) into bbhost" ${_bbhost_fsr_vulkan_default})

set(BBHOST_FSR_VULKAN_DIR ${CMAKE_SOURCE_DIR}/third_party/FSR-Vulkan)
if(BBHOST_FSR_VULKAN AND Vulkan_FOUND)
  if(NOT EXISTS ${BBHOST_FSR_VULKAN_DIR}/CMakeLists.txt)
    message(FATAL_ERROR "bbhost: third_party/FSR-Vulkan is empty: run `git submodule update --init` "
                        "(or configure with -DBBHOST_FSR_VULKAN=OFF)")
  endif()
  set(FFX_VK_PORTABLE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(FFX_VK_PORTABLE_BUILD_PROBE OFF CACHE BOOL "" FORCE)
  set(FFX_VK_PORTABLE_BUILD_FSR3_HOST ON CACHE BOOL "" FORCE)
  set(FFX_VK_PORTABLE_BUILD_FSR3_VK_BACKEND ON CACHE BOOL "" FORCE)
  set(FFX_VK_PORTABLE_BUILD_FSR3_3_1_5_SCAFFOLD ON CACHE BOOL "" FORCE)
  set(FFX_VK_PORTABLE_BUILD_FSR4_V07_VULKAN OFF CACHE BOOL "" FORCE)
  set(FFX_VK_PORTABLE_INSTALL OFF CACHE BOOL "" FORCE)
  set(FFX_VK_PORTABLE_INSTALL_FSR4_V07_ASSETS OFF CACHE BOOL "" FORCE)
  # Its static libraries go into a position-independent executable.
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  add_subdirectory(${BBHOST_FSR_VULKAN_DIR} ${CMAKE_BINARY_DIR}/third_party/FSR-Vulkan EXCLUDE_FROM_ALL SYSTEM)
  # Its own -Wall -Wextra -Wpedantic would drown bbhost's build log; it is
  # third-party code, built as it ships.
  foreach(t ffx_vulkan_portable ffx_vulkan_fsr3_host_1_1_4 ffx_vulkan_fsr3_vk_backend_1_1_4
            ffx_vulkan_fsr3_host_3_1_5_scaffold ffx_vulkan_fsr3_vk_backend_3_1_5)
    if(TARGET ${t})
      target_compile_options(${t} PRIVATE -w)
    endif()
  endforeach()
  target_sources(bbhost PRIVATE src/host/upscaler_fsr3.cpp src/host/fsr3_upscaler.cpp)
  set_source_files_properties(src/host/fsr3_upscaler.cpp PROPERTIES COMPILE_OPTIONS "-Wno-missing-field-initializers")
  target_compile_definitions(bbhost PRIVATE BBHOST_HAVE_FSR_VULKAN=1)
  target_link_libraries(bbhost PRIVATE ffx-vulkan::portable ffx-vulkan::fsr3-vk-backend-3.1.5)
  message(STATUS "bbhost: FSR-Vulkan linked (FSR 3.1.5 and 3.1.4 upscalers, shader tables embedded)")
else()
  message(STATUS "bbhost: FSR-Vulkan not linked (BBHOST_FSR_VULKAN=${BBHOST_FSR_VULKAN})")
endif()
