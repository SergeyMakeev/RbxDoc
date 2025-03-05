cmake_minimum_required(VERSION 3.6)

set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(PROJ_NAME RbxDoc)
project(${PROJ_NAME})

set (CMAKE_CXX_STANDARD 17)


set(RBX_DOC_SOURCES 
  main.cpp
)
set (EXE_NAME ${PROJ_NAME})
add_executable(${EXE_NAME} ${RBX_DOC_SOURCES})
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${EXE_NAME})

if(MSVC)
  target_compile_options(${EXE_NAME} PRIVATE "$<$<CONFIG:Release>:/Zi>")
  target_link_options(${EXE_NAME} PRIVATE "$<$<CONFIG:Release>:/DEBUG>")
  target_link_options(${EXE_NAME} PRIVATE "$<$<CONFIG:Release>:/OPT:REF>")
  target_link_options(${EXE_NAME} PRIVATE "$<$<CONFIG:Release>:/OPT:ICF>")
endif()



# add zstd
set(ZSTD_BUILD_STATIC ON)
set(ZSTD_BUILD_SHARED OFF)
set(ZSTD_BUILD_PROGRAMS OFF)
add_subdirectory("${PROJECT_SOURCE_DIR}/extern/zstd/build/cmake")
target_link_libraries(
    ${PROJ_NAME}
    PRIVATE
    libzstd_static
)
target_include_directories(
    ${PROJ_NAME}
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
    ${PROJ_NAME}
    PRIVATE
    lz4_static
)


#add pugi xml
add_subdirectory("${PROJECT_SOURCE_DIR}/extern/pugixml")
target_link_libraries(
    ${PROJ_NAME}
    PRIVATE
    pugixml-static
)



