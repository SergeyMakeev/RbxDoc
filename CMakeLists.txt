cmake_minimum_required(VERSION 3.6)
project(rbxdoc)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()



# static library
# ---------------------------------------------------------------------------------------
include(${PROJECT_SOURCE_DIR}/rbx-doc/Sources.cmake)

add_library(rbxdoc-static STATIC ${LIB-SOURCES} ${LIB-HEADERS})
target_include_directories(rbxdoc-static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/rbx-doc/)

# library dependencies
# add zstd
set(ZSTD_BUILD_STATIC ON)
set(ZSTD_BUILD_SHARED OFF)
set(ZSTD_BUILD_PROGRAMS OFF)
add_subdirectory("${PROJECT_SOURCE_DIR}/extern/zstd/build/cmake")
target_link_libraries(
    rbxdoc-static
    PRIVATE
    libzstd_static
)
target_include_directories(
    rbxdoc-static
    PRIVATE
    ${zstd_SOURCE_DIR}/lib
)


#add lz4
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(LZ4_BUILD_CLI OFF)
set(LZ4_BUILD_LEGACY_LZ4C OFF)
set(LZ4_BUNDLED_MODE ON)
add_subdirectory("${PROJECT_SOURCE_DIR}/extern/lz4/build/cmake")
target_link_libraries(
    rbxdoc-static
    PRIVATE
    lz4_static
)

# ---------------------------------------------------------------------------------------

# unit test
option(BUILD_RBX_DOC_TESTS "Build the unit tests" ON)

if(BUILD_RBX_DOC_TESTS)
  include(${PROJECT_SOURCE_DIR}/unittest/Sources.cmake)
  add_executable(rbxdoc-test ${TEST-SOURCES} ${TEST-HEADERS})
  target_link_libraries(rbxdoc-test PRIVATE rbxdoc-static)
  set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT rbxdoc-test)
endif()

