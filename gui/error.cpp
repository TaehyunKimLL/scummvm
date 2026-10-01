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

#include "common/error.h"
#include "gui/message.h"
#include "gui/error.h"

#ifdef DISABLE_GUI
#include "common/system.h"
#endif

namespace GUI {

void displayErrorDialog(const Common::U32String &text) {
#ifdef DISABLE_GUI
	// No dialog: an error goes to the log as one (the backend shows the
	// last one when it exits).
	g_system->logMessage(LogMessageType::kError, (text.encode() + "\n").c_str());
#else
	GUI::MessageDialog alert(text);
	alert.runModal();
#endif
}

void displayErrorDialog(const Common::Error &error, const Common::U32String &extraText) {
	Common::U32String errorText(extraText);
	errorText += Common::U32String(" ");
	errorText += error.getTranslatedDesc();
#ifdef DISABLE_GUI
	displayErrorDialog(errorText);
#else
	GUI::MessageDialog alert(errorText);
	alert.runModal();
#endif
}

} // End of namespace GUI
