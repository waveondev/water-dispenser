# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Users/Yuna/.espressif/v5.5.4/esp-idf/components/bootloader/subproject")
  file(MAKE_DIRECTORY "/Users/Yuna/.espressif/v5.5.4/esp-idf/components/bootloader/subproject")
endif()
file(MAKE_DIRECTORY
  "/Users/Yuna/Documents/Dev/fw/water-dispenser/build/bootloader"
  "/Users/Yuna/Documents/Dev/fw/water-dispenser/build/bootloader-prefix"
  "/Users/Yuna/Documents/Dev/fw/water-dispenser/build/bootloader-prefix/tmp"
  "/Users/Yuna/Documents/Dev/fw/water-dispenser/build/bootloader-prefix/src/bootloader-stamp"
  "/Users/Yuna/Documents/Dev/fw/water-dispenser/build/bootloader-prefix/src"
  "/Users/Yuna/Documents/Dev/fw/water-dispenser/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/Yuna/Documents/Dev/fw/water-dispenser/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/Yuna/Documents/Dev/fw/water-dispenser/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
