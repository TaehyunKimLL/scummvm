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

#ifndef BACKENDS_PLATFORM_DOS_LAUNCHER_PLAY_MERGE_H
#define BACKENDS_PLATFORM_DOS_LAUNCHER_PLAY_MERGE_H

#include <string>
#include <vector>

#include "play-home.h"
#include "play-sound.h"

// The configuration of a game run is made by PLAY for each run from layers,
// none of them a copy of another (low to high):
//
//   <pack>\ID.INI [target]          what the game is; read only, never copied
//   HOME\SCUMMVM.INI [scummvm]      the player's settings for all games
//                                   (an older PLAY's HOME\DEFAULT.INI below it)
//   HOME\GAMES\ID\USER.INI          the player's settings for this game only
//   the command line                --language=, --set=KEY=VALUE: the engine's
//                                   transient domain, never written back
//
// The first three make HOME\GAMES\ID\RUN.INI, the engine's --config. The keys
// the engine changed in that file during the run go to USER.INI only. Over
// them PLAY sets the target's path= to the game folder it passes as --path
// (setGamePath()), which no layer can change.
namespace Play {

/** An INI as ScummVM reads it: sections and keys lower case, values trimmed; order kept. */
struct Ini {
	struct Section {
		std::string name;
		std::vector<std::pair<std::string, std::string> > keys;
	};
	std::vector<Section> sections;

