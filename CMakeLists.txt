project(brokenithm LANGUAGES CXX)
cmake_minimum_required(VERSION 3.4)
SET(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/")
INCLUDE_DIRECTORIES("${CMAKE_SOURCE_DIR}/include/")
IF(NOT CMAKE_BUILD_TYPE)
    SET(CMAKE_BUILD_TYPE Release)
ENDIF()
SET(CMAKE_CXX_STANDARD 17)

IF (NOT WIN32)
	MESSAGE(FATAL_ERROR "This is not supposed to be run on non-Windows platforms!")
ENDIF()


IF (NOT MSVC)
    ADD_COMPILE_OPTIONS(-Wall -Wextra -Wno-unused-parameter -Wno-unused-result)
ELSE()
    ADD_COMPILE_OPTIONS(/W4)
ENDIF()

ADD_EXECUTABLE(brokenithm 
    src/main.cpp)
INCLUDE_DIRECTORIES(src)
LINK_DIRECTORIES(${CMAKE_SOURCE_DIR})

TARGET_LINK_LIBRARIES(brokenithm ws2_32)
