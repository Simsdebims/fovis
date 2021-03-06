#include "pyramid_level.hpp"

#include <stdio.h>

#include "internal_utils.hpp"
#include "gauss_pyramid.h"

#ifndef ALIGNMENT
#define ALIGNMENT 16
#endif

#ifdef WIN32
#include <malloc.h>
#endif

namespace fovis
{

PyramidLevel::PyramidLevel(int width, int height, int level_num,
                           int feature_window_size,
                           GridKeyPointFilter& grid_filter) :
    _grid_filter(grid_filter)
{
  _width = width;
  _height = height;
  _raw_gray_stride = round_up_to_multiple(_width, ALIGNMENT);
  _descriptor_extractor = new IntensityDescriptorExtractor(_raw_gray_stride, feature_window_size);

  // set default that fail
  int status=1;
  
#ifndef WIN32
  status = posix_memalign((void**)&_raw_gray, ALIGNMENT, _raw_gray_stride * _height);
#else
  if (_raw_gray = (uint8_t*)_aligned_malloc( _raw_gray_stride * _height, ALIGNMENT))
	  status=0;
#endif

  if(0 != status) {
      fprintf(stderr, "memory allocation (%d bytes) failed for pyramid level %d\n",
              _raw_gray_stride * _height, level_num);
      _raw_gray = NULL;
  }
  memset(_raw_gray, 0, _raw_gray_stride * _height);

  _keypoint_min_x = feature_window_size;
  _keypoint_min_y = feature_window_size;
  _keypoint_max_x = _width - feature_window_size - 2;
  _keypoint_max_y = _height - feature_window_size - 2;

  // allocate workspace for computing the next pyramid level
  int pyrbuf_size = gauss_pyr_down_get_buf_size_8u_C1R(_width, _height);
  _pyrbuf = (uint8_t*) malloc(pyrbuf_size);

  _level_num = level_num;

  _num_keypoints = 0;
  _keypoints_capacity = 1500;
  _keypoints = new KeypointData[_keypoints_capacity];

  _initial_keypoints.reserve(2000);

  _descriptors = NULL;

  // allocate descriptor buffers
  int desc_buf_size = _keypoints_capacity * _descriptor_extractor->getDescriptorStride();

#ifndef WIN32
  if(0 != posix_memalign((void**)&_descriptors, ALIGNMENT, desc_buf_size)) {
    fprintf(stderr, "error allocating descriptor memory\n");
  }
#else
  void* ptr = _aligned_malloc(desc_buf_size, ALIGNMENT);
  if (ptr == NULL)
	  fprintf(stderr, "error allocating descriptor memory\n");
  _descriptors = (uint8_t*)ptr;
#endif

}

void
PyramidLevel::increase_capacity(int new_capacity)
{
  _keypoints_capacity = new_capacity;
  delete[] _keypoints;
  _keypoints = new KeypointData[_keypoints_capacity];

  // allocate descriptor buffers
  int descriptor_buf_size = _keypoints_capacity * getDescriptorStride();
#ifndef WIN32
  free(_descriptors);
#else
	_aligned_free(_descriptors);
#endif
  int status=-1;

#ifndef WIN32
  status = posix_memalign((void**)&_descriptors, ALIGNMENT, descriptor_buf_size);
#else
  void* ptr = _aligned_malloc(descriptor_buf_size, ALIGNMENT);
  if (ptr != NULL)
	  status = 0;
  _descriptors = (uint8_t*)ptr; 
#endif
  if(0 != status) {
	  fprintf(stderr, "error allocating descriptor memory. status: %i, size: %i\n", status, descriptor_buf_size);
  }
}

PyramidLevel::~PyramidLevel()
{
#ifndef WIN32
	free(_raw_gray);
	free(_descriptors);
#else
	_aligned_free(_raw_gray);
	_aligned_free(_descriptors);
#endif
  free(_pyrbuf);
  free(_keypoints);
  delete _descriptor_extractor;
  _raw_gray = NULL;
  _descriptors = NULL;
  _pyrbuf = NULL;
  _keypoints = NULL;
  _keypoints_capacity = 0;
}

void
PyramidLevel::populateDescriptorInterp(float x, float y, uint8_t* descriptor) const
{
  _descriptor_extractor->populateDescriptorInterp(_raw_gray, x, y, descriptor);
}

void
PyramidLevel::populateDescriptorAligned(int x, int y, uint8_t* descriptor) const
{
  _descriptor_extractor->populateDescriptorAligned(_raw_gray, x, y, descriptor);
}

void
PyramidLevel::populateDescriptorsInterp(const KeypointData* keypoints,
                                        int num_keypoints,
                                        uint8_t* descriptors) const
{
  _descriptor_extractor->populateDescriptorsInterp(_raw_gray, keypoints,
                                                   num_keypoints, descriptors);
}

void
PyramidLevel::populateDescriptorsAligned(const KeypointData* keypoints,
                                        int num_keypoints,
                                        uint8_t* descriptors) const
{
  _descriptor_extractor->populateDescriptorsAligned(_raw_gray, keypoints,
                                                    num_keypoints, descriptors);
}

}
