#ifndef IHOSTWORDS_H
#define IHOSTWORDS_H

// PC-only: what the game's text calls the machine it runs on, as string
// literals so they concatenate into the text around them. iTextPatch.cpp
// rewrites the console's names into these; the menus written as code
// (src/SB/Core/pc/menu) use them directly.
//
// A phone or a tablet on Android is a "device". iTextPatch rewrites in place,
// so each word there has to fit inside the console name it replaces, which is
// why Android's are short.
#ifdef __ANDROID__
#define HOST_NAME "device"
#define HOST_MACHINE "device"
#define HOST_DISK "storage"
#define HOST_HOME "home screen"
#define HOST_QUIT "Quit Game"
#else
#define HOST_NAME "PC"
#define HOST_MACHINE "computer"
#define HOST_DISK "hard drive"
#define HOST_HOME "Desktop"
#define HOST_QUIT "Quit to Desktop"
#endif

#endif
