#include "zEntSimpleObj.h"

#include <types.h>

static S32 sMgrCount;
static xSphere* sMgrList;
static u32 sSimpleCustomCount;
static xEnt** sSimpleCustomList;

void zEntSimpleObj_MgrInit(zEntSimpleObj** entList, U32 entCount)
{
    f32 sp14;
    RpClump* sp10;
    s32 spC;
    s32 sp8;
    RpAtomic* temp_r24;
    RwMatrixTag* temp_r4_2;
    f32 temp_f1;
    f32 temp_f1_2;
    f32 temp_f1_3;
    f32 temp_f1_4;
    f32 temp_f1_5;
    f32 temp_f2;
    f32 temp_f3;
    f32 var_f0;
    f32 var_f0_2;
    f32 var_f0_3;
    f32 var_f0_4;
    f32 var_f2;
    s32 temp_r0;
    s32 temp_r0_2;
    s32 temp_r0_3;
    s32 temp_r10;
    s32 temp_r11;
    xEnt* temp_r28;
    s32 temp_r5;
    s32 temp_r6;
    s32 temp_r6_4;
    s32 temp_r7;
    s32 temp_r8;
    s32 temp_r9;
    s32 var_ctr_2;
    s32 var_r3;
    s32 var_r6;
    u32 trailerHash;
    u32 temp_r6_3;
    u32 var_ctr;
    u32 custEntCount;
    u32 tempEntCount;
    u32 i;
    u32 var_r30_2;
    u32 var_r4;
    u8 temp_r4;
    void* temp_r3_3;
    xSphere* smgr;
    zEntSimpleObj** tempEntList;
    zEntSimpleObj** var_r29;
    zEntSimpleObj** var_r29_2;
    zEntSimpleObj** var_r30;
    zEntSimpleObj* temp_r3;
    zEntSimpleObj* temp_r3_2;
    zEntSimpleObj* temp_r6_2;

    sMgrCount = 0;
    sMgrList = NULL;
    sSimpleCustomCount = 0;
    sSimpleCustomList = NULL;
    if (entCount != 0)
    {
        tempEntCount = 0;
        tempEntList = (zEntSimpleObj**)RwMalloc(entCount * 4);
        custEntCount = 0;
        trailerHash = xStrHash("trailer_hitch\0xEntAutoEventSimple");
        var_r30 = entList;
        var_r29 = tempEntList;
        i = 0U;
        while (i < entCount)
        {
            temp_r3 = *var_r30;
            temp_r6 = temp_r3->sflags;
            if (!(temp_r6 & 0x10))
            {
                if ((temp_r3->update != (xEntUpdateCallback)zEntSimpleObj_Update) ||
                    (temp_r3->render != zEntSimpleObj_Render) ||
                    (temp_r3->eventFunc != (xBaseEventCB)zEntSimpleObjEventCB) ||
                    (temp_r3->move != NULL) ||
                    (temp_r4 = temp_r3->moreFlags, (((temp_r4 & 8) == 0) == 0)) ||
                    (temp_r4 & 0x20) || (temp_r3->miscflags & 1) || (temp_r3->atbl != NULL) ||
                    (temp_r6 & 4) || (temp_r6 & 8) || (trailerHash == temp_r3->asset->modelInfoID) ||
                    (temp_r3->baseType == eBaseTypeTrackPhysics) || (temp_r3->driver != NULL))
                {
                    temp_r0 = (entCount - 1) - custEntCount;
                    custEntCount += 1;
                    tempEntList[temp_r0] = temp_r3;
                    temp_r6_2 = *var_r30;
                    if ((temp_r6_2->driver != NULL) && (temp_r6_2->move == NULL))
                    {
                        temp_r6_2->move = zEntSimpleObj_Move;
                        temp_r3_2 = *var_r30;
                        temp_r3_2->pflags |= 1;
                        (*var_r30)->frame = (xEntFrame*)xMemAlloc(gActiveHeap, 0xE4U, 0);
                    }
                }
                else
                {
                    tempEntCount += 1;
                    temp_r3->baseFlags |= 0x80;
                    *var_r29 = *var_r30;
                    var_r29 += 1;
                }
            }
            var_r30 += 1;
            i += 1;
        }

        if (custEntCount != 0)
        {
            sSimpleCustomCount = custEntCount;
            sSimpleCustomList = (xEnt**)xMemAlloc(gActiveHeap, custEntCount * 4, 0);
            temp_r0_2 = entCount - 1;
            for (var_r4 = 0; var_r4 < custEntCount; var_r4++)
            {
                sSimpleCustomList[var_r4] = (xEnt*)tempEntList[temp_r0_2 - var_r4];
            }
        }
        else
        {
            sSimpleCustomCount = 0;
            sSimpleCustomList = NULL;
        }
        if (tempEntCount == 0)
        {
            RwFree(tempEntList);
            return;
        }
        sMgrCount = tempEntCount;
        sMgrList = (xSphere*)xMemAlloc(gActiveHeap, tempEntCount << 6, 0x40);
        var_r29_2 = tempEntList;
        var_r30_2 = 0U;
        smgr = sMgrList;
        while (var_r30_2 < tempEntCount)
        {
            RpAtomic* model = (*var_r29_2)->model->Data;
            RwSphere oldbound = model->boundingSphere;

            model->boundingSphere.radius *= 1.1f;
            iModelCull(model, (*var_r29_2)->model->Mat);
            model->boundingSphere = oldbound;

            ((F32*)smgr)[0] = model->worldBoundingSphere.center.x;
            ((F32*)smgr)[1] = model->worldBoundingSphere.center.y;
            ((F32*)smgr)[2] = model->worldBoundingSphere.center.z;
            ((F32*)smgr)[3] = model->worldBoundingSphere.radius;

            zLODTable* lod = zLOD_Get(*var_r29_2);
            if (lod != NULL)
            {
                RwMatrixTag* m = (*var_r29_2)->model->Mat;
                F32 distscale = SQR(m->right.x) + SQR(m->right.y) + SQR(m->right.z);

                if (distscale < 0.0001f)
                {
                    distscale = 1.0f;
                }

                F32 d;

                d = lod->lodDist[0];
                ((F32*)smgr)[4] = (d != 0.0f) ? d * distscale : 1e38f;

                d = lod->lodDist[1];
                ((F32*)smgr)[5] = (d != 0.0f) ? d * distscale : 1e38f;

                d = lod->lodDist[2];
                ((F32*)smgr)[6] = (d != 0.0f) ? d * distscale : 1e38f;

                d = lod->noRenderDist;
                ((F32*)smgr)[7] = (d != 0.0f) ? d * distscale : 1e38f;

                ((void**)smgr)[9] = lod->baseBucket;
                ((void**)smgr)[10] = lod->lodBucket[0];
                ((void**)smgr)[11] = lod->lodBucket[1];
                ((void**)smgr)[12] = lod->lodBucket[2];

                if (((void**)smgr)[10] == NULL)
                {
                    ((F32*)smgr)[4] = 1e38f;
                }

                if (((void**)smgr)[11] == NULL)
                {
                    ((F32*)smgr)[5] = 1e38f;
                }

                if (((void**)smgr)[12] == NULL)
                {
                    ((F32*)smgr)[6] = 1e38f;
                }
            }
            else
            {
                ((F32*)smgr)[4] = 1e38f;
                ((F32*)smgr)[5] = 1e38f;
                ((F32*)smgr)[6] = 1e38f;
                ((F32*)smgr)[7] = 1e38f;

                ((void**)smgr)[9] = (*var_r29_2)->model->Bucket;
                ((void**)smgr)[10] = NULL;
                ((void**)smgr)[11] = NULL;
                ((void**)smgr)[12] = NULL;
            }

            ((S16*)smgr)[16] = (*var_r29_2)->flags;
            ((void**)smgr)[13] = (*var_r29_2)->model->Mat;
            ((void**)smgr)[14] = *var_r29_2;
            ((U8*)smgr)[34] = 0xFF;

            xEntUpdate(*var_r29_2, globals.sceneCur, 0.0f);

            smgr += 4;
            var_r29_2 += 1;
            var_r30_2 += 1;
        }
        RwFree(tempEntList);
    }
}

