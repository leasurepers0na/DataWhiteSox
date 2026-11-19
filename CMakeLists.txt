cmake_minimum_required(VERSION 3.10)
project(DataWhiteSox CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

file(GLOB SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_executable(dws ${SRC})
target_compile_options(dws PRIVATE -O2 -Wall)

# Optional unit tests (no CTP library required)
option(DWS_BUILD_TESTS "Build DWS unit tests" ON)
if(DWS_BUILD_TESTS)
  enable_testing()
  add_executable(dws_time_test tests/test_time.cpp)
  target_include_directories(dws_time_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_compile_options(dws_time_test PRIVATE -O2 -Wall)
  add_test(NAME dws_time_test COMMAND dws_time_test)

  # Syntax-check source_ctp.hpp without the official CTP SDK
  add_executable(dws_ctp_syntax tests/test_ctp_syntax.cpp)
  target_include_directories(dws_ctp_syntax PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/stub
    ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_compile_definitions(dws_ctp_syntax PRIVATE DWS_HAVE_CTP=1)
  target_compile_options(dws_ctp_syntax PRIVATE -O2 -Wall)

  # Optional: syntax-check against the real CTP headers (no library/link needed)
  set(DWS_CTP_REAL_HEADERS "" CACHE PATH "Directory containing ThostFtdcMdApi.h")
  if(DWS_CTP_REAL_HEADERS AND EXISTS "${DWS_CTP_REAL_HEADERS}/ThostFtdcMdApi.h")
    add_executable(dws_ctp_real_syntax tests/test_ctp_real_syntax.cpp)
    target_include_directories(dws_ctp_real_syntax PRIVATE
      ${DWS_CTP_REAL_HEADERS}
      ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_definitions(dws_ctp_real_syntax PRIVATE DWS_HAVE_CTP=1)
    target_compile_options(dws_ctp_real_syntax PRIVATE -O2 -Wall)
    message(STATUS "DWS: real CTP header syntax test enabled: ${DWS_CTP_REAL_HEADERS}")
  endif()
endif()

# Link CTP if third_party/ctp/libthostmduserapi_se.so exists
set(CTP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ctp)
if(EXISTS ${CTP_DIR}/libthostmduserapi_se.so)
  target_include_directories(dws PRIVATE ${CTP_DIR})
  target_link_libraries(dws PRIVATE ${CTP_DIR}/libthostmduserapi_se.so)
  target_compile_definitions(dws PRIVATE DWS_HAVE_CTP=1)
  message(STATUS "DWS: CTP library found, enabled")
else()
  message(STATUS "DWS: no CTP library, mock mode only")
endif()

install(TARGETS dws DESTINATION bin)
