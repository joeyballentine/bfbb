# bfbb_config

`config.ini` with controls on it.

Every setting the port has, grouped by section, each with the description the
generated file carries and the values it accepts. A value that is not the
default says what the default was. Save writes the file; Cancel writes nothing.

## A model and a window

`config_model.cpp` is the program without a window on it: the settings as
editable values, what the file said about each, and the reading and writing of
that file. Nothing in it draws, measures or asks the user anything -- a call
that can fail fills in a sentence saying why and the caller decides how to show
it. `pc_selftest` links it and checks it, which nothing could do while it lived
inside a window ctest cannot run.

`main_wx.cpp` draws it in wxWidgets, on every host. wx wraps the platform's own
controls rather than imitating them: Win32 common controls on Windows, GTK on
Linux, Cocoa on macOS.

There was a second front end written directly in Win32 controls, so a Windows
machine could build a configurator with no dependency at all. It is gone.
Keeping two windows in step to save one host a dependency meant reading both
every time a setting changed shape, and one of them was always the one nobody
had opened.

## Why a separate program

`config.ini` is read before the window opens. `assets.path` says where the
game's data is and `video.mode` says how to open the window at all, and neither
can be answered from inside a game that has already started on the old values.
So it runs before the game does, not inside it.

## Where the settings come from

`kConfigSettings` in `../iConfigTable.cpp`, and nowhere else. That table already
held each setting's default and its comment; it now also holds its kind, the
words it accepts, its numeric range, and the setting it is a detail of. Adding a
row there gives it a control here, with its description and validation, and
nothing in the front end has to be told about it.

## Groups

A row whose `group` names another setting in the same section is drawn inside a
collapsible pane under it rather than beside it, closed. `[experimental]` is
what this is for: `hipoly_assets` is a question someone answers, and the eleven
numbers that shape the smoothing are worth having without being worth reading
past every time.

It groups and it does not gate. The details stay editable with the master
turned off, because setting them up before switching it on is a reasonable
thing to do.

A group is one level deep. Nothing draws a group inside a group, and
`pc_selftest` fails if the table ever asks for one.

The table is split out of `iConfig.cpp` so this program does not link it.
`iConfig.cpp` reaches `iPadBind.cpp` for the two binding sections, and that
reaches `xPad.h` and the pad backend — most of the game, to read a file this
program reads itself.

## How it writes

`../iConfigEdit.cpp`, which holds the file as its own lines and rewrites only
the text between the `=` and any trailing comment. Comments, blank lines,
ordering, spacing around the `=`, hand-written sections, the commented-out
`[pad]` block and lines this build does not recognise all survive a save
exactly. A save that changes nothing changes no bytes. The write goes to a
temporary file and is renamed over the target, so an interrupted save cannot
leave half a `config.ini`.

A setting the file never mentioned and that nobody touched is not written out.
The game answers those from the table, and writing one would pin it to whatever
today's default happens to be.

Which file it edits is the order the game searches in: `BFBB_CONFIG`, then
`config.ini` in the working directory, then beside the executable. An argument
overrides all three. A missing file is written at the defaults, as the game
does it.

## Not yet

- `[pad]` and `[keyboard]`. Editing a binding wants a control that captures a
  button press, and the grammar takes `,` `+` and `!`, which is a second screen
  rather than a row.
- Live validation. A value is checked on Save, which reports the first one that
  will not do and shows it.

## Building

Part of the normal PC build, with nothing to pass: `build-release.bat` and
`build-debug.bat` put it in `bin/` beside the game, and the CI package carries
it on every host.

`BFBB_WX` says where wxWidgets comes from, on the same three words SDL uses:
`auto`, `system`, `vendored`. Off Windows a distribution's `libwxgtk` or a
`brew install wxwidgets` answers `auto`; a Windows machine usually has none, so
`auto` falls back to `third_party/wxWidgets` and compiles it. That is a long
first build and it happens once per build directory, which is what
`-DBFBB_BUILD_CONFIGURATOR=OFF` is for: a build that only wants the game skips
it entirely.

3.2 is a supported version, and it is what a distribution packages -- Ubuntu's
is literally called `libwxgtk3.2-dev`. The submodule is 3.3. Anything written
here that 3.2 does not have goes behind `wxCHECK_VERSION`, and there is one
such thing today: `SetAppearance`, which is what asks Windows for dark mode.
GTK reads the desktop's theme without being asked, so 3.2 loses nothing that
shows.

Building this needs a toolchain that can include the C++ standard library,
which the rest of the tree never does. On Windows that means Microsoft's STL,
and it refuses any clang older than the version it shipped against -- a clang
that compiles the whole game will not necessarily compile `main_wx.cpp`, and
says so in `yvals_core.h` rather than anywhere useful.

Two Windows gotchas, both pre-dating the wx front end. `bfbb_config.rc` lives
in `../res/` next to the icon it names: `rc.exe` resolves a resource's file
against the script's own directory and the include path, and a script elsewhere
naming an icon in `res/` compiles to nothing and reports nothing. The manifest
is a CMake source rather than a line in the `.rc`, because CMake embeds a
manifest of its own into every executable and a second `RT_MANIFEST` in the
resources is a duplicate-resource link error.
