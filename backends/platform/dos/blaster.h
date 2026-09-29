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

#ifndef BACKENDS_PLATFORM_DOS_BLASTER_H
#define BACKENDS_PLATFORM_DOS_BLASTER_H

#include "common/scummsys.h"

namespace DOS {

/**
 * The settings carried by the BLASTER environment variable, e.g.
 * "A220 I7 D1 H5 P330 T6". Unset/unrecognised fields keep the Sound
 * Blaster defaults below.
 */
struct BlasterConfig {
	uint16 port = 0x220;
	int irq = 5;
	int dma = 1;
	int hdma = -1;
	uint16 mpu = 0x330;
	int type = 0;
	bool present = false;
};

namespace BlasterDetail {

inline bool isDecDigit(char c) {
	return c >= '0' && c <= '9';
}

inline bool isHexDigit(char c) {
	return isDecDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

inline int hexDigitValue(char c) {
	if (isDecDigit(c))
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return c - 'A' + 10;
}

/**
 * Parses the NUL-terminated digit string @p digits as hex (base 16) into
 * *out. Returns false (leaving *out untouched) if it is empty or contains
 * any character that is not a hex digit.
 */
inline bool parseHex(const char *digits, uint32 &out) {
	if (digits[0] == '\0')
		return false;
	uint32 v = 0;
	for (const char *p = digits; *p != '\0'; ++p) {
		if (!isHexDigit(*p))
			return false;
		v = v * 16 + (uint32)hexDigitValue(*p);
	}
	out = v;
	return true;
}

/**
 * Parses the NUL-terminated digit string @p digits as decimal into *out.
 * Returns false (leaving *out untouched) if it is empty or contains any
 * character that is not a decimal digit.
 */
inline bool parseDec(const char *digits, uint32 &out) {
	if (digits[0] == '\0')
		return false;
	uint32 v = 0;
	for (const char *p = digits; *p != '\0'; ++p) {
		if (!isDecDigit(*p))
			return false;
		v = v * 10 + (uint32)(*p - '0');
	}
	out = v;
	return true;
}

} // End of namespace BlasterDetail

/**
 * Parses a BLASTER-style environment string: whitespace-separated tokens,
 * each a one-letter tag (A/I/D/H/P/T, case-insensitive) followed by a
 * number (hex for A and P, decimal for the rest). Unknown tags are
 * ignored; a tag whose number is malformed (empty or with an invalid
 * digit) leaves that field at its default. @p s == nullptr yields
 * defaults with present == false; any other string (including empty)
 * yields present == true.
 */
inline BlasterConfig parseBlaster(const char *s) {
	BlasterConfig cfg;
	if (s == nullptr)
		return cfg;
	cfg.present = true;

	size_t i = 0;
	while (s[i] != '\0') {
		while (s[i] == ' ' || s[i] == '\t')
			++i;
		if (s[i] == '\0')
			break;

		size_t start = i;
		while (s[i] != '\0' && s[i] != ' ' && s[i] != '\t')
			++i;
		size_t len = i - start;
		if (len < 2)
			continue; // Tag with no digits at all.

		char tag = s[start];
		if (tag >= 'a' && tag <= 'z')
			tag = (char)(tag - ('a' - 'A'));

		char digits[32];
		size_t digitLen = len - 1;
		if (digitLen >= sizeof(digits))
			continue; // Absurdly long token; not a real field.
		memcpy(digits, s + start + 1, digitLen);
		digits[digitLen] = '\0';

		uint32 v = 0;
		switch (tag) {
		case 'A':
			if (BlasterDetail::parseHex(digits, v))
				cfg.port = (uint16)v;
			break;
		case 'I':
			if (BlasterDetail::parseDec(digits, v))
				cfg.irq = (int)v;
			break;
		case 'D':
			if (BlasterDetail::parseDec(digits, v))
				cfg.dma = (int)v;
			break;
		case 'H':
			if (BlasterDetail::parseDec(digits, v))
				cfg.hdma = (int)v;
			break;
		case 'P':
			if (BlasterDetail::parseHex(digits, v))
				cfg.mpu = (uint16)v;
			break;
		case 'T':
			if (BlasterDetail::parseDec(digits, v))
				cfg.type = (int)v;
			break;
		default:
			break; // Unknown tag; ignore.
		}
	}

	return cfg;
}

} // End of namespace DOS

#endif
