enum { FONT_TEXT, FONT_TEXT_SEMIBOLD, FONT_DISPLAY, FONT_ICON, FONT__COUNT };

typedef struct Face {
    DWFontFace *face;
    f32 upem;
    f32 ascent, descent, cap_height;
} Face;

static const WCHAR *k_fallback_families[] = {
    L"Segoe UI", L"Segoe UI Symbol", L"Segoe UI Emoji", L"Microsoft YaHei UI", L"Yu Gothic UI",
    L"Malgun Gothic", L"Microsoft JhengHei UI", L"Nirmala UI", L"Leelawadee UI", L"Segoe UI Historic",
    L"Ebrima", L"Gadugi", L"Myanmar Text", L"Javanese Text", L"Mongolian Baiti", L"Microsoft Himalaya",
    L"Sylfaen", L"Segoe MDL2 Assets",
};
#define FALLBACK_COUNT ((int)(sizeof(k_fallback_families) / sizeof(k_fallback_families[0])))

static struct {
    DWFactory *dw;
    bool v2;
    DWFontCollection *coll;
    Face faces[FONT__COUNT + FALLBACK_COUNT];
    bool fallback_tried[FALLBACK_COUNT];
    bool overflow;
    int shelf_x, shelf_y, shelf_h;
} F;

typedef struct GlyphMap {
    u32 key;
    u16 glyph;
    u8 face;
    u8 used;
    f32 adv;
} GlyphMap;

#define GMAP_SIZE 16384
static GlyphMap g_gmap[GMAP_SIZE];

typedef struct GlyphRaster {
    u64 key;
    u16 x, y, w, h;
    i16 ox, oy;
} GlyphRaster;

#define GRAST_SIZE 16384
static GlyphRaster g_grast[GRAST_SIZE];
static int g_grast_count;

static DWFontFace *font_load_face(const WCHAR *family, UINT32 weight)
{
    UINT32 idx = 0;
    BOOL exists = FALSE;
    if (!F.coll || FAILED(F.coll->lpVtbl->FindFamilyName(F.coll, family, &idx, &exists)) || !exists) return NULL;
    DWFontFamily *fam = NULL;
    DWFont *font = NULL;
    DWFontFace *face = NULL;
    if (SUCCEEDED(F.coll->lpVtbl->GetFontFamily(F.coll, idx, &fam))) {
        if (SUCCEEDED(fam->lpVtbl->GetFirstMatchingFont(fam, weight, DW_FONT_STRETCH_NORMAL, DW_FONT_STYLE_NORMAL, &font))) {
            font->lpVtbl->CreateFontFace(font, &face);
            font->lpVtbl->Release(font);
        }
        fam->lpVtbl->Release(fam);
    }
    return face;
}

static void face_setup(Face *f, DWFontFace *face)
{
    f->face = face;
    DW_FONT_METRICS m;
    memset(&m, 0, sizeof m);
    face->lpVtbl->GetMetrics(face, &m);
    f->upem = m.designUnitsPerEm ? (f32)m.designUnitsPerEm : 2048.f;
    f->ascent = m.ascent;
    f->descent = m.descent;
    f->cap_height = m.capHeight ? m.capHeight : m.ascent * 0.7f;
}

static bool font_init(void)
{
    HMODULE m = LoadLibraryW(L"dwrite.dll");
    PFN_DWriteCreateFactory create = m ? (PFN_DWriteCreateFactory)(void *)GetProcAddress(m, "DWriteCreateFactory") : NULL;
    if (!create) return false;
    IUnknown *unk = NULL;
    if (FAILED(create(0 /*shared*/, &ML_IID_IDWriteFactory, &unk)) || !unk) return false;
    DWFactory *f2 = NULL;
    if (SUCCEEDED(unk->lpVtbl->QueryInterface(unk, &ML_IID_IDWriteFactory2, (void **)&f2)) && f2) {
        F.dw = f2;
        F.v2 = true;
        unk->lpVtbl->Release(unk);
    } else {
        F.dw = (DWFactory *)unk;
    }
    if (FAILED(F.dw->lpVtbl->GetSystemFontCollection(F.dw, &F.coll, FALSE))) return false;

    struct { int slot; const WCHAR *fam; UINT32 w; const WCHAR *alt; UINT32 aw; } want[] = {
        { FONT_TEXT,          L"Segoe UI Variable Text",    400, L"Segoe UI", 400 },
        { FONT_TEXT_SEMIBOLD, L"Segoe UI Variable Text",    600, L"Segoe UI", 600 },
        { FONT_DISPLAY,       L"Segoe UI Variable Display", 400, L"Segoe UI", 400 },
        { FONT_ICON,          L"Segoe Fluent Icons",        400, L"Segoe MDL2 Assets", 400 },
    };
    for (int i = 0; i < countof(want); i++) {
        DWFontFace *face = font_load_face(want[i].fam, want[i].w);
        if (!face) face = font_load_face(want[i].alt, want[i].aw);
        if (!face) face = font_load_face(L"Arial", 400);
        if (!face) return false;
        face_setup(&F.faces[want[i].slot], face);
    }
    return true;
}

