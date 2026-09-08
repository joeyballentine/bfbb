#ifndef CONFIG_MODEL_H
#define CONFIG_MODEL_H

#include "iConfigTable.h"

#include <stddef.h>

// The configurator without a window on it: the settings as editable values,
// what the file said about each, and the reading and writing of that file.
//
// It is here so that the UI is the only thing a port has to rewrite. Nothing
// below draws, measures or asks the user anything -- a call that can fail
// fills in a sentence saying why and the caller decides how to show it.

const S32 kConfigModelMaxValue = 512;
const S32 kConfigModelMaxPath = 512;

// Open the file the game would read and load every setting from it.
//
// `fromCommandLine` overrides the search and may be NULL. A file that is not
// there is written at the defaults first, which is what the game does. False
// means there is nothing to edit and `why` says what happened.
bool ConfigModelOpen(const char* fromCommandLine, char* why, size_t whySize);
void ConfigModelClose();

// The file being edited, absolute. Absolute because ConfigModelStartGame hands
// it to the game as BFBB_CONFIG from a different working directory.
const char* ConfigModelPath();

// -----------------------------------------------------------------------
// The settings

S32 ConfigModelSettingCount();
const iConfigSetting* ConfigModelSetting(S32 i);

// The distinct sections of the table, in table order.
S32 ConfigModelSectionCount();
const char* ConfigModelSectionName(S32 section);
S32 ConfigModelSectionOf(S32 setting);

// What the control should show: the file's value, or the table's default for a
// setting the file does not mention -- which is what the game would run with.
const char* ConfigModelText(S32 setting);

// Record what the user typed. A value that differs from what was there marks
// the setting present and the model dirty; one that does not changes nothing,
// so re-reading an untouched control cannot make the file grow.
void ConfigModelSetText(S32 setting, const char* text);

bool ConfigModelIsDefault(S32 setting);

// Everything a row shows below its control: what the setting is for, and --
// only when the value is not the default -- what the default was. That second
// line is the answer to "what did this used to say".
void ConfigModelDescribe(S32 setting, char* out, size_t outSize);

// -----------------------------------------------------------------------
// Reading the domain

// The n'th '|'-separated word of `choices`, or false past the end.
bool ConfigModelChoiceAt(const char* choices, S32 index, char* out, size_t outSize);

// Whether the setting's control is a list of words, and whether it is worth a
// Browse button beside it.
bool ConfigModelWantsCombo(const iConfigSetting* s);
bool ConfigModelWantsBrowse(const iConfigSetting* s);

// -----------------------------------------------------------------------
// Saving

// Whether anything has been changed since the last write.
bool ConfigModelDirty();

enum ConfigModelResult
{
    CONFIG_MODEL_OK,

    // A value the table will not accept. `badSetting` and `badSection` say
    // which, so the UI can show that setting before it shows the message.
    CONFIG_MODEL_BAD_VALUE,

    // The file could not take another line, or could not be written.
    CONFIG_MODEL_NO_ROOM,
    CONFIG_MODEL_WRITE_FAILED
};

// Write every setting the file has to carry, then the file. Anything but
// CONFIG_MODEL_OK fills in `why`; `badSetting` and `badSection` are set only
// for CONFIG_MODEL_BAD_VALUE and may be NULL.
ConfigModelResult ConfigModelSave(char* why, size_t whySize, S32* badSetting, S32* badSection);

// Put one section back to the table's defaults. False if it was already there.
bool ConfigModelResetSection(S32 section);

// -----------------------------------------------------------------------
// Starting the game

// The game beside this program, if it is there.
bool ConfigModelGamePath(char* out, size_t outSize);

// Run it and return without waiting. False fills in `why`.
//
// The game is pointed at the file this program edited, whichever of the
// candidate paths it was. Without that the game runs its own search and can
// answer differently -- this program may have been started from somewhere
// else, or handed a path -- and "I changed a setting and it did nothing" is
// the result.
bool ConfigModelStartGame(char* why, size_t whySize);

#endif
