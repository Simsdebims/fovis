cmake_minimum_required (VERSION 3.0)

add_definitions(-march=native)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

if(USE_SSE)
    add_definitions(-msse2 -msse3 -msse4.2)
endif()

IF(MSVC)
   ADD_DEFINITIONS(/arch:SSE)
ENDIF(MSVC)

add_library(fovis SHARED
    src/frame.cpp
    src/visual_odometry.cpp
    src/fast.cpp
    src/motion_estimation.cpp
    src/gauss_pyramid.c
    src/refine_motion_estimate.cpp
    src/tictoc.cpp
    src/initial_homography_estimation.cpp
    src/grid_filter.cpp
    src/intensity_descriptor.cpp
    src/pyramid_level.cpp
    src/feature_matcher.cpp
    src/refine_feature_match.cpp
    src/depth_image.cpp
    src/internal_utils.cpp
    src/normalize_image.cpp
)

#set_target_properties(fovis PROPERTIES SOVERSION 1)
