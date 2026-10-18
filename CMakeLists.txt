cmake_minimum_required(VERSION 3.16)
project(s376-visual VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(S376_WITH_OSG "Build the OSG / osgEarth renderer" ON)
option(S376_WITH_GDAL "Load Copernicus GeoTIFF DEM/imagery through GDAL" ON)

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)

set(S376_CORE_SRC
    src/vaxfloat.cpp
    src/log.cpp
    src/safety.cpp
    src/xt4.cpp
    src/config.cpp
    src/state.cpp
    src/host.cpp
    src/synth.cpp
    src/geo.cpp
    src/diagnostics.cpp
    src/render.cpp
    src/terrain.cpp
    src/dxt.cpp
    src/airport.cpp
)

if(S376_WITH_GDAL)
    find_package(GDAL QUIET)
    if(GDAL_FOUND)
        add_compile_definitions(S376_HAS_GDAL)
        if(DEFINED GDAL_VERSION)
            message(STATUS "GDAL ${GDAL_VERSION} — GeoTIFF DEM/imagery enabled")
        else()
            message(STATUS "GDAL found — GeoTIFF DEM/imagery enabled")
        endif()
    else()
        message(STATUS "GDAL not found — portable .s376dem + synth DEM only")
    endif()
endif()

if(S376_WITH_OSG)
    find_package(OpenSceneGraph COMPONENTS osg osgDB osgGA osgViewer osgUtil osgText)
    if(OPENSCENEGRAPH_FOUND)
        add_compile_definitions(S376_HAS_OSG)
        list(APPEND S376_CORE_SRC src/render_osg.cpp)
        message(STATUS "OSG ${OPENSCENEGRAPH_VERSION} — renderer enabled")
        find_package(osgEarth QUIET)
        if(osgEarth_FOUND OR TARGET osgEarth)
            add_compile_definitions(S376_HAS_OSGEARTH)
            message(STATUS "osgEarth found — streamed tiles enabled")
        else()
            message(STATUS "osgEarth not found — procedural globe (see docs/DESIGN.md)")
        endif()
    else()
        message(STATUS "OSG not found — headless core only")
    endif()
endif()

add_library(s376_core STATIC ${S376_CORE_SRC})
target_include_directories(s376_core PUBLIC ${CMAKE_SOURCE_DIR}/include)
if(WIN32)
    target_link_libraries(s376_core PUBLIC ws2_32)
endif()
if(OPENSCENEGRAPH_FOUND)
    target_include_directories(s376_core PUBLIC ${OPENSCENEGRAPH_INCLUDE_DIRS})
    target_link_libraries(s376_core PUBLIC ${OPENSCENEGRAPH_LIBRARIES})
    if(osgEarth_FOUND)
        target_link_libraries(s376_core PUBLIC osgEarth)
    endif()
endif()
if(GDAL_FOUND)
    if(TARGET GDAL::GDAL)
        target_link_libraries(s376_core PUBLIC GDAL::GDAL)
    else()
        target_include_directories(s376_core PUBLIC ${GDAL_INCLUDE_DIRS})
        target_link_libraries(s376_core PUBLIC ${GDAL_LIBRARIES})
    endif()
endif()

add_executable(s376-visual src/main.cpp)
target_link_libraries(s376-visual PRIVATE s376_core)

add_executable(s376-synth src/synth_main.cpp)
target_link_libraries(s376-synth PRIVATE s376_core)

add_executable(s376-doctor src/doctor_main.cpp)
target_link_libraries(s376-doctor PRIVATE s376_core)

add_executable(s376-cache src/cache_main.cpp)
target_link_libraries(s376-cache PRIVATE s376_core)

add_executable(s376-tests
    tests/test_main.cpp
    tests/test_vaxfloat.cpp
    tests/test_xt4.cpp
    tests/test_safety.cpp
    tests/test_config.cpp
    tests/test_geo.cpp
    tests/test_synth_loop.cpp
    tests/test_channels.cpp
    tests/test_terrain.cpp
    tests/test_airport.cpp
)
target_link_libraries(s376-tests PRIVATE s376_core)

enable_testing()
add_test(NAME unit COMMAND s376-tests)
set_tests_properties(unit PROPERTIES
    ENVIRONMENT "S376_ALLOW_SIM_LAN=;S376_ALLOW_LAN=;S376_LOG_DIR=${CMAKE_BINARY_DIR}/logs"
)

# Copy default config next to the binaries for out-of-tree runs.
file(COPY ${CMAKE_SOURCE_DIR}/config DESTINATION ${CMAKE_BINARY_DIR})
file(COPY ${CMAKE_SOURCE_DIR}/data DESTINATION ${CMAKE_BINARY_DIR})
