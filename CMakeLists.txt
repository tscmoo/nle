cmake_minimum_required(VERSION 3.10)
project(libnethack)

add_compile_definitions(GCC_WARN NOCLIPPING NOMAIL NOTPARMDECL HACKDIR="/Users/hnr/nethackdir" DEFAULT_WINDOW_SYS="tty" DLB)

file(GLOB NETHACK_SRC
  "src/*.c"
  "sys/share/posixregex.c"
  "sys/share/ioctl.c"
  "sys/unix/unixmain.c"
  "sys/unix/unixunix.c"
  "sys/share/unixtty.c"
  "sys/unix/unixres.c"
  "win/tty/*.c"
  "win/rl/winrl.cc"
  )

# libnethack library
add_library(nethack SHARED ${NETHACK_SRC})
set_target_properties(nethack PROPERTIES CXX_STANDARD 14)
target_include_directories(nethack PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include /usr/local/include)
target_link_directories(nethack PUBLIC /usr/local/lib)
target_link_libraries(nethack PUBLIC ncurses m zmq lua)
target_link_libraries(nethack PRIVATE flatbuffers)
