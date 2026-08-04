#include "zNPCTypePrawn.h"

#include "rwcore.h"
#include "xDebug.h"

#include "xMemMgr.h"
#include "xGroup.h"
#include "xMath.h"
#include "xMathInlines.h"
#include "zEntCruiseBubble.h"
#include "zNPCTypeCommon.h"
#include "zRenderState.h"
#include <types.h>
#include <stdio.h>
#include <string.h>

#define f1052 1.0f
#define f1053 0.0f
#define f1454 0.2f
#define f1455 0.1f

#define ANIM_Unknown 0 // 0x0
#define ANIM_Idle01 1 // 0x04
#define ANIM_Idle02 2 // 0x08
#define ANIM_Idle03 3 // 0xC
#define ANIM_Fidget01 4 //
#define ANIM_Fidget02 5
#define ANIM_Fidget03 6
#define ANIM_Taunt01 7 // 0x1c
#define ANIM_Attack01 8 //0x20
#define ANIM_Damage01 9 //0x24
#define ANIM_Damage02 10 //0x28
#define ANIM_Death01 11 //0x2c
#define ANIM_AttackWindup01 12 //0x30
#define ANIM_AttackLoop01 13 //0x34
#define ANIM_AttackEnd01 14 //0x38
#define ANIM_SpawnKids01 15 //0x3C
#define ANIM_Attack02Windup01 16
#define ANIM_Attack02Loop01 17
#define ANIM_Attack02End01 18
#define ANIM_LassoGrab01 19

U32 xSndPlay3DFade(U32 id, F32 vol, F32 pitch, U32 priority, U32 flags, const xVec3* pos,
                   F32 innerRadius, F32 outerRadius, sound_category category, F32 fade, F32 delay);

namespace
{
    struct sound_asset_type
    {
        S32 category;
        const char* name;
        U32 priority;
        U32 mode;
    };

    const sound_asset_type sound_assets[14] = {};

    const char* sound_asset_names[4][6];
    U32 sound_asset_ids[4][6];
    S32 sound_asset_names_size[4];
    sound_data_type sound_data[4];

    struct tweak_group
    {
        F32 turn_accel;
        F32 spawn_delay;
        F32 repel_radius;
        struct
        {
            range_type pattern;
        } safe;
        struct
        {
            range_type pattern;
            F32 state_delay; // 0x1c
            F32 transition_delay;
        } begin;
        struct
        {
            F32 delay[3];
            range_type pattern;
            F32 state_delay;
            F32 transition_delay;
            F32 exhaust_vel;
            F32 knock_back;
            struct
            {
                S32 amount[3];
                F32 arc;
                F32 delay;
                F32 accel;
                F32 max_vel;
            } sweep;
            fire_type fire;
        } beam;
        _class_7 aim_lane;
        _class_14 lane;
        _class_25 danger;
        sound_property sound[4];
        void* context;
        tweak_callback cb_sound;
        tweak_callback cb_sound_asset;

        void load(xModelAssetParam* p, U32 i);
        void register_tweaks(bool init, xModelAssetParam* ap, U32 apsiz, const char* c);
    }; // namespace tweak_group

    static tweak_group tweak;

    void init_sound()
    {
    }

    void reset_sound()
    {
        sound_data[0].handle = 0;
        sound_data[1].handle = 0;
        sound_data[2].handle = 0;
        sound_data[3].handle = 0;
    }

    U32 play_sound(S32 which, const xVec3* loc, F32 volume)
    {
        const sound_asset_type& asset = sound_assets[tweak.sound[which].asset];

        if ((asset.mode & 2) && sound_data[which].handle != 0)
        {
            return sound_data[which].handle;
        }

        if (asset.mode & 1)
        {
            sound_data[which].handle =
                xSndPlay3DFade(sound_data[which].id, volume * tweak.sound[which].volume, 1.0f,
                               asset.priority, 0x800, loc, tweak.sound[which].range_inner,
                               tweak.sound[which].range_outer, (sound_category)0, 0.0f,
                               tweak.sound[which].delay);
        }
        else
        {
            sound_data[which].handle =
                xSndPlay3D(sound_data[which].id, volume * tweak.sound[which].volume, 1.0f,
                           asset.priority, 0x800, loc, tweak.sound[which].range_inner,
                           tweak.sound[which].range_outer, (sound_category)0,
                           tweak.sound[which].delay);
        }

        sound_data[which].loc = loc;
        sound_data[which].volume = volume;
        return sound_data[which].handle;
    }

    void kill_sound(S32 which, U32 handle)
    {
        sound_data_type& data = sound_data[which];
        sound_property& prop = tweak.sound[which];

        if (sound_assets[prop.asset].mode & 1)
        {
            xSndStopFade(handle, prop.fade_time);
        }
        else
        {
            xSndStop(handle);
        }
        data.handle = 0;
    }

    void kill_sound(S32 which)
    {
        U32 handle = sound_data[which].handle;
        if (handle != 0)
        {
            if (sound_assets[tweak.sound[which].asset].mode & 1)
            {
                xSndStopFade(handle, tweak.sound[which].fade_time);
            }
            else
            {
                xSndStop(handle);
            }
            sound_data[which].handle = 0;
        }
    }
} // namespace

void aqua_beam::load(const aqua_beam::config& c, U32 i)
{
    void* a = xSTFindAsset(i, 0);
    aqua_beam::load(c, *(RpAtomic*)a);
}

void aqua_beam::load(const aqua_beam::config& c, RpAtomic& a)
{
    ring.model_data = &a;
    cfg = c;
    ring.queue.reset();
}

void aqua_beam::reset()
{
    firing = 0;
    bool tempBvar;

    while (!(tempBvar = ring.queue.empty()))
    {
        aqua_beam::kill_ring();
    }
    ring_sounds = 0;
}

void aqua_beam::start()
{
    firing = 1;
    time = 0.0f;
    ring.emit_time = -1000000000.0f;
}

void aqua_beam::stop()
{
    firing = 0;
}

void aqua_beam::update(F32 dt)
{
    time += dt;
    if ((cfg.duration >= 0.0f) && (time >= cfg.duration))
    {
        firing = 0;
    }
    update_rings(dt);
}

void aqua_beam::render()
{
    fixed_queue<aqua_beam::ring_segment, 31>::iterator entry = ring.queue.begin();
    while (entry != ring.queue.end())
    {
        render_ring(*entry);
        ++entry;
    }
}

bool aqua_beam::hits_sphere(const xSphere& sphere) const
{
    xVec3 center = sphere.center - cfg.ring.hit_offset;

    fixed_queue<aqua_beam::ring_segment, 31>::iterator entry = ring.queue.begin();
    while (entry != ring.queue.end())
    {
        aqua_beam::ring_segment& segment = *entry;
        F32 radius = cfg.ring.hit_radius * (cfg.ring.grow * segment.dist + 1.0f) + sphere.r;
        xVec3 delta = center - *(xVec3*)&segment.model->Mat->pos;

        if (delta.length2() <= radius * radius && FABS(delta.dot(segment.mat.at)) < radius)
        {
            return true;
        }
        ++entry;
    }
    return false;
}

void aqua_beam::update_rings(F32 dt)
{
    if (firing || !ring.queue.empty())
    {
        if (firing && time >= ring.emit_time + cfg.ring.emit_delay)
        {
            ring.emit_time += cfg.ring.emit_delay;
            if (time >= ring.emit_time + cfg.ring.emit_delay)
            {
                ring.emit_time = time;
            }
            emit_ring();
        }

        fixed_queue<aqua_beam::ring_segment, 31>::iterator entry = ring.queue.begin();
        while (entry != ring.queue.end())
        {
            update_ring(entry, dt);
            ++entry;
        }

        while (!ring.queue.empty() && FABS(ring.queue.back().dist) >= cfg.ring.kill_dist)
        {
            kill_ring();
        }
    }
}

void aqua_beam::emit_ring()
{
    if (ring.queue.full())
    {
        kill_ring();
    }
    ring.queue.push_front();

    aqua_beam::ring_segment& segment = ring.queue.front();
    segment.origin = loc;
    segment.dist = 0.0f;
    segment.vel = cfg.ring.vel;
    segment.mat.right.assign(dir.z, 0.0f, -dir.x).normalize();
    segment.mat.up = dir.cross(segment.mat.right);
    segment.mat.at = dir;
    segment.mat.pos = segment.origin;
    segment.model = xModelInstanceAlloc(ring.model_data, NULL, 0, 0, NULL);

    ring_sounds++;
    if (ring_sounds % cfg.sound_interval == 0)
    {
        ring_sounds = 0;
        segment.sound_handle = play_sound(3, &segment.mat.pos, 1.0f);
    }
    else
    {
        segment.sound_handle = 0;
    }
}

