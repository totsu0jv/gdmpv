cmake_minimum_required(VERSION 3.17)

# Silence unused variable warning when specified from toolchain
if(CMAKE_C_COMPILER)
endif()

set(LIBNAME "godot-mpv" CACHE STRING "The name of the library")
set(GODOT_PROJECT_DIR "demo" CACHE STRING "The directory of a Godot project folder")

# Make sure all the dependencies are satisfied
find_package(Python3 3.4 REQUIRED)
find_program(GIT git REQUIRED)

# Ensure godot-cpp submodule has been updated
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/godot-cpp/src")
    message(NOTICE "godot-cpp bindings source not found")
    message(NOTICE "initializing/updating the godot-cpp submodule...")

    execute_process(
        COMMAND git submodule update --init godot-cpp
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
        COMMAND_ERROR_IS_FATAL ANY
    )
endif()
add_subdirectory(godot-cpp SYSTEM)

# Add godot-cpp's module path and include the exported functions
set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${godot-cpp_SOURCE_DIR}/cmake")
include(GodotCPPModule)

# Get godot-cpp properties
get_target_property(GODOTCPP_SUFFIX godot::cpp GODOTCPP_SUFFIX)
get_target_property(GODOTCPP_PLATFORM godot::cpp GODOTCPP_PLATFORM)

# Project definition
project(godot-mpv
    VERSION 1.0
    DESCRIPTION "MPV video streaming GDExtension for Godot 4.x"
    HOMEPAGE_URL "https://github.com/yourusername/godot-mpv"
    LANGUAGES CXX
)

# Find libmpv
if(WIN32)
    # Check environment variables first (for CI)
    set(MPV_INCLUDE $ENV{MPV_INCLUDE})
    set(MPV_LIB $ENV{MPV_LIB})
    
    if(MPV_INCLUDE AND MPV_LIB)
        message(STATUS "Using MPV from environment variables")
        message(STATUS "  Include: ${MPV_INCLUDE}")
        message(STATUS "  Lib: ${MPV_LIB}")
        
        set(MPV_FOUND TRUE)
        set(MPV_INCLUDE_DIRS ${MPV_INCLUDE})
        set(MPV_LIBRARY_DIRS ${MPV_LIB})
        set(MPV_LIBRARIES "${MPV_LIB}/libmpv.dll.a")
    else()
        # Try to find in common locations
        find_path(MPV_INCLUDE_DIR mpv/client.h
            PATHS
                "C:/mpv-dev"
                "C:/libmpv"
                "$ENV{USERPROFILE}/mpv-dev"
            PATH_SUFFIXES include
        )
        
        find_library(MPV_LIBRARY
            NAMES libmpv.dll.a mpv
            PATHS
                "C:/mpv-dev"
                "C:/libmpv"
                "$ENV{USERPROFILE}/mpv-dev"
            PATH_SUFFIXES lib
        )
        
        if(MPV_INCLUDE_DIR AND MPV_LIBRARY)
            set(MPV_FOUND TRUE)
            set(MPV_INCLUDE_DIRS ${MPV_INCLUDE_DIR})
            get_filename_component(MPV_LIBRARY_DIRS ${MPV_LIBRARY} DIRECTORY)
            set(MPV_LIBRARIES ${MPV_LIBRARY})
            message(STATUS "Found MPV: ${MPV_LIBRARY}")
        else()
            message(WARNING "libmpv not found. Please download from https://sourceforge.net/projects/mpv-player-windows/files/libmpv/")
        endif()
    endif()
    
elseif(APPLE)
    # Try Homebrew paths
    find_path(MPV_INCLUDE_DIR mpv/client.h
        PATHS
            /opt/homebrew  # Apple Silicon
            /usr/local     # Intel
        PATH_SUFFIXES include
    )
    
    find_library(MPV_LIBRARY
        NAMES mpv
        PATHS
            /opt/homebrew
            /usr/local
        PATH_SUFFIXES lib
    )
    
    if(MPV_INCLUDE_DIR AND MPV_LIBRARY)
        set(MPV_FOUND TRUE)
        set(MPV_INCLUDE_DIRS ${MPV_INCLUDE_DIR})
        get_filename_component(MPV_LIBRARY_DIRS ${MPV_LIBRARY} DIRECTORY)
        set(MPV_LIBRARIES ${MPV_LIBRARY})
        message(STATUS "Found MPV: ${MPV_LIBRARY}")
    else()
        message(WARNING "libmpv not found. Install with: brew install mpv")
    endif()
    
