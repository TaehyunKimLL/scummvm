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

// PLAY.EXE: starts a game of a (possibly read-only) pack with ScummVM
// installed on a writable drive, keeping the game's saves, logs and the
// player's own settings in a folder of its own beside the program.
//
//   PLAY ID [options] target   run `target` from GAMES\ID of the current folder
//   PLAY --install ID          create the game's folder, run nothing
//   PLAY --sound=adlib|mt32|gm ID   set the music output of the game (USER.INI)
//   PLAY --sound ID            ask which music output, then set it
//   PLAY --sound=default ID    drop the game's own choice: it follows the common one
//   PLAY --sound[=x] DEFAULT   the common choice of all games, in HOME\SCUMMVM.INI [scummvm]
//
// The pack's ID.INI is read on every run and never copied: each run writes
// HOME\GAMES\ID\RUN.INI from it, the common [scummvm] of HOME\SCUMMVM.INI and
// the player's HOME\GAMES\ID\USER.INI (play-merge.h), gives it to the engine,
// keeps what the engine changed in USER.INI and deletes it. Nothing is
// written to the pack's folder.

#include <conio.h>
#include <dir.h>
#include <dirent.h>
#include <errno.h>
#include <process.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "play-home.h"
#include "play-merge.h"
#include "play-sound.h"