void aqua_beam::kill_ring()
{
    aqua_beam::ring_segment& back = ring.queue.back();
    xModelInstanceFree(back.model);

    if (back.sound_handle != 0)
    {
        kill_sound(3, back.sound_handle);
    }
    ring.queue.pop_back();
}

void aqua_beam::update_ring(fixed_queue<aqua_beam::ring_segment, 31>::iterator entry, F32 dt)
{
    aqua_beam::ring_segment& segment = *entry;

    segment.vel = cfg.ring.accel * dt + segment.vel;
    segment.dist += dt * (0.5f * cfg.ring.accel * dt) + segment.vel * dt;
    segment.mat.pos = segment.origin + segment.mat.at * segment.dist;
}

void aqua_beam::render_ring(aqua_beam::ring_segment& segment)
{
    xMat4x3* mat = (xMat4x3*)segment.model->Mat;
    F32 scale = segment.dist * cfg.ring.grow + cfg.ring.size;

    *mat = segment.mat;
    mat->right *= scale;
    mat->up *= scale;
    mat->at *= scale;

    xModelInstance* model = segment.model;
    model->Alpha = cfg.ring.alpha;

    F32 dist = FABS(segment.dist) - cfg.ring.fade_dist;
    if (dist > 0.0f)
    {
        F32 range = cfg.ring.kill_dist - cfg.ring.fade_dist;
        if (dist >= range)
        {
            model->Alpha = 0.0f;
        }
        else
        {
            model->Alpha *= 1.0f - dist / range;
        }
    }

    xModelRender(segment.model);
}

xAnimTable* ZNPC_AnimTable_Prawn()
{
    // clang-format off
    S32 ourAnims[10] = {
        ANIM_Idle01,
        ANIM_Fidget01,
        ANIM_Fidget02,
        ANIM_Taunt01,
        ANIM_AttackWindup01,
        ANIM_AttackLoop01,
        ANIM_AttackLoop01,
        ANIM_AttackEnd01,
        ANIM_Damage01,
        ANIM_Damage02,

    };
    // clang-format on
    xAnimTable* table = xAnimTableNew("zNPCPrawn", NULL, 0);

    xAnimTableNewState(table, g_strz_subbanim[ANIM_Idle01], 0x10, 0, f1052, NULL, NULL, f1053, NULL,
                       NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_subbanim[ANIM_Fidget01], 0x20, 0, f1052, NULL, NULL, f1053,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_subbanim[ANIM_Fidget02], 0x20, 0, f1052, NULL, NULL, f1053,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_subbanim[ANIM_Taunt01], 0x20, 0, f1052, NULL, NULL, f1053,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_subbanim[ANIM_AttackWindup01], 0x20, 0, f1052, NULL, NULL,
                       f1053, NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_subbanim[ANIM_AttackLoop01], 0x10, 0, f1052, NULL, NULL, f1053,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_subbanim[ANIM_AttackEnd01], 0x20, 0, f1052, NULL, NULL, f1053,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_subbanim[ANIM_Damage01], 0x20, 0, f1052, NULL, NULL, f1053,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);
    xAnimTableNewState(table, g_strz_subbanim[ANIM_Damage02], 0x20, 0, f1052, NULL, NULL, f1053,
                       NULL, NULL, xAnimDefaultBeforeEnter, NULL, NULL);

    NPCC_BuildStandardAnimTran(table, g_strz_subbanim, ourAnims, 1, f1454);

    xAnimTableNewTransition(table, g_strz_subbanim[ANIM_AttackWindup01],
                            g_strz_subbanim[ANIM_AttackLoop01], 0, 0, 0x10, 0, 0, 0, 0, 0, f1455,
                            0);
    xAnimTableNewTransition(table, g_strz_subbanim[ANIM_AttackLoop01],
                            g_strz_subbanim[ANIM_AttackEnd01], 0, 0, 0, 0, 0, 0, 0, 0, f1455, 0);
    xAnimTableNewTransition(table, g_strz_subbanim[ANIM_AttackEnd01], g_strz_subbanim[ANIM_Idle01],
                            0, 0, 0x10, 0, 0, 0, 0, 0, f1455, 0);

    return table;
}

zNPCPrawn::zNPCPrawn(S32 myType) : zNPCSubBoss(myType)
{
    this->fighting = 0;
}

namespace
{
    struct television
    {
        RwCamera* cam;
        RwRaster* raster;
        RwRaster* bgraster;
        RpWorld* world;
        RwTexture* texture;
        U32 vert_buffer_used;
        RwRGBA bgcolor;
        F32 rcz;
        F32 w;
        F32 h;

        bool create(S32, S32);
        void destroy();
        void set_background(iColor_tag);
        void set_model_texture(xModelInstance&);
        void update(xModelInstance&, xLightKit*);
        void render_static();
        void render_background();
        void set_vert(rwGameCube2DVertex&, F32, F32, F32, F32);
        void move(const xVec3&, const xVec3&);
    };

    static television closeup;

    xBinaryCamera boss_cam = { { { 6.0f, 3.0f, 2.0f },
                                 { 0.2f, 2.2f, -1.0f },
                                 { 1.0f, 0.2f, 1.5f },
                                 10.0f,
                                 10.0f,
                                 10.0f,
                                 10.0f,
                                 50.0f,
                                 -3.1415927f } };

    zParEmitter* exhaust_emitter;
    xParEmitterCustomSettings exhaust_emitter_settings;

    S32 load_patterns(xModelAssetParam*, U32, const char*, zNPCPrawn::range_type*, S32);
    void set_yaw_matrix(xMat3x3&, F32);
    void mulat(xVec3&, const xMat3x3&, const xMat3x3&);
} // namespace

void zNPCPrawn::Init(xEntAsset* a)
{
    boss_cam.init();
    init_sound();
    zNPCCommon::Init(a);
    memset(&this->flag, 0, 1);
    this->flg_move = 1;
    this->flg_vuln = 0x11;
    this->chkby = 0x10;
    this->penby = 0x10;
    this->beam.load(tweak.beam.fire, xStrHash("glow_ring_add.dff"));
    closeup.create(0x100, 0x100);
}

namespace
{
    bool television::create(S32 width, S32 height)
    {
        cam = NULL;
        raster = NULL;
        bgraster = NULL;
        world = NULL;
        memset(&bgcolor, 0x0, sizeof(bgcolor));

        w = (float)width;
        h = (float)height;
        cam = RwCameraCreate();
        if (cam == NULL)
        {
            destroy();
            return FALSE;
        }

        RwBBox worldBbox;
        worldBbox.sup.z = 100000.f;
        worldBbox.sup.y = 100000.f;
        worldBbox.sup.x = 100000.f;

        worldBbox.inf.z = -100000.f;
        worldBbox.inf.y = -100000.f;
        worldBbox.inf.x = -100000.f;

        world = RpWorldCreate(&worldBbox);
        if (world == NULL)
        {
            destroy();
            return FALSE;
        }

        RpWorldAddCamera(world, cam);
        _rwObjectHasFrameSetFrame(cam, RwFrameCreate());

        xVec2 windowSize;
        windowSize.x = 1.0;
        windowSize.y = 1.0;
        RwCameraSetViewWindow(cam, (const RwV2d*)&windowSize);
        RwCameraSetProjection(cam, rwPERSPECTIVE);

        if (cam->object.object.parent == NULL)
        {
            destroy();
            return FALSE;
        }

        raster =
            RwRasterCreate(width, height, 32, rwRASTERGAMMACORRECTED | rwRASTERPIXELLOCKEDWRITE);
        if (raster == NULL)
        {
            destroy();
            return FALSE;
        }
        cam->frameBuffer = raster;
        if (cam == NULL)
        {
            destroy();
            return FALSE;
        }
        texture = RwTextureCreate(raster);
        if (texture == NULL)
        {
            destroy();
            return FALSE;
        }
        texture->filterAddressing = (texture->filterAddressing & 0xFFFFFF00) | rwFILTERLINEAR;

        RwCameraSetNearClipPlane(cam, 0.3f);
        RwCameraSetFarClipPlane(cam, 10000.0f);

        return TRUE;
    }

    void television::destroy()
    {
        if (texture != NULL)
        {
            RwTextureDestroy(texture);
        }
        else if (raster != NULL)
        {
            RwRasterDestroy(raster);
        }
        if (cam != NULL)
        {
            RwFrame* cam_frame = (RwFrame*)cam->object.object.parent;
            if (cam_frame != NULL)
            {
                _rwObjectHasFrameSetFrame(cam, NULL);
                RwFrameDestroy(cam_frame);
            }
            RpWorldRemoveCamera(world, cam);
            RwCameraDestroy(cam);
        }
        if (world != NULL)
        {
            RpWorldDestroy(world);
        }
        cam = NULL;
        raster = NULL;
        world = NULL;
    }
} // namespace

