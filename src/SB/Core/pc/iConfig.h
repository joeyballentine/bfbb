#ifndef ICONFIG_H
#define ICONFIG_H

#include <types.h>

// PC-only: `config.ini`, the port's settings file. There is no GameCube
// counterpart -- a console's settings are the ones the game itself offers, and
// everything in here is a question the console never had to answer.
//
// **Why not SB.INI.** The game already ships an INI parser (`xIni.h`) and
// already reads a file with it: `zMainReadINI` loads `SB.INI` for PATH, BOOT
// and the debug keys. Reusing that would be the obvious move and it does not
// work, for two independent reasons.
//
//   1. `xIniParse` allocates with `RwMalloc`, which reaches through
//      `RWSRCGLOBAL(memoryFuncs)` and so needs `RwEngineInstance`. That is set
//      in `RwEngineInit`, inside `RenderWareInit`, inside `iSystemInit`. A
//      setting that decides how the window is opened has to be readable
//      BEFORE that, so it cannot come through xIni.
//   2. `zMainReadINI` runs at `zMain.cpp:138`, twelve lines after
//      `iSystemInit(FALSE)`. Even if the parser worked early, the call site is
//      in the wrong place, and it is retail's call site.
//
// So this is a second, smaller parser that answers before the engine exists.
// It allocates nothing and reads the file once. `SB.INI` keeps meaning exactly
// what it means on the GameCube.
//
// **Where the file is looked for**, in order:
//
//   1. `$BFBB_CONFIG`, if set -- the full path to a file, not a directory.
//   2. `config.ini` in the working directory.
//   3. `config.ini` beside the executable.
//
// The working directory comes first because that is where someone editing a
// config while testing expects it, and beside the executable last because that
// is where it ends up for someone who just wants to play.
//
// **A file that is not found is written**, with every setting at its default,
// beside the executable -- or at the path BFBB_CONFIG named, if it named one.
// Someone who wants to turn a feature off should not have to learn from
// documentation that a file exists and what may go in it. Since every value in
// a generated file is the default, writing one changes nothing about how the
// port behaves, and deleting it is always safe. A directory that cannot be
// written to is reported and not retried.
//
// **Reading it is diagnostics-free by design.** Unknown keys are reported at
// load, once, by name. A settings file where a typo silently does nothing is
// the single most common way one of these wastes an afternoon.

// Reads the file. Idempotent -- the second call does nothing -- and called by
// `iSystemInit` before anything reads a setting. Every accessor below also
// calls it, so a query that somehow runs first still gets an answer rather
// than a default; static initialisation order cannot break this.
void iConfigLoad();

// The file the settings came from -- read, or written by the load when there
// was none -- and NULL when neither could happen. For the startup log: "which
// config.ini did it get" is otherwise unanswerable, and with three candidate
// paths it comes up.
const char* iConfigPath();

// `key` is "section.name", lower case, e.g. "xbox.glow". Case-insensitive.
//
// `def` is the LAST resort, not the default. A key the file does not mention
// is answered from the settings table in iConfig.cpp, which is the same table
// the generated file is written from -- so the value a fresh config.ini shows
// and the value a missing one produces cannot drift apart. `def` is reached
// only for a key that table does not have.
//
// Booleans accept 1/0, true/false, yes/no and on/off. A value that is none of
// those is reported and the default is used -- silently treating "of" as false
// because it is not "on" would be the wrong way to be lenient.
S32 iConfigGetBool(const char* key, S32 def);
S32 iConfigGetInt(const char* key, S32 def);
F32 iConfigGetFloat(const char* key, F32 def);
const char* iConfigGetString(const char* key, const char* def);

// Write a config.ini holding every setting at its default, with the comments
// that document each one. This is what the load calls when it finds no file;
// it is public because a settings front end -- a launcher, whenever there is
// one -- wants exactly this to lay a fresh file down.
//
// EXCLUSIVE: false if the path already exists, so it can never overwrite
// someone's edits, and false if the directory cannot be written to. Neither is
// an error worth stopping for -- a read-only install directory is ordinary --
// and the caller carries on with the defaults either way.
bool iConfigWriteDefaults(const char* path);

#endif
