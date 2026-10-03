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
// installed on a writable drive, keeping the game's INI, saves and logs in
// a folder of its own beside the program.
//
//   PLAY ID [options] target   run `target` from GAMES\ID of the current folder
//   PLAY --install ID          create the game's folder and INI, run nothing
//   PLAY --sound=adlib|mt32|gm ID   set the music output of the game's INI
//   PLAY --sound ID            ask which music output, then set it

#include <conio.h>
#include <dir.h>
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
#include "play-sound.h"

namespace {

const char kUsage[] =
	"PLAY ID [options] target       run a game of the pack in the current folder\n"
	"PLAY --install ID              create the game's folder and INI only\n"
	"PLAY --sound=adlib|mt32|gm ID  set the music output of the game's INI\n"
	"PLAY --sound ID                ask which music output, then set it\n"
	"ID is the folder name of the game, like MI1KO. The game's INI, saves and\n"
	"logs are kept in GAMES\\ID below the folder of PLAY.EXE.\n";

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
	return rename(tmp.c_str(), path.c_str()) == 0;
}

// Makes the folder of a game and its INI from the pack's sample when there is
// none. Returns false when a folder cannot be made.
bool ensureProfile(const Play::Profile &p, const std::string &sample, bool say) {
	const bool had = isDir(p.dir);
	if (!makeDirs(p.saves)) {
		fprintf(stderr, "PLAY: cannot create %s (is the folder of PLAY.EXE writable?)\n", Play::dosPath(p.saves).c_str());
		return false;
	}
	if (say && !had)
		printf("Created %s\n", Play::dosPath(p.dir).c_str());
	if (isFile(p.ini))
		return true;
	std::string text;
	if (!readFile(sample, text)) {
		fprintf(stderr, "PLAY: no %s in the current folder; %s starts empty.\n", Play::dosPath(sample).c_str(), Play::dosPath(p.ini).c_str());
		return true;
	}
	if (!writeFile(p.ini, text)) {
		fprintf(stderr, "PLAY: cannot write %s\n", Play::dosPath(p.ini).c_str());
		return false;
	}
	if (say)
		printf("Created %s from %s\n", Play::dosPath(p.ini).c_str(), Play::dosPath(sample).c_str());
	return true;
}

bool askSound(const std::string &id, Play::SoundChoice &choice) {
	printf("Select music output for %s:\n"
		   "  1) AdLib / OPL\n"
		   "  2) Roland MT-32 or CM-32L\n"
		   "  3) General MIDI\n"
		   "Choice (1,2,3, Esc to cancel): ", id.c_str());
	fflush(stdout);
	for (;;) {
		const int key = getch();
		if (key == 27 || key == 3 || key == EOF) {
			printf("\n");
			return false;
		}
		if (Play::soundOfKey(key, choice)) {
			printf("%c\n", key);
			return true;
		}
	}
}

int setSound(const Play::Profile &p, Play::SoundChoice choice, const std::string &id) {
	std::string text;
	// An unreadable INI must stay as it is, not be rebuilt from a partial read.
	if (isFile(p.ini) && !readFile(p.ini, text))
		return fail("cannot read", p.ini);
	if (!replaceFile(p.ini, Play::applySoundChoice(text, choice)))
		return fail("cannot write", p.ini);
	static const char *const kLabels[] = { "AdLib / OPL", "Roland MT-32 or CM-32L", "General MIDI" };
	printf("Music output of %s: %s\n", id.c_str(), kLabels[choice]);
	if (choice != Play::kSoundAdlib && !Play::blasterHasMpuPort(getenv("BLASTER")))
		printf("Note: BLASTER has no P (MPU-401 port); 0x330 will be used.\n");
	return 0;
}

int play(DirGuard &guard, const std::string &home, const std::string &pack, const std::string &sample,
		 const Play::Profile &p, const std::string &id, const std::vector<std::string> &extra) {
	std::string iniText;
	readFile(p.ini, iniText);
	const std::string target = Play::targetOf(extra);
	if (!target.empty() && !Play::hasTarget(iniText, target) && isFile(sample))
		fprintf(stderr, "PLAY: %s has no [%s] section; the sample %s is not merged into it.\n"
				"      Delete that INI to take the sample again.\n",
				Play::dosPath(p.ini).c_str(), target.c_str(), Play::dosPath(sample).c_str());
	const std::string engine = Play::engineOf(iniText, target);
	// One EXE per engine family: the INI's engineid says which.
	const char *const first = engine == "scumm" ? "SCUMM.EXE" : "SCI.EXE";
	const char *const second = engine == "scumm" ? "SCI.EXE" : "SCUMM.EXE";
	std::string exe = Play::join(home, first);
	if (!isFile(exe))
		exe = Play::join(home, second);
	if (!isFile(exe))
		return fail("no SCI.EXE or SCUMM.EXE in", home);

	std::string gameDir = Play::join(Play::join(pack, "GAMES"), Play::Detail::upper(id));
	// Started from the home folder, GAMES\ID is the profile: a --path to it would hide the INI's own.
	if (!isDir(gameDir) || Play::samePath(pack, home))
		gameDir.clear();
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
	if (chdir(home.c_str()) != 0)
		return fail("cannot enter", home);

	// Ctrl-C belongs to the game; this program only waits for it.
	void (*const oldInt)(int) = signal(SIGINT, SIG_IGN);
	const int rc = spawnv(P_WAIT, exe.c_str(), argv.data());
	signal(SIGINT, oldInt);
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
	home = Play::Detail::slashed(home);
	const std::string pack = Play::Detail::slashed(guard.cwd());

	bool install = false;
	bool sound = false;
	bool ask = false;
	Play::SoundChoice choice = Play::kSoundAdlib;
	int i = 1;
	if (!strcmp(argv[i], "--install")) {
		install = true;
		++i;
	} else if (!strcmp(argv[i], "--sound")) {
		sound = true;
		ask = true;
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

	if ((install || sound) && !isFile(p.ini) && !isFile(sample)) {
		fprintf(stderr, "PLAY: run it from the game's folder: %s not found\n", Play::dosPath(sample).c_str());
		return 1;
	}
	if (!ensureProfile(p, sample, install || sound))
		return 1;
	if (install)
		return 0;
	if (sound) {
		if (ask && !askSound(id, choice))
			return 1;
		return setSound(p, choice, id);
	}

	std::vector<const char *> words;
	words.push_back("");
	for (int k = i; k < argc; ++k)
		words.push_back(argv[k]);
	std::vector<std::string> extra = Play::absolutizeArgs((int)words.size(), words.data(), pack, driveCwd);
	extra.erase(extra.begin());
	return play(guard, home, pack, sample, p, id, extra);
}