static Face *face_get(int face_id)
{
    if (face_id < FONT__COUNT) return &F.faces[face_id];
    int fb = face_id - FONT__COUNT;
    if (!F.fallback_tried[fb]) {
        F.fallback_tried[fb] = true;
        DWFontFace *face = font_load_face(k_fallback_families[fb], 400);
        if (face) face_setup(&F.faces[face_id], face);
    }
    return F.faces[face_id].face ? &F.faces[face_id] : NULL;
}

static u16 face_glyph_index(Face *f, u32 cp)
{
    UINT16 gi = 0;
    if (f && f->face) f->face->lpVtbl->GetGlyphIndices(f->face, &cp, 1, &gi);
    return gi;
}

static GlyphMap *glyph_map(int font, u32 cp)
{
    u32 key = ((u32)font << 24) | (cp & 0xFFFFFF);
    u32 h = (u32)hash_u64(key) & (GMAP_SIZE - 1);
    for (int probe = 0; probe < GMAP_SIZE; probe++) {
        GlyphMap *e = &g_gmap[(h + probe) & (GMAP_SIZE - 1)];
        if (e->used && e->key == key) return e;
        if (!e->used) {
            e->used = 1;
            e->key = key;
            int face_id = font;
            u16 gi = face_glyph_index(&F.faces[font], cp);
            if (!gi && cp >= 0x20) {
                for (int i = 0; i < FALLBACK_COUNT; i++) {
                    Face *fb = face_get(FONT__COUNT + i);
                    u16 g2 = fb ? face_glyph_index(fb, cp) : 0;
                    if (g2) {
                        gi = g2;
                        face_id = FONT__COUNT + i;
                        break;
                    }
                }
            }
            e->glyph = gi;
            e->face = (u8)face_id;
            Face *f = &F.faces[face_id];
            DW_GLYPH_METRICS gm;
            memset(&gm, 0, sizeof gm);
            f->face->lpVtbl->GetDesignGlyphMetrics(f->face, &gi, 1, &gm, FALSE);
            e->adv = (f32)gm.advanceWidth / f->upem;
            return e;
        }
    }
    memset(g_gmap, 0, sizeof g_gmap);
    return glyph_map(font, cp);
}

static u32 g_atlas_gen;  // bumped on every reset, so cached atlas regions know they are gone

static void font_reset_atlas(void)
{
    g_atlas_gen++;
    memset(g_grast, 0, sizeof g_grast);
    g_grast_count = 0;
    F.shelf_x = F.shelf_y = F.shelf_h = 0;
    if (R.glyph_cpu) memset(R.glyph_cpu, 0, (size_t)GLYPH_ATLAS * GLYPH_ATLAS);
    R.dirty_x0 = R.dirty_y0 = 0;
    R.dirty_x1 = R.dirty_y1 = GLYPH_ATLAS;
    F.overflow = false;
}

static bool atlas_alloc(int w, int h, int *ox, int *oy)
{
    const int pad = 1;
    w += pad;
    h += pad;
    if (F.shelf_x + w > GLYPH_ATLAS) {
        F.shelf_y += F.shelf_h;
        F.shelf_x = 0;
        F.shelf_h = 0;
    }
    if (F.shelf_y + h > GLYPH_ATLAS || w > GLYPH_ATLAS) return false;
    *ox = F.shelf_x;
    *oy = F.shelf_y;
    F.shelf_x += w;
    F.shelf_h = MAX(F.shelf_h, h);
    return true;
}