void zEntSimpleObj_MgrUpdateRender(RpWorld* world, F32 dt)
{
    s32 sp1C;
    s32 sp18;
    s32 sp14;
    f32 spC = 0.0f;
    xVec3 sp8;
    f32 temp_f0;
    f32 duration;
    f32 temp_f2;
    f32 camdist2;
    f32 temp_f3;
    f32 temp_f4;
    u32 i;
    u8 picklod;
    xModelInstance* var_r3;
    xModelInstance* var_r3_2;
    xEnt* ent;
    xModelInstance* temp_r3;
    xModelInstance* model;
    xQuat* q0;
    xSphere* smgr;
    xVec3* t0;

    smgr = sMgrList;
    for (i = 0; i < (u32)sMgrCount; i++)
    {
        ent = (xEnt*)&smgr[3].center.z;
        if (xEntIsVisible(ent) != 0U)
        {
            temp_f2 = globals.camera.mat.at.x - *((F32*)smgr + 0x0);
            temp_f4 = globals.camera.mat.at.y - *((F32*)smgr + 0x1);
            temp_f3 = globals.camera.mat.at.z - *((F32*)smgr + 0x2);
            camdist2 = (temp_f3 * temp_f3) + ((temp_f2 * temp_f2) + (temp_f4 * temp_f4));
            if (!(camdist2 > *((F32*)smgr + 0x7)) && (iModelSphereCull((xSphere*)smgr) == 0))
            {
                picklod = 0;
                if (camdist2 > *((F32*)smgr + 0x4))
                {
                    picklod = 1;
                    if (camdist2 > *((F32*)smgr + 0x5))
                    {
                        picklod = 2;
                        if (camdist2 > *((F32*)smgr + 0x6))
                        {
                            picklod = 3;
                        }
                    }
                }
                model = ent->model;
                model->Flags &= 0xFBFF;
                *((U8*)smgr + 0x22) = picklod;
                model->Bucket = (xModelBucket**)smgr + (((picklod * 4) & 0x3FC) + 0x24);
                model->Data = (*model->Bucket)->OriginalData;
                if (picklod == 0)
                {
                    var_r3 = model->Next;
                    while (var_r3 != NULL)
                    {
                        var_r3->Flags = (u16)(var_r3->Flags & 0xFBFF);
                        var_r3 = var_r3->Next;
                    }
                }
                else
                {
                    var_r3_2 = model->Next;
                    while (var_r3_2 != NULL)
                    {
                        var_r3_2->Flags = (u16)(var_r3_2->Flags | 0x400);
                        var_r3_2 = var_r3_2->Next;
                    }
                }
                if ((((zEntSimpleObj*)ent)->anim != NULL) && (zGameIsPaused() == 0))
                {
                    duration = iAnimDuration(((zEntSimpleObj*)ent));
                    ((zEntSimpleObj*)ent)->animTime += dt;
                    temp_f0 = ((zEntSimpleObj*)ent)->animTime;
                    if (temp_f0 >= duration)
                    {
                        ((zEntSimpleObj*)ent)->animTime = temp_f0 - duration;
                    }
                    q0 = (xQuat*)giAnimScratch;
                    t0 = (xVec3*)q0 + 0x410;
                    iAnimEval(((zEntSimpleObj*)ent)->anim,
                              ((zEntSimpleObj*)ent)->animTime, 0U, t0, q0);
                    temp_r3 = ent->model;
                    iModelAnimMatrices(temp_r3->Data, q0, t0,
                                       (RwMatrixTag*)&temp_r3->Mat);
                }
                xLightKit_Enable(ent->lightKit, globals.currWorld);
                zEntSimpleObj_Render(ent);
                if ((picklod == 0) && (xrand() < 0x55U))
                {
                    xVec3Copy(&sp8, (xVec3*)&ent->model->Mat->pos);
                    spC += (0.25f * xurand()) + 0.25f;
                    zFX_SpawnBubbleTrail(&sp8, (xrand() & 7) + 1, &ent->asset->pos, NULL);
                }
            }
        }
        smgr += 0x40;
    }
}

