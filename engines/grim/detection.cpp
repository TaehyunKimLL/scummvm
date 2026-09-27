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
#include "common/language.h"

#include "engines/advancedDetector.h"
#include "grim/debug.h"
#include "grim/detection.h"
#include "grim/detection_tables.h"

static const DebugChannelDef debugFlagList[] = {
	{Grim::Debug::Info, "info", ""},
	{Grim::Debug::Warning, "warning", ""},
	{Grim::Debug::Error, "error", ""},
	{Grim::Debug::Engine, "engine", ""},
	{Grim::Debug::Lua, "lua", ""},
	{Grim::Debug::Bitmaps, "bitmaps", ""},
	{Grim::Debug::Models, "models", ""},
	{Grim::Debug::Actors, "actors", ""},
	{Grim::Debug::Costumes, "costumes", ""},
	{Grim::Debug::Chores, "chores", ""},
	{Grim::Debug::Fonts, "fonts", ""},
	{Grim::Debug::Keyframes, "keyframes", ""},
	{Grim::Debug::Movie, "movie", ""},
	{Grim::Debug::Sound, "sound", ""},
	{Grim::Debug::Scripts, "scripts", ""},
	{Grim::Debug::Sets, "sets", ""},
	{Grim::Debug::TextObjects, "textobjects", ""},
	{Grim::Debug::Patchr, "patchr", ""},
	{Grim::Debug::Lipsync, "lipsync", ""},
	{Grim::Debug::Sprites, "sprites", ""},
	DEBUG_CHANNEL_END
};

namespace Grim {

static const PlainGameDescriptor grimGames[] = {
	{"grim", "Grim Fandango"},
	{"monkey4", "Escape From Monkey Island"},
	{nullptr, nullptr}
};

class GrimMetaEngineDetection : public AdvancedMetaEngineDetection<Grim::GrimGameDescription> {
public:
	GrimMetaEngineDetection() : AdvancedMetaEngineDetection(Grim::gameDescriptions, grimGames) {
		_guiOptions = GUIO_NOMIDI;
		_flags |= kADFlagCanTranscodeTraditionalChineseToSimplified;
	}

	PlainGameDescriptor findGame(const char *gameid) const override {
		return Engines::findGameID(gameid, _gameIds, obsoleteGameIDsTable);
	}

	Common::Error identifyGame(DetectedGame &game, const void **descriptor) override {
		Engines::upgradeTargetIfNecessary(obsoleteGameIDsTable);
		return AdvancedMetaEngineDetection::identifyGame(game, descriptor);
	}

	const char *getEngineName() const override {
		return "Grim";
	}

	const char *getName() const override {
		return "grim";
	}

	const char *getOriginalCopyright() const override {
		return "LucasArts GrimE Games (C) LucasArts";
	}

	const DebugChannelDef *getDebugChannels() const override {
		return debugFlagList;
	}

	/**
	 * FORK-ONLY (scummvm-korean i18n fork, C11 Task 9): a retail Grim with
	 * a translation table for the target's language (language=ja and
	 * grim.ja.tab, say) is the game the data is, whatever language the
	 * detection table gives it. The language filter of the detector would
	 * otherwise reject the English CD with language=ja. Only when that
	 * table is present; the engine reads it (engines/grim/localize.cpp).
	 */
	ADDetectedGame fallbackDetect(const FileMap &allFiles, const Common::FSList &fslist, ADDetectedGameExtraInfo **extra) const override {
		if (fslist.empty() || !ConfMan.hasKey("language"))
			return ADDetectedGame();
		const Common::Language lang = Common::parseLanguage(ConfMan.get("language"));
		if (lang == Common::UNK_LANG || lang == Common::EN_ANY)
			return ADDetectedGame();
		const Common::String table = Common::String::format("grim.%s.tab", Common::getLanguageCode(lang));
		if (!allFiles.contains(Common::Path(table)))
			return ADDetectedGame();

		Common::Platform platform = Common::kPlatformUnknown;
		if (ConfMan.hasKey("platform"))
			platform = Common::parsePlatform(ConfMan.get("platform"));
		// detectGame() is not const; it only reads the tables and the files.
		GrimMetaEngineDetection *self = const_cast<GrimMetaEngineDetection *>(this);
		const ADDetectedGames matches = self->detectGame(fslist.begin()->getParent(), allFiles, Common::UNK_LANG, platform, "");
		for (uint i = 0; i < matches.size(); i++) {
			const GrimGameDescription *g = reinterpret_cast<const GrimGameDescription *>(matches[i].desc);
			if (g->gameType == GType_GRIM && !matches[i].hasUnknownFiles &&
			    !(g->desc.flags & (ADGF_DEMO | ADGF_REMASTERED)) && g->desc.language != Common::KO_KOR)
				return matches[i];
		}
		return ADDetectedGame();
	}

	DetectedGame toDetectedGame(const ADDetectedGame &adGame, ADDetectedGameExtraInfo *extraInfo) const override {
		DetectedGame game = AdvancedMetaEngineDetection::toDetectedGame(adGame, extraInfo);
		GrimGameType gameID = reinterpret_cast<const GrimGameDescription *>(adGame.desc)->gameType;

		if (gameID == GType_MONKEY4 && adGame.desc->language == Common::Language::ZH_TWN) {
			game.appendGUIOptions(Common::getGameGUIOptionsDescriptionLanguage(Common::ZH_TWN));
			game.appendGUIOptions(Common::getGameGUIOptionsDescriptionLanguage(Common::ZH_CHN));
		}

		return game;
	}
};

} // End of namespace Grim


REGISTER_PLUGIN_STATIC(GRIM_DETECTION, PLUGIN_TYPE_ENGINE_DETECTION, Grim::GrimMetaEngineDetection);
