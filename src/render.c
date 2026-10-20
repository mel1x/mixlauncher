// The renderer: one instanced quad per rounded rect, glyph or icon, drawn by a single shader into a composition
// swap chain (DirectComposition) per window. Also the CPU rasterizing helpers of the taskbar windows and icons.

// One instance. mode: 0 rounded rect, 1 glyph (atlas coverage), 2 icon (atlas BGRA), 3 rounded rect turned by
// the 2x2 matrix in u0..v1, 4 rounded rect of backdrop (the screen behind the window, see r_backdrop).
// border > 0 draws only an outline that wide, soft blurs the edge (shadows).
typedef struct RInst {
    f32 x0, y0, x1, y1;
    f32 u0, v0, u1, v1;
    u32 color;
    f32 cx0, cy0, cx1, cy1;  // clip
    f32 radius, border, soft, mode;
} RInst;

#define GLYPH_ATLAS 2048
#define ICON_ATLAS  1024
#define RGBA(r, g, b, a) ((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | ((u32)(a) << 24))

// ---- CPU rasterizing: signed distance to a rounded box, premultiplied "over"

static f32 sd_box(f32 px, f32 py, f32 hx, f32 hy, f32 r)
{
    f32 qx = fabsf(px) - hx + r, qy = fabsf(py) - hy + r;
    f32 ox = MAX(qx, 0.f), oy = MAX(qy, 0.f);
    return sqrtf(ox * ox + oy * oy) + MIN(MAX(qx, qy), 0.f) - r;
}

// c (premultiplied by *a) = col at alpha over c
static void px_over(f32 *c, f32 *a, const f32 *col, f32 alpha)
{
    for (int k = 0; k < 3; k++) c[k] = col[k] * alpha + c[k] * (1.f - alpha);
    *a = alpha + *a * (1.f - alpha);
}

// A composited pixel as premultiplied BGRA. Never fully clear: alpha 0 would pass clicks to what is under it.
static u32 px_pack(const f32 *c, f32 a)
{
    a = MAX(a, 1.f / 255.f);
    u32 A = (u32)(a * 255.f + 0.5f);
    u32 R = (u32)(CLAMP(c[0], 0.f, a) * 255.f + 0.5f), G = (u32)(CLAMP(c[1], 0.f, a) * 255.f + 0.5f), B = (u32)(CLAMP(c[2], 0.f, a) * 255.f + 0.5f);
    return (A << 24) | (R << 16) | (G << 8) | B;
}

// The pixels of a layered window (Start button, media widget): a DIB section in a memory DC.
typedef struct Layer {
    HDC dc;
    HBITMAP bmp;
    u32 *bits;
    int w, h;
} Layer;

static void layer_free(Layer *l)
{
    if (l->dc) DeleteDC(l->dc);
    if (l->bmp) DeleteObject(l->bmp);
    memset(l, 0, sizeof *l);
}

// Makes it w x h (its pixels undefined after a change). False when it could not.
static bool layer_fit(Layer *l, int w, int h)
{
    if (l->bmp && l->w == w && l->h == h) return true;
    layer_free(l);
    BITMAPINFO bi = dib32(w, h);
    l->dc = CreateCompatibleDC(NULL);
    l->bmp = CreateDIBSection(l->dc, &bi, DIB_RGB_COLORS, (void **)&l->bits, NULL, 0);
    if (!l->bmp) {
        layer_free(l);
        return false;
    }
    SelectObject(l->dc, l->bmp);
    l->w = w;
    l->h = h;
    return true;
}

static void layer_show(const Layer *l, HWND wnd)
{
    SIZE size = { l->w, l->h };
    POINT src = { 0, 0 };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    if (wnd) UpdateLayeredWindow(wnd, NULL, NULL, &size, l->dc, &src, 0, &bf, ULW_ALPHA);
}

// ---- boxes: b = { x0, y0, x1, y1 }

static bool in_box(const f32 *b, f32 x, f32 y) { return x >= b[0] && x < b[2] && y >= b[1] && y < b[3]; }

static void box_set(f32 *b, f32 x, f32 y, f32 w, f32 h)
{
    b[0] = x;
    b[1] = y;
    b[2] = x + w;
    b[3] = y + h;
}

static u32 color_alpha(u32 c, f32 k)
{
    u32 a = (u32)((f32)(c >> 24) * CLAMP(k, 0.f, 1.f) + 0.5f);
    return (c & 0x00FFFFFFu) | (a << 24);
}

static u32 color_mix(u32 a, u32 b, f32 t)
{
    t = CLAMP(t, 0.f, 1.f);
    u32 r = 0;
    for (int k = 0; k < 32; k += 8) {
        f32 x = (f32)((a >> k) & 255), y = (f32)((b >> k) & 255);
        r |= (u32)(x + (y - x) * t + 0.5f) << k;
    }
    return r;
}

// ---- motion. Both helpers jump straight to the target under reduced motion and return whether still moving.

// A critically damped spring: omega sets how fast it arrives.
static bool spring_step(f32 *x, f32 *v, f32 target, f32 omega, f32 dt)
{
    if (omega <= 0.f || g_cfg.reduced_motion) {
        *x = target;
        *v = 0;
        return false;
    }
    f32 d = *x - target;
    f32 e = expf(-omega * dt), tmp = (*v + omega * d) * dt;
    d = (d + tmp) * e;
    *v = (*v - omega * tmp) * e;
    *x = target + d;
    if (fabsf(d) < 0.2f && fabsf(*v) < 4.f) {
        *x = target;
        *v = 0;
        return false;
    }
    return true;
}

// Exponential ease toward the target.
static bool approach(f32 *x, f32 target, f32 rate, f32 dt)
{
    f32 d = target - *x;
    if (fabsf(d) < 0.004f || g_cfg.reduced_motion) {
        *x = target;
        return false;
    }
    *x += d * (1.f - expf(-rate * dt));
    return true;
}

