#include <cxxtest/TestSuite.h>
#include "backends/platform/dos/cursor-convert.h"
#include "backends/platform/dos/dos-modes.h"

class DosCursorConvertTestSuite : public CxxTest::TestSuite {
public:
	// setMouseCursor(nullptr, 0, 0, ...): CursorMan with an empty stack.
	void test_empty_cursor_is_cleared() {
		Common::Array<byte> out;
		uint32 key = 99;
		byte pal[256 * 3] = { 0 };
		TS_ASSERT_EQUALS(DOS::cursorImage(nullptr, 0, 0, Graphics::PixelFormat::createFormatCLUT8(), 0,
										  DOS::xrgb8888(), pal, out, key), DOS::kCursorClear);
		TS_ASSERT_EQUALS(DOS::cursorImage(nullptr, 0, 0, Graphics::PixelFormat::createFormatCLUT8(), 0,
										  Graphics::PixelFormat::createFormatCLUT8(), pal, out, key), DOS::kCursorClear);
	}

	void test_same_format_goes_as_is() {
		Common::Array<byte> out;
		uint32 key = 0;
		byte pal[256 * 3] = { 0 };
		const byte img[1] = { 3 };
		TS_ASSERT_EQUALS(DOS::cursorImage(img, 1, 1, Graphics::PixelFormat::createFormatCLUT8(), 0,
										  Graphics::PixelFormat::createFormatCLUT8(), pal, out, key), DOS::kCursorAsIs);
	}

	// Palette entries whose XRGB8888 values are 0, 1, 2 and 3: the key must
	// be the first unused value, not collide with any of them.
	void test_clut8_on_4_byte_screen_is_converted_with_a_free_key() {
		byte pal[256 * 3];
		memset(pal, 0, sizeof(pal));
		for (int i = 1; i <= 4; ++i)
			pal[i * 3 + 2] = (byte)(i - 1);	// blue = 0..3
		pal[5 * 3] = 0xFF;	// red
		const byte img[6] = { 9, 1, 2, 3, 4, 5 };	// 9 is the engine's key
		Common::Array<byte> out;
		uint32 key = 0;
		TS_ASSERT_EQUALS(DOS::cursorImage(img, 3, 2, Graphics::PixelFormat::createFormatCLUT8(), 9,
										  DOS::xrgb8888(), pal, out, key), DOS::kCursorConverted);
		TS_ASSERT_EQUALS(out.size(), 6u * 4);
		TS_ASSERT_EQUALS(key, 4u);
		TS_ASSERT_EQUALS(READ_UINT32(&out[0]), key);
		for (int i = 1; i < 6; ++i)
			TS_ASSERT_DIFFERS(READ_UINT32(&out[i * 4]), key);
		TS_ASSERT_EQUALS(READ_UINT32(&out[1 * 4]), 0u);
		TS_ASSERT_EQUALS(READ_UINT32(&out[4 * 4]), 3u);
		TS_ASSERT_EQUALS(READ_UINT32(&out[5 * 4]), DOS::xrgb8888().RGBToColor(0xFF, 0, 0));
	}

	void test_clut8_on_2_byte_screen() {
		byte pal[256 * 3];
		memset(pal, 0xFF, sizeof(pal));
		const byte img[2] = { 0, 1 };
		Common::Array<byte> out;
		uint32 key = 0;
		TS_ASSERT_EQUALS(DOS::cursorImage(img, 2, 1, Graphics::PixelFormat::createFormatCLUT8(), 0,
										  DOS::rgb565(), pal, out, key), DOS::kCursorConverted);
		TS_ASSERT_EQUALS(out.size(), 4u);
		TS_ASSERT_EQUALS(READ_UINT16(&out[0]), key);
		TS_ASSERT_EQUALS(READ_UINT16(&out[2]), 0xFFFF);
		TS_ASSERT_DIFFERS(key, 0xFFFFu);
	}

	void test_mismatched_true_colour_format_is_cleared() {
		const uint16 img[1] = { 0xF800 };
		byte pal[256 * 3] = { 0 };
		Common::Array<byte> out;
		uint32 key = 0;
		TS_ASSERT_EQUALS(DOS::cursorImage((const byte *)img, 1, 1, DOS::rgb565(), 0,
										  DOS::xrgb8888(), pal, out, key), DOS::kCursorMismatch);
		TS_ASSERT_EQUALS(DOS::cursorImage((const byte *)img, 1, 1, DOS::rgb565(), 0,
										  Graphics::PixelFormat::createFormatCLUT8(), pal, out, key), DOS::kCursorMismatch);
	}
};
