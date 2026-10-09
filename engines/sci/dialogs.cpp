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

#include "engines/advancedDetector.h"
#include "base/plugins.h"
#include "common/file.h"
#include "common/hashmap.h"
#include "common/ptr.h"
#include "common/translation.h"

#include "sci/detection.h"
#include "sci/dialogs.h"
#include "sci/sci.h"

#include "sci/detection_options.h"

namespace Sci {

enum {
	kHangulInputCmd = 'SHIN',
	kHangulBuiltinCmd = 'SHBI'
};

// The engine reads these once when a game starts, so a change made here
// (including from the in-game Options dialog) applies on the next start.
const HangulOptionsMap hangulOptionsList[] = {
	{
		_s("Enable Korean text entry"),
		_s("Type Korean words at the parser prompt. Takes effect the next time the game starts"),
		"sci_hangul_input",
		false,
		kHangulInputCmd
	},
	{
		_s("Use built-in Hangul composer"),
		_s("Compose Hangul from a 2-set keyboard inside ScummVM and show the Han/Yeong indicator. "
		   "Turn this off to type with the operating system's input method. Takes effect the next time the game starts"),
		"sci_hangul_builtin",
#ifdef ANDROID_BACKEND
		false,
#else
		true,
#endif
		kHangulBuiltinCmd
	},
	{
		_s("Start in Korean input mode"),
		_s("Begin with Han/Yeong switched to Korean. Takes effect the next time the game starts"),
		"sci_hangul_start",
		false,
		0
	},
	HANGUL_OPTIONS_TERMINATOR
};

static bool wantsHangulOptions(const Common::String &domain) {
	if (ConfMan.hasKey("sci_hangul_input", domain))
		return true;
	return ConfMan.hasKey("language", domain) &&
		Common::parseLanguage(ConfMan.get("language", domain)) == Common::KO_KOR;
}

OptionsWidget::OptionsWidget(GuiObject *boss, const Common::String &name, const Common::String &domain) :
		OptionsContainerWidget(boss, name, "SciGameOptionsDialog", domain) {
	_guiOptions = ConfMan.get("guioptions", domain);
	_hangulShown = wantsHangulOptions(domain);

	for (const ADExtraGuiOptionsMap *entry = optionsList; entry->guioFlag; ++entry)
		if (checkGameGUIOption(entry->guioFlag, _guiOptions))
			_checkboxes[entry->option.configOption] = new GUI::CheckboxWidget(widgetsBoss(), _dialogLayout + "." + entry->option.configOption, _(entry->option.label), _(entry->option.tooltip));

	if (_hangulShown)
		for (const HangulOptionsMap *entry = hangulOptionsList; entry->configOption; ++entry)
			_checkboxes[entry->configOption] = new GUI::CheckboxWidget(widgetsBoss(), _dialogLayout + "." + entry->configOption, _(entry->label), _(entry->tooltip), entry->cmd);

	for (const PopUpOptionsMap *entry = popUpOptionsList; entry->guioFlag; ++entry)
		if (checkGameGUIOption(entry->guioFlag, _guiOptions)) {
			GUI::StaticTextWidget *textWidget = new GUI::StaticTextWidget(widgetsBoss(), _dialogLayout + "." + entry->configOption + "_desc", _(entry->label), _(entry->tooltip));
			textWidget->setAlign(Graphics::kTextAlignRight);

			_popUps[entry->configOption] = new GUI::PopUpWidget(widgetsBoss(), _dialogLayout + "." + entry->configOption);

			for (uint i = 0; entry->items[i].label; ++i)
				_popUps[entry->configOption]->appendEntry(_(entry->items[i].label), entry->items[i].configValue);
		}
}

void OptionsWidget::defineLayout(GUI::ThemeEval &layouts, const Common::String &layoutName, const Common::String &overlayedLayout) const {
	layouts.addDialog(layoutName, overlayedLayout);
	layouts.addLayout(GUI::ThemeLayout::kLayoutVertical).addPadding(0, 0, 0, 0);

	for (const ADExtraGuiOptionsMap *entry = optionsList; entry->guioFlag; ++entry)
		layouts.addWidget(entry->option.configOption, "Checkbox");

	for (const HangulOptionsMap *entry = hangulOptionsList; entry->configOption; ++entry)
		layouts.addWidget(entry->configOption, "Checkbox");

	for (const PopUpOptionsMap *entry = popUpOptionsList; entry->guioFlag; ++entry) {
		layouts.addLayout(GUI::ThemeLayout::kLayoutHorizontal).addPadding(0, 0, 0, 0);
		layouts.addWidget(Common::String(entry->configOption) + "_desc", "OptionsLabel");
		layouts.addWidget(entry->configOption, "PopUp").closeLayout();
	}

	layouts.closeLayout().closeDialog();
}

void OptionsWidget::load() {
	for (const ADExtraGuiOptionsMap *entry = optionsList; entry->guioFlag; ++entry)
		if (checkGameGUIOption(entry->guioFlag, _guiOptions))
			_checkboxes[entry->option.configOption]->setState(ConfMan.getBool(entry->option.configOption, _domain));

	if (_hangulShown) {
		for (const HangulOptionsMap *entry = hangulOptionsList; entry->configOption; ++entry)
			_checkboxes[entry->configOption]->setState(ConfMan.getBool(entry->configOption, _domain));
		updateHangulDependents();
	}

	for (const PopUpOptionsMap *entry = popUpOptionsList; entry->guioFlag; ++entry)
		if (checkGameGUIOption(entry->guioFlag, _guiOptions))
			_popUps[entry->configOption]->setSelectedTag(ConfMan.getInt(entry->configOption, _domain));

	// If the deprecated native_fb01 option is set, use it to set midi_mode
	if (ConfMan.hasKey("native_fb01", _domain) && ConfMan.getBool("native_fb01", _domain))
		_popUps["midi_mode"]->setSelectedTag(kMidiModeFB01);
}

bool OptionsWidget::save() {
	for (const ADExtraGuiOptionsMap *entry = optionsList; entry->guioFlag; ++entry)
		if (checkGameGUIOption(entry->guioFlag, _guiOptions))
			ConfMan.setBool(entry->option.configOption, _checkboxes[entry->option.configOption]->getState(), _domain);

	if (_hangulShown)
		for (const HangulOptionsMap *entry = hangulOptionsList; entry->configOption; ++entry)
			ConfMan.setBool(entry->configOption, _checkboxes[entry->configOption]->getState(), _domain);

	for (const PopUpOptionsMap *entry = popUpOptionsList; entry->guioFlag; ++entry)
		if (checkGameGUIOption(entry->guioFlag, _guiOptions))
			ConfMan.setInt(entry->configOption, _popUps[entry->configOption]->getSelectedTag(), _domain);

	// Remove deprecated option
	ConfMan.removeKey("native_fb01", _domain);

	return true;
}

// The composer needs Korean entry on, and the start mode needs the composer.
// A disabled box keeps its stored value, so switching the parent back on
// restores the earlier choice.
void OptionsWidget::updateHangulDependents() {
	const bool input = _checkboxes["sci_hangul_input"]->getState();
	const bool builtin = _checkboxes["sci_hangul_builtin"]->getState();
	_checkboxes["sci_hangul_builtin"]->setEnabled(input);
	_checkboxes["sci_hangul_start"]->setEnabled(input && builtin);
}

void OptionsWidget::handleCommand(GUI::CommandSender *sender, uint32 cmd, uint32 data) {
	switch (cmd) {
	case kHangulInputCmd:
	case kHangulBuiltinCmd:
		if (_hangulShown)
			updateHangulDependents();
		break;
	default:
		OptionsContainerWidget::handleCommand(sender, cmd, data);
	}
}

} // End of namespace Sci