static f32 ease_out_cubic(f32 t)
{
    t = 1.f - CLAMP(t, 0.f, 1.f);
    return 1.f - t * t * t;
}

static f32 ease_out_quart(f32 t)
{
    t = 1.f - CLAMP(t, 0.f, 1.f);
    return 1.f - t * t * t * t;
}

typedef struct RTarget {
    HWND hwnd;
    IDXGISwapChain1 *swap;
    HANDLE frame_wait;
    DCTarget *dct;
    DCVisual *dcv;
    ID3D11RenderTargetView *rtv;
    int w, h;
    UINT swap_flags;
    bool ok;
} RTarget;

// A texture the backdrop's passes read, and (with rtv) write.
typedef struct RTex {
    ID3D11Texture2D *tex;
    ID3D11ShaderResourceView *srv;
    ID3D11RenderTargetView *rtv;
    int w, h;
} RTex;

typedef struct Renderer {
    ID3D11Device *dev;
    ID3D11DeviceContext *ctx;
    IDXGIFactory2 *factory;
    DCDevice *dc;
    ID3D11VertexShader *vs;
    ID3D11PixelShader *ps;
    ID3D11InputLayout *layout;
    ID3D11Buffer *ibuf;
    UINT ibuf_cap;
    ID3D11Buffer *cbuf;
    ID3D11BlendState *blend;
    ID3D11RasterizerState *raster;
    ID3D11SamplerState *samp_point, *samp_linear;
    ID3D11Texture2D *glyph_tex;
    ID3D11ShaderResourceView *glyph_srv;
    ID3D11Texture2D *icon_tex;
    ID3D11ShaderResourceView *icon_srv;
    ID3D11VertexShader *fx_vs;
    ID3D11PixelShader *fx_down, *fx_blur;
    ID3D11Buffer *fx_cbuf;
    // The backdrop: the screen behind the window (src), halved, quartered and blurred there (quarter_b; quarter_t
    // holds the blur's first pass).
    RTex bd_src, bd_half, bd_quarter, bd_quarter_t, bd_quarter_b;
    f32 bd_sigma;  // the blur, src px
    bool bd_dirty; // src changed: blur again before the frame
    f32 bd[8];     // the backdrop's constants (bd0, bd1 in the shader)

    RTarget main;
    RTarget *t;

    RInst *inst;
    int count, cap;
    f32 clip[4];
    f32 scale;
    f32 opacity;

    u8 *glyph_cpu;
    int dirty_x0, dirty_y0, dirty_x1, dirty_y1;
    bool ok;
} Renderer;

static Renderer R;

// The vertex shader spreads an instance over its quad (grown by the soft edge) and applies the open/close zoom
// (vp.z); the pixel shader clips, then shades the rect's distance field, a contrast-boosted glyph, an icon or the
// backdrop. The backdrop is the blurred screen behind the window, sampled
// where each pixel really is on the screen. bd0: window px to source px, 1 / source size; bd1: kind, dark.
//   glass   - the blur, more saturated (vibrancy, as macOS has it), under the theme's tint
//   acrylic - the Fluent recipe: a luminosity layer, then the tint (Windows 11 base acrylic, a little clearer)
static const char k_shader_src[] =
    "cbuffer C : register(b0) { float4 vp; float4 gamma; float4 misc; float4 bd0; float4 bd1; };\n"
    "struct I { float4 r : RECT; float4 uv : UV; float4 c : COLOR; float4 clip : CLIP; float4 p : PARAM; uint vid : SV_VertexID; };\n"
    "struct V { float4 pos : SV_Position; float2 uv : UV; float2 local : LOCAL; float2 cpos : CPOS;\n"
    "  nointerpolation float2 hs : HALF; nointerpolation float4 c : COLOR;\n"
    "  nointerpolation float4 clip : CLIP; nointerpolation float4 p : PARAM; };\n"
    "V vs(I i) {\n"
    "  V o;\n"
    "  float2 t = float2(i.vid & 1, i.vid >> 1);\n"
    "  bool sdf = i.p.w < 0.5 || i.p.w > 2.5;\n"
    "  float pad = sdf ? (i.p.z + 1.0) : 0.0;\n"
    "  float2 p = lerp(i.r.xy - pad, i.r.zw + pad, t);\n"
    "  float2 mid = (i.r.xy + i.r.zw) * 0.5;\n"
    "  o.local = p - mid;\n"
    "  if (i.p.w > 2.5 && i.p.w < 3.5) p = mid + o.local.x * i.uv.xy + o.local.y * i.uv.zw;\n"
    "  o.cpos = p;\n"
    "  float2 ctr = 0.5 / vp.xy;\n"
    "  float2 q = ctr + (p - ctr) * vp.z;\n"
    "  o.pos = float4(q.x * vp.x * 2.0 - 1.0, 1.0 - q.y * vp.y * 2.0, 0.0, 1.0);\n"
    "  o.uv = lerp(i.uv.xy, i.uv.zw, t);\n"
    "  o.hs = (i.r.zw - i.r.xy) * 0.5;\n"
    "  o.c = i.c; o.clip = i.clip; o.p = i.p;\n"
    "  return o;\n"
    "}\n"
    "Texture2D<float> gtex : register(t0);\n"
    "Texture2D<float4> itex : register(t1);\n"
    "Texture2D<float4> btex : register(t2);\n"
    "SamplerState sp : register(s0);\n"
    "SamplerState sl : register(s1);\n"
    "float sdrr(float2 p, float2 b, float r) { float2 q = abs(p) - b + r; return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r; }\n"
    "float lum(float3 c) { return dot(c, float3(0.30, 0.59, 0.11)); }\n"
    "float3 clip_color(float3 c) {\n"
    "  float l = lum(c), n = min(c.r, min(c.g, c.b)), x = max(c.r, max(c.g, c.b));\n"
    "  if (n < 0.0) c = l + (c - l) * l / max(l - n, 1e-4);\n"
    "  if (x > 1.0) c = l + (c - l) * (1.0 - l) / max(x - l, 1e-4);\n"
    "  return c;\n"
    "}\n"
    "float3 with_lum(float3 c, float l) { return clip_color(c + (l - lum(c))); }\n"
    "float4 backdrop(V i) {\n"
    "  float r = min(i.p.x, min(i.hs.x, i.hs.y));\n"
    "  float a = saturate(0.5 - sdrr(i.local, i.hs, r)) * i.c.a;\n"
    "  float3 c = btex.SampleLevel(sl, (i.pos.xy + bd0.xy) * bd0.zw, 0).rgb;\n"
    "  bool dark = bd1.y > 0.5;\n"
    "  if (bd1.x < 0.5) {\n"
    "    c = lerp(lum(c).xxx, c, 1.5);\n"
    "  } else {\n"
    "    float3 tint = dark ? float3(0.173, 0.173, 0.173) : float3(0.988, 0.988, 0.988);\n"
    "    c = lerp(c, with_lum(c, lum(tint)), dark ? 0.88 : 0.78);\n"
    "    c = lerp(c, with_lum(tint, lum(c)), dark ? 0.15 : 0.0);\n"
    "  }\n"
    "  return float4(saturate(c) * a, a);\n"
    "}\n"
    "float4 ps(V i) : SV_Target {\n"
    "  float2 fp = i.cpos;\n"
    "  if (fp.x < i.clip.x || fp.y < i.clip.y || fp.x > i.clip.z || fp.y > i.clip.w) discard;\n"
    "  if (i.p.w > 3.5) return backdrop(i);\n"
    "  float4 c = i.c;\n"
    "  float a;\n"
    "  if (i.p.w < 0.5 || i.p.w > 2.5) {\n"
    "    float r = min(i.p.x, min(i.hs.x, i.hs.y));\n"
    "    float d = sdrr(i.local, i.hs, r);\n"
    "    if (i.p.z > 0.0) a = 1.0 - smoothstep(-i.p.z, i.p.z, d);\n"
    "    else a = saturate(0.5 - d);\n"
    "    if (i.p.y > 0.0) a *= saturate(d + i.p.y + 0.5);\n"
    "  } else if (i.p.w < 1.5) {\n"
    "    float g = gtex.Sample(sp, i.uv);\n"
    "    float k = misc.x * saturate(4.0 * (0.75 - dot(c.rgb, float3(0.30, 0.59, 0.11))));\n"
    "    g = g * (k + 1.0) / (g * k + 1.0);\n"
    "    float f = dot(c.rgb, float3(0.25, 0.5, 0.25));\n"
    "    g = g + g * (1.0 - g) * ((gamma.x * f + gamma.y) * g + (gamma.z * f + gamma.w));\n"
    "    a = saturate(g);\n"
    "  } else {\n"
    "    return itex.Sample(sl, i.uv) * c.a;\n"
    "  }\n"
    "  a *= c.a;\n"
    "  return float4(c.rgb * a, a);\n"
    "}\n";

