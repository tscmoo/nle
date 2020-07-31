cmake_minimum_required(VERSION 3.10)
project(libnethack)

if(CMAKE_BUILD_TYPE MATCHES Debug)
  message("Debug build.")
  # Unclear if this is even necessary. `dsymutil rlmain -o rlmain.dSYM` seems to
  # have done the trick.
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")
  set(CMAKE_XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym")
elseif(CMAKE_BUILD_TYPE MATCHES Release)
  message("Release build.")
else()
  message("Some other build type.")
endif()

add_compile_definitions(
  GCC_WARN
  NOCLIPPING
  NOMAIL
  NOTPARMDECL
  HACKDIR="/Users/hnr/nethackdir"
  DEFAULT_WINDOW_SYS="rl"
  DLB)

file(
  GLOB
  NETHACK_SRC
  "src/*.c"
  "sys/share/posixregex.c"
  "sys/share/ioctl.c"
  "sys/unix/unixunix.c"
  # "sys/share/unixtty.c"
  "sys/unix/unixres.c"
  "win/tty/*.c"
  "win/rl/winrl.cc")

add_subdirectory(third_party/deboost.context)

# libnethack library
add_library(nethack SHARED ${NETHACK_SRC})
set_target_properties(nethack PROPERTIES CXX_STANDARD 14)
target_include_directories(nethack PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
                                          /usr/local/include)
target_link_directories(nethack PUBLIC /usr/local/lib)
target_link_libraries(nethack PUBLIC ncurses m lua fcontext)

# dlopen wrapper library
add_library(nethackdl SHARED "sys/unix/nledl.c")
target_include_directories(nethackdl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# rlmain C++ (test) binary
add_executable(rlmain "sys/unix/rlmain.cc")
set_target_properties(rlmain PROPERTIES CXX_STANDARD 11)
target_link_libraries(rlmain PUBLIC nethackdl)

# pybind11 python library.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/third_party/pybind11)
pybind11_add_module(pynle win/rl/pynle.cc)
target_link_libraries(pynle PUBLIC nethackdl)
set_target_properties(pynle PROPERTIES CXX_STANDARD 14)
