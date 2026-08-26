#ifndef ZMAIN_H
#define ZMAIN_H

#include <types.h>
#include "xIni.h"
#include "xserializer.h"

enum eStartupErrors
{
    eNoError,
    eNoFormat,
    eDamagedCard,
    eWrongDevice,
    eNoCards,
    eCorruptFile,
    eNoController,
};

#ifdef GAMECUBE
void main(S32 argc, char** argv);
#else
int main(S32 argc, char** argv);
#endif
void iEnvStartup();
static void zMainOutputMgrSetup();
static void zMainInitGlobals();
static void zMainParseINIGlobals(xIniFile* ini);
static void zMainMemLvlChkCB();
void zMainShowProgressBar();
static void zMainLoop();
static void zMainReadINI();
void zMainFirstScreen(int);
static void zMainLoadFontHIP();

#endif