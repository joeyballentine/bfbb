// Lightly modified RW backend from librw's Skeleton project
//
// Removes IO code so we can compose this with the default imgui Win32 backend

#define WITH_D3D
#include <rw.h>
#include <assert.h>

#include "imgui.h"
#include "imgui_impl_rw.h"

using namespace rw::RWDEVICE;

static rw::Texture* g_FontTexture;
static Im2DVertex* g_vertbuf;
static int g_vertbufSize;

// TODO: scissor from librw itself
static void SetClip(const ImVec4& clip)
{
    ImGuiIO& io = ImGui::GetIO();
    float x1 = clip.x;
    float y1 = clip.y;
    float x2 = clip.z;
    float y2 = clip.w;
#ifdef RW_OPENGL
    glScissor(x1, io.DisplaySize.y - y2, x2 - x1, y2 - y1);
    glEnable(GL_SCISSOR_TEST);
#endif
#ifdef RW_D3D9
    rw::d3d::d3ddevice->SetRenderState(D3DRS_SCISSORTESTENABLE, 1);
    RECT r = { (LONG)x1, (LONG)y1, (LONG)x2, (LONG)y2 };
    rw::d3d::d3ddevice->SetScissorRect(&r);
#endif
}

static void DisableClip(void)
{
#ifdef RW_OPENGL
    glDisable(GL_SCISSOR_TEST);
#endif
#ifdef RW_D3D9
    rw::d3d::d3ddevice->SetRenderState(D3DRS_SCISSORTESTENABLE, 0);
#endif
}

void ImGui_ImplRW_RenderDrawLists(ImDrawData* draw_data)
{
    ImGuiIO& io = ImGui::GetIO();

    // minimized
    if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f)
        return;

    if (g_vertbuf == nil || g_vertbufSize < draw_data->TotalVtxCount)
    {
        if (g_vertbuf)
        {
            rwFree(g_vertbuf);
            g_vertbuf = nil;
        }
        g_vertbufSize = draw_data->TotalVtxCount + 5000;
        g_vertbuf = rwNewT(Im2DVertex, g_vertbufSize, 0);
    }

    float xoff = 0.0f;
    float yoff = 0.0f;
#ifdef RWHALFPIXEL
    xoff = -0.5;
    yoff = 0.5;
#endif

    rw::Camera* cam = (rw::Camera*)rw::engine->currentCamera;
    Im2DVertex* vtx_dst = g_vertbuf;
    float recipZ = 1.0f / cam->nearPlane;
    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        const ImDrawVert* vtx_src = cmd_list->VtxBuffer.Data;
        for (int i = 0; i < cmd_list->VtxBuffer.Size; i++)
        {
            vtx_dst[i].setScreenX(vtx_src[i].pos.x + xoff);
            vtx_dst[i].setScreenY(vtx_src[i].pos.y + yoff);
            vtx_dst[i].setScreenZ(rw::im2d::GetNearZ());
            vtx_dst[i].setCameraZ(cam->nearPlane);
            vtx_dst[i].setRecipCameraZ(recipZ);
            vtx_dst[i].setColor(vtx_src[i].col & 0xFF, vtx_src[i].col >> 8 & 0xFF,
                                vtx_src[i].col >> 16 & 0xFF, vtx_src[i].col >> 24 & 0xFF);
            vtx_dst[i].setU(vtx_src[i].uv.x, recipZ);
            vtx_dst[i].setV(vtx_src[i].uv.y, recipZ);
        }
        vtx_dst += cmd_list->VtxBuffer.Size;
    }

    int vertexAlpha = rw::GetRenderState(rw::VERTEXALPHA);
    int srcBlend = rw::GetRenderState(rw::SRCBLEND);
    int dstBlend = rw::GetRenderState(rw::DESTBLEND);
    int ztest = rw::GetRenderState(rw::ZTESTENABLE);
    void* tex = rw::GetRenderStatePtr(rw::TEXTURERASTER);
    int addrU = rw::GetRenderState(rw::TEXTUREADDRESSU);
    int addrV = rw::GetRenderState(rw::TEXTUREADDRESSV);
    int filter = rw::GetRenderState(rw::TEXTUREFILTER);
    int cullmode = rw::GetRenderState(rw::CULLMODE);

    rw::SetRenderState(rw::VERTEXALPHA, 1);
    rw::SetRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
    rw::SetRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
    rw::SetRenderState(rw::ZTESTENABLE, 0);
    rw::SetRenderState(rw::CULLMODE, rw::CULLNONE);

    int vtx_offset = 0;
    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        int idx_offset = 0;
        for (int i = 0; i < cmd_list->CmdBuffer.Size; i++)
        {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[i];
            if (pcmd->UserCallback)
                pcmd->UserCallback(cmd_list, pcmd);
            else
            {
                rw::Texture* tex = (rw::Texture*)pcmd->GetTexID();
                if (tex && tex->raster)
                {
                    rw::SetRenderStatePtr(rw::TEXTURERASTER, tex->raster);
                    rw::SetRenderState(rw::TEXTUREADDRESSU, tex->getAddressU());
                    rw::SetRenderState(rw::TEXTUREADDRESSV, tex->getAddressV());
                    rw::SetRenderState(rw::TEXTUREFILTER, tex->getFilter());
                }
                else
                    rw::SetRenderStatePtr(rw::TEXTURERASTER, nil);

                SetClip(pcmd->ClipRect);
                rw::im2d::RenderIndexedPrimitive(rw::PRIMTYPETRILIST, g_vertbuf + vtx_offset,
                                                 cmd_list->VtxBuffer.Size,
                                                 cmd_list->IdxBuffer.Data + idx_offset,
                                                 pcmd->ElemCount);
                DisableClip();
            }
            idx_offset += pcmd->ElemCount;
        }
        vtx_offset += cmd_list->VtxBuffer.Size;
    }

    rw::SetRenderState(rw::VERTEXALPHA, vertexAlpha);
    rw::SetRenderState(rw::SRCBLEND, srcBlend);
    rw::SetRenderState(rw::DESTBLEND, dstBlend);
    rw::SetRenderState(rw::ZTESTENABLE, ztest);
    rw::SetRenderStatePtr(rw::TEXTURERASTER, tex);
    rw::SetRenderState(rw::TEXTUREADDRESSU, addrU);
    rw::SetRenderState(rw::TEXTUREADDRESSV, addrV);
    rw::SetRenderState(rw::TEXTUREFILTER, filter);
    rw::SetRenderState(rw::CULLMODE, cullmode);
}

bool ImGui_ImplRW_Init(void)
{
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    return true;
}

void ImGui_ImplRW_Shutdown(void)
{
}

static bool ImGui_ImplRW_CreateFontsTexture()
{
    // Build texture atlas
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height, nil);

    rw::Image* image;
    image = rw::Image::create(width, height, 32);
    image->allocate();
    for (int y = 0; y < height; y++)
        memcpy(image->pixels + image->stride * y, pixels + width * 4 * y, width * 4);
    g_FontTexture = rw::Texture::create(rw::Raster::createFromImage(image));
    g_FontTexture->setFilter(rw::Texture::LINEAR);
    image->destroy();

    // Store our identifier
    io.Fonts->TexID = (void*)g_FontTexture;

    return true;
}

bool ImGui_ImplRW_CreateDeviceObjects()
{
    if (!ImGui_ImplRW_CreateFontsTexture())
        return false;
    return true;
}

void ImGui_ImplRW_NewFrame()
{
    if (!g_FontTexture)
        ImGui_ImplRW_CreateDeviceObjects();
}
