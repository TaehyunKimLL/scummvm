#ifndef SCI_GRAPHICS_TEXTCOMPOSE_H
#define SCI_GRAPHICS_TEXTCOMPOSE_H

#include "common/scummsys.h"
#include "graphics/pixelformat.h"

namespace Sci {

struct TextPixel;

/** The arithmetic of hi-res text, free of engine state so it is tested alone
 *  (HIRES_COMPOSITOR_DESIGN.md §3.2). */
namespace TextCompose {

byte expandCoverage(const byte *row, int x, int bpp);
void expandGlyphRow(byte *dstCoverage, const byte *row, int width, int bpp, bool greyed, int screenY, int screenX0);

inline byte blend(byte dst, byte src, byte a) {
	return (byte)(((uint32)dst * (255 - a) + (uint32)src * a + 127) / 255);
}

void composeSpan(byte *dst, const Graphics::PixelFormat &fmt, const TextPixel *text, int count, const byte *paletteRGB);
void stampSpan(byte *dstIndex, const TextPixel *text, int count);

} // End of namespace TextCompose
} // End of namespace Sci

#endif