// The backdrop's blur passes over a full-target triangle: fdown halves (a 2x2 box: one bilinear tap between four
// texels), fblur is one direction of a gaussian. fx: one texel along the blur, sigma, taps each way.
static const char k_fx_src[] =
    "struct F { float4 pos : SV_Position; float2 uv : UV; };\n"
    "F fvs(uint id : SV_VertexID) {\n"
    "  F o;\n"
    "  float2 t = float2((id << 1) & 2, id & 2);\n"
    "  o.uv = t;\n"
    "  o.pos = float4(t.x * 2.0 - 1.0, 1.0 - t.y * 2.0, 0.0, 1.0);\n"
    "  return o;\n"
    "}\n"
    "cbuffer X : register(b0) { float4 fx; };\n"
    "Texture2D<float4> src : register(t0);\n"
    "SamplerState sl : register(s0);\n"
    "float4 fdown(F i) : SV_Target { return float4(src.SampleLevel(sl, i.uv, 0).rgb, 1.0); }\n"
    "float4 fblur(F i) : SV_Target {\n"
    "  float3 acc = 0;\n"
    "  float sum = 0;\n"
    "  int n = (int)fx.w;\n"
    "  [loop] for (int j = -n; j <= n; j++) {\n"
    "    float w = exp(-0.5 * j * j / (fx.z * fx.z));\n"
    "    acc += w * src.SampleLevel(sl, i.uv + fx.xy * j, 0).rgb;\n"
    "    sum += w;\n"
    "  }\n"
    "  return float4(acc / sum, 1.0);\n"
    "}\n";

static bool r_target_views(RTarget *t)
{
    ID3D11Texture2D *back = NULL;
    if (FAILED(IDXGISwapChain1_GetBuffer(t->swap, 0, &ML_IID_ID3D11Texture2D, (void **)&back))) return false;
    HRESULT hr = ID3D11Device_CreateRenderTargetView(R.dev, (ID3D11Resource *)back, NULL, &t->rtv);
    ID3D11Texture2D_Release(back);
    return SUCCEEDED(hr);
}

static void r_target_release(RTarget *t)
{
    if (R.ctx) ID3D11DeviceContext_OMSetRenderTargets(R.ctx, 0, NULL, NULL);
    SAFE_RELEASE(t->rtv);
    SAFE_RELEASE(t->dcv);
    SAFE_RELEASE(t->dct);
    if (t->frame_wait) {
        CloseHandle(t->frame_wait);
        t->frame_wait = NULL;
    }
    SAFE_RELEASE(t->swap);
    if (R.dc) R.dc->lpVtbl->Commit(R.dc);
    t->ok = false;
}

