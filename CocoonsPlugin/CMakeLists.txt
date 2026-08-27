cmake_minimum_required(VERSION 3.20)
project(CocoonsPlugin
    VERSION 1.1.0
    DESCRIPTION "Cocoons: iOS/macOS Code Obfuscation LLVM Plugin"
    LANGUAGES CXX C
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(LLVM REQUIRED CONFIG
    PATHS
        ${LLVM_DIR}
        $ENV{LLVM_DIR}
        "/usr/local/opt/llvm@21/lib/cmake/llvm"
        "/usr/local/opt/llvm/lib/cmake/llvm"
        "$ENV{HOME}/Library/Developer/llvm-21/lib/cmake/llvm"
        "$ENV{HOME}/Library/Developer/Toolchains/llvm-21.1.8.xctoolchain/usr/lib/cmake/llvm"
)

message(STATUS "================ CocoonsPlugin ================")
message(STATUS "  LLVM Version    : ${LLVM_PACKAGE_VERSION}")
message(STATUS "  LLVM DIR        : ${LLVM_DIR}")
message(STATUS "  LLVM BIN        : ${LLVM_TOOLS_BINARY_DIR}")
message(STATUS "  Build Type      : ${CMAKE_BUILD_TYPE}")
message(STATUS "================================================")

if(NOT LLVM_PACKAGE_VERSION VERSION_GREATER_EQUAL "20.0")
    message(FATAL_ERROR "CocoonsPlugin requires LLVM >= 20.0, current = ${LLVM_PACKAGE_VERSION}")
endif()
if(NOT LLVM_PACKAGE_VERSION VERSION_GREATER_EQUAL "21.0")
    message(WARNING "CocoonsPlugin 推荐使用 LLVM >= 21.0，当前 = ${LLVM_PACKAGE_VERSION}")
endif()

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)
include(HandleLLVMOptions)

add_definitions(${LLVM_DEFINITIONS})

if(NOT LLVM_ENABLE_RTTI)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
endif()

include_directories(
    ${LLVM_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_llvm_pass_plugin(CocoonsPlugin
    lib/CocoonsPlugin.cpp
    lib/Config.cpp
    lib/StringObfuscationPass.cpp
    lib/SubstitutionPass.cpp
    lib/FlatteningPass.cpp
    lib/AntiDebuggingPass.cpp
)

target_include_directories(CocoonsPlugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

llvm_map_components_to_libnames(_COCOONS_LLVM_LIBS
    Core
    Support
    TransformUtils
    Passes
)

target_link_libraries(CocoonsPlugin PRIVATE
    ${_COCOONS_LLVM_LIBS}
)

set_target_properties(CocoonsPlugin PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "CocoonsPlugin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist"
    SUFFIX ".dylib"
)

install(TARGETS CocoonsPlugin
    LIBRARY DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/dist
    RUNTIME DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/dist
    COMPONENT CocoonsPlugin_Runtime
)

install(PROGRAMS
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/cocoons-clang/cocoons-clang.py
    DESTINATION ${CMAKE_CURRENT_SOURCE_DIR}/dist
    RENAME cocoons-clang
    OPTIONAL
)
