"""What each asset type is made of, for gen.py.

A type is a list of steps, applied from the start of the asset:

    "xEntAsset"                 a struct, swapped by its DWARF layout
    ("array", elem, count)      `count` copies of `elem` -- a struct name,
                                "u16" or "u32". `count` is a number, a
                                "Struct.member" read from a struct placed
                                earlier at a fixed offset, or "rest" for as
                                many as fit before the links.
    ("words",)                  every aligned u32 to the end of the body
    ("fields", name, size, [(offset, width), ...])
                                a struct the DWARF does not have, or has
                                under a name another type took, by hand
    ("bytes",)                  the rest of the body is bytes
    ("tagged", "Struct.count", {type: struct or size})
                                a list whose entries start with a u32 type
                                that picks the entry's struct; a size means
                                an 8-byte xParCmdAsset header, then words
    ("custom", "fn")            a hand-written function in iGCSwap.cpp,
                                handed the whole asset, links and all.
                                EXTRA_STRUCTS makes sure the generated
                                swaps it calls exist.

An asset derived from xBaseAsset ends in `linkCount` xLinkAssets, and the
generator swaps those for every type not in NOBASE.

EITHER names fields the GameCube packer swapped for some assets and not for
others: on the disc they are big-endian in most and little-endian in the rest.
They look like fields added after the packer's own field tables were written.
Each is decided per asset by which byte order gives a plausible value: a
float of sensible magnitude, or an int below 0x10000.

UNIONS says how to pick a union's member: the struct, the offset the union is
at, the member that selects, and the struct for each value.

DYNA is a dictionary instead: xDynAsset's `type` is xStrHash of one of these
names, and each maps to steps that start at the beginning of the asset. The
xDynAsset fields are swapped once, before the dispatch.

The answer key for all of it is the Xbox asset of the same id; see check.py.
"""

# Types that do not start with an xBaseAsset, so carry no links.
NOBASE = {"MRKR", "TEXT", "ALST", "SHDW", "PIPT", "LODT", "COLL", "LKIT", "ATBL", "JAW ",
          "SHRP", "MAPR", "VILP", "FLY ", "CRDT", "PICK", "SNDI", "CTOC", "MINF", "ANIM"}

TYPES = {
    "SIMP": ["xEntAsset", "xSimpleObjAsset"],
    "PLAT": ["xEntAsset", "xPlatformAsset", "xEntMotionAsset"],
    "BUTN": ["xEntAsset", "zEntButtonAsset", "xEntMotionAsset"],
    "VIL ": ["xEntAsset", "xEntNPCAsset"],
    "PKUP": ["xEntAsset", "xEntPickupAsset"],
    "TRIG": ["xEntAsset", "xTriggerAsset"],
    "DSTR": ["xEntAsset", "zEntDestructObjAsset"],
    "BOUL": ["xEntAsset", "xEntBoulderAsset"],
    "PLYR": [("custom", "iGCSwapPlayer")],
    "TIMR": ["xTimerAsset"],
    # Mods only: retail has no SCRP assets. See pc/iModPatch.h.
    "SCRP": ["xScriptAsset", ("array", "xScriptEventAsset", "xScriptAsset.eventCount")],
    "SFX ": ["xSFXAsset"],
    "CAM ": ["xCamAsset"],
    "CNTR": ["xCounterAsset"],
    "COND": ["zCondAsset"],
    "DPAT": ["xBaseAsset"],
    "UI  ": ["zUIAsset"],
    "UIFT": ["zUIFontAsset"],
    "PARE": ["xParEmitterAsset"],
    "PARP": ["xParEmitterPropsAsset"],
    "PORT": ["xPortalAsset"],
    "EGEN": ["zEGenAsset"],
    "SURF": ["zSurfAssetBase"],
    "CSNM": ["xCutsceneMgrAsset"],
    "FOG ": ["xFogAsset"],
    "ENV ": ["xEnvAsset"],
    "GRUP": ["xGroupAsset", ("array", "u32", "xGroupAsset.itemCount")],
    "MVPT": ["xMovePointAsset", ("array", "u32", "xMovePointAsset.numPoints")],
    "PARS": ["xParSysAsset", ("tagged", "xParSysAsset.cmdCount", "PARCMDS")],
    "PICK": ["zAssetPickupTable", ("array", "zAssetPickup", "zAssetPickupTable.Count")],
    "DSCO": [("custom", "iGCSwapDisco")],
    "MINF": [("custom", "iGCSwapModelInfo")],
    "TEXT": ["xTextAsset"],

    # Every field a u32 or a float: the Xbox copy is the GameCube one with each
    # word reversed.
    "MRKR": [("words",)],
    "ALST": [("words",)],
    "SHDW": [("words",)],
    "PIPT": [("words",)],
    "COLL": [("words",)],
    "LKIT": [("words",)],
    # The raw animation ids, the files -- whose NumAnims pair is 16-bit -- and
    # the states, then the files' index arrays and the effects, all words.
    "ATBL": ["xAnimAssetTable", ("array", "u32", "xAnimAssetTable.NumRaw"),
             ("array", "xAnimAssetFile", "xAnimAssetTable.NumFiles"),
             ("array", "xAnimAssetState", "xAnimAssetTable.NumStates"), ("words",)],
    # Flythrough keyframes: the GameCube disc ships them little-endian,
    # byte-identical to the Xbox's.
    "FLY ": [("bytes",)],
    "MAPR": [("words",)],
    "VILP": [("words",)],
    "SHRP": [("custom", "iGCSwapShrapnel")],
    "LODT": [("words",)],
    "CRDT": [("custom", "iGCSwapCredits")],
    "CTOC": [("custom", "iGCSwapCutsceneToc")],

    "ANIM": [("custom", "iGCSwapAnim")],
    "JAW ": [("custom", "iGCSwapJaw")],
    "JSP ": [("custom", "iGCSwapJspInfo")],
    # The sound table. Its layout is the GameCube's own, not the Xbox's, so
    # there is no twin to check it against; iSnd.cpp reads both.
    "SNDI": [("custom", "iGCSwapSoundTable")],
}

