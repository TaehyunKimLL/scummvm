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


// The engine symbols engines/agi/words.o refers to, for the test runner,
// which links the Korean units of the AGI engine one by one. The tests use
// Words with no engine (Words(nullptr)): analyze() and the table loaders
// never reach these; each one fails the test if it is called.

#include "agi/agi.h"

#include "common/textconsole.h"

namespace Agi {

void AgiBase::setFlag(int16, bool) { error("AGI test stub: setFlag"); }
void AgiEngine::setVar(int16, byte) { error("AGI test stub: setVar"); }
void AgiGame::setSpeedLevel(byte) { error("AGI test stub: setSpeedLevel"); }
Common::Language AgiBase::getLanguage() const { error("AGI test stub: getLanguage"); }
Common::Platform AgiBase::getPlatform() const { error("AGI test stub: getPlatform"); }
Common::TranslationFileHeader AgiEngine::translationHeader(bool) const { error("AGI test stub: translationHeader"); }

} // End of namespace Agi
