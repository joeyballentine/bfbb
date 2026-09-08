#ifndef ICONFIGEDIT_H
#define ICONFIGEDIT_H

#include <types.h>

#include <stddef.h>

// PC-only: changing a value in an existing config.ini without disturbing
// anything else in it.
//
// iConfig.cpp reads the file and never writes to one that exists -- the only
// writes it has are an exclusive create of the defaults and an append of
// settings a newer build added. That is right for the game, which has no
// business rewriting a file someone is editing. The configurator does have
// that business, and this is what it uses.
//
// The file is held as its own LINES, not as a set of key/value pairs that get
// written back out. Setting a value rewrites the text between the '=' and any
// trailing comment on the one line that holds it, and touches nothing else --
// so comments, blank lines, ordering, hand-written sections, the commented-out
// [pad] block and even a line this build does not recognise all come back
// exactly as they went in. A round trip that changes nothing changes no bytes.
//
// A key the file does not have is inserted at the end of its section, or under
// a new section at the end of the file. A key the file has twice is answered
// and rewritten at the FIRST occurrence, which is not what the game's parser
// does -- it keeps both entries and answers with the first, so the two agree
// on which one matters.

struct iConfigEditFile;

// Read `path`. NULL when it cannot be read, or when it is larger than this is
// willing to hold -- a config.ini is a few hundred lines.
iConfigEditFile* iConfigEditOpen(const char* path);

// Read nothing: an empty file, for writing one from scratch.
iConfigEditFile* iConfigEditNew();

void iConfigEditClose(iConfigEditFile* file);

// The value of "section.name" as the file has it, or NULL when the file does
// not mention it. Distinct from the game's accessors, which answer from the
// settings table when the file is silent; here the difference matters, because
// a key the file does not have is one the configurator must not write back
// unless it was changed.
const char* iConfigEditGet(const iConfigEditFile* file, const char* section, const char* name);

// Set it, inserting the line if the file does not have it. Returns false only
// when the file has no room left for a new line.
bool iConfigEditSet(iConfigEditFile* file, const char* section, const char* name,
                    const char* value);

// Remove the line, if there is one. The setting then falls back to the table's
// default, which is what the game does for a key the file does not mention --
// so this is how a front end says "leave this one alone" as opposed to
// "pin it to the value that happens to be the default today".
void iConfigEditUnset(iConfigEditFile* file, const char* section, const char* name);

// Whether anything has been set or unset since the file was opened.
bool iConfigEditChanged(const iConfigEditFile* file);

// Write it to `path`. Goes to a temporary file beside the target and renames
// over it, so an interrupted write cannot leave a half-written config.ini --
// the file this replaces is the one holding the settings the game is about to
// read.
bool iConfigEditSave(const iConfigEditFile* file, const char* path);

#endif
