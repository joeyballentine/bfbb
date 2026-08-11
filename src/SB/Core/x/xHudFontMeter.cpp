#include "xHudFontMeter.h"

#include <types.h>
#include <xMath2.h>
#include <PowerPC_EABI_Support\MSL_C\MSL_Common\printf.h>
#include <PowerPC_EABI_Support\MSL_C++\MSL_Common\Include\new.h>

void xhud::font_meter_widget::load(xBase& data, xDynAsset& asset, size_t)
{
    init_base(data, asset, sizeof(xBase) + sizeof(font_meter_widget));
    font_meter_widget* widget = (font_meter_widget*)(&data + 1);
    new (widget) font_meter_widget((font_meter_asset&)asset);
}

static const basic_rect<F32> screen_bounds = { 0.0f, 0.0f, 1.0f, 1.0f };

xhud::font_meter_widget::font_meter_widget(const xhud::font_meter_asset& init)
    : meter_widget(init), font(init.font), start_font(init.font)
{
    this->last_value = ((S32)(this->value)) - 20;
    this->xf.id = 0;
    this->xf.width = this->font.w;
    this->xf.height = this->font.h;
    this->xf.space = this->font.space;

    this->xf.color = *(iColor_tag*)&this->font.c;
    this->xf.clip = screen_bounds;
}

void xhud::font_meter_widget::destruct()
{
    xhud::meter_widget::destruct();
}

void xhud::font_meter_widget::destroy()
{
    this->destruct();
}

U32 xhud::font_meter_widget::type() const
{
    static U32 myid;
    static S8 init;

    if (init == 0)
    {
        myid = xStrHash(((xhud::font_meter_asset*)this)->type_name());
        init = 1;
    }
    return myid;
}

bool xhud::font_meter_widget::is(U32 id) const
{
    bool val = false;

    if (id == xhud::font_meter_widget::type() || xhud::meter_widget::is(id))
    {
        val = true;
    }
    return val;
}

void xhud::font_meter_widget::update(F32 dt)

{
    // Not right yet, and the last 16% of this function is here. Only [0] is
    // ever written, yet sprintf indexes this with counter_mode (0..2), so the
    // other two entries are read uninitialised -- retail must have had a real
    // table of format strings. The target's frame is 0x10 larger than ours and
    // it does not emit this store, so recovering the strings out of the target
    // object's .data (as was done for zMain's g_xser_sizeinfo) should close it.
    char* format_text[3];
    format_text[0] = 0;

    F32 a;
    S32 new_value;
    basic_rect<F32> bounds;

    U8 flag_1;
    U8 flag_2;

    this->updater(dt);
    this->xf.id = this->font.id;
    this->xf.space = this->font.space;
    a = this->rc.size.x;
    this->font.w = a;
    this->xf.width = a;
    a = this->rc.size.y;
    this->font.h = a;
    this->xf.height = a;

    a = this->rc.a * (F32)this->start_font.c.a + 0.5f;
    if (a <= 0.0f)
    {
        flag_1 = 0;
    }
    else if (a >= 255.0f)
    {
        flag_1 = 255;
    }
    else
    {
        flag_1 = (U8)(S32)a;
    }
    this->font.c.a = flag_1;

    a = this->rc.a * (F32)this->start_font.drop_c.a + 0.5f;
    if (a <= 0.0f)
    {
        flag_1 = 0;
    }
    else if (a >= 255.0f)
    {
        flag_1 = 255;
    }
    else
    {
        flag_1 = (U8)(S32)a;
    }
    this->font.drop_c.a = flag_1;

    new_value = (S32)(this->value + 0.5f);
    if (this->last_value != new_value)
    {
        this->last_value = new_value;
        a = this->max_value;
        if (a < this->min_value)
        {
            flag_2 = 0;
        }
        else
        {
            flag_2 = ((font_meter_asset*)(this->a))->counter_mode;
        }
        sprintf(this->buffer, format_text[flag_2], new_value, (S32)(a + 0.5f));
        bounds = this->xf.bounds(this->buffer);
        this->offset.x = -bounds.x;
        this->offset.y = -bounds.y;
    }
    return;
}

void xhud::font_meter_widget::render()

{
    F32 x;
    F32 temp_x;
    F32 y;

    temp_x = this->offset.x + this->rc.loc.x;
    y = this->offset.y + this->rc.loc.y;
    if (this->font.drop_c.a != 0)
    {
        this->xf.color = this->font.drop_c;
        x = temp_x + this->font.drop_x;
        this->xf.render(this->buffer, x, y + this->font.drop_y);
    }
    if (this->font.c.a != 0)
    {
        this->xf.color = this->font.c;
        this->xf.render(this->buffer, temp_x, y);
    }
    return;
}

char* xhud::font_meter_asset::type_name()
{
    return "hud:meter:font";
}

// NOTE: this belongs in xFont.h. It is inline, so the compiler emits a weak
// out-of-line copy into every translation unit that calls it.
inline void xfont::render(const char* text, F32 x, F32 y) const
{
    start_render();
    irender(text, x, y);
    stop_render();
}
