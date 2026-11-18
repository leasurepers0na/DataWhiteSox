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

# 若存在 CTP 库则自动链接（third_party/ctp）
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
