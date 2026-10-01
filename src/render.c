typedef struct RInst {
    f32 x0, y0, x1, y1;
    f32 u0, v0, u1, v1;
    u32 color;
    f32 cx0, cy0, cx1, cy1;
    f32 radius, border, soft, mode;
} RInst;

#define GLYPH_ATLAS 2048
#define ICON_ATLAS  1024
#define RGBA(r, g, b, a) ((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | ((u32)(a) << 24))

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

static bool spring_step(f32 *x, f32 *v, f32 target, f32 omega, f32 dt)
{
    if (omega <= 0.f) {
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

static bool approach(f32 *x, f32 target, f32 rate, f32 dt)
{
    f32 d = target - *x;
    if (fabsf(d) < 0.004f) {
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

    RTarget main;
    RTarget *t;

    RInst *inst;
    u32 count, cap;
    f32 clip[4];
    f32 scale;
    f32 opacity;

    u8 *glyph_cpu;
    int dirty_x0, dirty_y0, dirty_x1, dirty_y1;
    bool ok;
} Renderer;

static Renderer R;

static const char k_shader_src[] =
    "cbuffer C : register(b0) { float4 vp; float4 gamma; float4 misc; };\n"
    "struct I { float4 r : RECT; float4 uv : UV; float4 c : COLOR; float4 clip : CLIP; float4 p : PARAM; uint vid : SV_VertexID; };\n"
    "struct V { float4 pos : SV_Position; float2 uv : UV; float2 local : LOCAL; float2 cpos : CPOS;\n"
    "  nointerpolation float2 hs : HALF; nointerpolation float4 c : COLOR;\n"
    "  nointerpolation float4 clip : CLIP; nointerpolation float4 p : PARAM; };\n"
    "V vs(I i) {\n"
    "  V o;\n"
    "  float2 t = float2(i.vid & 1, i.vid >> 1);\n"
    "  float pad = (i.p.w < 0.5) ? (i.p.z + 1.0) : 0.0;\n"
    "  float2 p = lerp(i.r.xy - pad, i.r.zw + pad, t);\n"
    "  o.cpos = p;\n"
    "  float2 ctr = 0.5 / vp.xy;\n"
    "  float2 q = ctr + (p - ctr) * vp.z;\n"
    "  o.pos = float4(q.x * vp.x * 2.0 - 1.0, 1.0 - q.y * vp.y * 2.0, 0.0, 1.0);\n"
    "  o.uv = lerp(i.uv.xy, i.uv.zw, t);\n"
    "  o.local = p - (i.r.xy + i.r.zw) * 0.5;\n"
    "  o.hs = (i.r.zw - i.r.xy) * 0.5;\n"
    "  o.c = i.c; o.clip = i.clip; o.p = i.p;\n"
    "  return o;\n"
    "}\n"
    "Texture2D<float> gtex : register(t0);\n"
    "Texture2D<float4> itex : register(t1);\n"
    "SamplerState sp : register(s0);\n"
    "SamplerState sl : register(s1);\n"
    "float sdrr(float2 p, float2 b, float r) { float2 q = abs(p) - b + r; return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r; }\n"
    "float4 ps(V i) : SV_Target {\n"
    "  float2 fp = i.cpos;\n"
    "  if (fp.x < i.clip.x || fp.y < i.clip.y || fp.x > i.clip.z || fp.y > i.clip.w) discard;\n"
    "  float4 c = i.c;\n"
    "  float a;\n"
    "  if (i.p.w < 0.5) {\n"
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

static void r_shutdown(void)
{
    r_target_release(&R.main);
    SAFE_RELEASE(R.dc);
    SAFE_RELEASE(R.factory);
    SAFE_RELEASE(R.glyph_srv);
    SAFE_RELEASE(R.glyph_tex);
    SAFE_RELEASE(R.icon_srv);
    SAFE_RELEASE(R.icon_tex);
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

static bool r_compile(PFN_D3DCompile compile, const char *entry, const char *target, ID3DBlob **out)
{
    ID3DBlob *err = NULL;
    HRESULT hr = compile(k_shader_src, sizeof(k_shader_src) - 1, "mixlauncher", NULL, NULL, entry, target,
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
    if (!r_compile(compile, "vs", "vs_4_0", &vsb) || !r_compile(compile, "ps", "ps_4_0", &psb)) {
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

    bd.ByteWidth = 48;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ID3D11Device_CreateBuffer(R.dev, &bd, NULL, &R.cbuf);

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
    if (!R.inst) {
        R.cap = 16384;
        R.inst = (RInst *)malloc(sizeof(RInst) * R.cap);
    }
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

static void r_begin(void)
{
    R.count = 0;
    R.opacity = 1.f;
    r_set_clip(0, 0, (f32)R.t->w, (f32)R.t->h);
}

static RInst *r_push(void)
{
    if (R.count >= R.cap) {
        u32 nc = R.cap * 2;
        RInst *ni = (RInst *)realloc(R.inst, sizeof(RInst) * nc);
        if (!ni) return NULL;
        R.inst = ni;
        R.cap = nc;
    }
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
        c[4] = 0.1469f / 4.f;
        c[5] = -0.8911f / 4.f;
        c[6] = 1.4644f / 4.f;
        c[7] = -0.3234f / 4.f;
        c[8] = 1.0f;
        c[9] = c[10] = c[11] = 0;
        ID3D11DeviceContext_Unmap(R.ctx, (ID3D11Resource *)R.cbuf, 0);
    }

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
    ID3D11ShaderResourceView *srvs[2] = { R.glyph_srv, R.icon_srv };
    ID3D11DeviceContext_PSSetShaderResources(R.ctx, 0, 2, srvs);
    ID3D11SamplerState *samps[2] = { R.scale != 1.0f ? R.samp_linear : R.samp_point, R.samp_linear };
    ID3D11DeviceContext_PSSetSamplers(R.ctx, 0, 2, samps);

    for (u32 done = 0; done < R.count;) {
        u32 n = MIN(R.count - done, R.ibuf_cap);
        if (FAILED(ID3D11DeviceContext_Map(R.ctx, (ID3D11Resource *)R.ibuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) break;
        memcpy(m.pData, R.inst + done, n * sizeof(RInst));
        ID3D11DeviceContext_Unmap(R.ctx, (ID3D11Resource *)R.ibuf, 0);
        ID3D11DeviceContext_DrawInstanced(R.ctx, 4, n, 0, 0);
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