static bool r_target_init(RTarget *t, HWND hwnd, int w, int h)
{
    memset(t, 0, sizeof *t);
    t->hwnd = hwnd;
    t->w = MAX(w, 1);
    t->h = MAX(h, 1);
    if (!R.dev || !R.factory || !R.dc) return false;
    DXGI_SWAP_CHAIN_DESC1 sd = { 0 };
    sd.Width = (UINT)t->w;
    sd.Height = (UINT)t->h;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.Scaling = DXGI_SCALING_STRETCH;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    HRESULT hr = IDXGIFactory2_CreateSwapChainForComposition(R.factory, (IUnknown *)R.dev, &sd, NULL, &t->swap);
    if (FAILED(hr)) {
        sd.Flags = 0;
        hr = IDXGIFactory2_CreateSwapChainForComposition(R.factory, (IUnknown *)R.dev, &sd, NULL, &t->swap);
    }
    if (FAILED(hr)) {
        log_msg("CreateSwapChainForComposition failed 0x%08lx", hr);
        return false;
    }
    t->swap_flags = sd.Flags;
    if (sd.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT) {
        IDXGISwapChain2 *sc2 = NULL;
        if (SUCCEEDED(IDXGISwapChain1_QueryInterface(t->swap, &ML_IID_IDXGISwapChain2, (void **)&sc2))) {
            IDXGISwapChain2_SetMaximumFrameLatency(sc2, 1);
            t->frame_wait = IDXGISwapChain2_GetFrameLatencyWaitableObject(sc2);
            IDXGISwapChain2_Release(sc2);
        }
    }
    if (FAILED(R.dc->lpVtbl->CreateTargetForHwnd(R.dc, hwnd, TRUE, &t->dct)) ||
        FAILED(R.dc->lpVtbl->CreateVisual(R.dc, &t->dcv)) ||
        FAILED(t->dcv->lpVtbl->SetContent(t->dcv, (IUnknown *)t->swap)) ||
        FAILED(t->dct->lpVtbl->SetRoot(t->dct, t->dcv)) ||
        FAILED(R.dc->lpVtbl->Commit(R.dc)) || !r_target_views(t)) {
        log_msg("DirectComposition target setup failed");
        r_target_release(t);
        return false;
    }
    t->ok = true;
    return true;
}

static void r_target_resize(RTarget *t, int w, int h)
{
    w = MAX(w, 1);
    h = MAX(h, 1);
    if (!R.ok || !t->ok || (w == t->w && h == t->h)) return;
    t->w = w;
    t->h = h;
    ID3D11DeviceContext_OMSetRenderTargets(R.ctx, 0, NULL, NULL);
    SAFE_RELEASE(t->rtv);
    HRESULT hr = IDXGISwapChain1_ResizeBuffers(t->swap, 0, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN, t->swap_flags);
    if (FAILED(hr) || !r_target_views(t)) {
        log_msg("ResizeBuffers failed 0x%08lx", hr);
        t->ok = false;
        R.ok = false;
    }
}

static void r_resize(int w, int h) { r_target_resize(&R.main, w, h); }

static void r_use(RTarget *t) { R.t = t; }

static void r_bd_free(void);

static void r_shutdown(void)
{
    r_target_release(&R.main);
    SAFE_RELEASE(R.dc);
    SAFE_RELEASE(R.factory);
    SAFE_RELEASE(R.glyph_srv);
    SAFE_RELEASE(R.glyph_tex);
    SAFE_RELEASE(R.icon_srv);
    SAFE_RELEASE(R.icon_tex);
    r_bd_free();
    SAFE_RELEASE(R.fx_cbuf);
    SAFE_RELEASE(R.fx_blur);
    SAFE_RELEASE(R.fx_down);
    SAFE_RELEASE(R.fx_vs);
    SAFE_RELEASE(R.samp_point);
    SAFE_RELEASE(R.samp_linear);
    SAFE_RELEASE(R.raster);
    SAFE_RELEASE(R.blend);
    SAFE_RELEASE(R.cbuf);
    SAFE_RELEASE(R.ibuf);
    SAFE_RELEASE(R.layout);
    SAFE_RELEASE(R.ps);
    SAFE_RELEASE(R.vs);
    SAFE_RELEASE(R.ctx);
    SAFE_RELEASE(R.dev);
    R.ok = false;
}

static bool r_compile(PFN_D3DCompile compile, const char *src, const char *entry, const char *target, ID3DBlob **out)
{
    ID3DBlob *err = NULL;
    HRESULT hr = compile(src, strlen(src), "mixlauncher", NULL, NULL, entry, target,
                         (1 << 15) /*OPTIMIZATION_LEVEL3*/, 0, out, &err);
    if (FAILED(hr)) {
        if (err) {
            log_msg("shader %s: %s", entry, (const char *)ID3D10Blob_GetBufferPointer(err));
            ID3D10Blob_Release(err);
        }
        return false;
    }
    SAFE_RELEASE(err);
    return true;
}

