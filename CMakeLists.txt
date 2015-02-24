project(pvr.dvbviewer)

cmake_minimum_required(VERSION 2.6)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR})

enable_language(CXX)

find_package(TinyXML REQUIRED)
find_package(kodi REQUIRED)
find_package(kodiplatform REQUIRED)

include_directories(${kodiplatform_INCLUDE_DIRS}
                    ${KODI_INCLUDE_DIR}
                    ${TINYXML_INCLUDE_DIRS})

add_definitions(-D__STDC_FORMAT_MACROS)

set(DVBVIEWER_SOURCES src/client.cpp
                      src/DvbData.cpp
                      src/RecordingReader.cpp
                      src/TimeshiftBuffer.cpp)

set(DEPLIBS ${kodiplatform_LIBRARIES} ${TINYXML_LIBRARIES})

build_addon(pvr.dvbviewer DVBVIEWER DEPLIBS)

include(CPack)
