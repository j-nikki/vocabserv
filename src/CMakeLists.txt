cmake_minimum_required(VERSION 3.20)

find_path(BOOST_ASIO_INCLUDE_DIRS "boost/asio.hpp")
find_package(magic_enum CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(Boost QUIET REQUIRED COMPONENTS thread system)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gen")
find_package(Python COMPONENTS Interpreter)
set(resfiles "res/index.html" "res/reset.css" "res/index.js" "res/index.css")
add_custom_command(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/gen/res.h"
         "${CMAKE_CURRENT_BINARY_DIR}/gen/res.cpp"
  COMMAND "${Python_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/resgen.py"
          $<CONFIG:Debug> "${CMAKE_CURRENT_BINARY_DIR}/gen" ${resfiles}
  DEPENDS "${PROJECT_SOURCE_DIR}/scripts/resgen.py" ${resfiles}
  WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
  VERBATIM)

add_executable(vocabserv "server.cpp" "${CMAKE_CURRENT_BINARY_DIR}/gen/res.cpp"
                         "message.cpp" "vocabserv.cpp")
if(MSVC)
  target_compile_options(
    vocabserv PRIVATE /std:c++latest /Zc:preprocessor /W4
                      /D_CRT_SECURE_NO_WARNINGS /DSPDLOG_WCHAR_FILENAMES)
else()
  find_package(fmt CONFIG REQUIRED)
  target_link_libraries(vocabserv PRIVATE fmt::fmt)
  target_compile_options(vocabserv PRIVATE -std=c++2b)
endif()

target_include_directories(
  vocabserv
  PRIVATE ${GZIP_HPP_INCLUDE_DIRS} ${BOOST_ASIO_INCLUDE_DIRS}
          ${BOOST_HANA_INCLUDE_DIRS} "${CMAKE_CURRENT_BINARY_DIR}/gen")
target_link_libraries(
  vocabserv PRIVATE Boost::boost Boost::system Boost::thread
                    magic_enum::magic_enum spdlog::spdlog_header_only)
