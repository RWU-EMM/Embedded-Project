# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "D:/RWU/Embedded-Project/task_3/rpi_pico_w/sdk/picotool-cache/picotool-src")
  file(MAKE_DIRECTORY "D:/RWU/Embedded-Project/task_3/rpi_pico_w/sdk/picotool-cache/picotool-src")
endif()
file(MAKE_DIRECTORY
  "D:/RWU/Embedded-Project/task_3/rpi_pico_w/sdk/picotool-cache/picotool-build"
  "D:/RWU/Embedded-Project/task_3/rpi_pico_w/sdk/picotool-cache/picotool-subbuild/picotool-populate-prefix"
  "D:/RWU/Embedded-Project/task_3/rpi_pico_w/sdk/picotool-cache/picotool-subbuild/picotool-populate-prefix/tmp"
  "D:/RWU/Embedded-Project/task_3/rpi_pico_w/sdk/picotool-cache/picotool-subbuild/picotool-populate-prefix/src/picotool-populate-stamp"
  "D:/RWU/Embedded-Project/task_3/rpi_pico_w/sdk/picotool-cache/picotool-subbuild/picotool-populate-prefix/src"
  "D:/RWU/Embedded-Project/task_3/rpi_pico_w/sdk/picotool-cache/picotool-subbuild/picotool-populate-prefix/src/picotool-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "D:/RWU/Embedded-Project/task_3/rpi_pico_w/sdk/picotool-cache/picotool-subbuild/picotool-populate-prefix/src/picotool-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "D:/RWU/Embedded-Project/task_3/rpi_pico_w/sdk/picotool-cache/picotool-subbuild/picotool-populate-prefix/src/picotool-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
