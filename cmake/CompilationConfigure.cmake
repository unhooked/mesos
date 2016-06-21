# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# CONFIGURE COMPILATION.
########################
option(BUILD_SHARED_LIBS "Build shared libraries (DLLs)." FALSE)

string(COMPARE EQUAL ${CMAKE_SYSTEM_NAME} "Linux" LINUX)

# Check that we are targeting a 64-bit architecture.
if (NOT (CMAKE_SIZEOF_VOID_P EQUAL 8))
  message(
    FATAL_ERROR
    "Mesos requires that we compile to a 64-bit target. Following are some "
    "examples of how to accomplish this on some well-used platforms:\n"
    "  * Linux: (on gcc) set `CMAKE_CXX_FLAGS` to include `-m64`:\n"
    "    `cmake -DCMAKE_CXX_FLAGS=-m64 `.\n"
    "  * Windows: use the VS win64 CMake generator:\n"
    "    `cmake -G \"Visual Studio 10 Win64\"`.\n"
    "  * OS X: add `x86_64` to the `CMAKE_OSX_ARCHITECTURES`:\n"
    "    `cmake -DCMAKE_OSX_ARCHITECTURES=x86_64`.\n")
endif (NOT (CMAKE_SIZEOF_VOID_P EQUAL 8))

if (_DEBUG)
  set(CMAKE_BUILD_TYPE Debug)
endif (_DEBUG)

# Make sure C++ 11 features we need are supported. This is split into two
# cases: Windows and "other platforms".
#   * For "other platforms", we simply check if the C++11 flags work
#   * For Windows, it looks like (1) C++11 is enabled by default on MSVC 1900 or
#     later, and (2) C++11 is totally broken for 1800 or earlier (i.e., Mesos
#     will not compile on MSVC pre-1900). So, when in Windows, we just check the
#     MSVC version, and don't try to check or pass in C++11 flags at all.
CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
if (WIN32)
  # Windows case first.

  # We don't support compilation against mingw headers (which, e.g., Clang on
  # Windows does at this point), because this is likely to cost us more effort
  # to support than it will be worth at least in the short term.
  if (NOT CMAKE_CXX_COMPILER_ID MATCHES MSVC)
    message(
      WARNING
      "Mesos does not support compiling on Windows with "
      "${CMAKE_CXX_COMPILER_ID}. Please use MSVC.")
  endif (NOT CMAKE_CXX_COMPILER_ID MATCHES MSVC)

  # MSVC 1900 supports C++11; earlier versions don't. So, warn if you try to
  # use anything else.
  if (${MSVC_VERSION} LESS 1900)
    message(
      WARNING
      "Mesos does not support compiling on MSVC versions earlier than 1900. "
      "Please use MSVC 1900 (included with Visual Studio 2015 or later).")
  endif (${MSVC_VERSION} LESS 1900)

  # COFF/PE and friends are somewhat limited in the number of sections they
  # allow for an object file. We use this to avoid those problems.
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /bigobj -DGOOGLE_GLOG_DLL_DECL= -DCURL_STATICLIB -D_SCL_SECURE_NO_WARNINGS /vd2 /MP")

  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
elseif (COMPILER_SUPPORTS_CXX11)
  # Finally, on non-Windows platforms, we must check that the current compiler
  # supports C++11.
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
else (WIN32)
  message(
    FATAL_ERROR
    "The compiler ${CMAKE_CXX_COMPILER} does not support the `-std=c++11` "
    "flag. Please use a different C++ compiler.")
endif (WIN32)

# Convenience flags to simplify Windows support in C++ source, used to #ifdef
# out some platform-specific parts of Mesos. We choose to define a new flag
# rather than using an existing flag (e.g., `_WIN32`) because we want to give
# the build system fine-grained control over what code is #ifdef'd out in  the
# future. Using only flags defined by our build system to control this logic is
# the clearest and most stable way of accomplishing this.
if (WIN32)
  add_definitions(-D__WINDOWS__)
  add_definitions(-DHAVE_LIBZ)
