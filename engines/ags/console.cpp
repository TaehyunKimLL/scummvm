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

#include "ags/console.h"
#include "ags/ags.h"
#include "ags/globals.h"
#include "ags/shared/ac/game_setup_struct.h"
#include "ags/shared/ac/sprite_cache.h"
#include "ags/shared/core/asset_manager.h"
#include "ags/shared/game/tra_file.h"
#include "ags/engine/ac/game_state.h"
#include "ags/engine/ac/global_display.h"
#include "ags/shared/gfx/allegro_bitmap.h"
#include "ags/shared/script/cc_common.h"
#include "graphics/palette.h"
#include "image/png.h"

namespace AGS {

AGSConsole::AGSConsole(AGSEngine *vm) : GUI::Debugger(), _vm(vm), _logOutputTarget(nullptr), _agsDebuggerOutput(nullptr),
	_sayPending(false), _sayFont(0) {
	registerCmd("ags_debug_groups_list",   WRAP_METHOD(AGSConsole, Cmd_listDebugGroups));
	registerCmd("ags_debug_groups_set",  WRAP_METHOD(AGSConsole, Cmd_setDebugGroupLevel));
	registerCmd("ags_set_script_dump", WRAP_METHOD(AGSConsole, Cmd_SetScriptDump));
	registerCmd("ags_sprite_info",   WRAP_METHOD(AGSConsole, Cmd_getSpriteInfo));
	registerCmd("ags_sprite_dump",  WRAP_METHOD(AGSConsole, Cmd_dumpSprite));
	registerCmd("ags_say",  WRAP_METHOD(AGSConsole, Cmd_say));

	_logOutputTarget = new LogOutputTarget();
	_agsDebuggerOutput = _GP(DbgMgr).RegisterOutput("ScummVMLog", _logOutputTarget, AGS3::AGS::Shared::kDbgMsg_None);
}

AGSConsole::~AGSConsole() {
	delete _logOutputTarget;
}

struct LevelName {
	const char *name;
	AGS3::AGS::Shared::MessageType level;
};

static const LevelName levelNames[] = {
	{"none", AGS3::AGS::Shared::kDbgMsg_None},
	{"alerts", AGS3::AGS::Shared::kDbgMsg_Alert},
	{"fatal", AGS3::AGS::Shared::kDbgMsg_Fatal},
	{"errors", AGS3::AGS::Shared::kDbgMsg_Error},
	{"warnings", AGS3::AGS::Shared::kDbgMsg_None},
	{"info", AGS3::AGS::Shared::kDbgMsg_Info},
	{"debug", AGS3::AGS::Shared::kDbgMsg_Debug},
	{nullptr, AGS3::AGS::Shared::kDbgMsg_None}
};

struct GroupName {
	const char *name;
	AGS3::uint32_t group;
};

static const GroupName groupNames[] = {
	{"Main", AGS3::AGS::Shared::kDbgGroup_Main},
	{"Game", AGS3::AGS::Shared::kDbgGroup_Game},
	{"Script", AGS3::AGS::Shared::kDbgGroup_Script},
	{"SpriteCache", AGS3::AGS::Shared::kDbgGroup_SprCache},
	{"ManObj", AGS3::AGS::Shared::kDbgGroup_ManObj},
	{nullptr, (AGS3::uint32_t)-1}
};

bool AGSConsole::Cmd_listDebugGroups(int argc, const char **argv) {
	if (argc != 1) {
		debugPrintf("Usage: %s\n", argv[0]);
		return true;
	}

	debugPrintf("%-16s %-16s\n", "Name", "Level");
	for (int i = 0 ; groupNames[i].name != nullptr ; ++i)
		debugPrintf("%-16s %-16s\n", groupNames[i].name, getVerbosityLevel(groupNames[i].group));
	return true;
}

bool AGSConsole::Cmd_setDebugGroupLevel(int argc, const char **argv) {
	if (argc != 3) {
		debugPrintf("Usage: %s group level\n", argv[0]);
		debugPrintf("   valid groups: ");
		printGroupList();
		debugPrintf("\n");
		debugPrintf("   valid levels: ");
		printLevelList();
		debugPrintf("\n");
		return true;
	}

	bool found = false;
	AGS3::uint32_t group = parseGroup(argv[1], found);
	if (!found) {
		debugPrintf("Unknown debug group '%s'\n", argv[1]);
		debugPrintf("Valid groups are: ");
		printGroupList();
		debugPrintf("\n");
		return true;
	}

	AGS3::AGS::Shared::MessageType level = parseLevel(argv[2], found);
	if (!found) {
		debugPrintf("Unknown level '%s'\n", argv[2]);
		debugPrintf("Valid levels are: ");
		printLevelList();
		debugPrintf("\n");
		return true;
	}

	_agsDebuggerOutput->SetGroupFilter(group, level);
	return true;
}

const char *AGSConsole::getVerbosityLevel(AGS3::uint32_t groupID) const {
	int i = 1;
	while (levelNames[i].name != nullptr) {
		if (!_agsDebuggerOutput->TestGroup(groupID, levelNames[i].level))
			break;
		++i;
	}
	return levelNames[i - 1].name;
}

AGS3::uint32_t AGSConsole::parseGroup(const char *name, bool &found) const {
	int i = 0;
	while (groupNames[i].name != nullptr) {
		if (scumm_stricmp(name, groupNames[i].name) == 0) {
			found = true;
			return groupNames[i].group;
		}
		++i;
	}

	found = false;
	return (AGS3::uint32_t)-1;
}

AGS3::AGS::Shared::MessageType AGSConsole::parseLevel(const char *name, bool &found) const {
	int i = 0;
	while (levelNames[i].name != nullptr) {
		if (scumm_stricmp(name, levelNames[i].name) == 0) {
			found = true;
			return levelNames[i].level;
		}
		++i;
	}

	found = false;
	return AGS3::AGS::Shared::kDbgMsg_None;
}

void AGSConsole::printGroupList() {
	debugPrintf("%s", groupNames[0].name);
	for (int i = 1 ; groupNames[i].name != nullptr ; ++i)
		debugPrintf(", %s", groupNames[i].name);
}

void AGSConsole::printLevelList() {
	debugPrintf("%s", levelNames[0].name);
	for (int i = 1 ; levelNames[i].name != nullptr ; ++i)
		debugPrintf(", %s", levelNames[i].name);
}

bool AGSConsole::Cmd_SetScriptDump(int argc, const char **argv) {
	if (argc != 2) {
		debugPrintf("Usage: %s [on|off]\n", argv[0]);
		return true;
	}

	if (strcmp(argv[1], "on") == 0 || strcmp(argv[1], "true") == 0)
		AGS3::ccSetOption(SCOPT_DEBUGRUN, 1);
	else
		AGS3::ccSetOption(SCOPT_DEBUGRUN, 0);
	return true;
}

bool AGSConsole::Cmd_getSpriteInfo(int argc, const char **argv) {
	if (argc != 2) {
		debugPrintf("Usage: %s SpriteNumber\n", argv[0]);
		return true;
	}

	int spriteId = atoi(argv[1]);
	if (!_GP(spriteset).DoesSpriteExist(spriteId)) {
		debugPrintf("Sprite %d does not exist\n", spriteId);
		return true;
	}

	AGS3::Shared::Bitmap *sprite = _GP(spriteset)[spriteId];
	if (!sprite) {
		debugPrintf("Failed to get sprite %d\n", spriteId);
		return true;
	}

	debugPrintf("Size: %dx%d\n", sprite->GetWidth(), sprite->GetHeight());
	debugPrintf("Color depth: %d\n", sprite->GetColorDepth());
	return true;
}

bool AGSConsole::Cmd_dumpSprite(int argc, const char **argv) {
	if (argc != 2) {
		debugPrintf("Usage: %s SpriteNumber\n", argv[0]);
		return true;
	}

	int spriteId = atoi(argv[1]);
	if (!_GP(spriteset).DoesSpriteExist(spriteId)) {
		debugPrintf("Sprite %d does not exist\n", spriteId);
		return true;
	}

	AGS3::Shared::Bitmap *sprite = _GP(spriteset)[spriteId];
	if (!sprite) {
		debugPrintf("Failed to get sprite %d\n", spriteId);
		return true;
	}

	Common::Path pngFile(Common::String::format("%s-sprite%03d.png", _vm->getGameId().c_str(), spriteId));
	Common::DumpFile df;
	if (df.open(pngFile)) {
		byte *palette = nullptr;
		if (sprite->GetColorDepth() == 8) {
			palette = new byte[Graphics::PALETTE_SIZE];
			for (int c = 0, i = 0 ; c < Graphics::PALETTE_COUNT ; ++c, i += 3) {
				palette[i] = PALETTE_6BIT_TO_8BIT(_G(current_palette)[c].r);
				palette[i + 1] = PALETTE_6BIT_TO_8BIT(_G(current_palette)[c].g);
				palette[i + 2] = PALETTE_6BIT_TO_8BIT(_G(current_palette)[c].b);
			}
		}
		Image::writePNG(df, sprite->GetAllegroBitmap()->getSurface().rawSurface(), palette);
		delete[] palette;
	}

	return true;
}

LogOutputTarget::LogOutputTarget() {
}

LogOutputTarget::~LogOutputTarget() {
}

void LogOutputTarget::PrintMessage(const AGS3::AGS::Shared::DebugMessage &msg) {
	LogMessageType::Type msgType = LogMessageType::kInfo;
	switch (msg.MT) {
	case AGS3::AGS::Shared::kDbgMsg_None:
		return;
	case AGS3::AGS::Shared::kDbgMsg_Alert:
	case AGS3::AGS::Shared::kDbgMsg_Fatal:
	case AGS3::AGS::Shared::kDbgMsg_Error:
		msgType = LogMessageType::kError;
		break;
	case AGS3::AGS::Shared::kDbgMsg_Warn:
		msgType = LogMessageType::kWarning;
		break;
	case AGS3::AGS::Shared::kDbgMsg_Info:
		msgType = LogMessageType::kInfo;
		break;
	case AGS3::AGS::Shared::kDbgMsg_Debug:
		msgType = LogMessageType::kDebug;
		break;
	}
	Common::String text = Common::String::format("%s\n", msg.Text.GetCStr());
	g_system->logMessage(msgType, text.c_str());
}


// ags_say <font> <key-substring|#n>
//
// A probe for text rendering: put one line of the loaded translation on
// screen in a given font, without playing the game to where it is said.
// The entry is the first one (in .tra file order) whose source key contains
// the substring, or entry n (0-based, same order) with #n. The translated
// text is shown by DisplayAtY(-1, text) from the next game loop, with the
// font as both the speech and the normal font for that one call (Display()
// uses the normal one unless the game always speaks), then both restored.
bool AGSConsole::Cmd_say(int argc, const char **argv) {
	if (argc < 3) {
		debugPrintf("Usage: %s <font> <key-substring|#n>\n", argv[0]);
		return true;
	}
	const int font = atoi(argv[1]);
	if (font < 0 || font >= _GP(game).numfonts) {
		debugPrintf("No font %d (the game has %d)\n", font, _GP(game).numfonts);
		return true;
	}
	Common::String what = argv[2];
	for (int i = 3; i < argc; i++)
		what += Common::String(" ") + argv[i];
	if (_G(trans_filename).IsEmpty()) {
		debugPrintf("No translation loaded\n");
		return true;
	}

	// The game's own dictionary is a hash map, which has no order; read the
	// file again to get one.
	Std::vector<Std::pair<AGS3::AGS::Shared::String, AGS3::AGS::Shared::String> > entries;
	{
		AGS3::AGS::Shared::Translation tra;
		tra.DictOrder = &entries;
		Std::unique_ptr<AGS3::AGS::Shared::Stream> in(_GP(AssetMgr)->OpenAsset(_G(trans_filename)));
		if (!in || !AGS3::AGS::Shared::ReadTraData(tra, in.get())) {
			debugPrintf("Cannot read %s\n", _G(trans_filename).GetCStr());
			return true;
		}
	}

	int found = -1;
	if (what.size() > 1 && what[0] == '#') {
		const int n = atoi(what.c_str() + 1);
		if (n >= 0 && n < (int)entries.size())
			found = n;
	} else {
		for (uint i = 0; i < entries.size() && found < 0; i++)
			if (strstr(entries[i].first.GetCStr(), what.c_str()))
				found = (int)i;
	}
	if (found < 0) {
		debugPrintf("No entry %s among %u\n", what.c_str(), (uint)entries.size());
		return true;
	}

	_sayFont = font;
	_sayText = entries[found].second.GetCStr();
	_sayPending = true;
	debugPrintf("#%d font %d: %s\n", found, font, entries[found].first.GetCStr());
	return true;
}

void AGSConsole::runPendingSay() {
	if (!_sayPending)
		return;
	_sayPending = false;
	const int normal = _GP(play).normal_font, speech = _GP(play).speech_font;
	_GP(play).normal_font = _sayFont;
	_GP(play).speech_font = _sayFont;
	AGS3::DisplayAtY(-1, _sayText.c_str());
	_GP(play).normal_font = normal;
	_GP(play).speech_font = speech;
}

} // End of namespace AGS