void zNPCPrawn::Setup()
{
    zNPCSubBoss::Setup();
}

namespace
{
    void television::set_background(iColor_tag color)
    {
        this->bgraster = 0;
        this->bgcolor.red = color.r;
        this->bgcolor.green = color.g;
        this->bgcolor.blue = color.b;
        this->bgcolor.alpha = color.a;
    }
} // namespace

void zNPCPrawn::Reset()
{
    reset_sound();
    zNPCCommon::Reset();
    memset(&this->flag, 0, 1);
    decompose();
    reappear();
    show_model();

    this->life = this->cfg_npc->pts_damage;
    this->face_player = 1;

    for (S32 i = 0; i < 3; i++)
    {
        if (this->spawner[i] != NULL)
        {
            this->spawner[i]->Reset();
        }
    }

    update_round();

    this->turn.accel = tweak.turn_accel;
    this->turn.max_vel = this->cfg_npc->spd_turnMax;
    this->floor_state_index = 0;
    this->fighting = 0;
    this->first_update = 1;
    this->beam.reset();

    exhaust_emitter = zParEmitterFind("PAREMIT_PRAWN_EXHAUST");
    exhaust_emitter_settings.custom_flags = 0x300;
    exhaust_emitter_settings.pos = g_O3;
    exhaust_emitter_settings.vel = g_O3;

    this->psy_instinct->GoalSet(NPC_GOAL_PRAWNIDLE, 1);
}

void zNPCPrawn::Destroy()
{
    zNPCCommon::Destroy();
    closeup.destroy();
}

void zNPCPrawn::Process(xScene* xscn, F32 dt)
{
    if (this->first_update)
    {
        set_floor_state(FS_SAFE, true, true);
        this->first_update = 0;
    }

    repel_player();

    for (S32 i = 0; i < 3; i++)
    {
        if (this->spawner[i] != NULL)
        {
            this->spawner[i]->Timestep(dt);
        }
    }

    if (!this->fighting || this->psy_instinct->GIDOfActive() == NPC_GOAL_LIMBO)
    {
        zNPCCommon::Process(xscn, dt);
        return;
    }

    this->delay += dt;
    this->psy_instinct->Timestep(dt, NULL);

    if (this->face_player)
    {
        xVec3& ppos = *(xVec3*)&globals.player.ent.model->Mat->pos;
        xVec3& center = get_center();
        this->look_dir.assign(ppos.x - center.x, ppos.z - center.z);
        this->look_dir.normalize();
    }

    update_turn(dt);
    update_animation(dt);
    update_floor(dt);
    update_beam(dt);
    update_particles(dt);
    check_player_damage();
    update_camera(dt);

    zNPCCommon::Process(xscn, dt);
}

void zNPCPrawn::NewTime(xScene* xscn, float dt)
{
    zNPCCommon::NewTime(xscn, dt);
    zNPCPrawn::render_closeup();
}

S32 zNPCPrawn::SysEvent(xBase* from, xBase* to, U32 toEvent, const F32* toParam,
                        xBase* toParamWidget, S32* handled)
{
    switch ((S32)toEvent)
    {
    case 0x134:
    case 0x135:
    case 0x136:
    case 0x137:
    case 0x138:
        break;
    case 0x1b5:
        start_fight();
        break;
    case 0x1d9:
        this->psy_instinct->GoalSet(NPC_GOAL_PRAWNDEATH, 1);
        break;
    default:
        *handled = 0;
        return zNPCCommon::SysEvent(from, to, toEvent, toParam, toParamWidget, handled);
    }

    return 1;
}

namespace
{
    S32 load_patterns(xModelAssetParam* ap, U32 apsize, const char* fmt,
                      zNPCPrawn::range_type* pattern, S32 count)
    {
        char name[140];
        S32 i;

        for (i = 0; i < count; pattern++, i++)
        {
            sprintf(name, fmt, i);
            strcat(name, ".min");
            pattern->min = zParamGetInt(ap, apsize, name, pattern->min);

            sprintf(name, fmt, i);
            strcat(name, ".max");
            pattern->max = zParamGetInt(ap, apsize, name, pattern->max);

            if (pattern->min < 0 || pattern->max < 0)
            {
                break;
            }
        }
        return i;
    }
} // namespace

void zNPCPrawn::ParseINI()
{
    zNPCCommon::ParseINI();
    tweak.load(this->parmdata, this->pdatsize);
    tweak.turn_accel = zParamGetFloat(this->parmdata, this->pdatsize, "turn_accel", 5.0f);
    tweak.spawn_delay = zParamGetFloat(this->parmdata, this->pdatsize, "spawn_delay", 1.0f);
    tweak.repel_radius = zParamGetFloat(this->parmdata, this->pdatsize, "repel_radius", 4.2f);
    tweak.safe.pattern.min = zParamGetInt(this->parmdata, this->pdatsize, "safe.pattern.min", 0);
    tweak.safe.pattern.max = zParamGetInt(this->parmdata, this->pdatsize, "safe.pattern.max", 0);
    tweak.begin.pattern.min = zParamGetInt(this->parmdata, this->pdatsize, "begin.pattern.min", 0);
    tweak.begin.pattern.max = zParamGetInt(this->parmdata, this->pdatsize, "begin.pattern.max", 1);
    tweak.begin.state_delay =
        zParamGetFloat(this->parmdata, this->pdatsize, "begin.state_delay", 0.0f);
    tweak.begin.transition_delay =
        zParamGetFloat(this->parmdata, this->pdatsize, "begin.transition_delay", 1.0f);
    tweak.beam.delay[0] = zParamGetFloat(this->parmdata, this->pdatsize, "beam.delay[0]", 2.5f);
    tweak.beam.delay[1] = zParamGetFloat(this->parmdata, this->pdatsize, "beam.delay[1]", 4.5f);
    tweak.beam.delay[2] = zParamGetFloat(this->parmdata, this->pdatsize, "beam.delay[2]", 6.5f);
    tweak.beam.pattern.min = zParamGetInt(this->parmdata, this->pdatsize, "beam.pattern.min", 3);
    tweak.beam.pattern.max = zParamGetInt(this->parmdata, this->pdatsize, "beam.pattern.max", 0x13);
    tweak.beam.state_delay =
        zParamGetFloat(this->parmdata, this->pdatsize, "beam.state_delay", 0.05f);
    tweak.beam.transition_delay =
        zParamGetFloat(this->parmdata, this->pdatsize, "beam.transition_delay", 0.05f);
    tweak.beam.exhaust_vel =
        zParamGetFloat(this->parmdata, this->pdatsize, "beam.exhaust_vel", 15.0f);
    tweak.beam.knock_back = zParamGetFloat(this->parmdata, this->pdatsize, "beam.knock_back", 1.0f);
    tweak.beam.sweep.amount[0] =
        zParamGetInt(this->parmdata, this->pdatsize, "beam.sweep.amount[0]", 2);
    tweak.beam.sweep.amount[1] =
        zParamGetInt(this->parmdata, this->pdatsize, "beam.sweep.amount[1]", 3);
    tweak.beam.sweep.amount[2] =
        zParamGetInt(this->parmdata, this->pdatsize, "beam.sweep.amount[2]", 4);
    tweak.beam.sweep.arc = zParamGetFloat(this->parmdata, this->pdatsize, "beam.sweep.arc", 20.0f);
    tweak.beam.sweep.arc = zParamGetFloat(this->parmdata, this->pdatsize, "beam.sweep.delay", 0.5f);
    tweak.beam.sweep.arc =
        zParamGetFloat(this->parmdata, this->pdatsize, "beam.sweep.accel", 60.0f);
    tweak.beam.sweep.arc =
        zParamGetFloat(this->parmdata, this->pdatsize, "beam.sweep.max_vel", 26.5f);
    tweak.beam.sweep.arc =
        zParamGetFloat(this->parmdata, this->pdatsize, "beam.fire.ring.size", 0.4f);
    tweak.beam.sweep.arc =
        zParamGetFloat(this->parmdata, this->pdatsize, "beam.fire.ring.alpha", 1.0f);
    tweak.beam.sweep.arc =
        zParamGetFloat(this->parmdata, this->pdatsize, "beam.fire.ring.vel", 9.0f);
    tweak.beam.sweep.arc =
        zParamGetFloat(this->parmdata, this->pdatsize, "beam.fire.ring.accel", 10.0f);
    tweak.beam.sweep.arc =
        zParamGetFloat(this->parmdata, this->pdatsize, "beam.fire.ring.emit_delay", 0.1f);
    tweak.beam.sweep.arc =
        zParamGetFloat(this->parmdata, this->pdatsize, "beam.fire.ring.grow", 0.15f);
    tweak.beam.sweep.arc =
        zParamGetFloat(this->parmdata, this->pdatsize, "beam.fire.ring.fade_dist", 15.0f);
    tweak.beam.sweep.arc =
        zParamGetFloat(this->parmdata, this->pdatsize, "beam.fire.ring.kill_dist", 20.0f);
    tweak.beam.sweep.arc =
        zParamGetFloat(this->parmdata, this->pdatsize, "beam.fire.ring.follow", 0.0f);
    tweak.beam.sweep.arc =
        zParamGetFloat(this->parmdata, this->pdatsize, "beam.fire.ring.hit_radius", 0.3f);
    tweak.beam.fire.ring.hit_offset =
        zParamGetVector(this->parmdata, this->pdatsize, "beam.fire.ring.hit_offset",
                        xVec3::create(0.0f, 0.0f, 0.0f), &tweak.beam.fire.ring.hit_offset);
    tweak.beam.fire.emit_bone =
        zParamGetInt(this->parmdata, this->pdatsize, "beam.fire.emit_bone", 0x2b);
    tweak.beam.fire.offset =
        zParamGetVector(this->parmdata, this->pdatsize, "beam.fire.offset",
                        xVec3::create(0.0f, 0.0f, 0.0f), &tweak.beam.fire.offset);
    tweak.beam.fire.yaw = zParamGetFloat(this->parmdata, this->pdatsize, "beam.fire.yaw", 0.0f);
    tweak.beam.fire.pitch = zParamGetFloat(this->parmdata, this->pdatsize, "beam.fire.pitch", 5.5f);
    isin(tweak.beam.fire.pitch);
    icos(tweak.beam.fire.pitch);
    tweak.aim_lane.duration =
        zParamGetFloat(this->parmdata, this->pdatsize, "aim_lane.duration", 2.0f);
    tweak.aim_lane.state_delay =
        zParamGetFloat(this->parmdata, this->pdatsize, "aim_lane.state_delay", 0.1f);
    tweak.aim_lane.transition_delay =
        zParamGetFloat(this->parmdata, this->pdatsize, "aim_lane.transition_delay", 0.1f);
    tweak.aim_lane.pattern.first =
        zParamGetInt(this->parmdata, this->pdatsize, "aim_lane.pattern.first", 0x15);
    tweak.aim_lane.pattern.range =
        zParamGetInt(this->parmdata, this->pdatsize, "aim_lane.pattern.range", 0x8);
    tweak.aim_lane.pattern.offset =
        zParamGetInt(this->parmdata, this->pdatsize, "aim_lane.pattern.offset", 0x9);
    tweak.aim_lane.pattern.size =
        zParamGetInt(this->parmdata, this->pdatsize, "aim_lane.pattern.size", 0x4);
    tweak.lane.duration[0] =
        zParamGetFloat(this->parmdata, this->pdatsize, "lane.duration[0]", 8.0f);
    tweak.lane.duration[1] =
        zParamGetFloat(this->parmdata, this->pdatsize, "lane.duration[1]", 7.0f);
    tweak.lane.duration[2] =
        zParamGetFloat(this->parmdata, this->pdatsize, "lane.duration[2]", 6.0f);
    tweak.lane.state_delay =
        zParamGetFloat(this->parmdata, this->pdatsize, "lane.state_delay", 0.0f);
    tweak.lane.transition_delay =
        zParamGetFloat(this->parmdata, this->pdatsize, "lane.transition_delay", 0.1f);
    tweak.lane.pattern.first =
        zParamGetInt(this->parmdata, this->pdatsize, "lane.pattern.first", 0x39);
    tweak.lane.pattern.range =
        zParamGetInt(this->parmdata, this->pdatsize, "lane.pattern.range", 0x8);
    tweak.lane.pattern.offset =
        zParamGetInt(this->parmdata, this->pdatsize, "lane.pattern.offset", 0x9);
    tweak.lane.pattern.size =
        zParamGetInt(this->parmdata, this->pdatsize, "lane.pattern.size", 0x4);
    tweak.danger.state_delay =
        zParamGetFloat(this->parmdata, this->pdatsize, "danger.state_delay", 0.2f);
    tweak.danger.transition_delay =
        zParamGetFloat(this->parmdata, this->pdatsize, "danger.transition_delay", 0.2f);
    tweak.danger.cycle_delay =
        zParamGetFloat(this->parmdata, this->pdatsize, "danger.cycle_delay", 6.0f);
    tweak.danger.pattern_offset =
        zParamGetInt(this->parmdata, this->pdatsize, "danger.pattern_offset", 0x5d);
}