EITHER = {
    "xTimerAsset.randomRange": "float",
    "xEnvAsset.loldHeight": "float",
    "xParEmitterAsset.cull_mode": "int",
    "zSurfAssetBase.damage_timer": "float",
    "zSurfAssetBase.damage_bounce": "float",
}

EXTRA_STRUCTS = ["xModelAssetInfo", "xModelAssetInst", "z_disco_floor_asset"]

# The command types xParCmd.cpp registers, and what each one's entry is. A
# number is an entry the DWARF has no struct for: xParCmd.h's, all words after
# the header.
PARCMDS = {
    0: "xParCmdMove", 1: "xParCmdMove", 2: "xParCmdAccelerate", 3: "xParCmdAsset",
    5: 0xC, 6: "xParCmdKillSlow", 7: "xParCmdFollow", 8: "xParCmdOrbitPoint",
    9: "xParCmdOrbitLine", 10: "xParCmdMoveRandomPar", 11: 0x18, 12: "xParCmdTex",
    13: "xParCmdTexAnim", 15: "xParCmdRandomVelocityPar",
    # CUSTOM: unregistered too; boot.HIP's one entry is the header and five
    # words.
    16: 0x1C,
    18: 0xC, 19: 0x18, 20: 0xC,
    21: "xParCmdRotPar", 23: "xParCmdRotateAround", 24: 0xC, 25: 0xC,
    # DAMAGEPLAYER: unregistered, so the game gives it no size, but the assets
    # carry one -- the header and two words.
    28: 0x10,
    29: "xParCmdCollideFall", 30: "xParCmdShaperData", 31: "xParCmdAlphaInOutData",
    32: "xParCmdSizeInOutData", 33: "xParCmdDampenData", 34: "xParCmdCollideFallSticky",
}

UNIONS = {
    # en_xParEmitterEmitTypes. Every shape but the two entity ones is floats
    # and ids, so "u32" swaps the whole union a word at a time.
    "xParEmitterAsset": {"at": 0x10, "selector": "emit_type", "cases": dict(
        [(k, "u32") for k in range(15)] + [(15, "xPEEntBone"), (16, "xPEEntBound")])},
    "xEntMotionAsset": {"at": 0x4, "selector": "type", "cases": {
        0: "xEntMotionERData", 1: "xEntMotionOrbitData", 2: "xEntMotionSplineData",
        3: "xEntMotionMPData", 4: "xEntMotionMechData", 5: "xEntMotionPenData"}},
}

# DYNA types known only by their hash.
DYNA_NAMES = {
    0x8768334A: "zNPCSettings",
}

DYNA = {
    "game_object:Teleport": ["teleport_asset"],
    "game_object:BusStop": ["busstop_asset"],
    "game_object:Taxi": ["taxi_asset"],
    "game_object:BoulderGenerator": ["xBoulderGeneratorAsset"],
    "game_object:bungee_hook": ["hook_asset"],
    "game_object:Flythrough": ["CameraFly_asset"],
    "game_object:Camera_Tweak": ["CameraTweak_asset"],
    "hud:model": ["model_asset"],
    "hud:meter:font": ["font_meter_asset"],
    "hud:meter:unit": ["unit_meter_asset"],
    "hud:text": ["text_asset"],
    "game_object:bungee_drop": ["drop_asset"],
    "pointer": ["pointer_asset"],
    # ztextbox::asset_type: text, bounds, font, size, space, a colour's four
    # bytes, inset, the two justifications, max_height, then the backdrop's
    # type, colour bytes and texture.
    "game_object:text_box": [("fields", "ztextbox_asset", 0x64,
                              [(o, 4) for o in range(0x10, 0x38, 4)] +
                              [(o, 4) for o in range(0x3C, 0x5C, 4)] + [(0x60, 4)])],
    # ztalkbox::asset_type: three ids, eight flag bytes, teleport, the four
    # auto_wait type bytes, delay, which_event, and five prompt ids.
    "game_object:talk_box": [("fields", "ztalkbox_asset", 0x48,
                              [(0x10, 4), (0x14, 4), (0x18, 4), (0x24, 4)] +
                              [(o, 4) for o in range(0x2C, 0x48, 4)])],
    # ztaskbox::asset_type: four flag bytes, then eight ids.
    "game_object:task_box": [("fields", "ztaskbox_asset", 0x34,
                              [(o, 4) for o in range(0x14, 0x34, 4)])],
    0x8768334A: [("fields", "zNPCSettings", 0x2C, [(0x10, 4), (0x20, 4), (0x24, 4), (0x28, 4)])],
}
