#ifndef XEVENT_H
#define XEVENT_H

#include "xBase.h"

enum en_xEventTags
{
    eEventUnknown,
    eEventEnable,
    eEventDisable,
    eEventVisible,
    eEventInvisible,
    eEventEnterPlayer,
    eEventExitPlayer,
    eEventTouchPlayer,
    eEventControlOff,
    eEventControlOn,
    eEventReset,
    eEventIncrement,
    eEventDecrement,
    eEventOpen,
    eEventClose,
    eEventToggle,
    eEventTeleportPlayer,
    eEventOutOfBounds,
    eEventRun,
    eEventStop,
    eEventExpired,
    eEventMove,
    eEventDestroy,
    eEventPause,
    eEventPlay,
    eEventPlayOne,
    eEventPlayMaybe,
    eEventRoomStart,
    eEventInvalidate,
    eEventTilt,
    eEventUntilt,
    eEventArrive,
    eEventMount,
    eEventDismount,
    eEventBreak,
    eEventPickup,
    eEventDeath,
    eEventKill,
    eEventOn,
    eEventOff,
    eEventNPCPatrolOn,
    eEventNPCPatrolOff,
    eEventNPCWanderOn,
    eEventNPCWanderOff,
    eEventNPCDetectOn,
    eEventNPCDetectOff,
    eEventNPCChaseOn,
    eEventNPCChaseOff,
    eEventNPCGoToSleep,
    eEventNPCWakeUp,
    eEventNPCRespawn,
    eEventPlayerDeath,
    eEventGiveChance,
    eEventGiveShinyObjects,
    eEventGiveHealth,
    eEventPress,
    eEventUnpress,
    eEventArriveHalfway,
    eEventHit,
    eEventButtonPressAction,
    eEventEvaluate,
    eEventTrue,
    eEventFalse,
    eEventPadPressX,
    eEventPadPressSquare,
    eEventPadPressO,
    eEventPadPressTriangle,
    eEventPadPressL1,
    eEventPadPressL2,
    eEventPadPressR1,
    eEventPadPressR2,
    eEventPadPressStart,
    eEventPadPressSelect,
    eEventPadPressUp,
    eEventPadPressDown,
    eEventPadPressRight,
    eEventPadPressLeft,
    eEventFontBackdropOn,
    eEventFontBackdropOff,
    eEventUISelect,
    eEventUIUnselect,
    eEventUIFocusOn,
    eEventUIFocusOff,
    eEventCollisionOn,
    eEventCollisionOff,
    eEventCollision_Visible_On,
    eEventCollision_Visible_Off,
    eEventSceneBegin,
    eEventSceneEnd,
    eEventRoomBegin,
    eEventRoomEnd,
    eEventLobMasterShoot,
    eEventLobMasterReset,
    eEventFallToDeath,
    eEventUIFocusOn_Select,
    eEventUIFocusOff_Unselect,
    eEventDispatcher_PadCfg_PresetA,
    eEventDispatcher_PadCfg_PresetB,
    eEventDispatcher_PadCfg_PresetC,
    eEventDispatcher_PadCfg_PresetD,
    eEventDispatcher_PadVibrateOn,
    eEventDispatcher_PadVibrateOff,
    eEventDispatcher_SoundMono,
    eEventDispatcher_SoundStereo,
    eEventDispatcher_SoundMasterIncrease,
    eEventDispatcher_SoundMasterDecrease,
    eEventDispatcher_SoundMusicIncrease,
    eEventDispatcher_SoundMusicDecrease,
    eEventDispatcher_SoundSFXIncrease,
    eEventDispatcher_SoundSFXDecrease,
    eEventDispatcher_IntroState_Sony,
    eEventDispatcher_IntroState_Publisher,
    eEventDispatcher_IntroState_Developer,
    eEventDispatcher_IntroState_License,
    eEventDispatcher_IntroState_Count,
    eEventDispatcher_TitleState_Start,
    eEventDispatcher_TitleState_Attract,
    eEventDispatcher_TitleState_Count,
    eEventDispatcher_LoadState_SelectMemCard,
    eEventDispatcher_LoadState_SelectSlot,
    eEventDispatcher_LoadState_Loading,
    eEventDispatcher_LoadState_Count,
    eEventDispatcher_OptionsState_Options,
    eEventDispatcher_OptionsState_Count,
    eEventDispatcher_SaveState_SelectMemCard,
    eEventDispatcher_SaveState_SelectSlot,
    eEventDispatcher_SaveState_Saving,
    eEventDispatcher_SaveState_Count,
    eEventDispatcher_PauseState_Pause,
    eEventDispatcher_PauseState_Options,
    eEventDispatcher_PauseState_Count,
    eEventDispatcher_GameState_FirstTime,
    eEventDispatcher_GameState_Play,
    eEventDispatcher_GameState_LoseChance,
    eEventDispatcher_GameState_GameOver,
    eEventDispatcher_GameState_SceneSwitch,
    eEventDispatcher_GameState_Dead,
    eEventDispatcher_SetIntroState_Sony,
    eEventDispatcher_SetIntroState_Publisher,
    eEventDispatcher_SetIntroState_Developer,
    eEventDispatcher_SetIntroState_License,
    eEventDispatcher_SetIntroState_Count,
    eEventDispatcher_SetTitleState_Start,
    eEventDispatcher_SetTitleState_Attract,
    eEventDispatcher_SetTitleState_Count,
    eEventDispatcher_SetLoadState_SelectMemCard,
    eEventDispatcher_SetLoadState_SelectSlot,
    eEventDispatcher_SetLoadState_Loading,
    eEventDispatcher_SetLoadState_Count,
    eEventDispatcher_SetOptionsState_Options,
    eEventDispatcher_SetOptionsState_Count,
    eEventDispatcher_SetSaveState_SelectMemCard,
    eEventDispatcher_SetSaveState_SelectSlot,
    eEventDispatcher_SetSaveState_Saving,
    eEventDispatcher_SetSaveState_Count,
    eEventDispatcher_SetPauseState_Pause,
    eEventDispatcher_SetPauseState_Options,
    eEventDispatcher_SetPauseState_Count,
    eEventDispatcher_SetGameState_FirstTime,
    eEventDispatcher_SetGameState_Play,
    eEventDispatcher_SetGameState_LoseChance,
    eEventDispatcher_SetGameState_GameOver,
    eEventDispatcher_SetGameState_SceneSwitch,
    eEventDispatcher_SetGameState_Dead,
    eEventDigup,
    eEventDispatcher_GameState_Exit,
    eEventDispatcher_SetGameState_Exit,
    eEventLobMasterShootFromWidget,
    eEventDispatcher_SLBack,
    eEventDispatcher_SLCancel,
    eEventDispatcher_SLRetry,
    eEventDispatcher_SLSelectCard,
    eEventDispatcher_SLSelectSlot,
    eEventDispatcher_SLOkay,
    eEventVilHurtBoss,
    eEventAttack,
    eEventAttackOn,
    eEventAttackOff,
    eEventDrop,
    eEventVilReport_StartingIdle,
    eEventVilReport_StartingSleep,
    eEventVilReport_StartingGuard,
    eEventVilReport_StartingPatrol,
    eEventVilReport_StartingDazed,
    eEventVilReport_StartingLook,
    eEventVilReport_StartingListen,
    eEventVilReport_StartingInvestigate,
    eEventVilReport_StartingChase,
    eEventVilReport_StartingAttack,
    eEventVilReport_StartingRetreat,
    eEventPreload,
    eEventDone,
    eEventArcto,
    eEventDigupReaction,
    eEventDispatcher_StoreCheckPoint,
    eEventAnimPlay,
    eEventAnimPlayLoop,
    eEventAnimStop,
    eEventAnimPause,
    eEventAnimResume,
    eEventAnimTogglePause,
    eEventAnimPlayRandom,
    eEventAnimPlayMaybe,
    eEventSetSpeed,
    eEventAccelerate,
    eEventMoveToTarget,
    eEventSwingerFollow,
    eEventShaggyMount,
    eEventShaggyWitchDrop,
    eEventShaggySwap,
    eEventShaggyState,
    eEventShaggyAction,
    eEventEnterEntity,
    eEventExitEntity,
    eEventEnterEntityFLAG,
    eEventExitEntityFLAG,
    eEventDrivenby,
    eEventFollowTarget,
    eEventFaceTarget,
    eEventWatchTarget,
    eEventShaggyCollideOnly,
    eEventShaggy1_ThrowTarget,
    eEventShaggy8_CallEnable,
    eEventShaggy8_CallDisable,
    eEventShaggy8_SetMagnet,
    eEventShaggy8_ClearMagnet,
    eEventStartMoving,
    eEventStopMoving,
    eEventSwoosh,
    eEventShaggySetDown,
    eEventShaggyGrabEnable,
    eEventShaggyGrabDisable,
    eEventShaggyGrabbed,
    eEventShaggyThrown,
    eEventVilDoAction,
    eEventGangDoBossAction,
    eEventVilFakeChaseOn,
    eEventVilFakeChaseOff,
    eEventBossMMPushButton,
    eEventVilReport_DecayComplete,
    eEventVilGuardWidget,
    eEventTextureAnimateOn,
    eEventTextureAnimateOff,
    eEventTextureAnimateToggle,
    eEventColorEffectOn,
    eEventColorEffectOff,
    eEventColorEffectToggle,
    eEventSetTextureAnimGroup,
    eEventSetTextureAnimSpeed,
    eEventTextureAnimateStep,
    eEventEmit,
    eEventEmitted,
    eEventTranslucentOn,
    eEventTranslucentOff,
    eEventTranslucentToggle,
    eEventVilGangTalkOn,
    eEventVilGangTalkOff,
    eEventGivePowerUp,
    eEventUnlockR001,
    eEventUnlockS001,
    eEventUnlockE001,
    eEventUnlockF001,
    eEventDisableGroupContents,
    eEventShaggyPhysHack,
    eEventOccludeOn,
    eEventOccludeOff,
    eEventWaveSetLinear,
    eEventWaveSetRipple,
    eEventSituationLaugh,
    eEventSituationBossBattleGreenGhost,
    eEventSituationBossBattleRedBeard,
    eEventSituationBossBattleMasterMind,
    eEventSituationBossBattleBlacknight,
    eEventSituationPlayerScare,
    eEventSituationPlayerSafe,
    eEventSituationPlayerDanger,
    eEventSituationPlayerChaseBegin,
    eEventSituationPlayerChaseEnd,
    eEventSituationPlayerSeeShaggy,
    eEventSituationPlayerSeeFood,
    eEventSituationPlayerSeeToken,
    eEventSituationPlayerSeeScoobySnack,
    eEventSituationPlayerSeePowerup,
    eEventSituationPlayerSeeMonster,
    eEventSituationPlayerSuccess,
    eEventSituationPlayerFailure,
    eEventDispatcher_ShowHud,
    eEventDispatcher_HideHud,
    eEventDispatcher_FadeOut,
    eEventSetRain,
    eEventSetSnow,
    eEventShaggyMowerStopMode,
    eEventScriptReset,
    eEventWaitForInput,
    eEventPlayMovie,
    eEventDefeatedMM,
    eEventDispatcher_SetGameState_GameStats,
    eEventPlayMusic,
    eEventForward,
    eEventReverse,
    eEventPlayerRumbleTest,
    eEventPlayerRumbleLight,
    eEventPlayerRumbleMedium,
    eEventPlayerRumbleHeavy,
    eEventDispatcherScreenAdjustON,
    eEventDispatcherScreenAdjustOFF,
    eEventSetSkyDome,
    eEventConnectToChild,
    eEventDuploWaveBegin,
    eEventDuploWaveComplete,
    eEventDuploNPCBorn,
    eEventDuploNPCKilled,
    eEventDuploExpiredMaxNPC,
    eEventDuploPause,
    eEventDuploResume,
    eEventSetGoo,
    eEventNPCScript_ScriptBegin,
    eEventNPCScript_ScriptEnd,
    eEventNPCScript_ScriptReady,
    eEventNPCScript_Halt,
    eEventNPCScript_SetPos,
    eEventNPCScript_SetDir,
    eEventNPCScript_LookNormal,
    eEventNPCScript_LookAlert,
    eEventNPCScript_FaceWidget,
    eEventNPCScript_FaceWidgetDone,
    eEventNPCScript_GotoWidget,
    eEventNPCScript_GotoWidgetDone,
    eEventNPCScript_AttackWidget,
    eEventNPCScript_AttackWidgetDone,
    eEventNPCScript_FollowWidget,
    eEventNPCScript_PlayAnim,
    eEventNPCScript_PlayAnimDone,
    eEventNPCScript_LeadPlayer,
    eEventSetText,
    eEventStartConversation,
    eEventEndConversation,
    eEventSwitch,
    eEventAddText,
    eEventClearText,
    eEventOpenTBox,
    eEventCloseTBox,
    eEventTalkBox_OnSignal0,
    eEventTalkBox_OnSignal1,
    eEventTalkBox_OnSignal2,
    eEventTalkBox_OnSignal3,
    eEventTalkBox_OnSignal4,
    eEventTalkBox_OnSignal5,
    eEventTalkBox_OnSignal6,
    eEventTalkBox_OnSignal7,
    eEventTalkBox_OnSignal8,
    eEventTalkBox_OnSignal9,
    eEventTalkBox_StopWait,
    eEventTalkBox_OnStart,
    eEventTalkBox_OnStop,
    eEventHit_Melee,
    eEventHit_BubbleBounce,
    eEventHit_BubbleBash,
    eEventHit_BubbleBowl,
    eEventHit_PatrickSlam,
    eEventHit_Throw,
    eEventHit_PaddleLeft,
    eEventHit_PaddleRight,
    eEventTaskBox_Initiate,
    eEventTaskBox_SetSuccess,
    eEventTaskBox_SetFailure,
    eEventTaskBox_OnAccept,
    eEventTaskBox_OnDecline,
    eEventTaskBox_OnComplete,
    eEventGenerateBoulder,
    eEventLaunchBoulderAtWidget,
    eEventLaunchBoulderAtPoint,
    eEventLaunchBoulderAtPlayer,
    eEventDuploSuperDuperDone,
    eEventDuploDuperIsDoner,
    eEventBusStopSwitchChr,
    eEventGroupUpdateTogether,
    eEventSetUpdateDistance,
    eEventTranslLocalX,
    eEventTranslLocalY,
    eEventTranslLocalZ,
    eEventTranslWorldX,
    eEventTranslWorldY,
    eEventTranslWorldZ,
    eEventRotLocalX,
    eEventRotLocalY,
    eEventRotLocalZ,
    eEventRotWorldX,
    eEventRotWorldY,
    eEventRotWorldZ,
    eEventTranslLocalXDone,
    eEventTranslLocalYDone,
    eEventTranslLocalZDone,
    eEventTranslWorldXDone,
    eEventTranslWorldYDone,
    eEventTranslWorldZDone,
    eEventRotLocalXDone,
    eEventRotLocalYDone,
    eEventRotLocalZDone,
    eEventRotWorldXDone,
    eEventRotWorldYDone,
    eEventRotWorldZDone,
    eEventCount1,
    eEventCount2,
    eEventCount3,
    eEventCount4,
    eEventCount5,
    eEventCount6,
    eEventCount7,
    eEventCount8,
    eEventCount9,
    eEventCount10,
    eEventCount11,
    eEventCount12,
    eEventCount13,
    eEventCount14,
    eEventCount15,
    eEventCount16,
    eEventCount17,
    eEventCount18,
    eEventCount19,
    eEventCount20,
    eEventSetState,
    eEventEnterSpongeBob,
    eEventEnterPatrick,
    eEventEnterSandy,
    eEventExitSpongeBob,
    eEventExitPatrick,
    eEventExitSandy,
    eEventNPCSpecial_PlatformSnap,
    eEventNPCSpecial_PlatformFall,
    eEventGooSetWarb,
    eEventGooSetFreezeDuration,
    eEventGooMelt,
    eEventSetStateRange,
    eEventSetStateDelay,
    eEventSetTransitionDelay,
    eEventNPCFightOn,
    eEventNPCFightOff,
    eEventNPCSplineOKOn,
    eEventNPCSplineOKOff,
    eEventNPCKillQuietly,
    eEventHitHead,
    eEventHitUpperBody,
    eEventHitLeftArm,
    eEventHitRightArm,
    eEventHitLeftLeg,
    eEventHitRightLeg,
    eEventHitLowerBody,
    eEventGiveCurrLevelSocks,
    eEventGiveCurrLevelPickup,
    eEventSetCurrLevelSocks,
    eEventSetCurrLevelPickup,
    eEventTalkBox_OnYes,
    eEventTalkBox_OnNo,
    eEventHit_Cruise,
    eEventDuploKillKids,
    eEventTalkBox_OnSignal10,
    eEventTalkBox_OnSignal11,
    eEventTalkBox_OnSignal12,
    eEventTalkBox_OnSignal13,
    eEventTalkBox_OnSignal14,
    eEventTalkBox_OnSignal15,
    eEventTalkBox_OnSignal16,
    eEventTalkBox_OnSignal17,
    eEventTalkBox_OnSignal18,
    eEventTalkBox_OnSignal19,
    eEventSpongeballOn,
    eEventSpongeballOff,
    eEventLaunchShrapnel,
    eEventNPCHPIncremented,
    eEventNPCHPDecremented,
    eEventNPCSetActiveOn,
    eEventNPCSetActiveOff,
    eEventPlrSwitchCharacter,
    eEventLevelBegin,
    eEventSceneReset,
    eEventSceneEnter,
    eEventSituationDestroyedTiki,
    eEventSituationDestroyedRobot,
    eEventSituationSeeWoodTiki,
    eEventSituationSeeLoveyTiki,
    eEventSituationSeeShhhTiki,
    eEventSituationSeeThunderTiki,
    eEventSituationSeeStoneTiki,
    eEventSituationSeeFodder,
    eEventSituationSeeHammer,
    eEventSituationSeeTarTar,
    eEventSituationSeeGLove,
    eEventSituationSeeMonsoon,
    eEventSituationSeeSleepyTime,
    eEventSituationSeeArf,
    eEventSituationSeeTubelets,
    eEventSituationSeeSlick,
    eEventSituationSeeKingJellyfish,
    eEventSituationSeePrawn,
    eEventSituationSeeDutchman,
    eEventSituationSeeSandyBoss,
    eEventSituationSeePatrickBoss,
    eEventSituationSeeSpongeBobBoss,
    eEventSituationSeeRobotPlankton,
    eEventUIChangeTexture,
    eEventNPCCheerForMe,
    eEventFastVisible,
    eEventFastInvisible,
    eEventZipLineMount,
    eEventZipLineDismount,
    eEventTarget,
    eEventFire,
    eEventCameraFXShake,
    eEventBulletTime,
    eEventThrown,
    eEventUpdateAnimMatrices,
    eEventEnterCruise,
    eEventExitCruise,
    eEventCruiseFired,
    eEventCruiseDied,
    eEventCruiseAddLife,
    eEventCruiseSetLife,
    eEventCruiseResetLife,
    eEventCameraCollideOff,
    eEventCameraCollideOn,
    eEventOnSliding,
    eEventOffSliding,
    eEventTimerSet,
    eEventTimerAdd,
    eEventNPCForceConverseStart,
    eEventMakeASplash,
    eEventCreditsStart,
    eEventCreditsStop,
    eEventCreditsEnded,
    eEventBubbleWipe,
    eEventSetLightKit,
    eEventSetOpacity,
    eEventTakeSocks,
    eEventDispatcherAssert,
    eEventBorn,
    eEventPlatPause,
    eEventPlatUnpause,
    eEventStoreOptions,
    eEventRestoreOptions,
    eEventCount
};

void zEntEvent(char* to, unsigned int toEvent);
void zEntEvent(unsigned int toID, unsigned int toEvent);
void zEntEvent(unsigned int toID, unsigned int toEvent, float toParam0,
               float toParam1, float toParam2, float toParam3);
void zEntEvent(xBase* to, unsigned int toEvent);
void zEntEvent(xBase* to, unsigned int toEvent, float toParam0, float toParam1,
               float toParam2, float toParam3);
void zEntEvent(xBase* to, unsigned int toEvent, const float* toParam);
void zEntEvent(xBase* to, unsigned int toEvent, const float* toParam,
               xBase* toParamWidget);
void zEntEvent(xBase* from, xBase* to, unsigned int toEvent);
void zEntEvent(xBase* from, xBase* to, unsigned int toEvent,
               const float* toParam);
void zEntEvent(xBase* from, unsigned int fromEvent, xBase* to,
               unsigned int toEvent, const float* toParam, xBase* toParamWidget,
               int forceEvent);

#endif