void tweak_group::load(xModelAssetParam* p, U32 i)
{
    this->register_tweaks(TRUE, p, i, 0);
}

/*

TODO: 42%, needs quite a bit of work still.
Thought this function was going to be an easy copy/paste like other register_tweak fn,
but turning out to be harder to decipher than I anticipated so leaving it like this for now.
*/
void tweak_group::register_tweaks(bool init, xModelAssetParam* ap, U32 apsize, const char* c)
{
    if (init)
    {
        this->sound[0].volume = 0;
        auto_tweak::load_param<F32, F32>(this->sound[0].volume, 1.0f, 0.0, 1.0f, ap, apsize,
                                         "TODO1");
    }

    if (init)
    {
        this->sound[0].range_inner = 0;
        auto_tweak::load_param<F32, F32>(this->sound[0].range_inner, 1.0f, 0.0, 1.0f, ap, apsize,
                                         "TODO2");
    }
    if (init)
    {
        this->sound[0].range_outer = 0;
        auto_tweak::load_param<F32, F32>(this->sound[0].range_outer, 1.0f, 0.0, 1.0f, ap, apsize,
                                         "TODO3");
    }

    if (init)
    {
        this->sound[0].delay = 0;
        auto_tweak::load_param<F32, F32>(this->sound[0].delay, 1.0f, 0.0, 1.0f, ap, apsize,
                                         "TODO4");
    }
    if (init)
    {
        this->sound[1].volume = 0;
        auto_tweak::load_param<F32, F32>(this->sound[1].volume, 1.0f, 0.0, 1.0f, ap, apsize,
                                         "TODo5");
    }
    if (init)
    {
        this->sound[1].range_inner = 0;
        auto_tweak::load_param<F32, F32>(this->sound[1].range_inner, 1.0f, 0.0, 1.0f, ap, apsize,
                                         "TODo6");
    }

    if (init)
    {
        this->sound[1].range_outer = 0;
        auto_tweak::load_param<F32, F32>(this->sound[1].range_outer, 1.0f, 0.0, 1.0f, ap, apsize,
                                         "todo7");
    }
    if (init)
    {
        this->sound[1].delay = 0;
        auto_tweak::load_param<F32, F32>(this->sound[1].delay, 1.0f, 0.0, 1.0f, ap, apsize,
                                         "todo8");
    }
    if (init)
    {
        this->sound[2].volume = 0;
        auto_tweak::load_param<F32, F32>(this->sound[2].volume, 1.0f, 0.0, 1.0f, ap, apsize,
                                         "todo9");
    }
    if (init)
    {
        this->sound[2].range_inner = 0;
        auto_tweak::load_param<F32, F32>(this->sound[2].range_inner, 1.0f, 0.0, 1.0f, ap, apsize,
                                         "todo10");
    }

    if (init)
    {
        this->sound[2].range_outer = 0;
        auto_tweak::load_param<F32, F32>(this->sound[2].range_outer, 1.0f, 0.0, 1.0f, ap, apsize,
                                         "todo11");
    }
    if (init)
    {
        this->sound[2].delay = 0;
        auto_tweak::load_param<F32, F32>(this->sound[2].delay, 1.0f, 0.0, 1.0f, ap, apsize,
                                         "todo12");
    }
    if (init)
    {
        this->sound[3].volume = 0;
        auto_tweak::load_param<F32, F32>(this->sound[3].volume, 1.0f, 0.0, 1.0f, ap, apsize,
                                         "todo13");
    }
    if (init)
    {
        this->sound[3].range_inner = 0;
        auto_tweak::load_param<F32, F32>(this->sound[3].range_inner, 1.0f, 0.0, 1.0f, ap, apsize,
                                         "todo14");
    }

    if (init)
    {
        this->sound[3].range_outer = 0;
        auto_tweak::load_param<F32, F32>(this->sound[3].range_outer, 1.0f, 0.0, 1.0f, ap, apsize,
                                         "todo15");
    }
    if (init)
    {
        this->sound[3].delay = 0;
        auto_tweak::load_param<F32, F32>(this->sound[3].delay, 1.0f, 0.0, 1.0f, ap, apsize,
                                         "todo16");
    }

    if (init)
    {
        this->sound[3].delay = 0;
        auto_tweak::load_param<F32, F32>(this->sound[3].delay, 1.0f, 0.0, 1.0f, ap, apsize,
                                         "todo17");
    }

    if (init)
    {
        sound_data[0].volume = sound[0].volume;
        sound_data[0].id = sound_asset_ids[0][0] = xStrHash("TES1");
    }
    if (init)
    {
        sound_data[1].volume = sound[1].volume;
        sound_data[1].id = sound_asset_ids[0][1] = xStrHash("TES2");
    }
    if (init)
    {
        sound_data[2].volume = sound[2].volume;
        sound_data[2].id = sound_asset_ids[0][2] = xStrHash("TES3");
    }
    if (init)
    {
        sound_data[3].volume = sound[3].volume;
        sound_data[3].id = sound_asset_ids[0][3] = xStrHash("TES4");
    }
}

