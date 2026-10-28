# 安卓真机探针：命令行 dmprobe（adb shell）与探针 App 的 libdmprobe.so，同一套源码。产物里不留本机路径（同 ../CMakeLists.txt）。
# Android device probe: the command-line dmprobe (adb shell) and the probe App's libdmprobe.so from the same sources. No local paths in the products (as ../CMakeLists.txt).
cmake_minimum_required(VERSION 3.22)
project(dm_probe C)
get_filename_component(DM_PATH_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../.. ABSOLUTE)
foreach(map "${CMAKE_BINARY_DIR}=build" "${CMAKE_ANDROID_NDK}=ndk" "${DM_PATH_ROOT}=src")
  add_compile_options("-ffile-prefix-map=${map}")
endforeach()
set(PROBE_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/dm_probe.c ${CMAKE_CURRENT_SOURCE_DIR}/probe_vk.c)
add_library(dmprobe_core STATIC ${PROBE_SRCS})
target_compile_options(dmprobe_core PUBLIC -O1 -g -Wall -std=gnu17)
target_link_libraries(dmprobe_core PUBLIC log dl)   # bionic 的 pthread 在 libc 里 | bionic has pthread inside libc
option(DM_PROBE_CLI "也编命令行 dmprobe（探针 App 的 Gradle 构建关掉它，AGP 只认共享库产物）| also build the command-line dmprobe (off in the probe App's Gradle build: AGP expects only the shared library)" ON)
if(DM_PROBE_CLI)
  add_executable(dmprobe_cli ${CMAKE_CURRENT_SOURCE_DIR}/main.c)   # 目标名与共享库区分，产物仍叫 dmprobe | target name distinct from the shared library, the product is still dmprobe
  set_target_properties(dmprobe_cli PROPERTIES OUTPUT_NAME dmprobe)
  target_link_libraries(dmprobe_cli PRIVATE dmprobe_core)
  target_link_options(dmprobe_cli PRIVATE -Wl,--no-undefined)
endif()
add_library(dmprobe SHARED ${CMAKE_CURRENT_SOURCE_DIR}/probe_jni.c)
target_link_libraries(dmprobe PRIVATE dmprobe_core)
target_link_options(dmprobe PRIVATE -Wl,--no-undefined -Wl,-z,max-page-size=16384)
