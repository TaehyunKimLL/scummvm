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


#ifndef BACKENDS_PLATFORM_DOS_DOS_EXIT_H
#define BACKENDS_PLATFORM_DOS_DOS_EXIT_H

namespace DOS {

/**
 * The steps of the way out, as dos_exit_trace=true shows them on the
 * text screen and writes them to EXITLOG.TXT. The numbers are what a user
 * reports: keep them stable, and README.TXT of the trace build in step.
 */
enum ExitStep {
	// OSystem_DOS::quit()
	kExitQuit = 10,
	kExitQuitMemInfo = 11,
	kExitQuitTimer = 12,
	kExitQuitSilenced = 13,
	kExitQuitAudio = 14,
	kExitQuitBlaster = 15,
	kExitQuitVideo = 16,
	kExitQuitSdl = 17,
	kExitQuitLoading = 18,
	kExitQuitExit = 19,
	// OSystem_DOS::fatalError()
	kExitFatal = 20,
	kExitFatalTimer = 22,
	kExitFatalSilenced = 23,
	kExitFatalAudio = 24,
	kExitFatalBlaster = 25,
	kExitFatalVideo = 26,
	kExitFatalSdl = 27,
	kExitFatalLoading = 28,
	kExitFatalExit = 29,
	// main() after scummvm_main()
	kExitMainReturned = 30,
	kExitMainTimer = 31,		// ~OSystem_DOS: the timer is out
	kExitMainDestroyed = 32,	// the audio device closed with the mixer
	kExitMainAudio = 33,
	kExitMainBlaster = 34,
	kExitMainVideo = 35,
	kExitMainSdl = 36,
	kExitMainLoading = 37,
	kExitMainReturn = 38,
	// atexit() handlers, in the order they run
	kExitAtexitMidiLog = 40,
	kExitAtexitUart = 41,
	kExitAtexitTimer = 42,
	kExitAtexitLog = 43,
	kExitAtexitLogDone = 44,
	kExitAtexitLoading = 45,
	kExitAtexitDone = 46,
	// a fatal signal (installExitSignals()), before DJGPP's traceback
	kExitSignal = 90,
	// DJGPP's _exit(): its own handlers out, then CWSDPMI and DOS
	kExitDjgppExit = 99
};

/**
 * main(), before SDL_Init(): the PIC masks as DOS left them, so that an
 * IRQ we unmasked and SDL3 masked on its way out goes back as it was.
 */
void saveIrqMasks();

/**
 * Puts EXITLOG.TXT in @p dir instead of the current directory, which the
 * program leaves before it exits. A no-op for a name that does not fit.
 */
void setExitLogDir(const char *dir);

/**
 * From initBackend() (dos_exit_trace): turns the exit trace on and starts
 * EXITLOG.TXT afresh.
 */
void exitTraceStart(bool on);

/**
 * With the trace on: @p step on the bottom line of the text screen
 * (direct to B800h, so it shows once text mode is back and costs no BIOS
 * or DOS call), then, with interrupts on, "<step> <what>" to EXITLOG.TXT
 * (opened, written and closed each time: the last line reached survives a
 * hang); a '.' after the number on screen says the line got there. No
 * heap, no stdio.
 */
void exitMark(int step, const char *what);

/** The audio mixer opened SDL3's Sound Blaster device. */
void noteSoundBlasterOpen();

/**
 * The port and IRQ SDL3 says it uses (its "SB: port=" and "SB: irq="
 * log lines); -1 leaves one as it was. soundBlasterClosed() prefers them
 * to its own reading of BLASTER.
 */
void noteSoundBlasterConfig(int port, int irq);

/**
 * main(): SIGABRT, SIGSEGV, SIGFPE, SIGILL, SIGINT (Ctrl-C, Ctrl-Break)
 * and SIGQUIT take the timer out and put the PIT back before DJGPP's
 * default action (a traceback and exit) runs.
 */
void installExitSignals();

/**
 * Right after SDL3 closed the Sound Blaster device: resets the DSP (SDL3
 * leaves a 16-bit auto-init transfer running), acknowledges an interrupt
 * it may still hold, and puts the card's IRQ (and the cascade, for IRQ
 * 8-15) back in the PIC as saveIrqMasks() found it: SDL3 masks it
 * whatever it was. Nothing without noteSoundBlasterOpen(), nor while the
 * SDL audio subsystem is still up (a later call does it then).
 */
void soundBlasterClosed();

} // End of namespace DOS

#endif
