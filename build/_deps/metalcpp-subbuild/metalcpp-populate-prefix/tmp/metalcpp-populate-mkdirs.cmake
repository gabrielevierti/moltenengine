# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Users/gabrielevierti/Desktop/Metal Game Engine/MoltenMetalEngine/build/_deps/metalcpp-src")
  file(MAKE_DIRECTORY "/Users/gabrielevierti/Desktop/Metal Game Engine/MoltenMetalEngine/build/_deps/metalcpp-src")
endif()
file(MAKE_DIRECTORY
  "/Users/gabrielevierti/Desktop/Metal Game Engine/MoltenMetalEngine/build/_deps/metalcpp-build"
  "/Users/gabrielevierti/Desktop/Metal Game Engine/MoltenMetalEngine/build/_deps/metalcpp-subbuild/metalcpp-populate-prefix"
  "/Users/gabrielevierti/Desktop/Metal Game Engine/MoltenMetalEngine/build/_deps/metalcpp-subbuild/metalcpp-populate-prefix/tmp"
  "/Users/gabrielevierti/Desktop/Metal Game Engine/MoltenMetalEngine/build/_deps/metalcpp-subbuild/metalcpp-populate-prefix/src/metalcpp-populate-stamp"
  "/Users/gabrielevierti/Desktop/Metal Game Engine/MoltenMetalEngine/build/_deps/metalcpp-subbuild/metalcpp-populate-prefix/src"
  "/Users/gabrielevierti/Desktop/Metal Game Engine/MoltenMetalEngine/build/_deps/metalcpp-subbuild/metalcpp-populate-prefix/src/metalcpp-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/gabrielevierti/Desktop/Metal Game Engine/MoltenMetalEngine/build/_deps/metalcpp-subbuild/metalcpp-populate-prefix/src/metalcpp-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/gabrielevierti/Desktop/Metal Game Engine/MoltenMetalEngine/build/_deps/metalcpp-subbuild/metalcpp-populate-prefix/src/metalcpp-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