static bool r_init(HWND hwnd, int w, int h)
{
    memset(&R.dev, 0, offsetof(Renderer, inst) - offsetof(Renderer, dev));
    R.scale = 1.0f;
    R.opacity = 1.0f;

    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS;
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, levels, countof(levels), D3D11_SDK_VERSION, &R.dev, NULL, &R.ctx);
    if (FAILED(hr)) hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, levels + 1, countof(levels) - 1, D3D11_SDK_VERSION, &R.dev, NULL, &R.ctx);
    if (FAILED(hr)) hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, flags, levels + 1, countof(levels) - 1, D3D11_SDK_VERSION, &R.dev, NULL, &R.ctx);
    if (FAILED(hr)) {
        log_msg("D3D11CreateDevice failed 0x%08lx", hr);
        return false;
    }

    IDXGIDevice *dxgi_dev = NULL;
    IDXGIAdapter *adapter = NULL;
    ID3D11Device_QueryInterface(R.dev, &ML_IID_IDXGIDevice, (void **)&dxgi_dev);
    if (dxgi_dev) IDXGIDevice_GetAdapter(dxgi_dev, &adapter);
    if (adapter) IDXGIAdapter_GetParent(adapter, &ML_IID_IDXGIFactory2, (void **)&R.factory);
    SAFE_RELEASE(adapter);
    PFN_DCompositionCreateDevice dcreate = (PFN_DCompositionCreateDevice)(void *)GetProcAddress(LoadLibraryW(L"dcomp.dll"), "DCompositionCreateDevice");
    if (dxgi_dev && dcreate) dcreate(dxgi_dev, &ML_IID_IDCompositionDevice, (void **)&R.dc);
    SAFE_RELEASE(dxgi_dev);
    if (!R.factory || !R.dc) {
        log_msg("no IDXGIFactory2 / DirectComposition");
        r_shutdown();
        return false;
    }

    static PFN_D3DCompile compile;
    if (!compile) {
        HMODULE m = LoadLibraryW(L"d3dcompiler_47.dll");
        if (m) compile = (PFN_D3DCompile)(void *)GetProcAddress(m, "D3DCompile");
    }
    if (!compile) {
        log_msg("d3dcompiler_47.dll not available");
        r_shutdown();
        return false;
    }
    ID3DBlob *vsb = NULL, *psb = NULL;
    if (!r_compile(compile, k_shader_src, "vs", "vs_4_0", &vsb) || !r_compile(compile, k_shader_src, "ps", "ps_4_0", &psb)) {
        SAFE_RELEASE(vsb);
        r_shutdown();
        return false;
    }
    ID3D11Device_CreateVertexShader(R.dev, ID3D10Blob_GetBufferPointer(vsb), ID3D10Blob_GetBufferSize(vsb), NULL, &R.vs);
    ID3D11Device_CreatePixelShader(R.dev, ID3D10Blob_GetBufferPointer(psb), ID3D10Blob_GetBufferSize(psb), NULL, &R.ps);
    D3D11_INPUT_ELEMENT_DESC il[] = {
        { "RECT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(RInst, x0),     D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "UV",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(RInst, u0),     D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM,     0, offsetof(RInst, color),  D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "CLIP",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(RInst, cx0),    D3D11_INPUT_PER_INSTANCE_DATA, 1 },
        { "PARAM", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(RInst, radius), D3D11_INPUT_PER_INSTANCE_DATA, 1 },
    };
    ID3D11Device_CreateInputLayout(R.dev, il, countof(il), ID3D10Blob_GetBufferPointer(vsb), ID3D10Blob_GetBufferSize(vsb), &R.layout);
    ID3D10Blob_Release(vsb);
    ID3D10Blob_Release(psb);
    ID3DBlob *fx[3] = { NULL, NULL, NULL };
    if (r_compile(compile, k_fx_src, "fvs", "vs_4_0", &fx[0]) && r_compile(compile, k_fx_src, "fdown", "ps_4_0", &fx[1]) &&
        r_compile(compile, k_fx_src, "fblur", "ps_4_0", &fx[2])) {
        ID3D11Device_CreateVertexShader(R.dev, ID3D10Blob_GetBufferPointer(fx[0]), ID3D10Blob_GetBufferSize(fx[0]), NULL, &R.fx_vs);
        ID3D11Device_CreatePixelShader(R.dev, ID3D10Blob_GetBufferPointer(fx[1]), ID3D10Blob_GetBufferSize(fx[1]), NULL, &R.fx_down);
        ID3D11Device_CreatePixelShader(R.dev, ID3D10Blob_GetBufferPointer(fx[2]), ID3D10Blob_GetBufferSize(fx[2]), NULL, &R.fx_blur);
    }
    for (int i = 0; i < 3; i++) SAFE_RELEASE(fx[i]);
    if (!R.vs || !R.ps || !R.layout) {
        r_shutdown();
        return false;
    }

    R.ibuf_cap = 8192;
    D3D11_BUFFER_DESC bd = { 0 };
    bd.ByteWidth = R.ibuf_cap * sizeof(RInst);
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    ID3D11Device_CreateBuffer(R.dev, &bd, NULL, &R.ibuf);

    bd.ByteWidth = 80;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ID3D11Device_CreateBuffer(R.dev, &bd, NULL, &R.cbuf);
    bd.ByteWidth = 16;
    ID3D11Device_CreateBuffer(R.dev, &bd, NULL, &R.fx_cbuf);

    D3D11_BLEND_DESC blend = { 0 };
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    ID3D11Device_CreateBlendState(R.dev, &blend, &R.blend);

    D3D11_RASTERIZER_DESC rs = { 0 };
    rs.FillMode = D3D11_FILL_SOLID;
    rs.CullMode = D3D11_CULL_NONE;
    rs.DepthClipEnable = TRUE;
    ID3D11Device_CreateRasterizerState(R.dev, &rs, &R.raster);

    D3D11_SAMPLER_DESC ss = { 0 };
    ss.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    ss.AddressU = ss.AddressV = ss.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    ss.MaxLOD = D3D11_FLOAT32_MAX;
    ID3D11Device_CreateSamplerState(R.dev, &ss, &R.samp_point);
    ss.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    ID3D11Device_CreateSamplerState(R.dev, &ss, &R.samp_linear);

    D3D11_TEXTURE2D_DESC td = { 0 };
    td.Width = td.Height = GLYPH_ATLAS;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    ID3D11Device_CreateTexture2D(R.dev, &td, NULL, &R.glyph_tex);
    if (R.glyph_tex) ID3D11Device_CreateShaderResourceView(R.dev, (ID3D11Resource *)R.glyph_tex, NULL, &R.glyph_srv);
    td.Width = td.Height = ICON_ATLAS;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    ID3D11Device_CreateTexture2D(R.dev, &td, NULL, &R.icon_tex);
    if (R.icon_tex) ID3D11Device_CreateShaderResourceView(R.dev, (ID3D11Resource *)R.icon_tex, NULL, &R.icon_srv);

    if (!R.ibuf || !R.cbuf || !R.blend || !R.raster || !R.samp_point || !R.samp_linear || !R.glyph_srv || !R.icon_srv ||
        !r_target_init(&R.main, hwnd, w, h)) {
        log_msg("D3D resource creation failed");
        r_shutdown();
        return false;
    }
    R.t = &R.main;

    if (!R.glyph_cpu) R.glyph_cpu = (u8 *)calloc(GLYPH_ATLAS, GLYPH_ATLAS);
    R.dirty_x0 = 0;
    R.dirty_y0 = 0;
    R.dirty_x1 = GLYPH_ATLAS;
    R.dirty_y1 = GLYPH_ATLAS;
    R.ok = true;
    return true;
}

