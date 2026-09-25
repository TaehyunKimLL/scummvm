#include "common/endian.h"
#include "sci/graphics/textcompose.h"
#include "sci/graphics/textlayer.h"

namespace Sci {
namespace TextCompose {

byte expandCoverage(const byte *row, int x, int bpp) {
	switch (bpp) {
	case 1:
		return (row[x >> 3] & (0x80 >> (x & 7))) ? 255 : 0;
	case 2:
		return ((row[x >> 2] >> (6 - ((x & 3) * 2))) & 3) * 85;
	default:
		return row[x];
	}
}

void expandGlyphRow(byte *dstCoverage, const byte *row, int width, int bpp, bool greyed, int screenY, int screenX0) {
	for (int x = 0; x < width; x++) {
		byte c = expandCoverage(row, x, bpp);
		// The engine's checkerboard for disabled text: drop every other pixel.
		if (greyed && (screenY % 2) == ((screenX0 + x) % 2))
			c = 0;
		dstCoverage[x] = c;
	}
}

static void blendRGB(byte &r, byte &g, byte &b, const byte *pal, byte index, byte a) {
	r = blend(r, pal[index * 3 + 0], a);
	g = blend(g, pal[index * 3 + 1], a);
	b = blend(b, pal[index * 3 + 2], a);
}

void composeSpan(byte *dst, const Graphics::PixelFormat &fmt, const TextPixel *text, int count, const byte *paletteRGB) {
	const int bpp = fmt.bytesPerPixel;
	for (int i = 0; i < count; i++, dst += bpp) {
		const TextPixel &t = text[i];
		if (!t.fgCoverage && !t.outlineCoverage)
			continue;
		uint32 c = (bpp == 2) ? READ_UINT16(dst) : READ_UINT32(dst);
		byte r, g, b;
		fmt.colorToRGB(c, r, g, b);
		if (t.outlineCoverage)
			blendRGB(r, g, b, paletteRGB, t.outlineIndex, t.outlineCoverage);
		if (t.fgCoverage)
			blendRGB(r, g, b, paletteRGB, t.fgIndex, t.fgCoverage);
		c = fmt.RGBToColor(r, g, b);
		if (bpp == 2)
			WRITE_UINT16(dst, c);
		else
			WRITE_UINT32(dst, c);
	}
}

void stampSpan(byte *dstIndex, const TextPixel *text, int count) {
	for (int i = 0; i < count; i++) {
		if (text[i].fgCoverage >= 128)
			dstIndex[i] = text[i].fgIndex;
		else if (text[i].outlineCoverage >= 128)
			dstIndex[i] = text[i].outlineIndex;
	}
}

} // End of namespace TextCompose
} // End of namespace Sci
