# Menus as code

The menu packages' logic assets are built here instead of read from their HIPs.
`iAssetOverride.cpp` serves them to the game in place of the HIP's own.

| package | what it is | loaded |
| --- | --- | --- |
| `mnu3` | the main menu: title, new/load game, options, extras | in the menu scene |
| `mnu4` | the pause menu, save/load dialogs, autosave text, the HUD, the NPC talk text boxes | at boot, never unloaded |
| `mnu5` | the talk box, the taxi warning, the task counters | at boot, never unloaded |
| `font` | the game-wide text; only the 7 strings that name a console are here (`font_text.cpp`), replacing the HIP's by ID | at boot, never unloaded |

Each package has these files, where it has assets of that kind:

| file | what it builds |
| --- | --- |
| `mnuN.cpp` | the owned types and the build order |
| `mnuN_text.cpp` | `TEXT`: every string the package shows |
| `mnuN_ui.cpp` | `UI  `: image widgets |
| `mnuN_uifont.cpp` | `UIFT`: text widgets |
| `mnuN_dyna.cpp` | `DYNA`: HUD models, text and meters; text boxes; talk boxes |
| `mnuN_logic.cpp` | `GRUP`, `COND`, `TIMR`, `DPAT`, `SFX `, `CNTR`, `PORT` |

Everything else in the HIPs is used as shipped: the 3D scene behind the main
menu, the credits, animation lists, model info, and every texture, model and
sound.

## Editing

An asset is its struct, set from the type's defaults, then its links:

```cpp
{
    zUIFontAsset a = kUIFontDefaults;
    a.pos = xVec3{ 180.0f, 150.0f, 0.0f };
    a.textAssetID = H("MNU3  NEW GAME TXT");
    p.Begin('UIFT', H("MNU3 START NEWGAME UIF"), a);
    p.Link(eEventUISelect, eEventPlay, H("MNU3 MOVE A SFX"));
    p.Link(eEventPadPressDown, eEventUISelect, H("MNU3 START LOAD UIF"));
    p.End();
}
```

- An ID is `H("name")`, xStrHash of the asset's name. A new asset needs only a
  new name. A hex ID is an Xbox-only asset whose name is not known.
- `p.Link(srcEvent, dstEvent, target, param0..3, paramWidget, check)`: when
  this asset receives `srcEvent`, send `dstEvent` to `target`. Trailing
  arguments default to zero. `iAssetParamID` puts an asset ID in a parameter.
- A group lists its members with `p.Item(id)` between `Begin` and `End`.
- Within a type, objects are created in the order they are added. For DYNA
  that order runs across all its kinds, which is why they share one function.
- `p.Text(id, "...")` strings are xtextbox markup (`{i:...}`, `{var:...}`,
  `{n}`). They are written in PC wording and do not go through iTextPatch;
  what the host is called comes from `iHostWords.h` (`"your " HOST_MACHINE`),
  which is what makes Android say "device".
- A portal's `sceneID` is `iAssetTag("SB10")`.
- Code that looks widgets up by name (`zMenu.cpp`, `zUI.cpp`,
  `zSaveLoad.cpp`, `zGame.cpp`, `zTaxi.cpp`) keeps working as long as the
  name stays.
- `mnu5`'s counters are saved with the game. Renaming or removing one breaks
  old saves.

An asset in more than one loaded package exists once: the packer keeps the
first package's copy. `mnu3.HIP` repeats 34 of `mnu4`'s assets, so those are
only in the `mnu4` files. A code asset whose ID an earlier package already has
is ignored, and the game prints its ID.

## The save and load screens

`iSaveScreen.cpp` replaces retail's folder-then-slot screens with one list of
every save, newest first, five rows at a time. The rows are `PC LD ROW n`
(mnu3) and `PC SV ROW n` (mnu4), inside the groups the retail screens focus
(`LD GAMESLOT GROUP`, `SV GAMESLOT GROUP`); their text is rewritten as the list
scrolls (`iAssetPkg::TextSpace`, `iAssetTextSet`). Retail's `LD/SV GAMESLOT n`
and folder buttons are still built but nothing shows them.

