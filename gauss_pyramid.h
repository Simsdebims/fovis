#ifndef __gauss_pyr_h__
#define __gauss_pyr_h__

#include <stdint.h>

#include <export_def.h>

#ifdef __cplusplus
extern "C" {
#endif

int FOVIS_EXPORT gauss_pyr_down_get_buf_size_8u_C1R(int width, int height);

int FOVIS_EXPORT gauss_pyr_down_8u_C1R(const uint8_t* src, int src_stride, int src_width,
        int src_height, uint8_t* dest, int dst_stride, uint8_t* buf);

#ifdef __cplusplus
}
#endif

#endif
