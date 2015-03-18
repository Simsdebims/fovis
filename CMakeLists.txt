cmake_minimum_required (VERSION 3.0)

add_definitions(-march=native)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

if(USE_SSE)
    add_definitions(-msse2 -msse3 -msse4.2)
endif()

IF(MSVC)
   ADD_DEFINITIONS(/arch:SSE)
ENDIF(MSVC)

add_library(fovis SHARED
    frame.cpp
    visual_odometry.cpp
    fast.cpp
    motion_estimation.cpp
    gauss_pyramid.c
    refine_motion_estimate.cpp
    tictoc.cpp
    initial_homography_estimation.cpp
    grid_filter.cpp
    intensity_descriptor.cpp
    pyramid_level.cpp
    feature_matcher.cpp
    refine_feature_match.cpp
    depth_image.cpp
    internal_utils.cpp
    normalize_image.cpp
)

#set_target_properties(fovis PROPERTIES SOVERSION 1)