Each save gets a still of the game beside it (`SpongeBob00.tga`, see
`iSaveThumb.h`), shown in the list where retail showed a stock picture of the
level: the frame on screen when the game was paused, for a save from the pause
menu, or when an autosave ran. A save from the title has none and shows the
stock picture. The stills come from the loading-screen snapshot, so with
`xbox.snapshot` off there are none.

A save is a folder and a slot, as retail's are. The backend reaches ten
folders with the PC menus on: retail's two, where saves made before this still
are, and eight more under `saves/more/` for the PC screen. A new save takes the
first empty slot, retail's folders first. With the PC menus off only retail's
two exist and retail's screens run.

## The settings screen

`iSettingsScreen.cpp`: config.ini's settings a player would change, from a
Settings entry on the title (after Options) and in the pause menu (after
Options). Up and down pick one, left and right change it, X steps round. A
change applies at once through the same setters `ApplyConfig` uses, and the
screen writes what changed back into config.ini when it closes
(`iConfigSet` / `iConfigSave`). A setting that only takes effect on a restart
or in the next area says so. Switching the window asks to be kept and puts
itself back after 15 seconds unanswered.

No game code knows it is there. The title's entry switches to Load mode and
the pause menu's to Save mode, as their Load and Save entries do, and stays
selected; `iSGLoadLoop` / `iSGSaveLoop` see that and run the settings screen
instead. Its widgets are in mnu4, which is loaded everywhere.

To add a setting, add a row to `kSettings`: the label, the config key, its
words and what to call them, when it takes effect, one line of help, and the
function that applies it.

## Checking

`BFBB_ASSET_VERIFY=1` compares every built asset with the HIP's after each
package loads, and prints what differs, what was added or removed, and
anything out of order. As generated, all three were identical: 409, 835 and
180 assets. Every edit since shows up as a difference; the PC wording is the
first 34 of them.

`[assets] code_menu = off` in config.ini goes back to the HIPs, with
iTextPatch rewriting their text as it loads, which is exactly retail plus the
word swaps.

A mod folder with its own `mn/mnu3.HIP`, `mnu4.HIP` or `mnu5.HIP` turns
code_menu off for the run. The built assets name the retail menus' textures
and models by ID, and a mod's menus have other ones.

## Regenerating

`tools/menugen/menugen.py` wrote these files from the Xbox HIPs, naming IDs
from the GameCube disc's packer listings (`*.LIP`). It overwrites; the files
here are the source now.

    python tools/menugen/menugen.py --hip <xbox>/mn/mnu4.HIP \
        --beside <xbox>/boot.HIP --beside <xbox>/font.HIP --beside <xbox>/plat.HIP \
        --names <gamecube>/files --out src/SB/Core/pc/menu --prefix mnu4

`--beside` names a package loaded before this one; its assets are left out.
`mnu3` is beside `mnu4`; `mnu5` is beside boot, font, plat and `mnu4`. A new
package also goes in `kOverrides` in `iAssetOverride.cpp`. A new DYNA kind
needs a spec in `DYNA_TYPES`.

## Looking at a change

`BFBB_TOUR=script.txt` runs the game in a hidden window, drives port 0 from
the script, and writes PNGs of its frames. The commands are in `iTour.h`.
Point `BFBB_CONFIG` at a scratch config.ini: the settings screen writes to
the one it was started with.

    wait 300          # past the movies and the title
    press start
    wait 60
    press start
    wait 60
    press start
    wait 420
    press start
    wait 420          # main menu
    press down
    press down
    press down
    press a           # Settings
    wait 90
    shot settings

At the main menu, New Game is selected and Settings is three down.