endif (WIN32)


# Configure directory structure for different platforms.
########################################################
if (NOT WIN32)
  set(EXEC_INSTALL_PREFIX  ${CMAKE_INSTALL_PREFIX})
  set(SHARE_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX}/share)
  set(DATA_INSTALL_PREFIX  ${SHARE_INSTALL_PREFIX}/mesos)

  set(LIBEXEC_INSTALL_DIR     ${EXEC_INSTALL_PREFIX}/libexec)
  set(PKG_LIBEXEC_INSTALL_DIR ${LIBEXEC_INSTALL_DIR}/mesos)
  set(LIB_INSTALL_DIR         ${EXEC_INSTALL_PREFIX}/libmesos)
else (NOT WIN32)
  # TODO(hausdorff): (MESOS-5455) These are placeholder values. Transition away
  # from them.
  set(EXEC_INSTALL_PREFIX     "WARNINGDONOTUSEME")
  set(LIBEXEC_INSTALL_DIR     "WARNINGDONOTUSEME")
  set(PKG_LIBEXEC_INSTALL_DIR "WARNINGDONOTUSEME")
  set(LIB_INSTALL_DIR         "WARNINGDONOTUSEME")
  set(TEST_LIB_EXEC_DIR       "WARNINGDONOTUSEME")
  set(PKG_MODULE_DIR          "WARNINGDONOTUSEME")
  set(S_BIN_DIR               "WARNINGDONOTUSEME")
endif (NOT WIN32)

# Add preprocessor definitions required to build third-party libraries.
#######################################################################
if (WIN32)
  # Windows-specific workaround for a glog issue documented here[1].
  # Basically, Windows.h and glog/logging.h both define ERROR. Since we don't
  # need the Windows ERROR, we can use this flag to avoid defining it at all.
  # Unlike the other fix (defining GLOG_NO_ABBREVIATED_SEVERITIES), this fix
  # is guaranteed to require no changes to the original Mesos code. See also
  # the note in the code itself[2].
  #
  # [1] https://google-glog.googlecode.com/svn/trunk/doc/glog.html#windows
  # [2] https://code.google.com/p/google-glog/source/browse/trunk/src/windows/glog/logging.h?r=113
  add_definitions(-DNOGDI)
  add_definitions(-DNOMINMAX)
else (WIN32)
  # TODO(hausdorff): Remove our hard dependency on SASL, as some platforms
  # (namely Windows) will not support it in the forseeable future. As a
  # stop-gap, we conditionally compile the code in libmesos that depends on
  # SASL, by always setting `HAS_AUTHENTICATION` on non-Windows platforms, and
  # never setting it on Windows platforms. This means that non-Windows builds
  # of libmesos will still take a hard dependency on SASL, while Windows builds
  # won't. Currently, the dependency is still assumed throughout the tests,
  # though the plan is to remove this hard dependency as well. See MESOS-5450.
  add_definitions(-DHAS_AUTHENTICATION=1)
endif (WIN32)

# Enable the INT64 support for PicoJSON.
add_definitions(-DPICOJSON_USE_INT64)
# NOTE: PicoJson requires __STDC_FORMAT_MACROS to be defined before importing
# 'inttypes.h'.  Since other libraries may also import this header, it must
# be globally defined so that PicoJson has access to the macros, regardless
# of the order of inclusion.
add_definitions(-D__STDC_FORMAT_MACROS)

add_definitions(-DPKGLIBEXECDIR="${PKG_LIBEXEC_INSTALL_DIR}")
add_definitions(-DLIBDIR="${LIB_INSTALL_DIR}")
add_definitions(-DVERSION="${PACKAGE_VERSION}")
add_definitions(-DPKGDATADIR="${DATA_INSTALL_PREFIX}")
