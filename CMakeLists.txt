cmake_minimum_required(VERSION 3.16...3.26)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_qt(COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt::Core Qt::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME} PRIVATE
    $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>)
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON
               AUTOUIC ON
               AUTORCC ON)
endif()

find_package(OpenVINO REQUIRED COMPONENTS Runtime)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE openvino::runtime)

set(USE_SYSTEM_OPENCV OFF CACHE STRING "Use system OpenCV")

if(USE_SYSTEM_OPENCV)
  if(OS_LINUX)
    find_package(OpenCV REQUIRED COMPONENTS core imgproc)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE "${OpenCV_LIBRARIES}")
    target_include_directories(${CMAKE_PROJECT_NAME} SYSTEM PUBLIC "${OpenCV_INCLUDE_DIRS}")
  else()
    message(FATAL_ERROR "System OpenCV is only supported on Linux!")
  endif()
else()
  include(cmake/FetchOpenCV.cmake)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OpenCV)
endif()

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE vendor)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE src)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.c
    src/detect-filter.cpp
    src/filter/SportEyesFilterProperties.cpp
    src/filter/SportEyesFilterLifecycle.cpp
    src/config/SportEyesProfileManager.cpp
    src/sport/GroupClustering.cpp
    src/pipeline/SportEyesVideoTick.cpp
    src/pipeline/SportEyesAsyncInference.cpp
    src/render/SportEyesFilterRender.cpp
    src/diagnostics/SportEyesCsvLogger.cpp
    src/detect-filter-info.c
    src/detect-filter-utils.cpp
    src/obs-utils/obs-utils.cpp
    src/director_ai/TemporalDirector.cpp
    src/director_ai/CameraPlanner.cpp
    src/director_ai/WorldState.cpp
    src/director_ai/CourtModel.cpp
    src/director_ai/DirectorAIEngine.cpp
    src/director_ai/DirectorAIIntegration.cpp
    src/director_ai/DirectorAILogger.cpp
    src/sort/Sort.cpp
    src/edgeyolo/edgeyolo_openvino.cpp
    src/yunet/YuNetOpenVINO.cpp
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
