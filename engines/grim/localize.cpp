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

#include "common/config-manager.h"
#include "common/file.h"
#include "common/language.h"
#include "common/str.h"
#include "common/endian.h"
#include "common/tokenizer.h"

#include "engines/grim/localize.h"
#include "engines/grim/localize_text.h"
#include "engines/grim/grim.h"
#include "engines/grim/resource.h"

namespace Grim {

Localizer *g_localizer = nullptr;

namespace {

class ResourceProbe : public TabFileProbe {
public:
	bool exists(const Common::String &name) const override {
		Common::SeekableReadStream *s = g_resourceloader->openNewStreamFile(name);
		delete s;
		return s != nullptr;
	}
};

} // End of anonymous namespace

Localizer::Localizer() {
	// To avoid too wide lines further below, we just name these here.
	bool isAnyDemo = g_grim->getGameFlags() & ADGF_DEMO;
	bool isGrimDemo = g_grim->getGameType() == GType_GRIM && isAnyDemo;
	bool isGerman = g_grim->getGameLanguage() == Common::DE_DEU;
	bool isFrench = g_grim->getGameLanguage() == Common::FR_FRA;
	bool isItalian = g_grim->getGameLanguage() == Common::IT_ITA;
	bool isSpanish = g_grim->getGameLanguage() == Common::ES_ESP;
	bool isKorean = g_grim->getGameLanguage() == Common::KO_KOR;	// Korean Fan Translation
	bool isTranslatedGrimDemo = (isGerman || isFrench || isItalian || isSpanish) && isGrimDemo;
	bool isPS2 = g_grim->getGamePlatform() == Common::kPlatformPS2;

	if (isGrimDemo && !isTranslatedGrimDemo)
		return;

	Common::String filename;
	if (g_grim->getGameType() == GType_MONKEY4) {
		filename = "script.tab";
	} else {
		if (g_grim->isRemastered()) {
			filename = Common::String("grim.") + g_grim->getLanguagePrefix() + Common::String(".tab"); // TODO: Detect based on language.
		} else if (isTranslatedGrimDemo) {
			filename = "language.tab";
		} else if (isPS2) {
			filename = "grim.tab";
		} else {
			// grim.<code>.tab for the language of the target (forced in the
			// ini or detected), then the Korean fan patch's grim.ko.tab, then
			// grim.tab. A retail game without a grim.<code>.tab reads what it
			// always read.
			Common::Language lang = g_grim->getGameLanguage();
			if (ConfMan.hasKey("language")) {
				const Common::Language forced = Common::parseLanguage(ConfMan.get("language"));
				if (forced != Common::UNK_LANG)
					lang = forced;
			}
			const char *code = lang != Common::UNK_LANG ? Common::getLanguageCode(lang) : nullptr;
			Common::String missing;
			filename = selectGrimTab(code ? code : "", isKorean, lang != g_grim->getGameLanguage(),
			                         ResourceProbe(), missing);
			if (!missing.empty())
				warning("%s", missing.c_str());
		}
	}

	Common::SeekableReadStream *f = g_resourceloader->openNewStreamFile(filename);
	if (!f) {
		error("Localizer::Localizer: Unable to find localization information (%s)", filename.c_str());
		return;
	}

	int32 filesize = f->size();

	// Read in the data
	char *data = new char[filesize + 1];
	f->read(data, filesize);
	data[filesize] = '\0';
	delete f;

	if (g_grim->isRemastered()) {
		parseRemasteredData(Common::String(data));
		delete[] data;
		return;
	}

	// A table saved as UTF-8 with a byte order mark (grim.<code>.tab, or a
	// grim.ko.tab so saved): plain text from byte 3, no magic. The mark is
	// what selects UTF-8 and the translation's fonts, not the language.
	int32 start = 4;
	const char *afterBom = data;
	int32 bomSize = filesize;
	if (g_grim->getGameType() == GType_GRIM && !isAnyDemo && !isPS2 && stripUtf8Bom(afterBom, bomSize)) {
		start = afterBom - data;
		g_grim->_isUtf8 = true;
		g_grim->_utf8Tab = true;
	// Explicitly white-list german demo, as it has a .tab-file
	} else if ((isTranslatedGrimDemo) || (!isAnyDemo && !isPS2)) {
		if (filesize < 4)
			error("%s to short: %i", filename.c_str(), filesize);
		switch (READ_BE_UINT32(data)) {
		case MKTAG('R','C','N','E'):
			// Decode the data
			if (g_grim->getGameType() == GType_MONKEY4) {
				uint32 next = 0x16;
				for (int i = 4; i < filesize; i++) {
					next = next * 0x343FD + 0x269EC3;
					data[i] ^= (int)(((((next >> 16) & 0x7FFF) / 32767.f) * 254) + 1);
				}
			} else {
				for (int i = 4; i < filesize; i++) {
					data[i] ^= '\xdd';
				}
			}
		case MKTAG('D', 'O', 'E', 'L'):
		case MKTAG('a', 'r', 't', 'p'):
		case MKTAG('s', 's', 'I', 'N'):
		case MKTAG('I', 'N', 'T', 'T'):
		case MKTAG('6', '6', '6', 'I'):
			break;
		case 0xfffe4600: {
			Common::String n = Common::U32String::decodeUTF16LE((const uint16 *) (data + 2), (filesize - 2) / 2).encode();
			delete[] data;
			data = new char[n.size() + 1];
			memcpy(data, n.c_str(), n.size() + 1);
			filesize = n.size();
			g_grim->_isUtf8 = true;
			break;
		}
		default:
			error("Invalid magic reading %s: %08x (%s)", filename.c_str(), READ_BE_UINT32(data), tag2str(READ_BE_UINT32(data)));
		}
	}

	Common::StringArray continuations;
	parseTabLines(data, filesize, start, g_grim->getGameType() == GType_GRIM,
	              g_grim->getGameType() == GType_MONKEY4, _entries, &continuations);
	for (uint i = 0; i < continuations.size(); i++)
		warning("%s", continuations[i].c_str());
	if (g_grim->_transcodeChineseToSimplified && g_grim->_isUtf8) {
		for (Common::StringMap::iterator it = _entries.begin(); it != _entries.end(); it++) {
			it->_value = it->_value.decode(Common::CodePage::kUtf8).transcodeChineseT2S().encode(Common::CodePage::kUtf8);
		}
	}
	delete[] data;
}

void Localizer::parseRemasteredData(const Common::String &data) {
	// This is probably cleaner implemented using a read line-by-line, but for now this works.
	Common::StringTokenizer tokens(data, "\t\n");
	while (!tokens.empty()) {
		Common::String key = tokens.nextToken();
		// Not sure if this is right, but it is necessary to get by the second line
		key.trim();
		// Handle comments
		if (!(key.size() > 0 && !(key[0] == '#'))) {
			continue;
		}
		Common::String string = tokens.nextToken();
		_entries[key] = string;
	}
}

Common::String Localizer::localize(const char *str) const {
	assert(str);

	const char *slash2;

	if (str[0] != '/' || str[0] == 0 || !(slash2 = strchr(str + 1, '/')))
		return str;

	Common::String key(str + 1, slash2 - str - 1);
	Common::StringMap::iterator it = _entries.find(key);
	if (it != _entries.end()) {
		return it->_value;
	} else {
		return slash2 + 1;
	}
}

} // end of namespace Grim