void zNPCPrawn::ParseLinks()
{
    zNPCCommon::ParseLinks();

    xLinkAsset* link = this->link;
    xLinkAsset* end = link + this->linkCount;

    for (; link != end; link++)
    {
        if (link->dstEvent == 0x133)
        {
            add_child(*zSceneFindObject(link->dstAssetID), (S32)link->param[0]);
        }
    }
}

void zNPCPrawn::SelfSetup()
{
    xBehaveMgr* bmgr;
    xPsyche* psy;

    bmgr = xBehaveMgr_GetSelf();
    psy_instinct = bmgr->Subscribe(this, 0);
    psy = psy_instinct;
    psy->BrainBegin();
    psy->AddGoal(NPC_GOAL_PRAWNIDLE, NULL);
    psy->AddGoal(NPC_GOAL_PRAWNBEAM, NULL);
    psy->AddGoal(NPC_GOAL_PRAWNBOWL, NULL);
    psy->AddGoal(NPC_GOAL_PRAWNDAMAGE, NULL);
    psy->AddGoal(NPC_GOAL_PRAWNDEATH, NULL);
    psy->AddGoal(NPC_GOAL_LIMBO, NULL);
    psy->BrainEnd();
    psy->SetSafety(NPC_GOAL_PRAWNIDLE);
}

void zNPCPrawn::Damage(en_NPC_DAMAGE_TYPE damage_type, xBase* base, const xVec3* vec)
{
    S32 active_id = this->psy_instinct->GIDOfActive();
    switch (damage_type)
    {
    case DMGTYP_BOULDER:
    case DMGTYP_BUBBOWL:
        if (active_id == NPC_GOAL_PRAWNBOWL)
        {
            set_life(this->life - 1);
        }
        break;
    default:
        break;
    }
}

void zNPCPrawn::DuploNotice(en_SM_NOTICES note, void* data)
{
    for (S32 i = 0; i < 3; i++)
    {
        if (this->spawner[i] != NULL && this->spawner[i]->Receivable(note, data))
        {
            this->spawner[i]->Notify(note, data);
            return;
        }
    }
}

U32 zNPCPrawn::AnimPick(S32 gid, en_NPC_GOAL_SPOT gspot, xGoal* goal)
{
    U32 hash = 0;
    S32 anim;

    switch (gid)
    {
    case NPC_GOAL_PRAWNIDLE:
    case NPC_GOAL_PRAWNBOWL:
        anim = ANIM_Idle01;
        break;
    case NPC_GOAL_PRAWNBEAM:
        anim = ANIM_AttackWindup01;
        break;
    case NPC_GOAL_PRAWNDAMAGE:
        anim = ANIM_Damage01;
        break;
    case NPC_GOAL_PRAWNDEATH:
        anim = -1;
        break;
    default:
        anim = ANIM_Idle01;
        break;
    }

    if (anim > -1)
    {
        hash = g_hash_subbanim[anim];
    }
    return hash;
}

void zNPCPrawn::Render()
{
    xNPCBasic::Render();
    isCulled = 0;
    beam.render();
    zNPCPrawn::render_debug();
}

void zNPCPrawn::update_round()
{
    S32 life = this->life;
    if (life == 0)
    {
        this->round = 3;
    }
    else
    {
        this->round = 2 - (S32)((life - 1) * 3) / this->cfg_npc->pts_damage;
    }
    S32 spawnerCount = sizeof(this->spawner) / sizeof(*this->spawner);
    for (S32 spawnerIndex = 0; spawnerIndex < spawnerCount; spawnerIndex++)
    {
        zNPCSpawner* current_spawner = this->spawner[spawnerIndex];
        if (current_spawner != NULL)
        {
            en_SM_NOTICES spawnerNotice = SM_NOTE_DUPRESUME;
            if (spawnerIndex > this->round)
            {
                spawnerNotice = SM_NOTE_DUPPAUSE;
            }
            current_spawner->Notify(spawnerNotice, NULL);
        }
    }
}

void zNPCPrawn::decompose()
{
    vanish();

    if (this->fighting != 0)
    {
        this->fighting = 0;
        set_floor_state(FS_SAFE, TRUE, TRUE);
        hide_model();

        S32 i = 0;
        do
        {
            if (this->spawner[i] != NULL)
            {
                this->spawner[i]->Notify(SM_NOTE_DUPDEAD, NULL);
                this->spawner[i]->Notify(SM_NOTE_KILLKIDS, NULL);
            }
            i++;
        } while (i < 3);

        zCameraEnableTracking(CO_BOSS);
        boss_cam.stop();
    }
}

zNPCSpawner* zNPCPrawn::make_spawner(S32 i)
{
    zNPCSpawner* spawner = this->spawner[i];
    if (spawner != NULL)
    {
        return spawner;
    }
    this->spawner[i] = zNPCSpawner_GetInstance();
    this->spawner[i]->Subscribe(this);
    this->spawner[i]->SetWaveMode(SM_WAVE_CONTINUOUS, tweak.spawn_delay, -1);
    return this->spawner[i];
}

void zNPCPrawn::add_child(xBase& child, S32 which)
{
    switch (child.baseType)
    {
    case eBaseTypeNPC:
        make_spawner(which)->AddSpawnNPC((zNPCCommon*)&child);
        break;
    case eBaseTypeMovePoint:
        make_spawner(which)->AddSpawnPoint((zMovePoint*)&child);
        break;
    case eBaseTypeGroup:
    {
        U32 i = 0;
        U32 count = xGroupGetCount((xGroup*)&child);
        for (; i < count; i++)
        {
            xBase* item = xGroupGetItemPtr((xGroup*)&child, i);
            add_child(*item, which);
        }
        break;
    }
    default:
        break;
    }
}

namespace
{
    void set_yaw_matrix(xMat3x3& mat, F32 yaw)
    {
        F32 s = isin(yaw);
        F32 c = icos(yaw);

        mat.right.assign(c, 0.0f, -s);
        mat.up.assign(0.0f, 1.0f, 0.0f);
        mat.at.assign(s, 0.0f, c);
    }
} // namespace

void zNPCPrawn::update_turn(F32 dt)
{
    get_center();

    RwMatrix* mat = this->model->Mat;
    F32 at_x = mat->at.x;
    F32 at_z = mat->at.z;

    if (!turning())
    {
        return;
    }

    F32 cur = xatan2(at_x, at_z);
    F32 diff = xatan2(this->look_dir.x, this->look_dir.y) - cur;
    if (diff > 3.1415927f)
    {
        diff -= 6.2831855f;
    }
    else if (diff < -3.1415927f)
    {
        diff += 6.2831855f;
    }

    bool decel = true;
    if (0.001f <= FABS(this->turn.vel) && (diff < 0.0f) == (this->turn.vel < 0.0f))
    {
        decel = false;
    }

    F32 time_to_target = 1.0e38f;
    if (!decel)
    {
        time_to_target = diff / this->turn.vel;
    }

    F32 time_to_stop = FABS(this->turn.vel / this->turn.accel);

    F32 dir = -1.0f;
    if (time_to_stop < time_to_target)
    {
        dir = 1.0f;
    }

    F32 sign = -1.0f;
    if (diff >= 0.0f)
    {
        sign = 1.0f;
    }

    F32 max_vel = this->turn.max_vel;
    F32 dvel = this->turn.accel * dir * sign * dt;
    F32 vel = this->turn.vel + dvel;

    if (max_vel < FABS(vel))
    {
        if (max_vel < FABS(this->turn.vel))
        {
            if ((vel < 0.0f) != (dvel < 0.0f))
            {
                this->turn.vel = vel;
            }
        }
        else
        {
            this->turn.vel = range_limit<F32>(vel, -max_vel, max_vel);
        }
    }
    else
    {
        this->turn.vel = vel;
    }

    F32 step = this->turn.vel * dt;
    if (time_to_target <= time_to_stop)
    {
        if ((step < 0.0f) != (diff < 0.0f))
        {
            this->turn.vel = 0.0f;
            step = diff;
        }
    }
    else if ((step < 0.0f) == (diff < 0.0f) && FABS(diff) < FABS(step))
    {
        this->turn.vel = 0.0f;
        step = diff;
    }

    set_yaw_matrix(*(xMat3x3*)this->frame, cur + step);
}