static void glyph_rasterize(GlyphRaster *out, Face *f, u16 glyph, f32 size_px, int phase)
{
    out->w = out->h = 0;
    FLOAT adv = 0;
    DW_GLYPH_OFFSET off = { 0, 0 };
    DW_GLYPH_RUN run;
    memset(&run, 0, sizeof run);
    run.fontFace = f->face;
    run.fontEmSize = size_px;
    run.glyphCount = 1;
    run.glyphIndices = &glyph;
    run.glyphAdvances = &adv;
    run.glyphOffsets = &off;

    DWGlyphRunAnalysis *ana = NULL;
    UINT32 tex_type = DW_TEXTURE_CLEARTYPE_3x1;
    f32 origin_x = (f32)phase * 0.25f;
    HRESULT hr = E_FAIL;
    if (F.v2) {
        hr = F.dw->lpVtbl->CreateGlyphRunAnalysis2(F.dw, &run, NULL, DW_RENDERING_MODE_NATURAL_SYMMETRIC, DW_MEASURING_MODE_NATURAL,
                                                   DW_GRID_FIT_MODE_DEFAULT, DW_TEXT_ANTIALIAS_MODE_GRAYSCALE, origin_x, 0, &ana);
        tex_type = DW_TEXTURE_ALIASED_1x1;
    }
    if (FAILED(hr)) {
        hr = F.dw->lpVtbl->CreateGlyphRunAnalysis(F.dw, &run, 1.0f, NULL, DW_RENDERING_MODE_NATURAL_SYMMETRIC, DW_MEASURING_MODE_NATURAL,
                                                  origin_x, 0, &ana);
        tex_type = DW_TEXTURE_CLEARTYPE_3x1;
    }
    if (FAILED(hr) || !ana) return;

    RECT b = { 0, 0, 0, 0 };
    ana->lpVtbl->GetAlphaTextureBounds(ana, tex_type, &b);
    if (b.right <= b.left && tex_type == DW_TEXTURE_ALIASED_1x1) {
        tex_type = DW_TEXTURE_CLEARTYPE_3x1;
        ana->lpVtbl->GetAlphaTextureBounds(ana, tex_type, &b);
    }
    int w = b.right - b.left, h = b.bottom - b.top;
    if (w <= 0 || h <= 0 || w > 512 || h > 512) {
        ana->lpVtbl->Release(ana);
        return;
    }
    int bpp = tex_type == DW_TEXTURE_CLEARTYPE_3x1 ? 3 : 1;
    static u8 tmp[512 * 512 * 3];
    if (FAILED(ana->lpVtbl->CreateAlphaTexture(ana, tex_type, &b, tmp, (UINT32)(w * h * bpp)))) {
        ana->lpVtbl->Release(ana);
        return;
    }
    ana->lpVtbl->Release(ana);

    int ax, ay;
    if (!atlas_alloc(w, h, &ax, &ay)) {
        F.overflow = true;
        return;
    }
    for (int y = 0; y < h; y++) {
        u8 *dst = R.glyph_cpu + (size_t)(ay + y) * GLYPH_ATLAS + ax;
        if (bpp == 1) {
            memcpy(dst, tmp + y * w, (size_t)w);
        } else {
            const u8 *src = tmp + (size_t)y * w * 3;
            for (int x = 0; x < w; x++) dst[x] = (u8)(((int)src[x * 3] + src[x * 3 + 1] + src[x * 3 + 2]) / 3);
        }
    }
    r_glyph_dirty(ax, ay, w, h);
    out->x = (u16)ax;
    out->y = (u16)ay;
    out->w = (u16)w;
    out->h = (u16)h;
    out->ox = (i16)b.left;
    out->oy = (i16)b.top;
}

static GlyphRaster *glyph_raster(int face_id, u16 glyph, f32 size_px, int phase)
{
    u32 size_q = (u32)(size_px * 16.f + 0.5f);
    u64 key = ((u64)face_id << 48) | ((u64)(size_q & 0xFFFF) << 32) | ((u64)glyph << 16) | (u64)(phase & 3) | 0x8000ull;
    u32 h = (u32)hash_u64(key) & (GRAST_SIZE - 1);
    for (int probe = 0; probe < GRAST_SIZE; probe++) {
        GlyphRaster *e = &g_grast[(h + probe) & (GRAST_SIZE - 1)];
        if (e->key == key) return e;
        if (!e->key) {
            if (g_grast_count > GRAST_SIZE * 3 / 4) {
                F.overflow = true;
                return NULL;
            }
            Face *f = face_get(face_id);
            if (!f) return NULL;
            glyph_rasterize(e, f, glyph, size_px, phase);
            if (F.overflow) return NULL;
            e->key = key;
            g_grast_count++;
            return e;
        }
    }
    F.overflow = true;
    return NULL;
}

static u32 utf16_next(const WCHAR *s, int len, int *i)
{
    u32 c = s[*i];
    (*i)++;
    if (c >= 0xD800 && c <= 0xDBFF && *i < len) {
        u32 d = s[*i];
        if (d >= 0xDC00 && d <= 0xDFFF) {
            (*i)++;
            c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00);
        }
    }
    return c;
}

