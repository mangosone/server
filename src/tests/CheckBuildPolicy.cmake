# SPDX-License-Identifier: GPL-3.0-or-later
#
# MaNGOS is a full featured server for World of Warcraft, supporting
# the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
#
# Copyright (C) 2005-2026 MaNGOS <https://www.getmangos.eu>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

file(READ "${SOURCE_ROOT}/CMakeLists.txt" ROOT_CMAKE)
file(READ "${SOURCE_ROOT}/src/mangosd/mangosd.cpp" MANGOSD_SOURCE)
file(READ "${SOURCE_ROOT}/src/CMakeLists.txt" SRC_CMAKE)
file(READ "${SOURCE_ROOT}/.github/workflows/core_linux_build.yml" LINUX_CI)
file(READ "${SOURCE_ROOT}/.github/workflows/core_windows_build.yml" WINDOWS_CI)

foreach(REQUIRED_TEXT
    "find_package(OpenSSL 3.0 REQUIRED)"
    "OPENSSL_VERSION VERSION_GREATER_EQUAL \"4.0.0\""
    "OpenSSL 4.x support is intentionally deferred until the OpenSSL 4.2 LTS migration")
  string(FIND "${ROOT_CMAKE}" "${REQUIRED_TEXT}" POSITION)
  if(POSITION EQUAL -1)
    message(FATAL_ERROR "Missing OpenSSL build policy: ${REQUIRED_TEXT}")
  endif()
endforeach()

# The provider clause is gone, with the provider it guarded.
#
# It required mangosd to leave through `return 1` when the OpenSSL provider manager
# failed to initialise -- a good rule, because returning 0 tells a supervisor the
# process exited cleanly and nothing restarts it. There is no longer anything for it to
# describe: RC4 was the only algorithm this tree fetched from the legacy provider, it is
# implemented in the tree now, and mangosd performs no fallible crypto init at start-up
# at all. SHA-1, SHA-256 and the bignum work come from the default provider, which is
# inside libcrypto and cannot be absent while libcrypto is present.
#
# Deleted rather than loosened. A policy that matches nothing is worse than no policy:
# it passes for the right reason today and for the wrong reason the moment someone
# reintroduces a start-up check and forgets its exit code.
string(FIND "${MANGOSD_SOURCE}" "OpenSSLProvider" POSITION)
if(NOT POSITION EQUAL -1)
  message(FATAL_ERROR
    "mangosd reaches for OpenSSLProvider again; if a fallible crypto init is back, so "
    "must be the rule that it exits with 1")
endif()

string(FIND "${SRC_CMAKE}" "Upstream realmd passes the *file*" POSITION)
if(NOT POSITION EQUAL -1)
  message(FATAL_ERROR "Obsolete external realmd VersionInfo workaround remains")
endif()

foreach(CI_TEXT IN ITEMS "${LINUX_CI}" "${WINDOWS_CI}")
  foreach(REQUIRED_TEXT "-DWITH_TESTS=1" "ctest --test-dir")
    string(FIND "${CI_TEXT}" "${REQUIRED_TEXT}" POSITION)
    if(POSITION EQUAL -1)
      message(FATAL_ERROR "CI does not run tests: ${REQUIRED_TEXT}")
    endif()
  endforeach()
endforeach()

string(REGEX MATCH "OPENSSL_VERSION: 3\\.[0-9]+\\.[0-9]+"
  OPENSSL_PIN "${WINDOWS_CI}")
if(NOT OPENSSL_PIN)
  message(FATAL_ERROR "Windows CI is not pinned to OpenSSL 3.x")
endif()
