#===--- CMakeLists.txt - Build the TensorFlow support library ------------===#
#
# This source file is part of the Swift.org open source project
#
# Copyright (c) 2014 - 2017 Apple Inc. and the Swift project authors
# Licensed under Apache License v2.0 with Runtime Library Exception
#
# See https://swift.org/LICENSE.txt for license information
# See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
#
#===----------------------------------------------------------------------===#
#
# SWIFT_ENABLE_TENSORFLOW
#
#===----------------------------------------------------------------------===#

if(NOT SWIFT_ENABLE_TENSORFLOW)
  return()
endif()

find_package(TensorFlow REQUIRED)
message(STATUS "Building TensorFlow.")

set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(swift_stdlib_compile_flags "${SWIFT_RUNTIME_SWIFT_COMPILE_FLAGS}")
list(APPEND swift_stdlib_compile_flags "-Xllvm" "-sil-inline-generics")
list(APPEND swift_stdlib_compile_flags "-Xllvm" "-sil-partial-specialization")
list(APPEND swift_stdlib_compile_flags "-Xfrontend" "-enable-sil-ownership")
list(APPEND swift_stdlib_compile_flags "-force-single-frontend-invocation")
# FIXME(SR-7972): Some tests fail when TensorFlow is optimized.
list(APPEND swift_stdlib_compile_flags "-Onone")
list(APPEND swift_stdlib_compile_flags "-DCOMPILING_TENSORFLOW_MODULE")

set(SOURCES
  CompilerRuntime.swift
  CompositeMath.swift
  Dataset.swift
  DataTypes.swift
  Execution.swift
  Gradients.swift
  Ops.swift
  ShapedArray.swift
  StringOps.swift
  StringTensor.swift
  Tensor.swift
  TensorGroup.swift
  TensorHandle.swift
  TensorProtocol.swift
  TensorShape.swift
  Utilities.swift
  Threading.swift
  # NumPy bridging for `ShapedArray` and `Tensor`.
  NumpyConversion.swift)

# Copy TensorFlow bindings file, if it exists.
if (TENSORFLOW_SWIFT_BINDINGS)
  list(APPEND SOURCES "${TENSORFLOW_SWIFT_BINDINGS}")
endif()

# Copy TensorFlow high-level API sources, if they exist.
if (TENSORFLOW_SWIFT_APIS)
  file(GLOB_RECURSE TENSORFLOW_SWIFT_API_SOURCES
    "${TENSORFLOW_SWIFT_APIS}/Sources/*.swift")
  message(STATUS "Using TensorFlow high-level APIs library.")
  list(APPEND SOURCES "${TENSORFLOW_SWIFT_API_SOURCES}")
endif()

# When Python exists, NumpyConversion.swift imports it, so it must be
# available at link time.
set(TENSORFLOW_DEPENDS_PYTHON)
if (SWIFT_PYTHON_EXISTS)
  list(APPEND TENSORFLOW_DEPENDS_PYTHON SWIFT_MODULE_DEPENDS Python)
endif()

add_swift_target_library(swiftTensorFlow ${SWIFT_STDLIB_LIBRARY_BUILD_TYPES} IS_STDLIB
  "${SOURCES}"

  INCORPORATE_OBJECT_LIBRARIES swiftCTensorFlow
  TARGET_SDKS OSX LINUX
  PRIVATE_LINK_LIBRARIES "${TF_LIBRARIES}"
  SWIFT_MODULE_DEPENDS SwiftOnoneSupport
  SWIFT_MODULE_DEPENDS_IOS Darwin
  SWIFT_MODULE_DEPENDS_OSX Darwin
  SWIFT_MODULE_DEPENDS_TVOS Darwin
  SWIFT_MODULE_DEPENDS_WATCHOS Darwin
  SWIFT_MODULE_DEPENDS_LINUX Glibc
  SWIFT_MODULE_DEPENDS_FREEBSD Glibc
  SWIFT_MODULE_DEPENDS_CYGWIN Glibc
  SWIFT_MODULE_DEPENDS_HAIKU Glibc
  ${TENSORFLOW_DEPENDS_PYTHON}
  SWIFT_COMPILE_FLAGS "${swift_stdlib_compile_flags}"
  LINK_FLAGS "${SWIFT_RUNTIME_SWIFT_LINK_FLAGS}"
  INSTALL_IN_COMPONENT stdlib
  EXTRA_RPATHS "${SWIFT_TENSORFLOW_TARGET_LIB_DIR}")