else() # Linux
    find_path(MPV_INCLUDE_DIR mpv/client.h
        PATHS
            /usr/include
            /usr/local/include
    )
    
    find_library(MPV_LIBRARY
        NAMES mpv
        PATHS
            /usr/lib
            /usr/lib/x86_64-linux-gnu
            /usr/lib/i386-linux-gnu
            /usr/lib/aarch64-linux-gnu
            /usr/lib/arm-linux-gnueabihf
            /usr/local/lib
    )
    
    if(MPV_INCLUDE_DIR AND MPV_LIBRARY)
        set(MPV_FOUND TRUE)
        set(MPV_INCLUDE_DIRS ${MPV_INCLUDE_DIR})
        get_filename_component(MPV_LIBRARY_DIRS ${MPV_LIBRARY} DIRECTORY)
        set(MPV_LIBRARIES ${MPV_LIBRARY})
        message(STATUS "Found MPV: ${MPV_LIBRARY}")
    else()
        message(WARNING "libmpv not found. Install with: sudo apt-get install libmpv-dev")
    endif()
endif()

# Create library target
add_library(${LIBNAME} SHARED)

target_sources(${LIBNAME}
    PRIVATE
    src/register_types.cpp
    src/register_types.h
    src/mpv_player.cpp
    src/mpv_player.h
    src/thumnail_player.cpp
    src/thumnail_player.h
)

# Add MPV include directories and libraries
if(MPV_FOUND)
    target_include_directories(${LIBNAME} PRIVATE ${MPV_INCLUDE_DIRS})
    target_link_directories(${LIBNAME} PRIVATE ${MPV_LIBRARY_DIRS})
    target_link_libraries(${LIBNAME} PRIVATE ${MPV_LIBRARIES})
else()
    message(FATAL_ERROR "MPV library not found!")
endif()

# Fetch documentation files
file(GLOB_RECURSE DOC_XML LIST_DIRECTORIES NO CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/doc_classes/*.xml")

# Add doc data for editor builds
if(DOC_XML)
    if(GODOTCPP_TARGET MATCHES "editor|template_debug")
        target_doc_sources(${LIBNAME} ${DOC_XML})
    endif()
endif()

target_link_libraries(${LIBNAME} PRIVATE godot-cpp)

# Require C++17
set_property(TARGET ${LIBNAME} PROPERTY CXX_STANDARD 17)

set_target_properties(${LIBNAME}
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "$<1:${PROJECT_SOURCE_DIR}/bin/${GODOTCPP_PLATFORM}>"
    PREFIX ""
    OUTPUT_NAME "${LIBNAME}${GODOTCPP_SUFFIX}"
)

set(GODOT_PROJECT_BINARY_DIR "${PROJECT_SOURCE_DIR}/${GODOT_PROJECT_DIR}/bin/${GODOTCPP_PLATFORM}")

# Copy built library to demo folder
add_custom_command(TARGET ${LIBNAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove -f "${GODOT_PROJECT_BINARY_DIR}/$<TARGET_FILE_NAME:${LIBNAME}>"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${LIBNAME}>" "${GODOT_PROJECT_BINARY_DIR}/$<TARGET_FILE_NAME:${LIBNAME}>"
)

# On Windows, also copy the DLL
if(WIN32 AND MPV_FOUND)
    get_filename_component(MPV_DLL_DIR ${MPV_LIBRARY_DIRS} ABSOLUTE)
    set(MPV_DLL "${MPV_DLL_DIR}/libmpv-2.dll")
    
    if(EXISTS ${MPV_DLL})
        if (NOT EXISTS "${GODOT_PROJECT_BINARY_DIR}/libmpv-2.dll")
                #add_custom_command(TARGET ${LIBNAME} POST_BUILD
                #COMMAND ${CMAKE_COMMAND} -E copy "${MPV_DLL}" "${GODOT_PROJECT_BINARY_DIR}/libmpv-2.dll"
                #COMMENT "Copying libmpv-2.dll to demo folder"
                #)
        endif()
        message(STATUS "Will copy libmpv-2.dll to demo folder")
    else()
        message(WARNING "libmpv-2.dll not found at ${MPV_DLL}")
    endif()
endif()

# On macOS, set rpath
if(APPLE)
    set_target_properties(${LIBNAME} PROPERTIES
        INSTALL_RPATH "@loader_path"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()
