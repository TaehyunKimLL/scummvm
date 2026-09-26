/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef BACKENDS_GRAPHICS_SURFACESDL_HWFORMAT_H
#define BACKENDS_GRAPHICS_SURFACESDL_HWFORMAT_H

#include "common/list.h"
#include "common/util.h"
#include "graphics/pixelformat.h"

/**
 * The SurfaceSDL hardware-screen format policy, kept free of SDL so the unit
 * tests can check it.
 *
 * The SDL_Renderer path (SDL2 and SDL3) normally presents through an RGB565
 * surface and texture. A game that asks for a 4-byte format - or the ini key
 * hw_screen_32bpp - gets a 32-bit one instead, so true-colour output (e.g.
 * 8-bit alpha text blending) reaches the window without being quantised.
 */
namespace SurfaceSdlHwFormat {

/**
 * The 32-bit hardware screen format: XRGB8888, i.e. 0x00RRGGBB in a native
 * uint32 (SDL2 SDL_PIXELFORMAT_RGB888, SDL3 SDL_PIXELFORMAT_XRGB8888).
 *
 * It has ARGB8888's memory layout - the native one of SDL's software renderer
 * and of the usual window surfaces - but no alpha mask, so the texture needs
 * no blending and the surfaces derived from the hardware screen (overlay, OSD)
 * keep the no-alpha semantics they have with RGB565.
 */
inline Graphics::PixelFormat hwFormat32() {
	return Graphics::PixelFormat(4, 8, 8, 8, 0, 16, 8, 0, 0);
}

/**
 * Whether the hardware screen should be 32-bit.
 *
 * @param gameFormat the format the game asked for (initSize)
 * @param forced     the hw_screen_32bpp ini key
 */
inline bool wantHwScreen32(const Graphics::PixelFormat &gameFormat, bool forced) {
	return forced || gameFormat.bytesPerPixel == 4;
}

/**
 * Build the list getSupportedFormats() returns.
 *
 * @param out        cleared and filled
 * @param hwFormat   the current hardware screen format, or nullptr before
 *                   the first mode set
 * @param isHwPalette the hardware screen is paletted (SDL 1.2 only)
 * @param offer32    the backend can switch its hardware screen to
 *                   hwFormat32() on request (the SDL_Renderer path)
 *
 * The hardware format comes first, so an engine that takes front() gets it
 * without conversion. Formats up to the hardware screen's depth follow, as
 * before. With offer32 and a narrower hardware screen, the 4-byte formats are
 * appended after all of those, hwFormat32() first: front() and the
 * backend-ordered negotiation of initGraphics(list) are unchanged, while an
 * engine that looks for a 4-byte format finds one and, by asking for it,
 * switches the hardware screen to 32 bits.
 */
inline void buildSupportedFormats(Common::List<Graphics::PixelFormat> &out,
                                  const Graphics::PixelFormat *hwFormat,
                                  bool isHwPalette, bool offer32) {
	out.clear();

	Graphics::PixelFormat format = Graphics::PixelFormat::createFormatCLUT8();

	if (hwFormat) {
		// This is the first supported format to prevent pixel format conversion
		// on blitting. This gives us a lot more performance on low perf hardware.
		out.push_back(*hwFormat);
		format = *hwFormat;
	}

	if (!isHwPalette) {
		// Some tables with standard formats that we always list
		// as "supported". If frontend code tries to use one of
		// these, we will perform the necessary format
		// conversion in the background. Of course this incurs a
		// performance hit, but on desktop ports this should not
		// matter. We still push the currently active format to
		// the front, so if frontend code just uses the first
		// available format, it will get one that is "cheap" to
		// use.
		const Graphics::PixelFormat RGBList[] = {
			// RGBA8888, ARGB8888, RGB888
			Graphics::PixelFormat(4, 8, 8, 8, 8, 24, 16, 8, 0),
			Graphics::PixelFormat(4, 8, 8, 8, 8, 16, 8, 0, 24),
			Graphics::PixelFormat(3, 8, 8, 8, 0, 16, 8, 0, 0),
			// RGB565, XRGB1555, RGB555, RGBA4444, ARGB4444
			Graphics::PixelFormat(2, 5, 6, 5, 0, 11, 5, 0, 0),
			Graphics::PixelFormat(2, 5, 5, 5, 1, 10, 5, 0, 15),
			Graphics::PixelFormat(2, 5, 5, 5, 0, 10, 5, 0, 0),
			Graphics::PixelFormat(2, 4, 4, 4, 4, 12, 8, 4, 0),
			Graphics::PixelFormat(2, 4, 4, 4, 4, 8, 4, 0, 12)
		};
		const Graphics::PixelFormat BGRList[] = {
			// ABGR8888, BGRA8888, BGR888
			Graphics::PixelFormat(4, 8, 8, 8, 8, 0, 8, 16, 24),
			Graphics::PixelFormat(4, 8, 8, 8, 8, 8, 16, 24, 0),
			Graphics::PixelFormat(3, 8, 8, 8, 0, 0, 8, 16, 0),
			// BGR565, XBGR1555, BGR555, ABGR4444, BGRA4444
			Graphics::PixelFormat(2, 5, 6, 5, 0, 0, 5, 11, 0),
			Graphics::PixelFormat(2, 5, 5, 5, 1, 0, 5, 10, 15),
			Graphics::PixelFormat(2, 5, 5, 5, 0, 0, 5, 10, 0),
			Graphics::PixelFormat(2, 4, 4, 4, 4, 0, 4, 8, 12),
			Graphics::PixelFormat(2, 4, 4, 4, 4, 4, 8, 12, 0)
		};

		// TODO: prioritize matching alpha masks
		int i;

		// Push some RGB formats
		for (i = 0; i < ARRAYSIZE(RGBList); i++) {
			if (hwFormat && (RGBList[i].bytesPerPixel > format.bytesPerPixel))
				continue;
			if (RGBList[i] != format)
				out.push_back(RGBList[i]);
		}

		// Push some BGR formats
		for (i = 0; i < ARRAYSIZE(BGRList); i++) {
			if (hwFormat && (BGRList[i].bytesPerPixel > format.bytesPerPixel))
				continue;
			if (BGRList[i] != format)
				out.push_back(BGRList[i]);
		}

		// The 4-byte formats the loops above skipped: asking for one of them
		// makes the hardware screen 32-bit, so they are no longer a
		// down-conversion. They go last so nothing that worked from the
		// front of the list sees a different answer.
		if (offer32 && hwFormat && format.bytesPerPixel < 4) {
			out.push_back(hwFormat32());
			for (i = 0; i < ARRAYSIZE(RGBList); i++) {
				if (RGBList[i].bytesPerPixel == 4)
					out.push_back(RGBList[i]);
			}
			for (i = 0; i < ARRAYSIZE(BGRList); i++) {
				if (BGRList[i].bytesPerPixel == 4)
					out.push_back(BGRList[i]);
			}
		}
	}

	// Finally, we always supposed 8 bit palette graphics
	out.push_back(Graphics::PixelFormat::createFormatCLUT8());
}

} // End of namespace SurfaceSdlHwFormat

#endif
