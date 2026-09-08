#ifndef ICONFIGTABLE_H
#define ICONFIGTABLE_H

#include <types.h>

#include <stddef.h>
#include <stdio.h>

// PC-only: every setting config.ini has, in one table. Split out of
// iConfig.cpp so that a program which only wants to KNOW the settings does not
// have to link the one that reads them -- iConfig.cpp reaches iPadBind.cpp for
// the two binding sections, and iPadBind.cpp reaches xPad.h and the pad
// backend, so including the table used to mean linking most of the game. The
// configurator wants the table and none of that.
//
// The table's three jobs are unchanged and are described where it is defined.
// This file adds a fourth: it says what each value MEANS, so a front end can
// draw the right control for it and reject a value the game would only report
// at load.

// What a setting's value is, when it is not one of the words in `choices`.
enum iConfigKind
{
    // on/off. The file also accepts true/false, yes/no and 1/0; a front end
    // that writes one of them writes "on" or "off", which is what a generated
    // file uses.
    ICONFIG_BOOL,

    // A whole number, inside [min, max].
    ICONFIG_INT,

    // A number, inside [min, max].
    ICONFIG_FLOAT,

    // One of `choices` and nothing else. The only kind for which `choices` is
    // the whole domain rather than words alongside a value.
    ICONFIG_ENUM,

    // Free text. Empty is allowed and means whatever the comment says it does.
    ICONFIG_STRING,

    // A folder. Empty is allowed.
    ICONFIG_FOLDER,

    // A TrueType file. Its own kind rather than a file kind carrying a filter,
    // because the two settings that take a file both take a .ttf, and a field
    // holding "*.ttf" twice and NULL thirty-six times is not worth having.
    ICONFIG_FONT
};

struct iConfigSetting
{
    const char* section;
    const char* name;

    // The default, and what a generated config.ini is written with.
    const char* value;

    // What it is for, as the comment above it in a generated file. Lines after
    // the first are already prefixed with "; ", because that is what the
    // writer needs; a front end showing this has to strip them.
    const char* comment;

    iConfigKind kind;

    // Words the value may be INSTEAD of a `kind` value, '|'-separated, or NULL
    // for none. For ICONFIG_ENUM this is the whole domain; for every other
    // kind these sit alongside it -- `framerate` is a number or the word
    // "display", and both are ordinary answers rather than one being an escape
    // from the other.
    const char* choices;

    // The range for ICONFIG_INT and ICONFIG_FLOAT. Equal bounds mean the
    // setting has no range worth enforcing, and are what every other kind
    // carries.
    //
    // What a front end OFFERS, not what the game enforces. The game has never
    // rejected a number and does not start now: a value outside these is a
    // wrong-looking picture rather than a crash, and someone who types 200
    // into `fov` on purpose is entitled to it.
    F32 min;
    F32 max;
};

extern const iConfigSetting kConfigSettings[];
extern const S32 kConfigSettingCount;

// The row for "section.name", or NULL. Case-insensitive.
const iConfigSetting* iConfigTableFind(const char* key);

// Whether `value` is one the setting's domain admits. `why` is filled with a
// sentence saying what is wrong when the answer is false, and left alone when
// it is true; pass NULL for no explanation.
bool iConfigTableValidate(const iConfigSetting* setting, const char* value, char* why,
                          size_t whySize);

// The first line of `comment`, unwrapped: the continuation lines joined onto
// it with their "; " removed, up to the first blank comment line. That is one
// sentence or two, which is what fits beside a control; the settings whose
// comment runs to paragraphs keep the rest for the file.
//
// Writes at most `outSize` bytes including the terminator.
void iConfigTableSummary(const iConfigSetting* setting, char* out, size_t outSize);

// Every setting at its default, with its comment above it and a [header] where
// the section changes -- the body of a generated config.ini.
//
// The caller supplies the file and whatever goes around it. iConfig.cpp puts
// the banner above and the two binding sections below; the configurator writes
// the banner and stops, because it does not link the binding table and a file
// missing [pad] is a file the game fills in from the preset.
void iConfigTableWriteDefaults(FILE* f);

#endif