void zEntSimpleObj_MgrCustomUpdate(zScene* s, F32 dt)
{
    s32 var_r31;
    u32 var_r30;
    xEnt* temp_r3;

    var_r31 = 0;
    var_r30 = 0U;

    while (var_r30 < (u32)sSimpleCustomCount)
    {
        temp_r3 = sSimpleCustomList[var_r31];
        if (!(temp_r3->baseFlags & 0x40))
        {
            temp_r3->update(temp_r3, s, dt);
        }
        var_r31 += 1;
        var_r30 += 1;
    }
}

void zEntSimpleObj_MgrCustomRender()
{
    s32 var_r30;
    u32 var_r29;
    xEnt* temp_r3;

    var_r30 = 0;
    var_r29 = 0U;

    while (var_r29 < (u32)sSimpleCustomCount)
    {
        xLightKit_Enable(sSimpleCustomList[var_r30]->lightKit, globals.currWorld);
        temp_r3 = sSimpleCustomList[var_r30];
        temp_r3->render(temp_r3);
        var_r30 += 1;
        var_r29 += 1;
    }
}

void zEntSimpleObj_Render(xEnt* ent)
{
    if (ent->model == NULL || xEntIsVisible(ent) == FALSE)
    {
        return;
    }

    xModelRender(ent->model);
}

