#ifndef CONFIG_MODEL_H
#define CONFIG_MODEL_H

#include "iConfigTable.h"
#include "iPadBind.h"

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

// The setting this one is a detail of, or -1 for one that stands on its own.
// A front end folds a master's details away under it rather than listing them
// beside it.
//
// Resolved once when the file is opened, so a front end laying out a section
// does not search the table per row. A `group` naming a setting that is not in
// the same section is treated as no group at all: the table is wrong, and the
// setting is better shown in the wrong place than not shown.
S32 ConfigModelGroupOf(S32 setting);

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
// Controls
//
// The [keyboard] and [pad] sections: one binding per button the game reads
// (kPadBindButtons), in the grammar iPadBind.h describes. A front end shows
// them as two more sections after the settings', numbered
// ConfigModelSectionCount() + device.

enum ConfigModelDevice
{
    CONFIG_MODEL_KEYBOARD,
    CONFIG_MODEL_PAD,
    CONFIG_MODEL_DEVICE_COUNT
};

// "keyboard" or "pad", as the file names the section.
const char* ConfigModelDeviceSection(ConfigModelDevice device);

S32 ConfigModelBindCount();

// The button's name, left of the '=', and what it does, or NULL.
const char* ConfigModelBindName(S32 row);
const char* ConfigModelBindDoes(S32 row);

// The binding the file gives the button, or "" for none. None means the
// default: the game answers a missing line from its own table.
const char* ConfigModelBindText(ConfigModelDevice device, S32 row);

// Record a binding. "" puts the button back on its default and takes the line
// out of the file on the next save. Checked on save, like a setting.
void ConfigModelBindSetText(ConfigModelDevice device, S32 row, const char* text);

// What the default is, in words: the key table's binding for the keyboard,
// and for the pad what input.preset (as this model now holds it) binds the
// row to. A preset that names a printed letter rather than a position says so.
void ConfigModelBindDescribeDefault(ConfigModelDevice device, S32 row, char* out, size_t outSize);

// The input names a binding for this device may use.
const iPadBindToken* ConfigModelBindTokens(ConfigModelDevice device, S32* count);

// Put every binding for one device back on its default. False if they all
// were already.
bool ConfigModelBindResetAll(ConfigModelDevice device);

// -----------------------------------------------------------------------
// Saving

// Whether anything has been changed since the last write.
bool ConfigModelDirty();

enum ConfigModelResult
{
    CONFIG_MODEL_OK,

    // A value the table will not accept, or a binding that does not parse.
    // `badSetting` and `badSection` say which, so the UI can show that setting
    // before it shows the message. A binding's section is
    // ConfigModelSectionCount() + device, and `badSetting` is then its row.
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