static f32 font_cap_height(int font, f32 size) { return F.faces[font].cap_height / F.faces[font].upem * size; }
static f32 font_ascent(int font, f32 size) { return F.faces[font].ascent / F.faces[font].upem * size; }
static f32 font_descent(int font, f32 size) { return F.faces[font].descent / F.faces[font].upem * size; }

static f32 text_width(int font, f32 size, const WCHAR *s, int len)
{
    if (len < 0) len = wlen(s);
    f32 w = 0;
    for (int i = 0; i < len;) {
        u32 cp = utf16_next(s, len, &i);
        if (cp == '\t') cp = ' ';
        w += glyph_map(font, cp)->adv * size;
    }
    return w;
}

static void text_offsets(int font, f32 size, const WCHAR *s, int len, f32 *out)
{
    f32 w = 0;
    for (int i = 0; i < len;) {
        int start = i;
        u32 cp = utf16_next(s, len, &i);
        if (cp == '\t') cp = ' ';
        for (int k = start; k < i; k++) out[k] = w;
        w += glyph_map(font, cp)->adv * size;
    }
    out[len] = w;
}

static f32 text_draw(int font, f32 size, f32 x, f32 baseline, const WCHAR *s, int len, u32 color)
{
    if (len < 0) len = wlen(s);
    f32 pen = x;
    f32 by = floorf(baseline + 0.5f);
    const f32 inv = 1.0f / GLYPH_ATLAS;
    for (int i = 0; i < len;) {
        u32 cp = utf16_next(s, len, &i);
        if (cp == '\t') cp = ' ';
        GlyphMap *g = glyph_map(font, cp);
        if (cp != ' ') {
            f32 px = floorf(pen);
            int phase = (int)((pen - px) * 4.f + 0.5f);
            if (phase == 4) {
                phase = 0;
                px += 1;
            }
            GlyphRaster *gr = glyph_raster(g->face, g->glyph, size, phase);
            if (gr && gr->w) {
                r_glyph(px + gr->ox, by + gr->oy, gr->w, gr->h, gr->x * inv, gr->y * inv, (gr->x + gr->w) * inv, (gr->y + gr->h) * inv, color);
            }
        }
        pen += g->adv * size;
    }
    return pen;
}

static f32 text_draw_fit(int font, f32 size, f32 x, f32 baseline, f32 max_w, const WCHAR *s, int len, u32 color, bool elide_start)
{
    if (len < 0) len = wlen(s);
    if (max_w <= 0 || len <= 0) return 0;
    f32 full = text_width(font, size, s, len);
    if (full <= max_w) {
        text_draw(font, size, x, baseline, s, len, color);
        return full;
    }
    static const WCHAR ell[] = { 0x2026, 0 };
    f32 ew = text_width(font, size, ell, 1);
    f32 avail = max_w - ew;
    if (avail <= 0) return 0;
    if (!elide_start) {
        f32 w = 0;
        int cut = 0;
        for (int i = 0; i < len;) {
            u32 cp = utf16_next(s, len, &i);
            f32 a = glyph_map(font, cp == '\t' ? ' ' : cp)->adv * size;
            if (w + a > avail) break;
            w += a;
            cut = i;
        }
        while (cut > 0 && s[cut - 1] == ' ') {
            cut--;
            w -= glyph_map(font, ' ')->adv * size;
        }
        f32 end = text_draw(font, size, x, baseline, s, cut, color);
        text_draw(font, size, end, baseline, ell, 1, color);
        return w + ew;
    } else {
        f32 w = 0;
        int cut = len;
        while (cut > 0) {
            int i = cut - 1;
            if (i > 0 && s[i] >= 0xDC00 && s[i] <= 0xDFFF && s[i - 1] >= 0xD800 && s[i - 1] <= 0xDBFF) i--;
            int j = i;
            u32 cp = utf16_next(s, len, &j);
            f32 a = glyph_map(font, cp == '\t' ? ' ' : cp)->adv * size;
            if (w + a > avail) break;
            w += a;
            cut = i;
        }
        f32 end = text_draw(font, size, x, baseline, ell, 1, color);
        text_draw(font, size, end, baseline, s + cut, len - cut, color);
        return w + ew;
    }
}

static void text_draw_icon(u32 codepoint, f32 size, f32 cx, f32 cy, u32 color)
{
    WCHAR s[2] = { (WCHAR)codepoint, 0 };
    f32 w = text_width(FONT_ICON, size, s, 1);
    f32 asc = font_ascent(FONT_ICON, size), desc = font_descent(FONT_ICON, size);
    f32 baseline = cy + (asc - desc) * 0.5f;
    text_draw(FONT_ICON, size, floorf(cx - w * 0.5f + 0.5f), baseline, s, 1, color);
}