void zEntTrackPhysics_Init(void* ent, void* asset)
{
    zEntSimpleObj_Init((zEntSimpleObj*)ent, (xEntAsset*)asset, 1);
}

void zEntSimpleObj_Init(void* ent, void* asset)
{
    zEntSimpleObj_Init((zEntSimpleObj*)ent, (xEntAsset*)asset, 0);
}

void zEntSimpleObj_Init(zEntSimpleObj* ent, xEntAsset* asset, bool physparams)
{
    U32 tmpsize;
    RpAtomic* modelData;
    void* temp_r3_5;
    void* animData;
    U32 temp_r3_4;
    U32 animBoneCount;
    xModelInstance* temp_r3;
    xAnimPlay* temp_r3_2;
    xAnimPlay* temp_r3_3;
    xAnimTable* temp_r4;
    xSimpleObjAsset* sasset;

    zEntInit((zEnt*)ent, asset, 0x53494D50U);

    if (physparams != 0)
    {
        ent->baseType = 0x3F;
    }

    if (physparams != 0)
    {
        sasset = (xSimpleObjAsset*)(asset + 1);
    }
    else
    {
        sasset = (xSimpleObjAsset*)(asset + 1);
    }

    ent->sasset = sasset;
    ent->sflags = 0;
    ent->pflags = 0;
    ent->penby |= 0x10;
    if (ent->sasset->collType & XENT_COLLTYPE_STAT)
    {
        ent->chkby = 0x18;
    }
    else
    {
        ent->chkby = 0;
    }
    ent->move = NULL;
    ent->update = (xEntUpdateCallback)zEntSimpleObj_Update;
    ent->eventFunc = (xBaseEventCB)zEntSimpleObjEventCB;
    ent->render = zEntSimpleObj_Render;
    if ((u8)ent->linkCount != 0)
    {
        if (physparams != 0)
        {
            ent->link = (xLinkAsset*)((char*)ent->asset + 0x9C);
        }
        else
        {
            ent->link = (xLinkAsset*)((char*)ent->asset + 0x60);
        }
    }
    else
    {
        ent->link = NULL;
    }
    ent->eventFunc = (xBaseEventCB)zEntSimpleObjEventCB;
    modelData = (RpAtomic*)xSTFindAsset(asset->modelInfoID, &tmpsize);
    animData = NULL;
    if (!(ent->miscflags & 1) && (ent->asset->modelInfoID != 0U) &&
        (temp_r3 = ent->model, ((temp_r3 == NULL) == 0)) &&
        (temp_r3_2 = temp_r3->Anim, ((temp_r3_2 == NULL) == 0)) &&
        (temp_r4 = temp_r3_2->Table, ((temp_r4 == NULL) == 0)) &&
        (strcmp(&"trailer_hitch\0xEntAutoEventSimple"[0xE], temp_r4->Name) == 0))
    {
        temp_r3_3 = ent->model->Anim;
        xAnimPlaySetState(temp_r3_3->Single, temp_r3_3->Table->StateList, 0.0f);
        ent->miscflags |= 1;
    }
    else
    {
        temp_r3_4 = asset->animListID;
        if ((temp_r3_4 != 0) && (ent->atbl == NULL))
        {
            animData = xSTFindAsset(temp_r3_4, &tmpsize);
            if ((animData != NULL) &&
                ((animBoneCount = iAnimBoneCount(animData), ((animBoneCount == 0U) != 0)) ||
                 (animBoneCount != iModelNumBones(modelData))))
            {
                animData = NULL;
            }
        }
    }
    ent->anim = animData;
    ent->animTime = 0.0f;
    zEntReset((zEnt*)ent);
}

