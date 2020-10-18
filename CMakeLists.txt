cmake_minimum_required(VERSION 3.5)
project(pvr.dvbviewer)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR})

find_package(TinyXML REQUIRED)
find_package(Kodi REQUIRED)

include_directories(${KODI_INCLUDE_DIR}/.. # Hack way with "/..", need bigger Kodi cmake rework to match right include ways
                    ${TINYXML_INCLUDE_DIRS})

add_definitions(-D__STDC_FORMAT_MACROS)

set(DVBVIEWER_SOURCES src/client.cpp
                      src/DvbData.cpp
                      src/KVStore.cpp
                      src/RecordingReader.cpp
                      src/Settings.cpp
                      src/StreamReader.cpp
                      src/Timers.cpp
                      src/TimeshiftBuffer.cpp)

set(DVBVIEWER_HEADERS src/client.h
                      src/DvbData.h
                      src/IStreamReader.h
                      src/KVStore.h
                      src/RecordingReader.h
                      src/Settings.h
                      src/StreamReader.h
                      src/Timers.h
                      src/TimeshiftBuffer.h
                      src/utilities/XMLUtils.h)

set(DEPLIBS ${TINYXML_LIBRARIES})

# check if the linker supports version script or symbols list
if(NOT "${CMAKE_SYSTEM_NAME}" STREQUAL "Windows")
  include(CheckCXXCompilerFlag)
  file(READ "${PROJECT_SOURCE_DIR}/symbols.exp" __symbols_exp)
  string(STRIP ${__symbols_exp} __symbols_exp)
  set(__symbols_file "${CMAKE_CURRENT_BINARY_DIR}/symbols.exp")
  unset(__ldflags)

  if(NOT __ldflags)
    # gnu ld
    file(WRITE ${__symbols_file} "{ local: *; };\n")
    set(CMAKE_REQUIRED_FLAGS "-Wl,--version-script=${__symbols_file}")
    check_cxx_compiler_flag("" LD_ACCEPTS_VERSION_SCRIPT)
    if(LD_ACCEPTS_VERSION_SCRIPT)
      string(REPLACE "\n" "; " __symbols ${__symbols_exp})
      file(WRITE ${__symbols_file} "{ global: ${__symbols}; local: *; };\n")
      set(__ldflags "-Wl,--version-script=${__symbols_file}")
    endif()
  endif()

  if(NOT __ldflags)
    # darwin ld
    file(WRITE ${__symbols_file} "\n")
    set(CMAKE_REQUIRED_FLAGS "-Wl,-exported_symbols_list,${__symbols_file}")
    check_cxx_compiler_flag("" LD_ACCEPTS_SYMBOLS_LIST)
    if(LD_ACCEPTS_SYMBOLS_LIST)
      string(REPLACE "\n" "\n_" __symbols ${__symbols_exp})
      file(WRITE ${__symbols_file} "_${__symbols}\n")
      set(__ldflags "-Wl,-exported_symbols_list,${__symbols_file}")
    endif()
  endif()

  if(__ldflags)
    add_options(ALL_LANGUAGES ALL_BUILDS "${__ldflags}")
  endif()
  unset(CMAKE_REQUIRED_FLAGS)
endif()

if(NOT MSVC)
  add_options(ALL_LANGUAGES DEBUG "-g" "-D_DEBUG" "-Wall")
endif()

build_addon(pvr.dvbviewer DVBVIEWER DEPLIBS)

include(CPack)