static void r_wait_frame(void)
{
    if (R.ok && R.t && R.t->frame_wait) WaitForSingleObjectEx(R.t->frame_wait, 100, TRUE);
}

static void r_set_clip(f32 x0, f32 y0, f32 x1, f32 y1)
{
    R.clip[0] = x0;
    R.clip[1] = y0;
    R.clip[2] = x1;
    R.clip[3] = y1;
}

static void r_clip_all(void) { r_set_clip(0, 0, (f32)R.t->w, (f32)R.t->h); }

static void r_begin(void)
{
    R.count = 0;
    R.opacity = 1.f;
    r_clip_all();
}

static RInst *r_push(void)
{
    if (!GROW(R.inst, R.cap, R.count + 1, 16384)) return NULL;
    RInst *q = &R.inst[R.count++];
    q->cx0 = R.clip[0];
    q->cy0 = R.clip[1];
    q->cx1 = R.clip[2];
    q->cy1 = R.clip[3];
    return q;
}

static u32 r_fade(u32 color)
{
    return R.opacity < 1.f ? color_alpha(color, R.opacity) : color;
}

static void r_rect_ex(f32 x, f32 y, f32 w, f32 h, u32 color, f32 radius, f32 border, f32 soft)
{
    color = r_fade(color);
    if ((color >> 24) == 0) return;
    RInst *q = r_push();
    if (!q) return;
    q->x0 = x;
    q->y0 = y;
    q->x1 = x + w;
    q->y1 = y + h;
    q->u0 = q->v0 = q->u1 = q->v1 = 0;
    q->color = color;
    q->radius = radius;
    q->border = border;
    q->soft = soft;
    q->mode = 0;
}

static void r_rect(f32 x, f32 y, f32 w, f32 h, u32 color, f32 radius) { r_rect_ex(x, y, w, h, color, radius, 0, 0); }

// A hairline outline.
static void r_border(f32 x, f32 y, f32 w, f32 h, u32 color, f32 radius) { r_rect_ex(x, y, w, h, color, radius, 1.f, 0); }

// A floating panel (menu, dialog, tooltip): a soft shadow shadow_dy below, the fill and a hairline border.
static void r_panel(f32 x, f32 y, f32 w, f32 h, f32 radius, u32 fill, u32 border, u32 shadow, f32 shadow_dy, f32 shadow_soft)
{
    r_rect_ex(x, y + shadow_dy, w, h, shadow, radius, 0, shadow_soft);
    r_rect(x, y, w, h, fill, radius);
    r_border(x, y, w, h, border, radius);
}

// A rounded rect centered at (cx, cy) mapped by the 2x2 matrix m: its local x axis goes to (m[0], m[1]), y to (m[2], m[3]).
static void r_rect_affine(f32 cx, f32 cy, f32 w, f32 h, const f32 m[4], u32 color, f32 radius)
{
    color = r_fade(color);
    if ((color >> 24) == 0) return;
    RInst *q = r_push();
    if (!q) return;
    q->x0 = cx - w * 0.5f;
    q->y0 = cy - h * 0.5f;
    q->x1 = cx + w * 0.5f;
    q->y1 = cy + h * 0.5f;
    q->u0 = m[0];
    q->v0 = m[1];
    q->u1 = m[2];
    q->v1 = m[3];
    q->color = color;
    q->radius = radius;
    q->border = 0;
    q->soft = 0;
    q->mode = 3;
}

// A rounded rect centered at (cx, cy), squashed horizontally by sx (a tumble in depth) and then rotated by angle.
static void r_rect_rot(f32 cx, f32 cy, f32 w, f32 h, f32 angle, f32 sx, u32 color, f32 radius)
{
    f32 c = cosf(angle), s = sinf(angle), m[4] = { c * sx, s * sx, -s, c };
    r_rect_affine(cx, cy, w, h, m, color, radius);
}

static void r_glyph(f32 x, f32 y, f32 w, f32 h, f32 u0, f32 v0, f32 u1, f32 v1, u32 color)
{
    color = r_fade(color);
    if ((color >> 24) == 0) return;
    RInst *q = r_push();
    if (!q) return;
    q->x0 = x;
    q->y0 = y;
    q->x1 = x + w;
    q->y1 = y + h;
    q->u0 = u0;
    q->v0 = v0;
    q->u1 = u1;
    q->v1 = v1;
    q->color = color;
    q->radius = q->border = q->soft = 0;
    q->mode = 1;
}

static void r_icon(f32 x, f32 y, f32 w, f32 h, f32 u0, f32 v0, f32 u1, f32 v1, f32 opacity)
{
    RInst *q = r_push();
    if (!q) return;
    q->x0 = x;
    q->y0 = y;
    q->x1 = x + w;
    q->y1 = y + h;
    q->u0 = u0;
    q->v0 = v0;
    q->u1 = u1;
    q->v1 = v1;
    q->color = RGBA(255, 255, 255, (u32)(CLAMP(opacity * R.opacity, 0.f, 1.f) * 255.f + 0.5f));
    q->radius = q->border = q->soft = 0;
    q->mode = 2;
}

