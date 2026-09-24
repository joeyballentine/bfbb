// The strings in font.HIP that name a console. font.HIP is the game-wide text
// every level shares; this replaces these by ID and leaves the rest of it as
// shipped, move prompts included, which iTextPatch still rewrites at load for
// the pad preset. See src/SB/Core/pc/menu/README.md.

#include "iAssetBuild.h"

// Replacements only: the rest of the package's TEXT stays the HIP's.
extern const U32 kFontOwnedTypes[] = { 'TEXT', 0 };

void FONT_Build(iAssetPkg& p)
{
    // The pause menu's last entry.
    p.Text(H("text_menu_reboot"), "{i:button_picture_03} " HOST_QUIT);

    p.Text(H("text_no_controller"),
           "No controller is detected.{n}Please connect a controller, and press the {i:ui_accept} "
           "button to continue");
    p.Text(H("text_no_controller_pal"),
           "No controller is detected.{n}Please connect a controller, and press the {i:ui_accept} "
           "button to continue");

    // The numbers these quote arrive with their own unit (iSGFormatSize).
    p.Text(H("text_mem_card_no_space"),
           "There is not enough free space to save games. You need to free more space to save a new "
           "{i:game_name} game.{n}{n}{n}Press {i:ui_accept} to continue or {i:ui_cancel} to free "
           "more space.");
    p.Text(H("text_mem_card_no_space_no_save"),
           "There is not enough free space to save games. You need "
           "{c=ff00d244}{var:BadCardNeeded}{~:c} more to save a new {i:game_name} "
           "game.{n}{n}{n}Press {i:ui_accept} to continue without saving or {i:ui_cancel} to free "
           "more space.");
    p.Text(H("text_mem_card_no_space_overwrite"),
           "There is not enough free space to save games. You need "
           "{c=ff00d244}{var:BadCardNeeded}{~:c} more to save a new {i:game_name} game.{n}Existing "
           "game saves may be loaded and overwritten.{n}{n}{n}Press {i:ui_accept} to continue or "
           "{i:ui_cancel} to free more space.");

    p.Text(H("text_mem_card_no_card"),
           "No save folder could be opened. Do you want to start the "
           "game?{n}{n}{n}{n}{font=0}{w*1.5}{h*1.5}{i:button_picture_03}No{n}{i:button_picture_01}"
           "Yes");
}