	Section *find(const std::string &name) {
		for (size_t i = 0; i < sections.size(); ++i)
			if (sections[i].name == name)
				return &sections[i];
		return nullptr;
	}
	const Section *find(const std::string &name) const {
		return const_cast<Ini *>(this)->find(name);
	}
	Section &section(const std::string &name) {
		Section *s = find(name);
		if (s)
			return *s;
		Section n;
		n.name = name;
		sections.push_back(n);
		return sections.back();
	}
	const std::string *get(const std::string &sec, const std::string &key) const {
		const Section *s = find(sec);
		if (!s)
			return nullptr;
		for (size_t i = 0; i < s->keys.size(); ++i)
			if (s->keys[i].first == key)
				return &s->keys[i].second;
		return nullptr;
	}
	void set(const std::string &sec, const std::string &key, const std::string &value) {
		Section &s = section(sec);
		for (size_t i = 0; i < s.keys.size(); ++i)
			if (s.keys[i].first == key) {
				s.keys[i].second = value;
				return;
			}
		s.keys.push_back(std::make_pair(key, value));
	}
	bool erase(const std::string &sec, const std::string &key) {
		Section *s = find(sec);
		if (!s)
			return false;
		for (size_t i = 0; i < s->keys.size(); ++i)
			if (s->keys[i].first == key) {
				s->keys.erase(s->keys.begin() + i);
				return true;
			}
		return false;
	}
	/** Drops the sections that have no keys (left by erase()). */
	void dropEmptySections() {
		for (size_t i = sections.size(); i-- > 0;)
			if (sections[i].keys.empty())
				sections.erase(sections.begin() + i);
	}
	/** The sections other than [scummvm]: the targets. */
	std::vector<std::string> targets() const {
		std::vector<std::string> out;
		for (size_t i = 0; i < sections.size(); ++i)
			if (sections[i].name != "scummvm")
				out.push_back(sections[i].name);
		return out;
	}
};

inline Ini parseIni(const std::string &text) {
	Ini ini;
	// A UTF-8 BOM, as the engine's parser skips it.
	const std::string body = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? text.substr(3) : text;
	// Sections first, in file order, so an empty [target] is still a target.
	size_t pos = 0;
	while (pos < body.size()) {
		size_t end = body.find('\n', pos);
		if (end == std::string::npos)
			end = body.size();
		const std::string line = Detail::trimmed(body.substr(pos, end - pos));
		pos = end + 1;
		if (!line.empty() && line[0] == '[') {
			const size_t close = line.find(']');
			ini.section(Detail::lower(Detail::trimmed(line.substr(1, close == std::string::npos ? std::string::npos : close - 1))));
		}
	}
	Detail::eachSetting(body, [&](const std::string &section, const std::string &key, const std::string &value) {
		ini.set(section, key, value);
		return false;
	});
	// Keys before any header are not ScummVM settings.
	Ini::Section *orphan = ini.find("");
	if (orphan)
		ini.sections.erase(ini.sections.begin() + (orphan - &ini.sections[0]));
	return ini;
}

/** `ini` as a DOS text file (CRLF), after `header` (comment lines, with their own CRLF). */
inline std::string writeIni(const Ini &ini, const std::string &header = std::string()) {
	std::string out = header;
	for (size_t i = 0; i < ini.sections.size(); ++i) {
		const Ini::Section &s = ini.sections[i];
		out += "[" + s.name + "]\r\n";
		for (size_t k = 0; k < s.keys.size(); ++k)
			out += s.keys[k].first + "=" + s.keys[k].second + "\r\n";
		out += "\r\n";
	}
	return out;
}

namespace MergeDetail {

// [scummvm] keys of the engine's own bookkeeping, never a setting of the player.
inline bool isBookkeeping(const std::string &key) {
	return key == "lastselectedgame" || key == "versioninfo";
}

// Target keys the pack owns: what the game is and where. An engine run that
// rewrites them (guioptions after detection) is not a choice of the player,
// and a common setting does not hide them.
inline bool isPackOwned(const std::string &key) {
	return key == "gameid" || key == "engineid" || key == "description" || key == "guioptions" ||
		   key == "path" || key == "extrapath" || key == "savepath";
}

} // End of namespace MergeDetail

/**
 * The run configuration (RUN.INI) of `target` (lower case):
 *   [scummvm]  the pack's [scummvm], then the home's common [scummvm] over it
 *   [target]   the pack's [target] less the keys the home sets in common
 *              (so the home wins over the pack), then USER.INI's [scummvm]
 *              (this game's own choices for all its targets, as --sound ID
 *              writes them) and USER.INI's [target] over it.
 * Only `target` is in it, so one target's settings never reach another.
 */
inline Ini mergeRun(const Ini &pack, const Ini &home, const Ini &user, const std::string &target) {
	Ini run;
	run.section("scummvm");
	const Ini *const commonLayers[] = { &pack, &home };
	for (int l = 0; l < 2; ++l) {
		const Ini::Section *s = commonLayers[l]->find("scummvm");
		for (size_t i = 0; s && i < s->keys.size(); ++i)
			if (!MergeDetail::isBookkeeping(s->keys[i].first))
				run.set("scummvm", s->keys[i].first, s->keys[i].second);
	}
	run.section(target);
	const Ini::Section *pt = pack.find(target);
	for (size_t i = 0; pt && i < pt->keys.size(); ++i) {
		const std::string &key = pt->keys[i].first;
		if (!MergeDetail::isPackOwned(key) && home.get("scummvm", key))
			continue;
		run.set(target, key, pt->keys[i].second);
	}
	const std::string userSecs[] = { "scummvm", target };
	for (int s = 0; s < 2; ++s) {
		const Ini::Section *us = user.find(userSecs[s]);
		for (size_t i = 0; us && i < us->keys.size(); ++i)
			if (!MergeDetail::isPackOwned(us->keys[i].first) && !MergeDetail::isBookkeeping(us->keys[i].first))
				run.set(target, us->keys[i].first, us->keys[i].second);
	}
	return run;
}

/**
 * The game folder the engine is told with --path: the player's own --path=X,
 * -pX or -p X when the command line has one (the last), else `gameDir`.
 * Empty when there is none (started from the home folder, where the pack
 * INI's own path= stands).
 */
inline std::string gamePathOf(const std::string &gameDir, const std::vector<std::string> &args) {
	std::string path = gameDir;
	for (size_t i = 0; i < args.size(); ++i) {
		const std::string &a = args[i];
		if (Detail::startsWith(a, "--path="))
			path = a.substr(7);
		else if (a.size() >= 2 && a[0] == '-' && (a[1] == 'p' || a[1] == 'P'))
			path = a.size() > 2 ? a.substr(2) : (i + 1 < args.size() ? args[++i] : std::string());
	}
	return path;
}

/**
 * `run` with path=`gamePath` in its [target], over any path= of the pack.
 * The engine's --path is a transient setting, and code that reads the
 * target's own domain (SCI's hi-res text: ConfMan.getPath("path", domain))
 * would not see it, so the run's INI says the same. path is pack owned:
 * captureChanges() never moves it to USER.INI. An empty `gamePath` leaves
 * `run` as it is.
 */
inline void setGamePath(Ini &run, const std::string &target, const std::string &gamePath) {
	if (!gamePath.empty())
		run.set(target, "path", gamePath);
}

/**
 * USER.INI after a run: `before` is the run configuration PLAY wrote, `after`
 * the same file as the engine left it. A target key the engine added or
 * changed is the player's choice and goes to USER.INI's [target], unless it
 * is what the layers below USER.INI give anyway (then USER.INI drops it). A
 * key the engine dropped is ignored: a cut-short RUN.INI never erases a
 * setting. Returns true when `user` changed.
 */
inline bool captureChanges(const Ini &before, const Ini &after, const Ini &pack, const Ini &home,
						   const std::string &target, Ini &user) {
	const Ini::Section *a = after.find(target);
	if (!a)
		return false;
	const Ini base = mergeRun(pack, home, Ini(), target);
	bool changed = false;
	for (size_t i = 0; i < a->keys.size(); ++i) {
		const std::string &key = a->keys[i].first;
		const std::string &value = a->keys[i].second;
		if (MergeDetail::isPackOwned(key) || MergeDetail::isBookkeeping(key))
			continue;
		const std::string *was = before.get(target, key);
		if (was && *was == value)
			continue;
		const std::string *below = base.get(target, key);
		if (!below)
			below = base.get("scummvm", key);
		if (below && *below == value) {
			changed = user.erase(target, key) || changed;
			changed = user.erase("scummvm", key) || changed;
			continue;
		}
		user.set(target, key, value);
		changed = true;
	}
	return changed;
}

/**
 * The keys of an older PLAY's copied SCUMMVM.INI that are the player's own
 * choices (volumes, text, the DOS screen settings). Everything else in such a
 * copy came from an older pack INI and must not shadow the current pack.
 */
inline bool isUserChoice(const std::string &key) {
	static const char *const kKeys[] = {
		"music_volume", "sfx_volume", "speech_volume", "mute", "speech_mute", "subtitles", "talkspeed",
		"dos_loading_screen", "dos_vsync", nullptr
	};
	for (int i = 0; kKeys[i]; ++i)
		if (key == kKeys[i])
			return true;
	return false;
}

/**
 * The USER.INI kept from an older PLAY's copied SCUMMVM.INI: its isUserChoice
 * keys, in [scummvm] (which applies to every target of the game; the old
 * targets were named otherwise, like kq1ko). A target's value wins over
 * [scummvm]'s, the first target's over a later one's.
 */
inline Ini migrateOld(const Ini &old) {
	Ini user;
	std::vector<std::string> fromTarget;
	for (size_t s = 0; s < old.sections.size(); ++s) {
		const Ini::Section &sec = old.sections[s];
		const bool app = sec.name == "scummvm";
		for (size_t i = 0; i < sec.keys.size(); ++i) {
			const std::string &key = sec.keys[i].first;
			if (!isUserChoice(key))
				continue;
			bool taken = false;
			for (size_t k = 0; k < fromTarget.size(); ++k)
				taken = taken || fromTarget[k] == key;
			if (taken)
				continue;
			user.set("scummvm", key, sec.keys[i].second);
			if (!app)
				fromTarget.push_back(key);
		}
	}
	return user;
}

/** `ini` with the music choice set in its [scummvm] (the keys PLAY --sound=x ID writes). */
inline void setSoundChoice(Ini &ini, SoundChoice choice) {
	const SoundSetting *set = soundSettingsOf(choice);
	for (int k = 0; k < kSoundSettingCount; ++k)
		ini.set("scummvm", set[k].key, set[k].value);
}

/** `ini` with no music choice in any section (PLAY --sound=default ID). Returns true when one was there. */
inline bool eraseSoundChoice(Ini &ini) {
	bool any = false;
	for (size_t s = 0; s < ini.sections.size(); ++s)
		for (size_t i = ini.sections[s].keys.size(); i-- > 0;)
			if (isSoundKey(ini.sections[s].keys[i].first)) {
				ini.sections[s].keys.erase(ini.sections[s].keys.begin() + i);
				any = true;
			}
	return any;
}

/**
 * The language folder code (ISO 639-1, lower case) of a --language= value,
 * as the engine reads it: every code of Common::parseLanguage(), the old
 * ones (jp, kr, se, ...) included, gives the code of
 * Common::getLanguageFolderCode() for that language (common/language.cpp).
 * Case is ignored. Empty for a value the engine does not know.
 */
inline std::string languageFolderOf(const std::string &value) {
	static const char *const kCodes[][2] = {
		{ "ar", "ar" }, { "bg", "bg" }, { "ca", "ca" }, { "zh", "zh" }, { "cn", "zh" }, { "tw", "zh" },
		{ "hr", "hr" }, { "cs", "cs" }, { "da", "da" }, { "nl", "nl" }, { "en", "en" }, { "gb", "en" },
		{ "us", "en" }, { "et", "et" }, { "fi", "fi" }, { "be", "nl" }, { "fr", "fr" }, { "fr-ca", "fr" },
		{ "de", "de" }, { "el", "el" }, { "he", "he" }, { "hu", "hu" }, { "it", "it" }, { "ja", "ja" },
		{ "ko", "ko" }, { "lt", "lt" }, { "lv", "lv" }, { "nb", "nb" }, { "fa", "fa" }, { "pl", "pl" },
		{ "br", "pt" }, { "pt", "pt" }, { "ru", "ru" }, { "sr", "sr" }, { "sk", "sk" }, { "es", "es" },
		{ "eu", "eu" }, { "sv", "sv" }, { "th", "th" }, { "tr", "tr" }, { "uk", "uk" }, { "vi", "vi" },
		// The engine's obsolete codes, still accepted.
		{ "cz", "cs" }, { "gr", "el" }, { "hb", "he" }, { "jp", "ja" }, { "kr", "ko" }, { "nz", "zh" },
		{ "se", "sv" }, { "zh-cn", "zh" },
		{ nullptr, nullptr }
	};
	const std::string v = Detail::lower(value);
	for (int i = 0; kCodes[i][0]; ++i)
		if (v == kCodes[i][0])
			return kCodes[i][1];
	return std::string();
}

namespace MergeDetail {

// The engine's one-letter options that take a value: attached (-qko) or,
// when none is attached, the next word (-q ko). Case is ignored, as the
// engine does (base/commandLine.cpp, DO_OPTION and DO_OPTION_OPT).
inline bool shortTakesValue(char c) {
	return strchr("bcdegilmpqrsx", tolower((unsigned char)c)) != nullptr;
}

} // End of namespace MergeDetail

/** The value of the last --language=X, -qX or -q X in `args`; empty when none. */
inline std::string languageArg(const std::vector<std::string> &args) {
	std::string lang;
	for (size_t i = 0; i < args.size(); ++i) {
		const std::string &a = args[i];
		if (Detail::startsWith(a, "--language="))
			lang = a.substr(11);
		else if (a.size() >= 2 && a[0] == '-' && (a[1] == 'q' || a[1] == 'Q'))
			lang = a.size() > 2 ? a.substr(2) : (i + 1 < args.size() ? args[++i] : std::string());
		else if (a.size() == 2 && a[0] == '-' && MergeDetail::shortTakesValue(a[1]))
			++i; // the next word is that option's value
	}
	return lang;
}

/**
 * The words of `args` that are not options nor an option's value: the
 * engine's target is the last of them, and PLAY wants exactly one.
 */
inline std::vector<std::string> bareWords(const std::vector<std::string> &args) {
	std::vector<std::string> out;
	for (size_t i = 0; i < args.size(); ++i) {
		const std::string &a = args[i];
		if (a.empty())
			continue;
		if (a[0] != '-') {
			out.push_back(a);
			continue;
		}
		if (a.size() == 2 && MergeDetail::shortTakesValue(a[1]))
			++i;
	}
	return out;
}

/**
 * For a command line with two target words, one of them a language, like
 * "PLAY KQ1 kq1 ko": the one meant, "PLAY KQ1 --language=ko kq1". Empty
 * when no such reading fits. `targets` are the pack's sections (lower case).
 */
inline std::string languageHint(const std::string &id, const std::vector<std::string> &words, const std::vector<std::string> &targets) {
	if (words.size() != 2)
		return std::string();
	for (int t = 0; t < 2; ++t) {
		const std::string target = Detail::lower(words[t]), lang = words[1 - t];
		bool known = false;
		for (size_t i = 0; i < targets.size(); ++i)
			known = known || targets[i] == target;
		if (known && !languageFolderOf(lang).empty())
			return "PLAY " + id + " --language=" + Detail::lower(lang) + " " + words[t];
	}
	return std::string();
}

/** The words that --8bit stands for: the L preset, an 8-bit screen with the map's 8-bit faces. */
inline std::vector<std::string> eightBitArgs() {
	std::vector<std::string> out;
	out.push_back("--set=render_target=clut8");
	out.push_back("--set=hires_text_font=bitmap");
	return out;
}

/** `args` with PLAY's own options turned into the engine's: --8bit becomes eightBitArgs(). */
inline std::vector<std::string> engineArgs(const std::vector<std::string> &args) {
	std::vector<std::string> out;
	for (size_t i = 0; i < args.size(); ++i) {
		if (args[i] == "--8bit") {
			const std::vector<std::string> e = eightBitArgs();
			out.insert(out.end(), e.begin(), e.end());
		} else {
			out.push_back(args[i]);
		}
	}
	return out;
}

/**
 * True when a game can be played in the language folder code `code`: English
 * always (the game's own files), the language the pack's [target] names, or a
 * language folder of the game (`folders`, lower case).
 */
inline bool hasLanguage(const std::string &code, const std::vector<std::string> &folders, const std::string &packLanguage) {
	if (code.empty())
		return false;
	if (code == "en" || code == languageFolderOf(packLanguage))
		return true;
	for (size_t i = 0; i < folders.size(); ++i)
		if (folders[i] == code)
			return true;
	return false;
}

/** The languages hasLanguage() accepts, for a message: "en ko". */
inline std::string languagesOf(const std::vector<std::string> &folders, const std::string &packLanguage) {
	std::vector<std::string> codes;
	codes.push_back("en");
	std::vector<std::string> more = folders;
	more.push_back(languageFolderOf(packLanguage));
	for (size_t i = 0; i < more.size(); ++i) {
		bool seen = more[i].empty() || languageFolderOf(more[i]) != more[i];
		for (size_t k = 0; k < codes.size(); ++k)
			seen = seen || codes[k] == more[i];
		if (!seen)
			codes.push_back(more[i]);
	}
	std::string out;
	for (size_t i = 0; i < codes.size(); ++i)
		out += (i ? " " : "") + codes[i];
	return out;
}

} // End of namespace Play

#endif