// A rounded rect of the backdrop (R.bd_src, blurred), shaded as R.bd says.
static void r_backdrop(f32 x, f32 y, f32 w, f32 h, f32 radius)
{
    if (!R.bd_src.srv || !R.fx_blur) return;
    RInst *q = r_push();
    if (!q) return;
    q->x0 = x;
    q->y0 = y;
    q->x1 = x + w;
    q->y1 = y + h;
    q->u0 = q->v0 = q->u1 = q->v1 = 0;
    q->color = RGBA(255, 255, 255, (u32)(CLAMP(R.opacity, 0.f, 1.f) * 255.f + 0.5f));
    q->radius = radius;
    q->border = q->soft = 0;
    q->mode = 4;
}

// ---- the backdrop's textures and blur passes

static void rtex_free(RTex *t)
{
    SAFE_RELEASE(t->rtv);
    SAFE_RELEASE(t->srv);
    SAFE_RELEASE(t->tex);
    t->w = t->h = 0;
}

// Makes t w x h BGRA (its pixels undefined after a change), a render target too with target.
static bool rtex_fit(RTex *t, int w, int h, bool target)
{
    w = MAX(w, 1);
    h = MAX(h, 1);
    if (t->tex && t->w == w && t->h == h) return true;
    rtex_free(t);
    D3D11_TEXTURE2D_DESC td = { 0 };
    td.Width = (UINT)w;
    td.Height = (UINT)h;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE | (target ? D3D11_BIND_RENDER_TARGET : 0);
    if (FAILED(ID3D11Device_CreateTexture2D(R.dev, &td, NULL, &t->tex)) ||
        FAILED(ID3D11Device_CreateShaderResourceView(R.dev, (ID3D11Resource *)t->tex, NULL, &t->srv)) ||
        (target && FAILED(ID3D11Device_CreateRenderTargetView(R.dev, (ID3D11Resource *)t->tex, NULL, &t->rtv)))) {
        rtex_free(t);
        return false;
    }
    t->w = w;
    t->h = h;
    return true;
}

static void r_bd_free(void)
{
    RTex *all[] = { &R.bd_src, &R.bd_half, &R.bd_quarter, &R.bd_quarter_t, &R.bd_quarter_b };
    for (int i = 0; i < countof(all); i++) rtex_free(all[i]);
}

// The source: w x h px of the screen, copied in (screen capture) or uploaded (r_bd_upload).
static bool r_bd_source(int w, int h)
{
    return R.ok && R.fx_blur && rtex_fit(&R.bd_src, w, h, false);
}

static void r_bd_upload(const u32 *bgra)
{
    ID3D11DeviceContext_UpdateSubresource(R.ctx, (ID3D11Resource *)R.bd_src.tex, 0, NULL, bgra, (UINT)R.bd_src.w * 4, 0);
    R.bd_dirty = true;
}