void zEntSimpleObj_Move(xEnt*, xScene*, F32, xEntFrame*)
{
}

void zEntSimpleObj_Update(zEntSimpleObj* ent, xScene* sc, float dt)
{
    void* temp_r3;
    f32 temp_f0;
    f32 duration;
    xModelInstance* temp_r3_2;
    xModelInstance* temp_r4;
    xQuat* q0;
    xVec3* t0;

    xEntUpdate((xEnt*)ent, sc, dt);
    temp_r3 = ent->anim;
    if (temp_r3 != NULL)
    {
        temp_r4 = ent->model;
        if ((temp_r4 != NULL) && !(temp_r4->Flags & 0x400))
        {
            duration = iAnimDuration(temp_r3);
            ent->animTime += dt;
            temp_f0 = ent->animTime;
            if (temp_f0 >= duration)
            {
                ent->animTime = temp_f0 - duration;
            }
            q0 = (xQuat*)giAnimScratch;
            t0 = (xVec3*)((char*)q0 + 0x410);
            iAnimEval(ent->anim, ent->animTime, 0U, t0, q0);
            temp_r3_2 = ent->model;
            iModelAnimMatrices(temp_r3_2->Data, q0, t0, temp_r3_2->Mat + 0x1);
        }
    }
}

void zEntSimpleObj_Setup(zEntSimpleObj* ent)
{
    zEntSetup((zEnt*)ent);
}

void zEntSimpleObj_Save(zEntSimpleObj* ent, xSerial* s)
{
    zEntSave((zEnt*)ent, s);
}

void zEntSimpleObj_Load(zEntSimpleObj* ent, xSerial* s)
{
    zEntLoad((zEnt*)ent, s);
}

void zEntSimpleObj_Reset(zEntSimpleObj* ent, xScene* scene)
{
    xEntBoundUpdateCallback temp_r12;

    zEntReset((zEnt*)ent);
    ent->animTime = 0.0f;
    ent->chkby &= 0xE3;
    if (ent->sasset->collType & 2)
    {
        ent->chkby |= 0x18;
    }
    temp_r12 = ent->bupdate;
    if (temp_r12 != NULL)
    {
        temp_r12(ent, (xVec3*)&ent->model->Mat->pos);
        return;
    }
    xEntDefaultBoundUpdate((xEnt*)ent, (xVec3*)&ent->model->Mat->pos);
}

