cmake_minimum_required(VERSION 3.20)

#
# dependencies (found on, e.g., vcpkg)
#

find_path(BOOST_ASIO_INCLUDE_DIRS "boost/asio.hpp")
find_package(magic_enum CONFIG REQUIRED)
find_package(Boost QUIET REQUIRED COMPONENTS thread system)

#
# populate ${CMAKE_CURRENT_BINARY_DIR}/include
#

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/include")

find_package(Python COMPONENTS Interpreter)
set(resfiles "res/index.html" "res/reset.css" "res/index.js" "res/index.css")
add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/include/res.h"
         "${CMAKE_CURRENT_BINARY_DIR}/include/res.cpp"
  COMMAND "${Python_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/resgen.py"
          $<CONFIG:Debug> "${CMAKE_CURRENT_BINARY_DIR}/include" ${resfiles}
  DEPENDS "${PROJECT_SOURCE_DIR}/scripts/resgen.py" ${resfiles}
  WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
  VERBATIM)

#
# vocabserv target
#

add_executable(
  vocabserv "server.cpp" "${CMAKE_CURRENT_BINARY_DIR}/include/res.cpp"
            "message.cpp" "vocabserv.cpp" "format.cpp" "buffer.cpp")
if(MSVC)
  target_compile_options(
    vocabserv PRIVATE /std:c++latest /Zc:preprocessor /W4
                      /D_CRT_SECURE_NO_WARNINGS /DFMT_ENFORCE_COMPILE_STRING)
else()
  target_compile_options(vocabserv PRIVATE -std=c++2b)
endif()

target_include_directories(
  vocabserv
  PRIVATE ${BOOST_ASIO_INCLUDE_DIRS} "${PROJECT_SOURCE_DIR}/include"
          ${BOOST_HANA_INCLUDE_DIRS} "${CMAKE_CURRENT_BINARY_DIR}/include")
target_link_libraries(vocabserv PRIVATE Boost::boost Boost::system
                                        Boost::thread magic_enum::magic_enum)