// One full-target pass of ps from in to out; (dx, dy) is the blur's direction.
static void r_fx(ID3D11PixelShader *ps, RTex *in, RTex *out, f32 dx, f32 dy, f32 sigma)
{
    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(ID3D11DeviceContext_Map(R.ctx, (ID3D11Resource *)R.fx_cbuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        f32 *c = (f32 *)m.pData;
        c[0] = dx / (f32)in->w;
        c[1] = dy / (f32)in->h;
        c[2] = MAX(sigma, 0.5f);
        c[3] = ceilf(MAX(sigma, 0.5f) * 2.5f);
        ID3D11DeviceContext_Unmap(R.ctx, (ID3D11Resource *)R.fx_cbuf, 0);
    }
    ID3D11ShaderResourceView *none = NULL;
    ID3D11DeviceContext_PSSetShaderResources(R.ctx, 0, 1, &none);
    ID3D11DeviceContext_OMSetRenderTargets(R.ctx, 1, &out->rtv, NULL);
    D3D11_VIEWPORT vp = { 0, 0, (FLOAT)out->w, (FLOAT)out->h, 0, 1 };
    ID3D11DeviceContext_RSSetViewports(R.ctx, 1, &vp);
    ID3D11DeviceContext_PSSetShader(R.ctx, ps, NULL, 0);
    ID3D11DeviceContext_PSSetShaderResources(R.ctx, 0, 1, &in->srv);
    ID3D11DeviceContext_Draw(R.ctx, 3, 0);
}

// The blur of the source: halved twice, then a gaussian at a quarter size. A few hundred thousand texels: well
// under a millisecond, so it runs for every new image of the screen.
static void r_bd_blur(void)
{
    R.bd_dirty = false;
    RTex *s = &R.bd_src;
    if (!s->srv) return;
    int hw = (s->w + 1) / 2, hh = (s->h + 1) / 2, qw = (hw + 1) / 2, qh = (hh + 1) / 2;
    if (!rtex_fit(&R.bd_half, hw, hh, true) || !rtex_fit(&R.bd_quarter, qw, qh, true) || !rtex_fit(&R.bd_quarter_t, qw, qh, true) ||
        !rtex_fit(&R.bd_quarter_b, qw, qh, true))
        return;
    FLOAT bf[4] = { 0, 0, 0, 0 };
    ID3D11DeviceContext_OMSetBlendState(R.ctx, NULL, bf, 0xFFFFFFFF);
    ID3D11DeviceContext_RSSetState(R.ctx, R.raster);
    ID3D11DeviceContext_IASetInputLayout(R.ctx, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(R.ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(R.ctx, R.fx_vs, NULL, 0);
    ID3D11DeviceContext_PSSetConstantBuffers(R.ctx, 0, 1, &R.fx_cbuf);
    ID3D11DeviceContext_PSSetSamplers(R.ctx, 0, 1, &R.samp_linear);
    r_fx(R.fx_down, s, &R.bd_half, 0, 0, 0);
    r_fx(R.fx_down, &R.bd_half, &R.bd_quarter, 0, 0, 0);
    r_fx(R.fx_blur, &R.bd_quarter, &R.bd_quarter_t, 1, 0, R.bd_sigma / 4.f);
    r_fx(R.fx_blur, &R.bd_quarter_t, &R.bd_quarter_b, 0, 1, R.bd_sigma / 4.f);
    ID3D11ShaderResourceView *none = NULL;
    ID3D11DeviceContext_PSSetShaderResources(R.ctx, 0, 1, &none);
}

static void r_glyph_dirty(int x, int y, int w, int h)
{
    R.dirty_x0 = MIN(R.dirty_x0, x);
    R.dirty_y0 = MIN(R.dirty_y0, y);
    R.dirty_x1 = MAX(R.dirty_x1, x + w);
    R.dirty_y1 = MAX(R.dirty_y1, y + h);
}

static void r_upload_icon(int x, int y, int size, const u32 *bgra)
{
    if (!R.ok) return;
    D3D11_BOX box = { (UINT)x, (UINT)y, 0, (UINT)(x + size), (UINT)(y + size), 1 };
    ID3D11DeviceContext_UpdateSubresource(R.ctx, (ID3D11Resource *)R.icon_tex, 0, &box, bgra, (UINT)size * 4, 0);
}

static bool r_end_and_present(void)
{
    RTarget *t = R.t;
    if (!R.ok || !t || !t->ok) return false;

    if (R.dirty_x1 > R.dirty_x0 && R.dirty_y1 > R.dirty_y0) {
        D3D11_BOX box = { (UINT)R.dirty_x0, (UINT)R.dirty_y0, 0, (UINT)R.dirty_x1, (UINT)R.dirty_y1, 1 };
        ID3D11DeviceContext_UpdateSubresource(R.ctx, (ID3D11Resource *)R.glyph_tex, 0, &box,
                                              R.glyph_cpu + (size_t)R.dirty_y0 * GLYPH_ATLAS + R.dirty_x0, GLYPH_ATLAS, 0);
    }
    R.dirty_x0 = R.dirty_y0 = INT32_MAX;
    R.dirty_x1 = R.dirty_y1 = 0;

    D3D11_MAPPED_SUBRESOURCE m;
    if (SUCCEEDED(ID3D11DeviceContext_Map(R.ctx, (ID3D11Resource *)R.cbuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        f32 *c = (f32 *)m.pData;
        c[0] = 1.0f / (f32)t->w;
        c[1] = 1.0f / (f32)t->h;
        c[2] = R.scale > 0 ? R.scale : 1.0f;
        c[3] = 0;
        // DirectWrite's alpha correction for gamma 1.8 (the ps: g + g(1 - g)(..)) and the text contrast boost
        c[4] = 0.1469f / 4.f;
        c[5] = -0.8911f / 4.f;
        c[6] = 1.4644f / 4.f;
        c[7] = -0.3234f / 4.f;
        c[8] = 1.0f;
        c[9] = c[10] = c[11] = 0;
        memcpy(c + 12, R.bd, sizeof R.bd);
        ID3D11DeviceContext_Unmap(R.ctx, (ID3D11Resource *)R.cbuf, 0);
    }

    if (R.bd_dirty) r_bd_blur();

    FLOAT clear[4] = { 0, 0, 0, 0 };
    ID3D11DeviceContext_OMSetRenderTargets(R.ctx, 1, &t->rtv, NULL);
    ID3D11DeviceContext_ClearRenderTargetView(R.ctx, t->rtv, clear);
    D3D11_VIEWPORT vp = { 0, 0, (FLOAT)t->w, (FLOAT)t->h, 0, 1 };
    ID3D11DeviceContext_RSSetViewports(R.ctx, 1, &vp);
    ID3D11DeviceContext_RSSetState(R.ctx, R.raster);
    FLOAT bf[4] = { 0, 0, 0, 0 };
    ID3D11DeviceContext_OMSetBlendState(R.ctx, R.blend, bf, 0xFFFFFFFF);
    ID3D11DeviceContext_IASetPrimitiveTopology(R.ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D11DeviceContext_IASetInputLayout(R.ctx, R.layout);
    UINT stride = sizeof(RInst), offset = 0;
    ID3D11DeviceContext_IASetVertexBuffers(R.ctx, 0, 1, &R.ibuf, &stride, &offset);
    ID3D11DeviceContext_VSSetShader(R.ctx, R.vs, NULL, 0);
    ID3D11DeviceContext_VSSetConstantBuffers(R.ctx, 0, 1, &R.cbuf);
    ID3D11DeviceContext_PSSetShader(R.ctx, R.ps, NULL, 0);
    ID3D11DeviceContext_PSSetConstantBuffers(R.ctx, 0, 1, &R.cbuf);
    ID3D11ShaderResourceView *srvs[3] = { R.glyph_srv, R.icon_srv, R.bd_quarter_b.srv };
    ID3D11DeviceContext_PSSetShaderResources(R.ctx, 0, 3, srvs);
    ID3D11SamplerState *samps[2] = { R.scale != 1.0f ? R.samp_linear : R.samp_point, R.samp_linear };
    ID3D11DeviceContext_PSSetSamplers(R.ctx, 0, 2, samps);

    for (int done = 0; done < R.count;) {
        int n = MIN(R.count - done, (int)R.ibuf_cap);
        if (FAILED(ID3D11DeviceContext_Map(R.ctx, (ID3D11Resource *)R.ibuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) break;
        memcpy(m.pData, R.inst + done, (size_t)n * sizeof(RInst));
        ID3D11DeviceContext_Unmap(R.ctx, (ID3D11Resource *)R.ibuf, 0);
        ID3D11DeviceContext_DrawInstanced(R.ctx, 4, (UINT)n, 0, 0);
        done += n;
    }

    HRESULT hr = IDXGISwapChain1_Present(t->swap, 1, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        log_msg("device lost 0x%08lx", hr);
        R.ok = false;
        return false;
    }
    return true;
}