s32 zEntSimpleObjEventCB(xBase* from, xBase* to, U32 toEvent, const F32* toParam, xBase* base3)
{
    zEntSimpleObj* s = (zEntSimpleObj*)to;

    switch (toEvent)
    {
    case eEventFastVisible:
    case eEventVisible:
        xEntShow((xEnt*)to);
        if ((toParam != NULL) && ((S32)(0.5f + toParam[0]) == 0x4D))
        {
            zFXPopOn((xEnt&)*to, toParam[1], toParam[2]);
        }
        break;
    case eEventFastInvisible:
    case eEventInvisible:
        xEntHide((xEnt*)to);
        if ((toParam != NULL) && ((S32)(0.5f + toParam[0]) == 0x4D))
        {
            zFXPopOff((xEnt&)*to, toParam[1], toParam[2]);
        }
        break;
    case eEventCollision_Visible_On:
        xEntShow((xEnt*)to);
        if ((toParam != NULL) && ((S32)(0.5f + toParam[0]) == 0x4D))
        {
            zFXPopOn((xEnt&)*to, toParam[1], toParam[2]);
        }
        /* fallthrough */
    case eEventCollisionOn:
        s->chkby = 0x18;
        if (s->bupdate != NULL)
        {
            s->bupdate((xEnt*)to, (xVec3*)&s->model->Mat->pos);
        }
        else
        {
            xEntDefaultBoundUpdate((xEnt*)to, (xVec3*)&s->model->Mat->pos);
        }
        break;
    case eEventCollision_Visible_Off:
        xEntHide((xEnt*)to);
        if ((toParam != NULL) && ((S32)(0.5f + toParam[0]) == 0x4D))
        {
            zFXPopOff((xEnt&)*to, toParam[1], toParam[2]);
        }
        /* fallthrough */
    case eEventCollisionOff:
        s->chkby = 0;
        if (s->bupdate != NULL)
        {
            s->bupdate((xEnt*)to, (xVec3*)&s->model->Mat->pos);
        }
        else
        {
            xEntDefaultBoundUpdate((xEnt*)to, (xVec3*)&s->model->Mat->pos);
        }
        break;
    case eEventCameraCollideOn:
        zCollGeom_CamEnable((xEnt*)to);
        break;
    case eEventCameraCollideOff:
        zCollGeom_CamDisable((xEnt*)to);
        break;
    case eEventReset:
        zEntSimpleObj_Reset((zEntSimpleObj*)to, globals.sceneCur);
        break;
    case eEventAnimPlay:
    case eEventAnimPlayLoop:
    case eEventAnimStop:
    case eEventAnimPause:
    case eEventAnimResume:
    case eEventAnimTogglePause:
    case eEventAnimPlayRandom:
    case eEventAnimPlayMaybe:
        zEntAnimEvent((zEnt*)to, toEvent, toParam);
        break;
    case eEventSetSkyDome:
        xSkyDome_AddEntity((xEnt*)to, (s32)toParam[0], (s32)toParam[1]);
        break;
    case eEventSetGoo:
        zGooAdd((xEnt*)to, toParam[0], (s32)toParam[1]);
        break;
    case eEventGooSetWarb:
        zFXGooEventSetWarb((xEnt*)to, toParam);
        break;
    case eEventGooSetFreezeDuration:
        zFXGooEventSetFreezeDuration((xEnt*)to, toParam[0]);
        break;
    case eEventGooMelt:
        zFXGooEventMelt((xEnt*)to);
        break;
    case eEventLaunchShrapnel:
        zShrapnelAsset* shrap = (zShrapnelAsset*)base3;
        if (shrap != NULL && shrap->initCB != NULL)
        {
            shrap->initCB(shrap, s->model, NULL, NULL);
        }
        break;
    case eEventDestroy:
        xEntHide((xEnt*)to);
        break;
    }
    return 1;
}
