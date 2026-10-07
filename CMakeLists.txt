# SPDX-FileCopyrightText: 2021 Dmitry Vedenko <dmitry@crsib.me>
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.19)

project(audacity-project-tools)

set_property( GLOBAL PROPERTY USE_FOLDERS ON )

if( CMAKE_GENERATOR MATCHES "Visual Studio" )
   set_directory_properties(
      PROPERTIES
         VS_STARTUP_PROJECT "audacity-project-tools"
    )
endif()

set(CMAKE_CXX_STANDARD 17)

if(WIN32)
    add_definitions(-DUNICODE -D_UNICODE)
endif()

find_package(fmt REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(SQLiteCpp REQUIRED)
find_package(gflags REQUIRED)
find_package(utf8cpp REQUIRED)
find_package(Boost REQUIRED COMPONENTS filesystem system)

set( CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin )

add_executable(audacity-project-tools
    src/main.cpp

    src/Buffer.h
    src/Buffer.cpp

    src/ProjectBlobReader.h
    src/ProjectBlobReader.cpp

    src/XMLHandler.h

    src/BinaryXMLConverter.h
    src/BinaryXMLConverter.cpp

    src/SampleFormat.h
    src/SampleFormat.cpp

    src/AudacityDatabase.h
    src/AudacityDatabase.cpp

    src/ProjectModel.h
    src/ProjectModel.cpp

    src/WaveFile.h
    src/WaveFile.cpp
)


target_link_libraries(audacity-project-tools
    fmt::fmt
    SQLite::SQLite3
    SQLiteCpp
    gflags::gflags
    utf8cpp
    Boost::boost
    Boost::filesystem
    Boost::system
)

add_subdirectory(3party/sqlite3)

install(TARGETS audacity-project-tools sqlite3_shell)

set(CPACK_PACKAGE_NAME "audacity-project-tools")
set(CPACK_PACKAGE_VENDOR "Audacity")
set(CPACK_PACKAGE_VERSION_MAJOR 1)
set(CPACK_PACKAGE_VERSION_MINOR 0)
set(CPACK_PACKAGE_VERSION_PATCH 6)
set(CPACK_GENERATOR ZIP)
SET(CPACK_OUTPUT_FILE_PREFIX packages)

include(CPack)