namespace {

const char kUsage[] =
	"PLAY ID [options] target       play a game of the pack in the current folder\n"
	"PLAY --install ID              create the game's folder only\n"
	"PLAY --sound=adlib|mt32|gm ID  set the music output of the game\n"
	"PLAY --sound ID                ask which music output, then set it\n"
	"PLAY --sound=default ID        the game follows the default again\n"
	"PLAY --sound[=x] DEFAULT       set the default music output of all games\n"
	"Options of a game:\n"
	"  --language=xx    the language: en, or one the game has files for (ko ...)\n"
	"  --8bit           an 8-bit screen with the 8-bit fonts\n"
	"  --set=KEY=VALUE  any setting, for this run only (other ScummVM options too)\n"
	"ID is the folder name of the game, target its section in ID.INI:\n"
	"  PLAY KQ1 --language=ko kq1\n"
	"Each run makes GAMES\\ID\\RUN.INI below the folder of PLAY.EXE from ID.INI,\n"
	"the [scummvm] of SCUMMVM.INI there and GAMES\\ID\\USER.INI. What you change\n"
	"in the game is kept in USER.INI; saves and logs are kept in GAMES\\ID.\n";

std::string driveCwd(char letter) {
	const int drive = toupper((unsigned char)letter) - 'A';
	const int back = getdisk();
	setdisk(drive);
	char buf[260];
	const std::string dir = (getdisk() == drive && getcwd(buf, sizeof(buf))) ? std::string(buf) : std::string();
	setdisk(back);
	return dir;
}

// DOS keeps the current drive and directory after a program ends, and one
// directory per drive: the drive of the home folder is put back too.
class DirGuard {
public:
	DirGuard() : _drive(getdisk()), _otherDrive(-1) {
		char buf[260];
		if (getcwd(buf, sizeof(buf)))
			_cwd = buf;
	}
	~DirGuard() {
		if (_otherDrive >= 0) {
			setdisk(_otherDrive);
			if (!_otherCwd.empty())
				chdir(_otherCwd.c_str());
		}
		setdisk(_drive);
		if (!_cwd.empty())
			chdir(_cwd.c_str());
	}
	const std::string &cwd() const { return _cwd; }
	// Call before changing to `path`.
	void willEnter(const std::string &path) {
		if (_otherDrive >= 0 || !Play::Detail::hasDrive(path.c_str()))
			return;
		const int drive = toupper((unsigned char)path[0]) - 'A';
		if (drive == _drive)
			return;
		_otherCwd = driveCwd(path[0]);
		_otherDrive = drive;
	}

private:
	int _drive;
	int _otherDrive;
	std::string _cwd;
	std::string _otherCwd;
};

bool isDir(const std::string &path) {
	struct stat st;
	return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool isFile(const std::string &path) {
	struct stat st;
	return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool readFile(const std::string &path, std::string &out) {
	FILE *f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	char buf[1024];
	size_t n;
	out.clear();
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
		out.append(buf, n);
	const bool ok = !ferror(f);
	fclose(f);
	return ok;
}

bool writeFile(const std::string &path, const std::string &data) {
	FILE *f = fopen(path.c_str(), "wb");
	if (!f)
		return false;
	const bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
	return fclose(f) == 0 && ok;
}

// mkdir for each level of `path` that is missing.
bool makeDirs(const std::string &path) {
	for (size_t i = 0; i <= path.size(); ++i) {
		if (i < path.size() && path[i] != '/')
			continue;
		const std::string part = path.substr(0, i);
		if (part.empty() || (part.size() == 2 && part[1] == ':'))
			continue;
		if (!isDir(part) && mkdir(part.c_str(), 0777) != 0 && !isDir(part))
			return false;
	}
	return true;
}

// The log files are the program's, written in its folder; they move to the
// game's folder after a run.
void moveFile(const std::string &from, const std::string &to) {
	std::string data;
	if (!isFile(from) || !readFile(from, data))
		return;
	if (writeFile(to, data))
		remove(from.c_str());
}

int fail(const char *what, const std::string &name) {
	fprintf(stderr, "PLAY: %s %s\n", what, Play::dosPath(name).c_str());
	return 1;
}

// Writes `data` beside `path` and swaps it in, so a failed write keeps the old file.
bool replaceFile(const std::string &path, const std::string &data) {
	const std::string tmp = path.substr(0, path.rfind('/') + 1) + "SCUMMVM.NEW";
	if (!writeFile(tmp, data)) {
		remove(tmp.c_str());
		return false;
	}
	remove(path.c_str());
	if (rename(tmp.c_str(), path.c_str()) == 0)
		return true;
	// The old file is gone or locked: write in place rather than leave no INI.
	const bool done = writeFile(path, data);
	remove(tmp.c_str());
	return done;
}

// Makes the folder of a game and its SAVES. Returns false when a folder cannot be made.
bool ensureProfile(const Play::Profile &p, bool say) {
	const bool had = isDir(p.dir);
	if (!makeDirs(p.saves)) {
		fprintf(stderr, "PLAY: cannot create %s (is the folder of PLAY.EXE writable?)\n", Play::dosPath(p.saves).c_str());
		return false;
	}
	if (say && !had)
		printf("Created %s\n", Play::dosPath(p.dir).c_str());
	return true;
}

// An INI file as settings; an empty one when it cannot be read.
Play::Ini readIni(const std::string &path) {
	std::string text;
	readFile(path, text);
	return Play::parseIni(text);
}

const char kUserHeader[] =
	"# Your own settings of this game, over the pack's INI and the common\r\n"
	"# [scummvm] of HOME\\SCUMMVM.INI. PLAY writes the keys you change in the game\r\n"
	"# here; you may edit or delete this file. RUN.INI is made anew for each run.\r\n";

bool writeUser(const Play::Profile &p, Play::Ini user) {
	user.dropEmptySections();
	if (replaceFile(p.user, Play::writeIni(user, kUserHeader)))
		return true;
	fail("cannot write", p.user);
	return false;
}

// An older PLAY's copy of the pack's INI (SCUMMVM.INI) and its SOUND.INI become
// USER.INI once: only the player's own choices, the rest came from an older
// pack. The copy is kept as SCUMMVM.OLD.
void migrateOldProfile(const Play::Profile &p) {
	if (isFile(p.user))
		return;
	const bool hasOld = isFile(p.oldIni), hasSound = isFile(p.sound);
	if (!hasOld && !hasSound)
		return;
	Play::Ini user;
	if (hasOld)
		user = Play::migrateOld(readIni(p.oldIni));
	Play::SoundChoice choice;
	std::string text;
	if (hasSound && readFile(p.sound, text) && Play::soundChoiceOf(text, choice))
		Play::setSoundChoice(user, choice);
	if (!writeUser(p, user))
		return;
	if (hasOld) {
		remove(p.oldBackup.c_str());
		if (rename(p.oldIni.c_str(), p.oldBackup.c_str()) == 0)
			printf("PLAY: %s is not used any more.\n"
				   "      Your own settings in it (volumes, subtitles, text speed, music output)\n"
				   "      are now in USER.INI; the old file is SCUMMVM.OLD.\n", Play::dosPath(p.oldIni).c_str());
	}
	if (hasSound)
		remove(p.sound.c_str());
}

const char *const kSoundLabels[] = { "AdLib / OPL", "Roland MT-32 or CM-32L", "General MIDI" };

// Asks on the keyboard. `follow` is set by 4 (use the default), offered when `allowFollow`.
bool askSound(const std::string &what, bool allowFollow, Play::SoundChoice &choice, bool &follow) {
	printf("Select music output for %s:\n"
		   "  1) AdLib / OPL\n"
		   "  2) Roland MT-32 or CM-32L\n"
		   "  3) General MIDI\n", what.c_str());
	if (allowFollow)
		printf("  4) Use the default of all games\n");
	printf(allowFollow ? "Choice (1,2,3,4, Esc to cancel): " : "Choice (1,2,3, Esc to cancel): ");
	fflush(stdout);
	for (;;) {
		const int key = getch();
		if (key == 0 || key == 0xE0) {
			getch(); // an extended key sends a scan code next; it is not a menu key
			continue;
		}
		if (key == 27 || key == 3 || key == EOF) {
			printf("\n");
			return false;
		}
		if (allowFollow && key == '4') {
			follow = true;
			printf("4\n");
			return true;
		}
		if (Play::soundOfKey(key, choice)) {
			printf("%c\n", key);
			return true;
		}
	}
}

void blasterNote(Play::SoundChoice choice) {
	if (choice != Play::kSoundAdlib && !Play::blasterHasMpuPort(getenv("BLASTER")))
		printf("Note: BLASTER has no P (MPU-401 port); 0x330 will be used.\n");
}

// The home's common settings: the [scummvm] of HOME\SCUMMVM.INI, over the
// DEFAULT.INI of an older PLAY (its music choice).
Play::Ini homeCommon(const std::string &home) {
	Play::Ini common = readIni(Play::join(home, "DEFAULT.INI"));
	const Play::Ini main = readIni(Play::join(home, "SCUMMVM.INI"));
	const Play::Ini::Section *s = main.find("scummvm");
	for (size_t i = 0; s && i < s->keys.size(); ++i)
		common.set("scummvm", s->keys[i].first, s->keys[i].second);
	Play::Ini out;
	const Play::Ini::Section *c = common.find("scummvm");
	for (size_t i = 0; c && i < c->keys.size(); ++i)
		out.set("scummvm", c->keys[i].first, c->keys[i].second);
	return out;
}

int setGameSound(const Play::Profile &p, const std::string &home, const std::string &id, bool follow, Play::SoundChoice choice) {
	Play::Ini user = readIni(p.user);
	Play::eraseSoundChoice(user);
	if (!follow)
		Play::setSoundChoice(user, choice);
	if (!writeUser(p, user))
		return 1;
	remove(p.sound.c_str());
	if (!follow) {
		printf("Music output of %s: %s (in %s)\n", id.c_str(), kSoundLabels[choice], Play::dosPath(p.user).c_str());
		blasterNote(choice);
		return 0;
	}
	Play::SoundChoice def;
	if (Play::soundChoiceOf(Play::writeIni(homeCommon(home)), def))
		printf("Music output of %s: the default, %s\n", id.c_str(), kSoundLabels[def]);
	else
		printf("Music output of %s: the default, which is not set (the game's INI decides)\n", id.c_str());
	return 0;
}

int setDefaultSound(const std::string &home, Play::SoundChoice choice) {
	const std::string ini = Play::join(home, "SCUMMVM.INI");
	std::string text;
	if (isFile(ini) && !readFile(ini, text))
		return fail("cannot read", ini);
	if (!replaceFile(ini, Play::applySoundChoice(text, choice)))
		return fail("cannot write", ini);
	// An older PLAY's default; its only settings are the music ones just written.
	remove(Play::join(home, "DEFAULT.INI").c_str());
	printf("Default music output: %s (in %s)\n"
		   "Games with no choice of their own use it from their next start.\n", kSoundLabels[choice], Play::dosPath(ini).c_str());
	blasterNote(choice);
	return 0;
}

// The language folders of a game folder (two-letter names, lower case).
std::vector<std::string> languageFolders(const std::string &gameDir) {
	std::vector<std::string> out;
	DIR *d = gameDir.empty() ? nullptr : opendir(gameDir.c_str());
	if (!d)
		return out;
	while (struct dirent *e = readdir(d)) {
		const std::string n = Play::Detail::lower(e->d_name);
		if (n.size() == 2 && isalpha((unsigned char)n[0]) && isalpha((unsigned char)n[1]) && isDir(Play::join(gameDir, n)))
			out.push_back(n);
	}
	closedir(d);
	return out;
}

std::string joinWords(const std::vector<std::string> &words) {
	std::string out;
	for (size_t i = 0; i < words.size(); ++i)
		out += (i ? " " : "") + words[i];
	return out;
}

int play(DirGuard &guard, const std::string &home, const std::string &pack, const std::string &sample,
		 const Play::Profile &p, const std::string &id, const std::vector<std::string> &extra) {
	std::string packText;
	if (!readFile(sample, packText)) {
		fprintf(stderr, "PLAY: run it from the game's folder: %s not found\n", Play::dosPath(sample).c_str());
		return 1;
	}
	const Play::Ini packIni = Play::parseIni(packText);

	const std::vector<std::string> targets = packIni.targets();
	const std::vector<std::string> words = Play::bareWords(extra);
	if (words.empty()) {
		fprintf(stderr, "PLAY: name the game to play, like PLAY %s %s\n"
				"      Targets in %s: %s\n", id.c_str(), targets.empty() ? "target" : targets[0].c_str(),
				Play::dosPath(sample).c_str(), joinWords(targets).c_str());
		return 1;
	}
	if (words.size() > 1) {
		const std::string hint = Play::languageHint(id, words, targets);
		fprintf(stderr, "PLAY: one target only, not \"%s\". A language is given as --language=xx", joinWords(words).c_str());
		fprintf(stderr, hint.empty() ? ".\n" : ":\n      %s\n", hint.c_str());
		return 1;
	}
	const std::string target = Play::Detail::lower(words[0]);
	if (!packIni.find(target) || target == "scummvm") {
		fprintf(stderr, "PLAY: %s has no target '%s'. Targets: %s\n", Play::dosPath(sample).c_str(), target.c_str(), joinWords(targets).c_str());
		return 1;
	}

	// The game's files; GAMES\ID of the home is also the profile when started there.
	std::string gameDir = Play::join(Play::join(pack, "GAMES"), Play::Detail::upper(id));
	if (!isDir(gameDir))
		gameDir.clear();
	// Started from the home folder, GAMES\ID is the profile, not the game: cleared before anything looks in it.
	if (Play::samePath(pack, home))
		gameDir.clear();

	// A language the pack has no files for is an error here, before the engine starts.
	const std::string lang = Play::languageArg(extra);
	if (!lang.empty()) {
		// The game folder the engine will get (a --path of the arguments, else GAMES\ID of the pack).
		const std::vector<std::string> folders = languageFolders(Play::gamePathOf(gameDir, extra));
		const std::string *packLang = packIni.get(target, "language");
		const std::string packCode = packLang ? *packLang : std::string();
		if (!Play::hasLanguage(Play::languageFolderOf(lang), folders, packCode)) {
			fprintf(stderr, "PLAY: %s has no files for the language '%s'. Languages: %s\n", id.c_str(), lang.c_str(),
					Play::languagesOf(folders, packCode).c_str());
			return 1;
		}
	}

	const Play::Ini common = homeCommon(home);
	Play::Ini user = readIni(p.user);
	Play::Ini run = Play::mergeRun(packIni, common, user, target);
	// The game folder the engine gets as --path, also in the target's own domain.
	Play::setGamePath(run, target, Play::gamePathOf(gameDir, extra));
	const std::string runText = Play::writeIni(run,
		"# Made by PLAY for this run from " + Play::dosPath(sample) + ", the [scummvm] of HOME\\SCUMMVM.INI\r\n"
		"# and USER.INI. Do not edit: it is made anew each time. Edit USER.INI.\r\n");

	const std::string engine = Play::engineOf(runText, target);
	// One EXE per engine family: the INI's engineid says which.
	const std::vector<std::string> exes = Play::exesFor(engine);
	std::string exe;
	for (size_t i = 0; i < exes.size() && exe.empty(); ++i) {
		if (isFile(Play::join(home, exes[i])))
			exe = Play::join(home, exes[i]);
	}
	if (exe.empty())
		return fail(engine == "agi" ? "no AGI.EXE in" : "no SCI.EXE or SCUMM.EXE in", home);

	if (!writeFile(p.run, runText)) {
		remove(p.run.c_str());
		return fail("cannot write", p.run);
	}

	const std::vector<std::string> args = Play::childArgs(p, gameDir, extra);

	std::vector<char *> argv;
	argv.push_back(const_cast<char *>(exe.c_str()));
	for (size_t i = 0; i < args.size(); ++i)
		argv.push_back(const_cast<char *>(args[i].c_str()));
	argv.push_back(nullptr);

	const std::string homeLog = Play::join(home, "SCUMMVM.LOG");
	const std::string homeExitLog = Play::join(home, "EXITLOG.TXT");
	remove(homeLog.c_str());
	remove(homeExitLog.c_str());
	guard.willEnter(home);
	if (Play::Detail::hasDrive(home.c_str()))
		setdisk(toupper((unsigned char)home[0]) - 'A');
	if (chdir(home.c_str()) != 0) {
		remove(p.run.c_str());
		return fail("cannot enter", home);
	}

	// Ctrl-C belongs to the game; this program only waits for it.
	void (*const oldInt)(int) = signal(SIGINT, SIG_IGN);
	const int rc = spawnv(P_WAIT, exe.c_str(), argv.data());
	signal(SIGINT, oldInt);

	// What the engine changed is the player's: into USER.INI, nowhere else.
	std::string afterText;
	if (readFile(p.run, afterText) && afterText != runText &&
		Play::captureChanges(run, Play::parseIni(afterText), packIni, common, target, user))
		writeUser(p, user);
	remove(p.run.c_str());

	// -1 is a failed start; another negative value is a game killed by a signal, whose logs matter most.
	if (rc == -1) {
		fprintf(stderr, "PLAY: cannot run %s: %s\n", Play::dosPath(exe).c_str(), strerror(errno));
		return 1;
	}
	moveFile(homeLog, p.log);
	moveFile(homeExitLog, p.exitLog);
	return rc < 0 ? 1 : rc;
}

} // namespace

int main(int argc, char *argv[]) {
	if (argc < 2 || !strcmp(argv[1], "--help") || !strcmp(argv[1], "-h") || !strcmp(argv[1], "/?")) {
		fputs(kUsage, stdout);
		return argc < 2 ? 1 : 0;
	}

	DirGuard guard;
	std::string home = Play::exeDir(argc > 0 ? argv[0] : nullptr);
	if (home.empty())
		home = guard.cwd();
	// argv[0] may be relative (".\\PLAY", "SUB\\PLAY"): made absolute now, before anything changes directory.
	home = Play::Detail::slashed(Play::absolutePath(Play::Detail::slashed(guard.cwd()), home.c_str(), driveCwd));
	const std::string pack = Play::Detail::slashed(guard.cwd());

	bool install = false;
	bool sound = false;
	bool ask = false;
	bool follow = false;
	Play::SoundChoice choice = Play::kSoundAdlib;
	int i = 1;
	if (!strcmp(argv[i], "--install")) {
		install = true;
		++i;
	} else if (!strcmp(argv[i], "--sound")) {
		sound = true;
		ask = true;
		++i;
	} else if (!strncmp(argv[i], "--sound=", 8) && Play::SoundDetail::lower(argv[i] + 8) == "default") {
		sound = true;
		follow = true;
		++i;
	} else if (!strncmp(argv[i], "--sound=", 8)) {
		if (!Play::parseSoundChoice(argv[i] + 8, choice)) {
			fputs(kUsage, stderr);
			return 1;
		}
		sound = true;
		++i;
	}
	if (i >= argc || !Play::validId(argv[i])) {
		fputs(kUsage, stderr);
		return 1;
	}
	const std::string id = Play::Detail::upper(argv[i++]);
	const Play::Profile p = Play::profileOf(home, id);
	const std::string sample = Play::join(pack, id + ".INI");

	if (Play::isReservedId(id)) {
		if (!sound || follow) {
			fputs("PLAY: DEFAULT is not a game; it only takes --sound=adlib|mt32|gm or --sound\n", stderr);
			return 1;
		}
		bool unused = false;
		if (ask && !askSound("the default", false, choice, unused))
			return 1;
		return setDefaultSound(home, choice);
	}

	// An ID with neither a profile nor an ID.INI in the current folder is refused before anything is created (a mistyped ID
	// must not leave an empty GAMES\BADID\SAVES behind).
	if (!isDir(p.dir) && !isFile(sample)) {
		fprintf(stderr, "PLAY: run it from the game's folder: %s not found\n", Play::dosPath(sample).c_str());
		return 1;
	}
	if (!ensureProfile(p, install || sound))
		return 1;
	migrateOldProfile(p);
	if (install)
		return 0;
	if (sound) {
		if (ask && !askSound(id, true, choice, follow))
			return 1;
		return setGameSound(p, home, id, follow, choice);
	}

	std::vector<const char *> words;
	words.push_back("");
	for (int k = i; k < argc; ++k)
		words.push_back(argv[k]);
	std::vector<std::string> extra = Play::absolutizeArgs((int)words.size(), words.data(), pack, driveCwd);
	extra.erase(extra.begin());
	extra = Play::engineArgs(extra);
	return play(guard, home, pack, sample, p, id, extra);
}