void zNPCPrawn::update_animation(F32 dt)
{
}

void zNPCPrawn::update_floor(F32 dt)
{
    if (this->floor_state == FS_DANGER && !this->pending.change)
    {
        this->floor_time += dt;
        if (this->floor_time >= tweak.danger.cycle_delay)
        {
            set_floor_state(FS_DANGER, false, true);
            this->floor_time = 0.0f;
        }
    }

    if (this->pending.change && this->disco->state_counter >= this->pending.counter)
    {
        apply_pending();
    }
}

void zNPCPrawn::update_beam(F32 dt)
{
    xMat4x3* mat = (xMat4x3*)this->model->Mat;
    xMat4x3& bone = ((xMat4x3*)mat)[tweak.beam.fire.emit_bone];

    xVec3 loc = mat->pos + bone.pos + tweak.beam.fire.offset;

    xVec3 dir;
    mulat(dir, *(xMat3x3*)mat, *(xMat3x3*)&bone);
    dir.y = -this->precomp.sin_pitch;

    F32 yaw = xatan2(dir.x, dir.z) + tweak.beam.fire.yaw;
    dir.x = isin(yaw) * this->precomp.cos_pitch;
    dir.z = icos(yaw) * this->precomp.cos_pitch;

    this->beam.move(loc, dir);

    if (this->beam.active())
    {
        this->beam.update(dt);
        exhaust_emitter_settings.pos = loc;
        exhaust_emitter_settings.vel = dir * tweak.beam.exhaust_vel;
        xParEmitterEmitCustom(exhaust_emitter, dt, &exhaust_emitter_settings);
    }
}

namespace
{
    void mulat(xVec3& out, const xMat3x3& m, const xMat3x3& n)
    {
        out.x = m.at.x * n.right.x + m.at.y * n.up.x + m.at.z * n.at.x;
        out.y = m.at.x * n.right.y + m.at.y * n.up.y + m.at.z * n.at.y;
        out.z = m.at.x * n.right.z + m.at.y * n.up.z + m.at.z * n.at.z;
    }
} // namespace

void zNPCPrawn::update_particles(float)
{
}

void zNPCPrawn::update_camera(F32 dt)
{
    zCameraDisableTracking(CO_BOSS);
    if (!(zCameraIsTrackingDisabled() & ~CO_BOSS))
    {
        boss_cam.update(dt);
    }
}

void zNPCPrawn::start_fight()
{
    if (!this->fighting)
    {
        this->fighting = 1;
        set_floor_state(FS_BEGIN, true, false);
        zCameraDisableTracking(CO_BOSS);
        boss_cam.start(globals.camera);
        boss_cam.set_targets(*(xVec3*)&globals.player.ent.model->Mat->pos, this->bound.sph.center,
                             this->bound.sph.r);
    }
}

void zNPCPrawn::get_floor_info(zNPCPrawn::floor_state_enum state, zNPCPrawn::range_type& pattern,
                               F32& transition_delay, F32& state_delay)
{
    switch (state)
    {
    case FS_SAFE:
        pattern = *(zNPCPrawn::range_type*)&tweak.safe.pattern;
        transition_delay = 0.0f;
        state_delay = 1000000000.0f;
        break;
    case FS_BEGIN:
        pattern = *(zNPCPrawn::range_type*)&tweak.begin.pattern;
        transition_delay = tweak.begin.transition_delay;
        state_delay = tweak.begin.state_delay;
        break;
    case FS_BEAM:
        pattern = *(zNPCPrawn::range_type*)&tweak.beam.pattern;
        transition_delay = tweak.beam.transition_delay;
        state_delay = tweak.beam.state_delay;
        break;
    case FS_AIM_LANE:
        this->floor_state_index = xrand() % tweak.aim_lane.pattern.size;
        pattern.min =
            tweak.aim_lane.pattern.first + this->floor_state_index * tweak.aim_lane.pattern.offset;
        pattern.max = pattern.min + tweak.aim_lane.pattern.range - 1;
        transition_delay = tweak.aim_lane.transition_delay;
        state_delay = tweak.aim_lane.state_delay;
        break;
    case FS_LANE:
        pattern.min =
            tweak.lane.pattern.first + this->floor_state_index * tweak.lane.pattern.offset;
        pattern.max = pattern.min + tweak.lane.pattern.range - 1;
        transition_delay = tweak.lane.transition_delay;
        state_delay = tweak.lane.state_delay;
        break;
    case FS_DANGER:
        this->floor_state_index = xrand() % tweak.danger.pattern_size;
        pattern = *(zNPCPrawn::range_type*)&tweak.danger.pattern[this->floor_state_index];
        pattern.min += tweak.danger.pattern_offset;
        pattern.max += tweak.danger.pattern_offset;
        transition_delay = tweak.danger.transition_delay;
        state_delay = tweak.danger.state_delay;
        break;
    }
}

void zNPCPrawn::apply_pending()
{
    pending.change = 0;
    disco->set_state_range(pending.pattern.min, pending.pattern.max, SM_NPC_DEAD);
    disco->set_transition_delay(pending.transition_delay);
    disco->set_state_delay(pending.state_delay);
}

void zNPCPrawn::set_floor_state(zNPCPrawn::floor_state_enum state, bool immediate, bool force)
{
    if (state != this->floor_state || force)
    {
        this->pending.floor_state = state;
        get_floor_info(state, this->pending.pattern, this->pending.transition_delay,
                       this->pending.state_delay);

        if (immediate)
        {
            apply_pending();
            return;
        }

        z_disco_floor* disco = this->disco;
        U32 state_now = disco->state;
        U32 offset;
        if (state_now < disco->min_state || state_now > disco->max_state)
        {
            offset = 1;
        }
        else
        {
            offset = (disco->max_state - state_now) + 1;
        }
        this->pending.counter = disco->state_counter + offset;
        this->pending.change = 1;
    }
}

void zNPCPrawn::vanish()
{
    this->model->Flags &= 0xFFFC;
    this->flags &= 0xFE;
    this->flags |= 0x40;
    this->pflags = 0;
    this->moreFlags = 0;
    this->chkby = 0;
    this->penby = 0;
    this->flags2.flg_colCheck = 0;
    this->flags2.flg_penCheck = 0;
}

void zNPCPrawn::reappear()
{
    xModelInstance* temp_r6;

    temp_r6 = this->model;
    temp_r6->Flags |= 3;
    this->flags |= 1;
    this->flags &= 0xBF;
    this->pflags = 0;
    this->moreFlags = 0x10;
    this->chkby = 0x10;
    this->penby = 0x10;
    this->flags2.flg_colCheck = 0;
    this->flags2.flg_penCheck = 0;
}

void zNPCPrawn::render_closeup()
{
}

namespace
{
    void television::set_model_texture(xModelInstance& model)
    {
        for (S32 bucketSlot = 0; bucketSlot < 2; bucketSlot++)
        {
            RpGeometry* modelGeometry = model.Bucket[bucketSlot]->Data->geometry;
            if (modelGeometry == 0 || modelGeometry->matList.numMaterials <= 0)
            {
                break;
            }
            RpMaterialSetTexture(modelGeometry->matList.materials[0], this->texture);
        }
    }

    void television::update(xModelInstance& model_inst, xLightKit* light_kit)
    {
        RwCamera* globalCamera = (RwCamera*)(RwEngineInstance->curCamera);
        if (globalCamera != NULL)
        {
            RwCameraEndUpdate(globalCamera);
            RwGameCubeCameraTextureFlush(globalCamera->frameBuffer, 0);
        }
        if (this->bgraster == NULL)
        {
            RwCameraClear(this->cam, &this->bgcolor, rwCAMERACLEARIMAGE | rwCAMERACLEARZ);
        }
        else
        {
            RwCameraClear(this->cam, &this->bgcolor, rwCAMERACLEARZ);
        }
        RwCameraBeginUpdate(this->cam);
        if (this->bgraster != NULL)
        {
            render_background();
        }
        zRenderState(SDRS_Default);

        if (light_kit != NULL)
        {
            xLightKit_Enable(light_kit, this->world);
        }
        xModelInstance* currentInstance = &model_inst;

        while (currentInstance != NULL)
        {
            if (currentInstance->Flags & 1)
            {
                iModelRender(currentInstance->Data, currentInstance->Mat);
            }
            currentInstance = currentInstance->Next;
        }
        if (light_kit != NULL)
        {
            // Doesn't use light_kit, but does a nullcheck?
            xLightKit_Enable(NULL, this->world);
        }
        render_static();
        RwCameraEndUpdate(this->cam);
        RwGameCubeCameraTextureFlush(this->cam->frameBuffer, 0);
        if (globalCamera != NULL)
        {
            RwCameraBeginUpdate(globalCamera);
        }
    }

