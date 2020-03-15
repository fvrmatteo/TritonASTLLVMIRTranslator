cmake_minimum_required(VERSION 3.4.3)

# Declare the CMake policy

if (POLICY CMP0025)
  cmake_policy(SET CMP0025 NEW)
endif ()

# Declare the project name

project(Translator)

# Enable the supported languages

enable_language(C CXX ASM)

# Enable C++17

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Compile it as debug if unspecified

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# Add the compilation options

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native")

# Search LLVM

find_package(LLVM REQUIRED CONFIG)

# Find the libraries that correspond to the LLVM components

set(LLVM_LIBRARIES LLVMCore
  LLVMSupport
  LLVMAnalysis
  LLVMipo
  LLVMIRReader
  LLVMBitReader
  LLVMBitWriter
  LLVMTransformUtils
  LLVMScalarOpts
  LLVMLTO)

# Add the include, definition and libraries directories

list(APPEND PROJECT_LIBRARIES ${LLVM_LIBRARIES})
list(APPEND PROJECT_DEFINITIONS ${LLVM_DEFINITIONS})
list(APPEND PROJECT_INCLUDEDIRECTORIES ${LLVM_INCLUDE_DIRS})
message(STATUS "LLVM libraries: " ${LLVM_LIBRARIES})
message(STATUS "LLVM includes: " ${LLVM_INCLUDE_DIRS})

# Search Boost

find_package(BOOST REQUIRED)

# Add the include directory

list(APPEND PROJECT_INCLUDEDIRECTORIES ${BOOST_INCLUDE_DIRS})
message(STATUS "Boost includes: " ${BOOST_INCLUDE_DIRS})

# Search Triton

find_package(TRITON REQUIRED)

# Add the include and libraries directories

list(APPEND PROJECT_LIBRARIES ${TRITON_LIBRARIES})
list(APPEND PROJECT_INCLUDEDIRECTORIES ${TRITON_INCLUDE_DIRS})
message(STATUS "Triton libraries: " ${TRITON_LIBRARIES})
message(STATUS "Triton includes: " ${TRITON_INCLUDE_DIRS})

# Search Capstone

find_package(CAPSTONE REQUIRED)

# Add the include and libraries directories

list(APPEND PROJECT_LIBRARIES ${CAPSTONE_LIBRARIES})
list(APPEND PROJECT_INCLUDEDIRECTORIES ${CAPSTONE_INCLUDE_DIRS})
message(STATUS "Capstone libraries: " ${CAPSTONE_LIBRARIES})
message(STATUS "Capstone includes: " ${CAPSTONE_INCLUDE_DIRS})

# Add our include folder

list(APPEND PROJECT_INCLUDEDIRECTORIES Include)

# Now build our tool

add_executable(${PROJECT_NAME} main.cpp
  Translator.cpp)

# Add all the dependiencies

target_link_libraries(${PROJECT_NAME} PUBLIC ${PROJECT_LIBRARIES})
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_SOURCE_DIR})
target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC ${PROJECT_INCLUDEDIRECTORIES})
target_compile_definitions(${PROJECT_NAME} PUBLIC ${PROJECT_DEFINITIONS} ${GLOBAL_DEFINITIONS} -DNOMINMAX)
target_compile_options(${PROJECT_NAME} PUBLIC ${GLOBAL_CXXFLAGS})

# Enable position independent code

set_property(TARGET ${PROJECT_NAME} PROPERTY POSITION_INDEPENDENT_CODE ON)