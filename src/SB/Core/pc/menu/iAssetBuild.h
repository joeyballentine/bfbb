#ifndef IASSETBUILD_H
#define IASSETBUILD_H

#include <types.h>
#include <string.h>

#include "iHostWords.h"
#include "xCounter.h"
#include "xDynAsset.h"
#include "xEvent.h"
#include "xGroup.h"
#include "xHudFontMeter.h"
#include "xHudModel.h"
#include "xHudText.h"
#include "xHudUnitMeter.h"
#include "xSFX.h"
#include "xString.h"
#include "xTimer.h"
#include "zBase.h"
#include "zConditional.h"
#include "zPortal.h"
#include "zTalkBox.h"
#include "zTextBox.h"
#include "zUI.h"
#include "zUIFont.h"

// PC-only: assets written as code. iAssetOverride.h serves a package built
// here in place of a HIP's own assets of the same types.
//
// Each asset is its struct, then its trailing arrays, exactly as the HIP lays
// it out: a group's item IDs, then every asset's links. Begin copies the
// struct, Item and Link append, End fills in the counts and stores it.
//
//     zUIFontAsset a = kUIFontDefaults;
//     a.pos = xVec3{ 340.0f, 325.0f, 0.0f };
//     p.Begin('UIFT', H("MNU3 PAUSE OPTIONS UIF"), a);
//     p.Link(eEventPadPressUp, eEventUISelect, H("MNU3 PAUSE RETURN GAME UIF"));
//     p.End();
//
// Order matters: within a type, assets are created in the order they are
// added, which is the order retail created them in.

// An asset ID is xStrHash of the asset's name.
inline U32 H(const char* name)
{
    return xStrHash(name);
}

// A link parameter that carries an asset ID in a float's bits.
inline F32 iAssetParamID(U32 id)
{
    F32 f;
    memcpy(&f, &id, sizeof(f));
    return f;
}

// A float no one typed (a denormal or a NaN), kept bit for bit.
inline F32 iAssetFloatBits(U32 bits)
{
    return iAssetParamID(bits);
}

// A four-character tag as the assets store it: the characters in reading
// order in memory, as a portal's sceneID holds "SB10".
inline U32 iAssetTag(const char* tag)
{
    U32 v;
    memcpy(&v, tag, sizeof(v));
    return v;
}

struct iAssetEntry
{
    U32 aid;
    U32 type;
    void* data;
    U32 size;
};

class iAssetPkg
{
public:
    iAssetPkg();
    ~iAssetPkg();

    template <class T> void Begin(U32 type, U32 id, const T& header)
    {
        BeginRaw(type, id, &header, sizeof(T));
    }
    void Item(U32 id);
    void Link(U16 srcEvent, U16 dstEvent, U32 target, F32 p0 = 0.0f, F32 p1 = 0.0f,
              F32 p2 = 0.0f, F32 p3 = 0.0f, U32 paramWidget = 0, U32 chk = 0);
    void End();

    // A TEXT asset: the length, the string, its terminator, padded to four.
    void Text(U32 id, const char* text);

    S32 Count() const { return m_count; }
    void Remove(S32 i);
    const iAssetEntry& Entry(S32 i) const { return m_entries[i]; }

    // The idx'th asset of a type, in the order added. NULL past the end.
    const iAssetEntry* ByType(U32 type, S32 idx) const;
    S32 CountType(U32 type) const;
    const iAssetEntry* Find(U32 aid) const;

private:
    void BeginRaw(U32 type, U32 id, const void* header, U32 size);
    void Append(const void* data, U32 size);
    void Store(U32 type, U32 id);

    iAssetEntry* m_entries;
    S32 m_count;
    S32 m_capacity;

    // The asset under construction.
    U8* m_buf;
    U32 m_size;
    U32 m_bufCapacity;
    U32 m_type;
    U32 m_id;
    U32 m_headerSize;
    U32 m_items;
    U32 m_links;

    iAssetPkg(const iAssetPkg&);
    iAssetPkg& operator=(const iAssetPkg&);
};

#endif