    void television::render_static()
    {
    }

    void television::render_background()
    {
        zRenderState(SDRS_Fill);
        RwRenderStateSet(rwRENDERSTATETEXTURERASTER, this->bgraster);
    }

    void television::set_vert(rwGameCube2DVertex& vert, F32 x, F32 y, F32 u, F32 v)
    {
        vert.x = x;
        vert.y = y;
        vert.z = 1.0f;
        vert.u = u;
        vert.v = v;
        vert.emissiveColor.red = 0xff;
        vert.emissiveColor.green = 0xff;
        vert.emissiveColor.blue = 0xff;
        vert.emissiveColor.alpha = 0xff;
    }

    void television::move(const xVec3& v1, const xVec3& v2)
    {
        RwFrameTranslate((RwFrame*)this->cam->object.object.parent, (const RwV3d*)&v1,
                         rwCOMBINEREPLACE);
        xMat3x3LookAt((xMat3x3*)this->cam->object.object.parent, &v2, &v1);
    }
} // namespace

void zNPCPrawn::set_life(S32 amount)
{
    S32 old_life = this->life;

    this->life = range_limit<S32>(amount, 0, this->cfg_npc->pts_damage);

    S32 gid = this->psy_instinct->GIDOfActive();
    if (gid == NPC_GOAL_PRAWNDAMAGE || gid == NPC_GOAL_PRAWNDEATH || old_life <= this->life)
    {
        update_round();
    }
    else
    {
        this->psy_instinct->GoalSet(NPC_GOAL_PRAWNDAMAGE, 1);
        play_sound(0, &this->bound.sph.center, 1.0f);

        for (S32 i = this->life; i < old_life; i++)
        {
            zEntEvent((xBase*)this, (xBase*)this, 0x1d7);
        }

        if (this->life < 1)
        {
            zEntEvent((xBase*)this, (xBase*)this, 0x24);
        }
    }
}

bool zNPCPrawn::check_player_damage()
{
    if (globals.player.cheat_mode)
    {
        return false;
    }

    if (!this->beam.hits_sphere(globals.player.ent.bound.sph))
    {
        return false;
    }

    xVec3 knock = get_away() * tweak.beam.knock_back;
    zEntPlayer_Damage((xBase*)this, 1, &knock);
    return true;
}

xVec3 zNPCPrawn::get_away() const
{
    xVec3 away;

    away.x = globals.player.ent.bound.sph.center.x - this->bound.sph.center.x;
    away.z = globals.player.ent.bound.sph.center.z - this->bound.sph.center.z;
    away.y = 0.0f;

    F32 dist2 = away.x * away.x + away.z * away.z;
    if (dist2 >= 0.001f)
    {
        F32 scale = 0.70710677f * (1.0f / xsqrt(dist2));
        away.assign(away.x * scale, 0.70710677f, away.z * scale);
    }
    else
    {
        away.assign(0.0f, 1.0f, 0.0f);
    }

    return away;
}

void zNPCPrawn::repel_player() const
{
    if (globals.player.cheat_mode)
    {
        return;
    }

    xVec3 center = get_center();
    xVec3* vel = &globals.player.ent.frame->vel;
    xVec3* pos = (xVec3*)&globals.player.ent.model->Mat->pos;

    xVec3 away = *pos - center;
    away.y = 0.0f;

    F32 dist2 = away.length2();
    if ((dist2 < -1.0e-05f || 1.0e-05f < dist2) && dist2 < tweak.repel_radius * tweak.repel_radius)
    {
        F32 dist = xsqrt(dist2);
        xVec3 dir = away;
        dir *= 1.0f / dist;

        F32 into = vel->dot(dir);
        if (into < 0.0f)
        {
            *vel -= dir * into;
        }

        *pos += dir * (tweak.repel_radius - dist);
    }
}

void zNPCPrawn::show_model()
{
    xModelInstance* inst = this->model;
    while (inst != NULL)
    {
        inst->Flags |= 3;
        inst = inst->Next;
    }
}

void zNPCPrawn::hide_model()
{
    xModelInstance* inst = this->model;
    while (inst != NULL)
    {
        inst->Flags &= 0xFFFC;
        inst = inst->Next;
    }
}

S32 zNPCGoalPrawnIdle::Enter(float dt, void* updCtxt)
{
    zNPCPrawn& prawn = *((zNPCPrawn*)this->psyche->clt_owner);
    prawn.face_player = 1;
    prawn.delay = 0;
    prawn.set_floor_state(prawn.FS_DANGER, false, false);
    return zNPCGoalCommon::Enter(dt, updCtxt);
}

S32 zNPCGoalPrawnIdle::Exit(float dt, void* updCtxt)
{
    return xGoal::Exit(dt, updCtxt);
}

S32 zNPCGoalPrawnIdle::Process(en_trantype* trantype, F32 dt, void* updCtxt, xScene* xscn)
{
    zNPCPrawn& prawn = *((zNPCPrawn*)this->psyche->clt_owner);

    if (prawn.delay >= tweak.beam.delay[prawn.round])
    {
        *trantype = GOAL_TRAN_SET;
        return NPC_GOAL_PRAWNBEAM;
    }

    return xGoal::Process(trantype, dt, updCtxt, xscn);
}

S32 zNPCGoalPrawnBeam::Enter(F32 dt, void* updCtxt)
{
    zNPCPrawn& prawn = *((zNPCPrawn*)this->psyche->clt_owner);

    prawn.face_player = 0;
    prawn.set_floor_state(prawn.FS_BEAM, true, false);
    this->sweeps = 0;
    this->substate = SS_AIM;

    xVec3& facing = prawn.get_facing();
    RwMatrix* mat = globals.player.ent.model->Mat;
    xVec3* at = (xVec3*)&mat->at;

    if (at->length2() >= 1.0e-05f)
    {
        F32 dir = 1.0f;
        if (facing.z * at->x - at->z * facing.x < 0.0f)
        {
            dir = -1.0f;
        }
        this->sweep_dir = dir;
    }
    else
    {
        F32 dir = 1.0f;
        if (xrand() & 0x20000)
        {
            dir = -1.0f;
        }
        this->sweep_dir = dir;
    }

    init_look_dir();
    play_sound(1, &prawn.beam.loc, 1.0f);
    play_sound(2, &prawn.beam.loc, 1.0f);

    return zNPCGoalCommon::Enter(dt, updCtxt);
}

S32 zNPCGoalPrawnBeam::Exit(F32 dt, void* updCtxt)
{
    zNPCPrawn& prawn = *((zNPCPrawn*)this->psyche->clt_owner);

    prawn.beam.stop();
    prawn.turn.accel = tweak.turn_accel;
    prawn.turn.max_vel = prawn.cfg_npc->spd_turnMax;
    kill_sound(2);

    return xGoal::Exit(dt, updCtxt);
}

S32 zNPCGoalPrawnBeam::Process(en_trantype* trantype, F32 dt, void* updCtxt, xScene* xscn)
{
    switch (this->substate)
    {
    case SS_AIM:
        this->substate = (substate_enum)update_aim(dt);
        break;
    case SS_FIRE:
        this->substate = (substate_enum)update_fire(dt);
        break;
    case SS_HOLD:
        this->substate = (substate_enum)update_hold(dt);
        break;
    case SS_SWEEP:
        this->substate = (substate_enum)update_sweep(dt);
        break;
    case SS_STOP:
        this->substate = (substate_enum)update_stop(dt);
        break;
    default:
        break;
    }

    if (this->substate < MAX_SS)
    {
        return xGoal::Process(trantype, dt, updCtxt, xscn);
    }

    *trantype = GOAL_TRAN_SET;
    return NPC_GOAL_PRAWNBOWL;
}

bool zNPCGoalPrawnBeam::update_aim(F32 dt)
{
    zNPCPrawn& prawn = *((zNPCPrawn*)this->psyche->clt_owner);
    return !prawn.turning();
}

S32 zNPCGoalPrawnBeam::update_fire(F32 dt)
{
    zNPCPrawn& prawn = *((zNPCPrawn*)this->psyche->clt_owner);
    prawn.beam.start();
    prawn.delay = 0.0f;
    prawn.turn.accel = tweak.beam.sweep.accel;
    prawn.turn.max_vel = tweak.beam.sweep.max_vel;
    return SS_HOLD;
}

