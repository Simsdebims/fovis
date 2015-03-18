#ifndef __fovis_normalize_image_hpp__
#define __fovis_normalize_image_hpp__

#include <inttypes.h>

#include <export_def.h>
namespace fovis
{

/**
 * \brief Normalize image intensities in place to approximately have mean 128 and sd 74.
 */
void FOVIS_EXPORT normalize_image(uint8_t * buf, int stride, int width, int height);

}

#endif
