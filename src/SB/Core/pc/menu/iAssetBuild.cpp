#include "iAssetBuild.h"

#include <stdlib.h>

#include "iTextPatch.h"
#include "xLinkAsset.h"

iAssetPkg::iAssetPkg()
    : m_entries(NULL), m_count(0), m_capacity(0), m_buf(NULL), m_size(0), m_bufCapacity(0),
      m_type(0), m_id(0), m_headerSize(0), m_items(0), m_links(0)
{
}

iAssetPkg::~iAssetPkg()
{
    for (S32 i = 0; i < m_count; i++)
    {
        free(m_entries[i].data);
    }
    free(m_entries);
    free(m_buf);
}

void iAssetPkg::Append(const void* data, U32 size)
{
    if (m_size + size > m_bufCapacity)
    {
        U32 cap = m_bufCapacity ? m_bufCapacity : 256;
        while (cap < m_size + size)
        {
            cap *= 2;
        }
        m_buf = (U8*)realloc(m_buf, cap);
        m_bufCapacity = cap;
    }
    memcpy(m_buf + m_size, data, size);
    m_size += size;
}

void iAssetPkg::BeginRaw(U32 type, U32 id, const void* header, U32 size)
{
    m_size = 0;
    m_type = type;
    m_id = id;
    m_headerSize = size;
    m_items = 0;
    m_links = 0;
    Append(header, size);
    ((xBaseAsset*)m_buf)->id = id;
}

void iAssetPkg::Item(U32 id)
{
    // A group's item IDs sit between its struct and its links.
    Append(&id, sizeof(id));
    m_items++;
}

void iAssetPkg::Link(U16 srcEvent, U16 dstEvent, U32 target, F32 p0, F32 p1, F32 p2, F32 p3,
                     U32 paramWidget, U32 chk)
{
    xLinkAsset l;
    l.srcEvent = srcEvent;
    l.dstEvent = dstEvent;
    l.dstAssetID = target;
    l.param[0] = p0;
    l.param[1] = p1;
    l.param[2] = p2;
    l.param[3] = p3;
    l.paramWidgetAssetID = paramWidget;
    l.chkAssetID = chk;
    Append(&l, sizeof(l));
    m_links++;
}

void iAssetPkg::End()
{
    xBaseAsset* base = (xBaseAsset*)m_buf;
    base->linkCount = (U8)m_links;
    if (m_type == 'GRUP')
    {
        ((xGroupAsset*)m_buf)->itemCount = (U16)m_items;
    }
    Store(m_type, m_id);
}

void iAssetPkg::Text(U32 id, const char* text)
{
    U32 len = (U32)strlen(text);
    U32 size = (sizeof(U32) + len + 1 + 3) & ~3u;

    m_size = 0;
    Append(&len, sizeof(len));
    Append(text, len + 1);
    while (m_size < size)
    {
        U8 zero = 0;
        Append(&zero, 1);
    }

    // The HIP's TEXT goes through iTextPatch as it loads (zAssetTypes.cpp's
    // TEXT_Read); text written here gets the same pass.
    iTextPatchAsset(id, (char*)m_buf + sizeof(U32), size - sizeof(U32));
    Store('TEXT', id);
}

void iAssetPkg::Store(U32 type, U32 id)
{
    if (m_count == m_capacity)
    {
        m_capacity = m_capacity ? m_capacity * 2 : 64;
        m_entries = (iAssetEntry*)realloc(m_entries, m_capacity * sizeof(iAssetEntry));
    }
    iAssetEntry& e = m_entries[m_count++];
    e.aid = id;
    e.type = type;
    e.size = m_size;
    e.data = malloc(m_size);
    memcpy(e.data, m_buf, m_size);
}

void iAssetPkg::Remove(S32 i)
{
    free(m_entries[i].data);
    memmove(&m_entries[i], &m_entries[i + 1], (m_count - i - 1) * sizeof(iAssetEntry));
    m_count--;
}

const iAssetEntry* iAssetPkg::ByType(U32 type, S32 idx) const
{
    for (S32 i = 0; i < m_count; i++)
    {
        if (m_entries[i].type == type && idx-- == 0)
        {
            return &m_entries[i];
        }
    }
    return NULL;
}

S32 iAssetPkg::CountType(U32 type) const
{
    S32 n = 0;
    for (S32 i = 0; i < m_count; i++)
    {
        n += m_entries[i].type == type;
    }
    return n;
}

const iAssetEntry* iAssetPkg::Find(U32 aid) const
{
    for (S32 i = 0; i < m_count; i++)
    {
        if (m_entries[i].aid == aid)
        {
            return &m_entries[i];
        }
    }
    return NULL;
}
