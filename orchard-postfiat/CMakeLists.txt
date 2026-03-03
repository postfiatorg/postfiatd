# CMake integration for orchard-postfiat Rust crate
cmake_minimum_required(VERSION 3.16)

# Find Cargo (Rust build tool)
find_program(CARGO cargo REQUIRED)
if(NOT CARGO)
    message(FATAL_ERROR "Cargo (Rust build tool) not found. Please install Rust from https://rustup.rs/")
endif()

# Determine build type for Rust
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(RUST_BUILD_TYPE "debug")
    set(RUST_BUILD_FLAG "")
    set(RUST_TARGET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/target/debug")
else()
    set(RUST_BUILD_TYPE "release")
    set(RUST_BUILD_FLAG "--release")
    set(RUST_TARGET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/target/release")
endif()

set(RUST_LIB_NAME "orchard_postfiat")
if(WIN32)
    set(RUST_LIB_FILE "${RUST_TARGET_DIR}/${RUST_LIB_NAME}.lib")
elseif(APPLE)
    set(RUST_LIB_FILE "${RUST_TARGET_DIR}/lib${RUST_LIB_NAME}.a")
else()
    set(RUST_LIB_FILE "${RUST_TARGET_DIR}/lib${RUST_LIB_NAME}.a")
endif()

# CXX bridge generated header location
# Note: cxx-build generates files in target/cxxbridge, not target/debug/cxxbridge
set(CXX_BRIDGE_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/target/cxxbridge/orchard-postfiat/src/ffi/bridge.rs.h")
set(CXX_BRIDGE_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/target/cxxbridge/orchard-postfiat/src/ffi/bridge.rs.cc")

# Custom command to build the Rust library
add_custom_command(
    OUTPUT ${RUST_LIB_FILE} ${CXX_BRIDGE_HEADER} ${CXX_BRIDGE_SOURCE}
    COMMAND ${CARGO} build ${RUST_BUILD_FLAG}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Building Rust orchard-postfiat crate (${RUST_BUILD_TYPE})"
    DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml
        ${CMAKE_CURRENT_SOURCE_DIR}/build.rs
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lib.rs
        ${CMAKE_CURRENT_SOURCE_DIR}/src/bundle_real.rs
        ${CMAKE_CURRENT_SOURCE_DIR}/src/bundle_stub.rs
        ${CMAKE_CURRENT_SOURCE_DIR}/src/bundle_builder.rs
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ffi/mod.rs
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ffi/bridge.rs
)

# Custom target for the Rust build
add_custom_target(orchard_postfiat_rust
    DEPENDS ${RUST_LIB_FILE} ${CXX_BRIDGE_HEADER}
)

# Import the Rust library as an imported library
add_library(orchard_postfiat_static STATIC IMPORTED GLOBAL)
set_target_properties(orchard_postfiat_static PROPERTIES
    IMPORTED_LOCATION ${RUST_LIB_FILE}
)
add_dependencies(orchard_postfiat_static orchard_postfiat_rust)

# Add the cxx bridge source to a library
add_library(orchard_postfiat_cxx STATIC
    ${CXX_BRIDGE_SOURCE}
)
target_include_directories(orchard_postfiat_cxx PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/target/cxxbridge
)
add_dependencies(orchard_postfiat_cxx orchard_postfiat_rust)

# Set C++17 for cxx bridge
target_compile_features(orchard_postfiat_cxx PUBLIC cxx_std_17)

# Create a combined interface library
add_library(orchard_postfiat INTERFACE)
target_link_libraries(orchard_postfiat INTERFACE
    orchard_postfiat_static
    orchard_postfiat_cxx
)
target_include_directories(orchard_postfiat INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/target/cxxbridge
)

# On Linux, we need to link against system libraries that Rust uses
if(UNIX AND NOT APPLE)
    target_link_libraries(orchard_postfiat INTERFACE
        pthread
        dl
        m
    )
endif()

# On macOS, link against required frameworks
if(APPLE)
    target_link_libraries(orchard_postfiat INTERFACE
        "-framework Security"
        "-framework Foundation"
    )
endif()

# On Windows, link against required system libraries
if(WIN32)
    target_link_libraries(orchard_postfiat INTERFACE
        ws2_32
        userenv
        bcrypt
    )
endif()

message(STATUS "Configured orchard-postfiat Rust integration (${RUST_BUILD_TYPE})")
message(STATUS "  Rust library: ${RUST_LIB_FILE}")
message(STATUS "  CXX bridge header: ${CXX_BRIDGE_HEADER}")