S32 zNPCGoalPrawnBeam::update_hold(F32 dt)
{
    zNPCPrawn& prawn = *((zNPCPrawn*)this->psyche->clt_owner);

    if (prawn.delay < tweak.beam.sweep.delay)
    {
        return SS_HOLD;
    }

    if (this->sweeps >= tweak.beam.sweep.amount[prawn.round])
    {
        prawn.AnimStart(g_hash_subbanim[ANIM_AttackEnd01], 0);
        return SS_STOP;
    }

    this->sweeps++;
    this->sweep_dir *= -1.0f;
    return SS_SWEEP;
}

S32 zNPCGoalPrawnBeam::update_sweep(F32 dt)
{
    zNPCPrawn& prawn = *((zNPCPrawn*)this->psyche->clt_owner);

    RwMatrix* mat = globals.player.ent.model->Mat;
    xVec3& center = prawn.get_center();
    xVec3& facing = prawn.get_facing();

    F32 goal = xatan2(mat->pos.x - center.x, mat->pos.z - center.z) +
               this->sweep_dir * tweak.beam.sweep.arc;
    F32 cur = xatan2(facing.x, facing.z);

    if ((xrmod(3.1415927f + (goal - cur)) - 3.1415927f < 0.0f) == (this->sweep_dir < 0.0f))
    {
        goal += (0.5f * this->sweep_dir) * tweak.beam.sweep.arc;
        prawn.look_dir.x = isin(goal);
        prawn.look_dir.y = icos(goal);
        return SS_SWEEP;
    }

    prawn.delay = 0.0f;
    prawn.look_dir.assign(facing.x, facing.z);
    prawn.look_dir.normalize();
    return SS_HOLD;
}

S32 zNPCGoalPrawnBeam::update_stop(F32 dt)
{
    zNPCPrawn& prawn = *((zNPCPrawn*)this->psyche->clt_owner);

    if (prawn.AnimCurStateID() == g_hash_subbanim[ANIM_AttackEnd01] &&
        prawn.AnimTimeRemain(NULL) < 0.001f + dt)
    {
        return SS_STOP;
    }
    return MAX_SS;
}

void zNPCGoalPrawnBeam::init_look_dir()
{
    zNPCPrawn& prawn = *((zNPCPrawn*)this->psyche->clt_owner);

    RwMatrix* mat = globals.player.ent.model->Mat;
    xVec3& center = prawn.get_center();

    xVec2 delta = { 0.0f, 0.0f };
    delta.x = mat->pos.x - center.x;
    delta.y = mat->pos.z - center.z;

    F32 goal = xatan2(delta.x, delta.y) + this->sweep_dir * tweak.beam.sweep.arc;

    prawn.look_dir.x = isin(goal);
    prawn.look_dir.y = icos(goal);
}

S32 zNPCGoalPrawnBowl::Enter(float dt, void* updCtxt)
{
    zNPCPrawn& prawn = *((zNPCPrawn*)this->psyche->clt_owner);
    prawn.face_player = 1;
    prawn.set_floor_state(prawn.FS_AIM_LANE, false, false);
    prawn.delay = 0;
    aiming = 1;
    return zNPCGoalCommon::Enter(dt, updCtxt);
}

S32 zNPCGoalPrawnBowl::Exit(float dt, void* updCtxt)
{
    zNPCPrawn& prawn = *(zNPCPrawn*)psyche->clt_owner;
    prawn.set_floor_state(prawn.FS_BEGIN, true, false);
    return xGoal::Exit(dt, updCtxt);
}

S32 zNPCGoalPrawnBowl::Process(en_trantype* trantype, F32 dt, void* updCtxt, xScene* xscn)
{
    zNPCPrawn& prawn = *((zNPCPrawn*)this->psyche->clt_owner);

    if (this->aiming)
    {
        if (prawn.delay >= tweak.aim_lane.duration)
        {
            prawn.set_floor_state(prawn.FS_LANE, false, false);
            this->aiming = 0;
            prawn.delay = 0.0f;
        }
    }
    else if (prawn.delay >= tweak.lane.duration[prawn.round])
    {
        *trantype = GOAL_TRAN_SET;
        return NPC_GOAL_PRAWNIDLE;
    }

    return xGoal::Process(trantype, dt, updCtxt, xscn);
}

S32 zNPCGoalPrawnDamage::Enter(float dt, void* updCtxt)
{
    zNPCPrawn& prawn = *(zNPCPrawn*)psyche->clt_owner;
    prawn.set_floor_state(prawn.FS_DANGER, false, false);
    return zNPCGoalCommon::Enter(dt, updCtxt);
}

S32 zNPCGoalPrawnDamage::Exit(float dt, void* updCtxt)
{
    zNPCPrawn& prawn = *(zNPCPrawn*)this->psyche->clt_owner;
    prawn.update_round();
    return xGoal::Exit(dt, updCtxt);
}

S32 zNPCGoalPrawnDamage::Process(en_trantype* trantype, F32 dt, void* updCtxt, xScene* xscn)
{
    zNPCPrawn& prawn = *((zNPCPrawn*)this->psyche->clt_owner);

    xAnimState* state = prawn.AnimCurState();
    if ((state->ID != g_hash_subbanim[ANIM_Damage01] &&
         state->ID != g_hash_subbanim[ANIM_Damage02]) ||
        prawn.AnimTimeRemain(NULL) < 0.001f + dt)
    {
        *trantype = GOAL_TRAN_SET;
        if (prawn.life <= 0)
        {
            return NPC_GOAL_PRAWNDEATH;
        }
        return NPC_GOAL_PRAWNIDLE;
    }

    return xGoal::Process(trantype, dt, updCtxt, xscn);
}

S32 zNPCGoalPrawnDeath::Enter(float dt, void* updCtxt)
{
    zNPCPrawn& prawn = *(zNPCPrawn*)this->psyche->clt_owner;
    prawn.decompose();
    return zNPCGoalCommon::Enter(dt, updCtxt);
}

S32 zNPCGoalPrawnDeath::Exit(float dt, void* updCtxt)
{
    return xGoal::Exit(dt, updCtxt);
}

S32 zNPCGoalPrawnDeath::Process(en_trantype* trantype, float dt, void* updCtxt, xScene* xscn)
{
    return xGoal::Process(trantype, dt, updCtxt, xscn);
}

void xDebugAddTweak(const char*, xVec3*, const tweak_callback*, void*, U32)
{
}

xVec3& zNPCPrawn::get_center() const
{
    return reinterpret_cast<xVec3&>(this->model->Mat->pos);
}

void zNPCPrawn::render_debug()
{
}

bool zNPCPrawn::turning() const
{
    bool result = false;
    RwMatrix* mat = this->model->Mat;

    xVec2 facing = { 0.0f, 0.0f };
    facing.x = mat->at.x;
    facing.y = mat->at.z;

    if (!(this->turn.vel >= -1.0e-05f && this->turn.vel <= 1.0e-05f) ||
        (!(this->turn.accel >= -1.0e-05f && this->turn.accel <= 1.0e-05f) &&
         (this->look_dir.x <= this->look_dir.y || !(FABS(this->look_dir.x - facing.x) < 0.001f)) &&
         (this->look_dir.x >= this->look_dir.y || !(FABS(this->look_dir.y - facing.y) < 0.001f))))
    {
        result = true;
    }

    return result;
}

bool aqua_beam::active() const
{
    return firing || !ring.queue.empty();
}

void aqua_beam::move(const xVec3& new_loc, const xVec3& new_dir)
{
    loc = new_loc;
    dir = new_dir;
}

xVec3& zNPCPrawn::get_facing() const
{
    return reinterpret_cast<xVec3&>(this->model->Mat->at);
}

U8 zNPCPrawn::ColChkFlags() const
{
    return 0;
}

U8 zNPCPrawn::ColPenFlags() const
{
    return 0;
}

U8 zNPCPrawn::ColChkByFlags() const
{
    return 16;
}

U8 zNPCPrawn::ColPenByFlags() const
{
    return 16;
}

U8 zNPCPrawn::PhysicsFlags() const
{
    return 3;
}

S32 zNPCPrawn::IsAlive()
{
    return this->life > 0;
}

namespace auto_tweak
{
    template <>
    void load_param<F32, F32>(F32& value, F32 scale, F32 lo, F32 hi, xModelAssetParam* ap,
                              U32 apsize, const char* name)
    {
        value = zParamGetFloat(ap, apsize, name, value);
        if (value < lo)
        {
            value = lo;
        }
        else if (value > hi)
        {
            value = hi;
        }
        value = value * scale;
    }

    template <>
    void load_param<S32, S32>(S32& value, S32 scale, S32 lo, S32 hi, xModelAssetParam* ap,
                              U32 apsize, const char* name)
    {
        S32 result = zParamGetInt(ap, apsize, name, value);
        if (result < lo)
        {
            result = lo;
        }
        else if (result > hi)
        {
            result = hi;
        }
        value = result * scale;
    }
} // namespace auto_tweak
