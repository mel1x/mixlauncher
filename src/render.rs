// The renderer: one instanced quad per rounded rect, glyph or icon, drawn by a single shader into a composition
// swap chain (DirectComposition) per window. Also the CPU rasterizing helpers of the taskbar windows and icons.

use crate::*;
use windows::Win32::Graphics::Direct3D::*;
use windows::Win32::Graphics::Direct3D11::*;
use windows::Win32::Graphics::DirectComposition::*;
use windows::Win32::Graphics::Dxgi::Common::*;
use windows::Win32::Graphics::Dxgi::*;
use windows::core::Interface;

// One instance. mode: 0 rounded rect, 1 glyph (atlas coverage), 2 icon (atlas BGRA), 3 rounded rect turned by
// the 2x2 matrix in u0..v1, 4 rounded rect of backdrop (the screen behind the window, see r_backdrop), 5 rounded
// rect of R.pv (premultiplied BGRA, see r_image).
// border > 0 draws only an outline that wide, soft blurs the edge (shadows). A negative radius (SQUIRCLE) gives
// superellipse corners instead of circular ones.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct RInst {
    pub x0: f32,
    pub y0: f32,
    pub x1: f32,
    pub y1: f32,
    pub u0: f32,
    pub v0: f32,
    pub u1: f32,
    pub v1: f32,
    pub color: u32,
    pub cx0: f32,
    pub cy0: f32,
    pub cx1: f32,
    pub cy1: f32, // clip
    pub radius: f32,
    pub border: f32,
    pub soft: f32,
    pub mode: f32,
}

#[inline]
pub fn SQUIRCLE(r: f32) -> f32 {
    -r
}
pub const GLYPH_ATLAS: i32 = 2048;
pub const ICON_ATLAS: i32 = 1024;
#[inline]
pub const fn RGBA(r: u32, g: u32, b: u32, a: u32) -> u32 {
    r | (g << 8) | (b << 16) | (a << 24)
}

// ---- CPU rasterizing: signed distance to a rounded box, premultiplied "over"

pub fn sd_box(px: f32, py: f32, hx: f32, hy: f32, r: f32) -> f32 {
    let qx = px.abs() - hx + r;
    let qy = py.abs() - hy + r;
    let ox = max(qx, 0.0);
    let oy = max(qy, 0.0);
    (ox * ox + oy * oy).sqrt() + min(max(qx, qy), 0.0) - r
}

// c (premultiplied by *a) = col at alpha over c
pub fn px_over(c: &mut [f32; 3], a: &mut f32, col: &[f32; 3], alpha: f32) {
    for k in 0..3 {
        c[k] = col[k] * alpha + c[k] * (1.0 - alpha);
    }
    *a = alpha + *a * (1.0 - alpha);
}

// A composited pixel as premultiplied BGRA. Never fully clear: alpha 0 would pass clicks to what is under it.
pub fn px_pack(c: &[f32; 3], a: f32) -> u32 {
    let a = max(a, 1.0 / 255.0);
    let A = (a * 255.0 + 0.5) as u32;
    let R_ = (clamp(c[0], 0.0, a) * 255.0 + 0.5) as u32;
    let G = (clamp(c[1], 0.0, a) * 255.0 + 0.5) as u32;
    let B = (clamp(c[2], 0.0, a) * 255.0 + 0.5) as u32;
    (A << 24) | (R_ << 16) | (G << 8) | B
}

// The pixels of a layered window (Start button, media widget): a DIB section in a memory DC.
#[repr(C)]
pub struct Layer {
    pub dc: HDC,
    pub bmp: HBITMAP,
    pub bits: *mut u32,
    pub w: i32,
    pub h: i32,
}

pub unsafe fn layer_free(l: *mut Layer) {
    if !(*l).dc.is_null() {
        DeleteDC((*l).dc);
    }
    if !(*l).bmp.is_null() {
        DeleteObject((*l).bmp);
    }
    zero(l);
}

// Makes it w x h (its pixels undefined after a change). False when it could not.
pub unsafe fn layer_fit(l: *mut Layer, w: i32, h: i32) -> bool {
    if !(*l).bmp.is_null() && (*l).w == w && (*l).h == h {
        return true;
    }
    layer_free(l);
    let bi = dib32(w, h);
    (*l).dc = CreateCompatibleDC(null_mut());
    let mut bits: *mut c_void = null_mut();
    (*l).bmp = CreateDIBSection((*l).dc, &bi, DIB_RGB_COLORS, &mut bits, null_mut(), 0);
    (*l).bits = bits as *mut u32;
    if (*l).bmp.is_null() {
        layer_free(l);
        return false;
    }
    SelectObject((*l).dc, (*l).bmp);
    (*l).w = w;
    (*l).h = h;
    true
}

pub unsafe fn layer_show(l: *const Layer, wnd: HWND) {
    let size = SIZE { cx: (*l).w, cy: (*l).h };
    let src = POINT { x: 0, y: 0 };
    let bf = BLENDFUNCTION { BlendOp: AC_SRC_OVER as u8, BlendFlags: 0, SourceConstantAlpha: 255, AlphaFormat: AC_SRC_ALPHA as u8 };
    if !wnd.is_null() {
        UpdateLayeredWindow(wnd, null_mut(), null(), &size, (*l).dc, &src, 0, &bf, ULW_ALPHA);
    }
}

// ---- boxes: b = { x0, y0, x1, y1 }

pub fn in_box(b: &[f32; 4], x: f32, y: f32) -> bool {
    x >= b[0] && x < b[2] && y >= b[1] && y < b[3]
}

pub fn box_set(b: &mut [f32; 4], x: f32, y: f32, w: f32, h: f32) {
    b[0] = x;
    b[1] = y;
    b[2] = x + w;
    b[3] = y + h;
}

pub fn color_alpha(c: u32, k: f32) -> u32 {
    let a = ((c >> 24) as f32 * clamp(k, 0.0, 1.0) + 0.5) as u32;
    (c & 0x00FFFFFF) | (a << 24)
}

pub fn color_mix(a: u32, b: u32, t: f32) -> u32 {
    let t = clamp(t, 0.0, 1.0);
    let mut r = 0u32;
    let mut k = 0;
    while k < 32 {
        let x = ((a >> k) & 255) as f32;
        let y = ((b >> k) & 255) as f32;
        r |= ((x + (y - x) * t + 0.5) as u32) << k;
        k += 8;
    }
    r
}

// ---- motion. Both helpers jump straight to the target under reduced motion and return whether still moving.

// A critically damped spring: omega sets how fast it arrives.
pub unsafe fn spring_step(x: &mut f32, v: &mut f32, target: f32, omega: f32, dt: f32) -> bool {
    if omega <= 0.0 || g_cfg.reduced_motion {
        *x = target;
        *v = 0.0;
        return false;
    }
    let mut d = *x - target;
    let e = (-omega * dt).exp();
    let tmp = (*v + omega * d) * dt;
    d = (d + tmp) * e;
    *v = (*v - omega * tmp) * e;
    *x = target + d;
    if d.abs() < 0.2 && v.abs() < 4.0 {
        *x = target;
        *v = 0.0;
        return false;
    }
    true
}

// Exponential ease toward the target.
pub unsafe fn approach(x: &mut f32, target: f32, rate: f32, dt: f32) -> bool {
    let d = target - *x;
    if d.abs() < 0.004 || g_cfg.reduced_motion {
        *x = target;
        return false;
    }
    *x += d * (1.0 - (-rate * dt).exp());
    true
}

pub fn ease_out_cubic(t: f32) -> f32 {
    let t = 1.0 - clamp(t, 0.0, 1.0);
    1.0 - t * t * t
}

pub fn ease_out_quart(t: f32) -> f32 {
    let t = 1.0 - clamp(t, 0.0, 1.0);
    1.0 - t * t * t * t
}

#[repr(C)]
pub struct RTarget {
    pub hwnd: HWND,
    pub swap: Option<IDXGISwapChain1>,
    pub frame_wait: HANDLE,
    pub dct: Option<IDCompositionTarget>,
    pub dcv: Option<IDCompositionVisual>,
    pub rtv: Option<ID3D11RenderTargetView>,
    pub w: i32,
    pub h: i32,
    pub swap_flags: u32,
    pub ok: bool,
}

// A texture the backdrop's passes read, and (with rtv) write.
#[repr(C)]
pub struct RTex {
    pub tex: Option<ID3D11Texture2D>,
    pub srv: Option<ID3D11ShaderResourceView>,
    pub rtv: Option<ID3D11RenderTargetView>,
    pub w: i32,
    pub h: i32,
}

#[repr(C)]
pub struct Renderer {
    pub dev: Option<ID3D11Device>,
    pub ctx: Option<ID3D11DeviceContext>,
    pub factory: Option<IDXGIFactory2>,
    pub dc: Option<IDCompositionDevice>,
    pub vs: Option<ID3D11VertexShader>,
    pub ps: Option<ID3D11PixelShader>,
    pub layout: Option<ID3D11InputLayout>,
    pub ibuf: Option<ID3D11Buffer>,
    pub ibuf_cap: u32,
    pub cbuf: Option<ID3D11Buffer>,
    pub blend: Option<ID3D11BlendState>,
    pub raster: Option<ID3D11RasterizerState>,
    pub samp_point: Option<ID3D11SamplerState>,
    pub samp_linear: Option<ID3D11SamplerState>,
    pub glyph_tex: Option<ID3D11Texture2D>,
    pub glyph_srv: Option<ID3D11ShaderResourceView>,
    pub icon_tex: Option<ID3D11Texture2D>,
    pub icon_srv: Option<ID3D11ShaderResourceView>,
    pub fx_vs: Option<ID3D11VertexShader>,
    pub fx_down: Option<ID3D11PixelShader>,
    pub fx_blur: Option<ID3D11PixelShader>,
    pub fx_cbuf: Option<ID3D11Buffer>,
    // The backdrop: the screen behind the window (src), halved, quartered and blurred there (quarter_b; quarter_t
    // holds the blur's first pass).
    pub bd_src: RTex,
    pub bd_half: RTex,
    pub bd_quarter: RTex,
    pub bd_quarter_t: RTex,
    pub bd_quarter_b: RTex,
    pub pv: RTex,
    pub pv_tmp: RTex, // pictures drawn by r_image (the Design page's previews), and the target one is rendered to
    pub bd_sigma: f32,  // the blur, src px
    pub bd_dirty: bool, // src changed: blur again before the frame
    pub bd: [f32; 8],   // the backdrop's constants (bd0, bd1 in the shader)

    pub main: RTarget,
    pub t: *mut RTarget,

    pub inst: *mut RInst,
    pub count: i32,
    pub cap: i32,
    pub clip: [f32; 4],
    pub scale: f32,
    pub opacity: f32,

    pub glyph_cpu: *mut u8,
    pub dirty_x0: i32,
    pub dirty_y0: i32,
    pub dirty_x1: i32,
    pub dirty_y1: i32,
    pub ok: bool,
}

pub static mut R: Renderer = unsafe { zeroed() };

#[inline]
pub unsafe fn rr() -> &'static mut Renderer {
    &mut *(&raw mut R)
}

// The vertex shader spreads an instance over its quad (grown by the soft edge) and applies the open/close zoom
// (vp.z); the pixel shader clips, then shades the rect's distance field, a contrast-boosted glyph, an icon or the
// backdrop. The backdrop is the blurred screen behind the window, sampled
// where each pixel really is on the screen. bd0: window px to source px, 1 / source size; bd1: kind, dark.
//   glass   - the blur, more saturated (vibrancy, as macOS has it), under the theme's tint
//   acrylic - the Fluent recipe: a luminosity layer, then the tint (Windows 11 base acrylic, a little clearer)
//   mica    - the wallpaper (backdrop.rs) with the tint's luminosity, under the Windows 11 Mica tint
static k_shader_src: &str = "cbuffer C : register(b0) { float4 vp; float4 gamma; float4 misc; float4 bd0; float4 bd1; };\n\
struct I { float4 r : RECT; float4 uv : UV; float4 c : COLOR; float4 clip : CLIP; float4 p : PARAM; uint vid : SV_VertexID; };\n\
struct V { float4 pos : SV_Position; float2 uv : UV; float2 local : LOCAL; float2 cpos : CPOS;\n\
  nointerpolation float2 hs : HALF; nointerpolation float4 c : COLOR;\n\
  nointerpolation float4 clip : CLIP; nointerpolation float4 p : PARAM; };\n\
V vs(I i) {\n\
  V o;\n\
  float2 t = float2(i.vid & 1, i.vid >> 1);\n\
  bool sdf = i.p.w < 0.5 || i.p.w > 2.5;\n\
  bool img = i.p.w > 4.5;\n\
  float pad = sdf ? (i.p.z + 1.0) : 0.0;\n\
  float2 p = lerp(i.r.xy - pad, i.r.zw + pad, t);\n\
  float2 mid = (i.r.xy + i.r.zw) * 0.5;\n\
  o.local = p - mid;\n\
  if (i.p.w > 2.5 && i.p.w < 3.5) p = mid + o.local.x * i.uv.xy + o.local.y * i.uv.zw;\n\
  o.cpos = p;\n\
  float2 ctr = 0.5 / vp.xy;\n\
  float2 q = ctr + (p - ctr) * vp.z;\n\
  o.pos = float4(q.x * vp.x * 2.0 - 1.0, 1.0 - q.y * vp.y * 2.0, 0.0, 1.0);\n\
  o.uv = img ? lerp(i.uv.xy, i.uv.zw, (p - i.r.xy) / max(i.r.zw - i.r.xy, 1.0)) : lerp(i.uv.xy, i.uv.zw, t);\n\
  o.hs = (i.r.zw - i.r.xy) * 0.5;\n\
  o.c = i.c; o.clip = i.clip; o.p = i.p;\n\
  return o;\n\
}\n\
Texture2D<float> gtex : register(t0);\n\
Texture2D<float4> itex : register(t1);\n\
Texture2D<float4> btex : register(t2);\n\
Texture2D<float4> ptex : register(t3);\n\
SamplerState sp : register(s0);\n\
SamplerState sl : register(s1);\n\
float sdrr(float2 p, float2 b, float r) { float2 q = abs(p) - b + r; return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r; }\n\
float sdsq(float2 p, float2 b, float r) {\n\
  float2 q = abs(p) - b + r, m = max(q, 0.0) / max(r, 1e-3);\n\
  m *= m;\n\
  return r * sqrt(sqrt(dot(m, m))) + min(max(q.x, q.y), 0.0) - r;\n\
}\n\
float shape(float2 p, float2 b, float rr) { float r = min(abs(rr), min(b.x, b.y)); return rr < 0.0 ? sdsq(p, b, r) : sdrr(p, b, r); }\n\
float lum(float3 c) { return dot(c, float3(0.30, 0.59, 0.11)); }\n\
float3 clip_color(float3 c) {\n\
  float l = lum(c), n = min(c.r, min(c.g, c.b)), x = max(c.r, max(c.g, c.b));\n\
  if (n < 0.0) c = l + (c - l) * l / max(l - n, 1e-4);\n\
  if (x > 1.0) c = l + (c - l) * (1.0 - l) / max(x - l, 1e-4);\n\
  return c;\n\
}\n\
float3 with_lum(float3 c, float l) { return clip_color(c + (l - lum(c))); }\n\
float4 backdrop(V i) {\n\
  float a = saturate(0.5 - shape(i.local, i.hs, i.p.x)) * i.c.a;\n\
  float3 c = btex.SampleLevel(sl, (i.pos.xy + bd0.xy) * bd0.zw, 0).rgb;\n\
  bool dark = bd1.y > 0.5;\n\
  if (bd1.x < 0.5) {\n\
    c = lerp(lum(c).xxx, c, 1.5);\n\
  } else if (bd1.x > 1.5) {\n\
    float3 tint = dark ? float3(0.125, 0.125, 0.125) : float3(0.953, 0.953, 0.953);\n\
    c = lerp(with_lum(c, lum(tint)), tint, dark ? 0.8 : 0.5);\n\
  } else {\n\
    float3 tint = dark ? float3(0.173, 0.173, 0.173) : float3(0.988, 0.988, 0.988);\n\
    c = lerp(c, with_lum(c, lum(tint)), dark ? 0.88 : 0.78);\n\
    c = lerp(c, with_lum(tint, lum(c)), dark ? 0.15 : 0.0);\n\
  }\n\
  return float4(saturate(c) * a, a);\n\
}\n\
float4 ps(V i) : SV_Target {\n\
  float2 fp = i.cpos;\n\
  if (fp.x < i.clip.x || fp.y < i.clip.y || fp.x > i.clip.z || fp.y > i.clip.w) discard;\n\
  if (i.p.w > 4.5) {\n\
    return ptex.SampleLevel(sl, i.uv, 0) * saturate(0.5 - shape(i.local, i.hs, i.p.x)) * i.c.a;\n\
  }\n\
  if (i.p.w > 3.5) return backdrop(i);\n\
  float4 c = i.c;\n\
  float a;\n\
  if (i.p.w < 0.5 || i.p.w > 2.5) {\n\
    float d = shape(i.local, i.hs, i.p.x);\n\
    if (i.p.z > 0.0) a = 1.0 - smoothstep(-i.p.z, i.p.z, d);\n\
    else a = saturate(0.5 - d);\n\
    if (i.p.y > 0.0) a *= saturate(d + i.p.y + 0.5);\n\
  } else if (i.p.w < 1.5) {\n\
    float g = gtex.Sample(sp, i.uv);\n\
    float k = misc.x * saturate(4.0 * (0.75 - dot(c.rgb, float3(0.30, 0.59, 0.11))));\n\
    g = g * (k + 1.0) / (g * k + 1.0);\n\
    float f = dot(c.rgb, float3(0.25, 0.5, 0.25));\n\
    g = g + g * (1.0 - g) * ((gamma.x * f + gamma.y) * g + (gamma.z * f + gamma.w));\n\
    a = saturate(g);\n\
  } else {\n\
    return itex.Sample(sl, i.uv) * c.a;\n\
  }\n\
  a *= c.a;\n\
  return float4(c.rgb * a, a);\n\
}\n";

// The backdrop's blur passes over a full-target triangle: fdown halves (a 2x2 box: one bilinear tap between four
// texels), fblur is one direction of a gaussian. fx: one texel along the blur, sigma, taps each way.
static k_fx_src: &str = "struct F { float4 pos : SV_Position; float2 uv : UV; };\n\
F fvs(uint id : SV_VertexID) {\n\
  F o;\n\
  float2 t = float2((id << 1) & 2, id & 2);\n\
  o.uv = t;\n\
  o.pos = float4(t.x * 2.0 - 1.0, 1.0 - t.y * 2.0, 0.0, 1.0);\n\
  return o;\n\
}\n\
cbuffer X : register(b0) { float4 fx; };\n\
Texture2D<float4> src : register(t0);\n\
SamplerState sl : register(s0);\n\
float4 fdown(F i) : SV_Target { return float4(src.SampleLevel(sl, i.uv, 0).rgb, 1.0); }\n\
float4 fblur(F i) : SV_Target {\n\
  float3 acc = 0;\n\
  float sum = 0;\n\
  int n = (int)fx.w;\n\
  [loop] for (int j = -n; j <= n; j++) {\n\
    float w = exp(-0.5 * j * j / (fx.z * fx.z));\n\
    acc += w * src.SampleLevel(sl, i.uv + fx.xy * j, 0).rgb;\n\
    sum += w;\n\
  }\n\
  return float4(acc / sum, 1.0);\n\
}\n";

pub fn whwnd(h: HWND) -> windows::Win32::Foundation::HWND {
    windows::Win32::Foundation::HWND(h)
}

unsafe fn r_target_views(t: *mut RTarget) -> bool {
    let Some(swap) = (*t).swap.as_ref() else { return false };
    let back: ID3D11Texture2D = match swap.GetBuffer(0) {
        Ok(b) => b,
        Err(_) => return false,
    };
    rr().dev.as_ref().unwrap().CreateRenderTargetView(&back, None, Some(&mut (*t).rtv)).is_ok()
}

pub unsafe fn r_target_release(t: *mut RTarget) {
    if let Some(ctx) = rr().ctx.as_ref() {
        ctx.OMSetRenderTargets(None, None);
    }
    (*t).rtv = None;
    (*t).dcv = None;
    (*t).dct = None;
    if !(*t).frame_wait.is_null() {
        CloseHandle((*t).frame_wait);
        (*t).frame_wait = null_mut();
    }
    (*t).swap = None;
    if let Some(dc) = rr().dc.as_ref() {
        let _ = dc.Commit();
    }
    (*t).ok = false;
}

pub unsafe fn r_target_init(t: *mut RTarget, hwnd: HWND, w: i32, h: i32) -> bool {
    core::ptr::write(t, zeroed());
    (*t).hwnd = hwnd;
    (*t).w = max(w, 1);
    (*t).h = max(h, 1);
    let r = rr();
    if r.dev.is_none() || r.factory.is_none() || r.dc.is_none() {
        return false;
    }
    let mut sd: DXGI_SWAP_CHAIN_DESC1 = zeroed();
    sd.Width = (*t).w as u32;
    sd.Height = (*t).h as u32;
    sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.Scaling = DXGI_SCALING_STRETCH;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT.0 as u32;
    let factory = r.factory.as_ref().unwrap();
    let dev = r.dev.as_ref().unwrap();
    let mut res = factory.CreateSwapChainForComposition(dev, &sd, None);
    if res.is_err() {
        sd.Flags = 0;
        res = factory.CreateSwapChainForComposition(dev, &sd, None);
    }
    match res {
        Ok(s) => (*t).swap = Some(s),
        Err(e) => {
            log_msg!("CreateSwapChainForComposition failed 0x{:08x}", e.code().0 as u32);
            return false;
        }
    }
    (*t).swap_flags = sd.Flags;
    if (sd.Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT.0 as u32) != 0 {
        if let Ok(sc2) = (*t).swap.as_ref().unwrap().cast::<IDXGISwapChain2>() {
            let _ = sc2.SetMaximumFrameLatency(1);
            (*t).frame_wait = sc2.GetFrameLatencyWaitableObject().0;
        }
    }
    let dc = r.dc.as_ref().unwrap();
    let ok = (|| -> windows::core::Result<()> {
        let dct = dc.CreateTargetForHwnd(whwnd(hwnd), true)?;
        let dcv = dc.CreateVisual()?;
        dcv.SetContent((*t).swap.as_ref().unwrap())?;
        dct.SetRoot(&dcv)?;
        (*t).dct = Some(dct);
        (*t).dcv = Some(dcv);
        dc.Commit()?;
        Ok(())
    })();
    if ok.is_err() || !r_target_views(t) {
        log_msg!("DirectComposition target setup failed");
        r_target_release(t);
        return false;
    }
    (*t).ok = true;
    true
}

pub unsafe fn r_target_resize(t: *mut RTarget, w: i32, h: i32) {
    let w = max(w, 1);
    let h = max(h, 1);
    if !R.ok || !(*t).ok || (w == (*t).w && h == (*t).h) {
        return;
    }
    (*t).w = w;
    (*t).h = h;
    rr().ctx.as_ref().unwrap().OMSetRenderTargets(None, None);
    (*t).rtv = None;
    let hr = (*t).swap.as_ref().unwrap().ResizeBuffers(0, w as u32, h as u32, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG((*t).swap_flags as i32));
    if hr.is_err() || !r_target_views(t) {
        log_msg!("ResizeBuffers failed 0x{:08x}", hr.err().map(|e| e.code().0 as u32).unwrap_or(0));
        (*t).ok = false;
        R.ok = false;
    }
}

pub unsafe fn r_resize(w: i32, h: i32) {
    r_target_resize(&raw mut R.main, w, h);
}

pub unsafe fn r_use(t: *mut RTarget) {
    R.t = t;
}

pub unsafe fn r_shutdown() {
    r_target_release(&raw mut R.main);
    let r = rr();
    r.dc = None;
    r.factory = None;
    r.glyph_srv = None;
    r.glyph_tex = None;
    r.icon_srv = None;
    r.icon_tex = None;
    r_bd_free();
    r.fx_cbuf = None;
    r.fx_blur = None;
    r.fx_down = None;
    r.fx_vs = None;
    r.samp_point = None;
    r.samp_linear = None;
    r.raster = None;
    r.blend = None;
    r.cbuf = None;
    r.ibuf = None;
    r.layout = None;
    r.ps = None;
    r.vs = None;
    r.ctx = None;
    r.dev = None;
    r.ok = false;
}

// d3dcompiler_47.dll is part of Windows 10+, loaded at runtime.
type PfnD3DCompile = unsafe extern "system" fn(
    src: *const c_void,
    len: usize,
    name: *const u8,
    defines: *const c_void,
    include: *mut c_void,
    entry: *const u8,
    target: *const u8,
    flags1: u32,
    flags2: u32,
    code: *mut *mut c_void,
    errors: *mut *mut c_void,
) -> HRESULT;

static mut COMPILE: Option<PfnD3DCompile> = None;

unsafe fn r_compile(compile: PfnD3DCompile, src: &str, entry: &[u8], target: &[u8]) -> Option<ID3DBlob> {
    let mut code: *mut c_void = null_mut();
    let mut err: *mut c_void = null_mut();
    let hr = compile(
        src.as_ptr() as *const c_void,
        src.len(),
        b"mixlauncher\0".as_ptr(),
        null(),
        null_mut(),
        entry.as_ptr(),
        target.as_ptr(),
        1 << 15, /*OPTIMIZATION_LEVEL3*/
        0,
        &mut code,
        &mut err,
    );
    let err: Option<ID3DBlob> = if err.is_null() { None } else { Some(ID3DBlob::from_raw(err)) };
    if failed(hr) {
        if let Some(e) = err {
            log_msg!("shader {}: {}", Cs(entry.as_ptr()), Cs(e.GetBufferPointer() as *const u8));
        }
        return None;
    }
    if code.is_null() { None } else { Some(ID3DBlob::from_raw(code)) }
}

unsafe fn blob_bytes(b: &ID3DBlob) -> &[u8] {
    core::slice::from_raw_parts(b.GetBufferPointer() as *const u8, b.GetBufferSize())
}

type PfnDCompositionCreateDevice = unsafe extern "system" fn(*mut c_void, *const windows::core::GUID, *mut *mut c_void) -> HRESULT;

pub unsafe fn r_init(hwnd: HWND, w: i32, h: i32) -> bool {
    // memset(&R.dev, 0, offsetof(Renderer, inst) - offsetof(Renderer, dev)): everything before the instances
    {
        let r = rr();
        r.dev = None;
        r.ctx = None;
        r.factory = None;
        r.dc = None;
        r.vs = None;
        r.ps = None;
        r.layout = None;
        r.ibuf = None;
        r.ibuf_cap = 0;
        r.cbuf = None;
        r.blend = None;
        r.raster = None;
        r.samp_point = None;
        r.samp_linear = None;
        r.glyph_tex = None;
        r.glyph_srv = None;
        r.icon_tex = None;
        r.icon_srv = None;
        r.fx_vs = None;
        r.fx_down = None;
        r.fx_blur = None;
        r.fx_cbuf = None;
        r_bd_free();
        r.bd_sigma = 0.0;
        r.bd_dirty = false;
        r.bd = [0.0; 8];
        core::ptr::write(&raw mut r.main, zeroed());
        r.t = null_mut();
    }
    R.scale = 1.0;
    R.opacity = 1.0;
    let r = rr();

    let levels = [D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0];
    let flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS;
    let none: Option<&IDXGIAdapter> = None;
    let hm = windows::Win32::Foundation::HMODULE::default();
    let mut hr = D3D11CreateDevice(none, D3D_DRIVER_TYPE_HARDWARE, hm, flags, Some(&levels), D3D11_SDK_VERSION, Some(&mut r.dev), None, Some(&mut r.ctx));
    if hr.is_err() {
        hr = D3D11CreateDevice(none, D3D_DRIVER_TYPE_HARDWARE, hm, flags, Some(&levels[1..]), D3D11_SDK_VERSION, Some(&mut r.dev), None, Some(&mut r.ctx));
    }
    if hr.is_err() {
        hr = D3D11CreateDevice(none, D3D_DRIVER_TYPE_WARP, hm, flags, Some(&levels[1..]), D3D11_SDK_VERSION, Some(&mut r.dev), None, Some(&mut r.ctx));
    }
    if let Err(e) = hr {
        log_msg!("D3D11CreateDevice failed 0x{:08x}", e.code().0 as u32);
        return false;
    }

    let dxgi_dev: Option<IDXGIDevice> = r.dev.as_ref().unwrap().cast().ok();
    if let Some(d) = dxgi_dev.as_ref() {
        if let Ok(adapter) = d.GetAdapter() {
            r.factory = adapter.GetParent::<IDXGIFactory2>().ok();
        }
    }
    let dcomp = LoadLibraryW(w!("dcomp.dll"));
    let dcreate: Option<PfnDCompositionCreateDevice> = core::mem::transmute(GetProcAddress(dcomp, s!("DCompositionCreateDevice")));
    if let (Some(d), Some(create)) = (dxgi_dev.as_ref(), dcreate) {
        let mut p: *mut c_void = null_mut();
        if succeeded(create(d.as_raw(), &IDCompositionDevice::IID, &mut p)) && !p.is_null() {
            r.dc = Some(IDCompositionDevice::from_raw(p));
        }
    }
    drop(dxgi_dev);
    if r.factory.is_none() || r.dc.is_none() {
        log_msg!("no IDXGIFactory2 / DirectComposition");
        r_shutdown();
        return false;
    }

    if COMPILE.is_none() {
        let m = LoadLibraryW(w!("d3dcompiler_47.dll"));
        if !m.is_null() {
            COMPILE = core::mem::transmute(GetProcAddress(m, s!("D3DCompile")));
        }
    }
    let Some(compile) = COMPILE else {
        log_msg!("d3dcompiler_47.dll not available");
        r_shutdown();
        return false;
    };
    let vsb = r_compile(compile, k_shader_src, b"vs\0", b"vs_4_0\0");
    let psb = if vsb.is_some() { r_compile(compile, k_shader_src, b"ps\0", b"ps_4_0\0") } else { None };
    let (Some(vsb), Some(psb)) = (vsb, psb) else {
        r_shutdown();
        return false;
    };
    let dev = r.dev.clone().unwrap();
    let _ = dev.CreateVertexShader(blob_bytes(&vsb), None, Some(&mut r.vs));
    let _ = dev.CreatePixelShader(blob_bytes(&psb), None, Some(&mut r.ps));
    let pi = D3D11_INPUT_PER_INSTANCE_DATA;
    let el = |name: &'static [u8], fmt: DXGI_FORMAT, off: usize| D3D11_INPUT_ELEMENT_DESC {
        SemanticName: windows::core::PCSTR(name.as_ptr()),
        SemanticIndex: 0,
        Format: fmt,
        InputSlot: 0,
        AlignedByteOffset: off as u32,
        InputSlotClass: pi,
        InstanceDataStepRate: 1,
    };
    let il = [
        el(b"RECT\0", DXGI_FORMAT_R32G32B32A32_FLOAT, core::mem::offset_of!(RInst, x0)),
        el(b"UV\0", DXGI_FORMAT_R32G32B32A32_FLOAT, core::mem::offset_of!(RInst, u0)),
        el(b"COLOR\0", DXGI_FORMAT_R8G8B8A8_UNORM, core::mem::offset_of!(RInst, color)),
        el(b"CLIP\0", DXGI_FORMAT_R32G32B32A32_FLOAT, core::mem::offset_of!(RInst, cx0)),
        el(b"PARAM\0", DXGI_FORMAT_R32G32B32A32_FLOAT, core::mem::offset_of!(RInst, radius)),
    ];
    let _ = dev.CreateInputLayout(&il, blob_bytes(&vsb), Some(&mut r.layout));
    drop(vsb);
    drop(psb);
    if let Some(f0) = r_compile(compile, k_fx_src, b"fvs\0", b"vs_4_0\0") {
        if let Some(f1) = r_compile(compile, k_fx_src, b"fdown\0", b"ps_4_0\0") {
            if let Some(f2) = r_compile(compile, k_fx_src, b"fblur\0", b"ps_4_0\0") {
                let _ = dev.CreateVertexShader(blob_bytes(&f0), None, Some(&mut r.fx_vs));
                let _ = dev.CreatePixelShader(blob_bytes(&f1), None, Some(&mut r.fx_down));
                let _ = dev.CreatePixelShader(blob_bytes(&f2), None, Some(&mut r.fx_blur));
            }
        }
    }
    if r.vs.is_none() || r.ps.is_none() || r.layout.is_none() {
        r_shutdown();
        return false;
    }

    r.ibuf_cap = 8192;
    let mut bd: D3D11_BUFFER_DESC = zeroed();
    bd.ByteWidth = r.ibuf_cap * size_of::<RInst>() as u32;
    bd.Usage = D3D11_USAGE_DYNAMIC;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER.0 as u32;
    bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE.0 as u32;
    let _ = dev.CreateBuffer(&bd, None, Some(&mut r.ibuf));

    bd.ByteWidth = 80;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER.0 as u32;
    let _ = dev.CreateBuffer(&bd, None, Some(&mut r.cbuf));
    bd.ByteWidth = 16;
    let _ = dev.CreateBuffer(&bd, None, Some(&mut r.fx_cbuf));

    let mut blend: D3D11_BLEND_DESC = zeroed();
    blend.RenderTarget[0].BlendEnable = true.into();
    blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL.0 as u8;
    let _ = dev.CreateBlendState(&blend, Some(&mut r.blend));

    let mut rs: D3D11_RASTERIZER_DESC = zeroed();
    rs.FillMode = D3D11_FILL_SOLID;
    rs.CullMode = D3D11_CULL_NONE;
    rs.DepthClipEnable = true.into();
    let _ = dev.CreateRasterizerState(&rs, Some(&mut r.raster));

    let mut ss: D3D11_SAMPLER_DESC = zeroed();
    ss.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    ss.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    ss.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    ss.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    ss.MaxLOD = D3D11_FLOAT32_MAX;
    let _ = dev.CreateSamplerState(&ss, Some(&mut r.samp_point));
    ss.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    let _ = dev.CreateSamplerState(&ss, Some(&mut r.samp_linear));

    let mut td: D3D11_TEXTURE2D_DESC = zeroed();
    td.Width = GLYPH_ATLAS as u32;
    td.Height = GLYPH_ATLAS as u32;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE.0 as u32;
    let _ = dev.CreateTexture2D(&td, None, Some(&mut r.glyph_tex));
    if let Some(t) = r.glyph_tex.as_ref() {
        let _ = dev.CreateShaderResourceView(t, None, Some(&mut r.glyph_srv));
    }
    td.Width = ICON_ATLAS as u32;
    td.Height = ICON_ATLAS as u32;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    let _ = dev.CreateTexture2D(&td, None, Some(&mut r.icon_tex));
    if let Some(t) = r.icon_tex.as_ref() {
        let _ = dev.CreateShaderResourceView(t, None, Some(&mut r.icon_srv));
    }

    if r.ibuf.is_none()
        || r.cbuf.is_none()
        || r.blend.is_none()
        || r.raster.is_none()
        || r.samp_point.is_none()
        || r.samp_linear.is_none()
        || r.glyph_srv.is_none()
        || r.icon_srv.is_none()
        || !r_target_init(&raw mut R.main, hwnd, w, h)
    {
        log_msg!("D3D resource creation failed");
        r_shutdown();
        return false;
    }
    R.t = &raw mut R.main;

    if R.glyph_cpu.is_null() {
        R.glyph_cpu = calloc(GLYPH_ATLAS as usize, GLYPH_ATLAS as usize) as *mut u8;
    }
    R.dirty_x0 = 0;
    R.dirty_y0 = 0;
    R.dirty_x1 = GLYPH_ATLAS;
    R.dirty_y1 = GLYPH_ATLAS;
    R.ok = true;
    true
}

pub unsafe fn r_wait_frame() {
    if R.ok && !R.t.is_null() && !(*R.t).frame_wait.is_null() {
        WaitForSingleObjectEx((*R.t).frame_wait, 100, TRUE);
    }
}

pub unsafe fn r_set_clip(x0: f32, y0: f32, x1: f32, y1: f32) {
    R.clip = [x0, y0, x1, y1];
}

pub unsafe fn r_clip_all() {
    r_set_clip(0.0, 0.0, (*R.t).w as f32, (*R.t).h as f32);
}

pub unsafe fn r_begin() {
    R.count = 0;
    R.opacity = 1.0;
    r_clip_all();
}

pub unsafe fn r_push() -> *mut RInst {
    if !grow(&raw mut R.inst, &raw mut R.cap, R.count + 1, 16384) {
        return null_mut();
    }
    let q = R.inst.add(R.count as usize);
    R.count += 1;
    (*q).cx0 = R.clip[0];
    (*q).cy0 = R.clip[1];
    (*q).cx1 = R.clip[2];
    (*q).cy1 = R.clip[3];
    q
}

pub unsafe fn r_fade(color: u32) -> u32 {
    if R.opacity < 1.0 { color_alpha(color, R.opacity) } else { color }
}

pub unsafe fn r_rect_ex(x: f32, y: f32, w: f32, h: f32, color: u32, radius: f32, border: f32, soft: f32) {
    let color = r_fade(color);
    if (color >> 24) == 0 {
        return;
    }
    let q = r_push();
    if q.is_null() {
        return;
    }
    let q = &mut *q;
    q.x0 = x;
    q.y0 = y;
    q.x1 = x + w;
    q.y1 = y + h;
    q.u0 = 0.0;
    q.v0 = 0.0;
    q.u1 = 0.0;
    q.v1 = 0.0;
    q.color = color;
    q.radius = radius;
    q.border = border;
    q.soft = soft;
    q.mode = 0.0;
}

pub unsafe fn r_rect(x: f32, y: f32, w: f32, h: f32, color: u32, radius: f32) {
    r_rect_ex(x, y, w, h, color, radius, 0.0, 0.0);
}

// A hairline outline.
pub unsafe fn r_border(x: f32, y: f32, w: f32, h: f32, color: u32, radius: f32) {
    r_rect_ex(x, y, w, h, color, radius, 1.0, 0.0);
}

// A floating panel (menu, dialog, tooltip): a soft shadow shadow_dy below, the fill and a hairline border.
pub unsafe fn r_panel(x: f32, y: f32, w: f32, h: f32, radius: f32, fill: u32, border: u32, shadow: u32, shadow_dy: f32, shadow_soft: f32) {
    r_rect_ex(x, y + shadow_dy, w, h, shadow, radius, 0.0, shadow_soft);
    r_rect(x, y, w, h, fill, radius);
    r_border(x, y, w, h, border, radius);
}

// A rounded rect centered at (cx, cy) mapped by the 2x2 matrix m: its local x axis goes to (m[0], m[1]), y to (m[2], m[3]).
pub unsafe fn r_rect_affine(cx: f32, cy: f32, w: f32, h: f32, m: &[f32; 4], color: u32, radius: f32) {
    let color = r_fade(color);
    if (color >> 24) == 0 {
        return;
    }
    let q = r_push();
    if q.is_null() {
        return;
    }
    let q = &mut *q;
    q.x0 = cx - w * 0.5;
    q.y0 = cy - h * 0.5;
    q.x1 = cx + w * 0.5;
    q.y1 = cy + h * 0.5;
    q.u0 = m[0];
    q.v0 = m[1];
    q.u1 = m[2];
    q.v1 = m[3];
    q.color = color;
    q.radius = radius;
    q.border = 0.0;
    q.soft = 0.0;
    q.mode = 3.0;
}

// A rounded rect centered at (cx, cy), squashed horizontally by sx (a tumble in depth) and then rotated by angle.
pub unsafe fn r_rect_rot(cx: f32, cy: f32, w: f32, h: f32, angle: f32, sx: f32, color: u32, radius: f32) {
    let c = angle.cos();
    let s = angle.sin();
    let m = [c * sx, s * sx, -s, c];
    r_rect_affine(cx, cy, w, h, &m, color, radius);
}

pub unsafe fn r_glyph(x: f32, y: f32, w: f32, h: f32, u0: f32, v0: f32, u1: f32, v1: f32, color: u32) {
    let color = r_fade(color);
    if (color >> 24) == 0 {
        return;
    }
    let q = r_push();
    if q.is_null() {
        return;
    }
    let q = &mut *q;
    q.x0 = x;
    q.y0 = y;
    q.x1 = x + w;
    q.y1 = y + h;
    q.u0 = u0;
    q.v0 = v0;
    q.u1 = u1;
    q.v1 = v1;
    q.color = color;
    q.radius = 0.0;
    q.border = 0.0;
    q.soft = 0.0;
    q.mode = 1.0;
}

pub unsafe fn r_icon(x: f32, y: f32, w: f32, h: f32, u0: f32, v0: f32, u1: f32, v1: f32, opacity: f32) {
    let q = r_push();
    if q.is_null() {
        return;
    }
    let q = &mut *q;
    q.x0 = x;
    q.y0 = y;
    q.x1 = x + w;
    q.y1 = y + h;
    q.u0 = u0;
    q.v0 = v0;
    q.u1 = u1;
    q.v1 = v1;
    q.color = RGBA(255, 255, 255, (clamp(opacity * R.opacity, 0.0, 1.0) * 255.0 + 0.5) as u32);
    q.radius = 0.0;
    q.border = 0.0;
    q.soft = 0.0;
    q.mode = 2.0;
}

// A rounded rect of R.pv: u0..v1 is the part of it (0..1), stretched over the rect.
pub unsafe fn r_image(x: f32, y: f32, w: f32, h: f32, u0: f32, v0: f32, u1: f32, v1: f32, radius: f32) {
    if rr().pv.srv.is_none() {
        return;
    }
    let q = r_push();
    if q.is_null() {
        return;
    }
    let q = &mut *q;
    q.x0 = x;
    q.y0 = y;
    q.x1 = x + w;
    q.y1 = y + h;
    q.u0 = u0;
    q.v0 = v0;
    q.u1 = u1;
    q.v1 = v1;
    q.color = RGBA(255, 255, 255, (clamp(R.opacity, 0.0, 1.0) * 255.0 + 0.5) as u32);
    q.radius = radius;
    q.border = 0.0;
    q.soft = 0.0;
    q.mode = 5.0;
}

// A rounded rect of the backdrop (R.bd_src, blurred), shaded as R.bd says.
pub unsafe fn r_backdrop(x: f32, y: f32, w: f32, h: f32, radius: f32) {
    if rr().bd_src.srv.is_none() || rr().fx_blur.is_none() {
        return;
    }
    let q = r_push();
    if q.is_null() {
        return;
    }
    let q = &mut *q;
    q.x0 = x;
    q.y0 = y;
    q.x1 = x + w;
    q.y1 = y + h;
    q.u0 = 0.0;
    q.v0 = 0.0;
    q.u1 = 0.0;
    q.v1 = 0.0;
    q.color = RGBA(255, 255, 255, (clamp(R.opacity, 0.0, 1.0) * 255.0 + 0.5) as u32);
    q.radius = radius;
    q.border = 0.0;
    q.soft = 0.0;
    q.mode = 4.0;
}

// ---- the backdrop's textures and blur passes

pub unsafe fn rtex_free(t: *mut RTex) {
    (*t).rtv = None;
    (*t).srv = None;
    (*t).tex = None;
    (*t).w = 0;
    (*t).h = 0;
}

// Makes t w x h BGRA (its pixels undefined after a change), a render target too with target.
pub unsafe fn rtex_fit(t: *mut RTex, w: i32, h: i32, target: bool) -> bool {
    let w = max(w, 1);
    let h = max(h, 1);
    if (*t).tex.is_some() && (*t).w == w && (*t).h == h {
        return true;
    }
    rtex_free(t);
    let mut td: D3D11_TEXTURE2D_DESC = zeroed();
    td.Width = w as u32;
    td.Height = h as u32;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = (D3D11_BIND_SHADER_RESOURCE.0 | if target { D3D11_BIND_RENDER_TARGET.0 } else { 0 }) as u32;
    let dev = rr().dev.as_ref().unwrap();
    let t = &mut *t;
    let ok = dev.CreateTexture2D(&td, None, Some(&mut t.tex)).is_ok()
        && dev.CreateShaderResourceView(t.tex.as_ref().unwrap(), None, Some(&mut t.srv)).is_ok()
        && (!target || dev.CreateRenderTargetView(t.tex.as_ref().unwrap(), None, Some(&mut t.rtv)).is_ok());
    if !ok {
        rtex_free(t);
        return false;
    }
    t.w = w;
    t.h = h;
    true
}

pub unsafe fn r_bd_free() {
    let r = rr();
    for t in [&mut r.bd_src, &mut r.bd_half, &mut r.bd_quarter, &mut r.bd_quarter_t, &mut r.bd_quarter_b, &mut r.pv, &mut r.pv_tmp] {
        rtex_free(t);
    }
}

// A target drawn to without a window: the texture's picture, read back with r_image once copied to R.pv.
pub unsafe fn r_target_tex(tex: *mut RTex) -> RTarget {
    let mut t: RTarget = zeroed();
    t.rtv = (*tex).rtv.clone();
    t.w = (*tex).w;
    t.h = (*tex).h;
    t.ok = t.rtv.is_some();
    t
}

// The source: w x h px of the screen, copied in (screen capture) or uploaded (r_bd_upload).
pub unsafe fn r_bd_source(w: i32, h: i32) -> bool {
    R.ok && rr().fx_blur.is_some() && rtex_fit(&raw mut R.bd_src, w, h, false)
}

pub unsafe fn r_bd_upload(bgra: *const u32) {
    let r = rr();
    r.ctx.as_ref().unwrap().UpdateSubresource(r.bd_src.tex.as_ref().unwrap(), 0, None, bgra as *const c_void, r.bd_src.w as u32 * 4, 0);
    r.bd_dirty = true;
}

// One full-target pass of ps from in to out; (dx, dy) is the blur's direction.
unsafe fn r_fx(ps: &Option<ID3D11PixelShader>, inp: *const RTex, out: *const RTex, dx: f32, dy: f32, sigma: f32) {
    let r = rr();
    let ctx = r.ctx.as_ref().unwrap();
    let mut m: D3D11_MAPPED_SUBRESOURCE = zeroed();
    if ctx.Map(r.fx_cbuf.as_ref().unwrap(), 0, D3D11_MAP_WRITE_DISCARD, 0, Some(&mut m)).is_ok() {
        let c = m.pData as *mut f32;
        *c = dx / (*inp).w as f32;
        *c.add(1) = dy / (*inp).h as f32;
        *c.add(2) = max(sigma, 0.5);
        *c.add(3) = (max(sigma, 0.5) * 2.5).ceil();
        ctx.Unmap(r.fx_cbuf.as_ref().unwrap(), 0);
    }
    ctx.PSSetShaderResources(0, Some(&[None]));
    ctx.OMSetRenderTargets(Some(&[(*out).rtv.clone()]), None);
    let vp = D3D11_VIEWPORT { TopLeftX: 0.0, TopLeftY: 0.0, Width: (*out).w as f32, Height: (*out).h as f32, MinDepth: 0.0, MaxDepth: 1.0 };
    ctx.RSSetViewports(Some(&[vp]));
    ctx.PSSetShader(ps.as_ref(), None);
    ctx.PSSetShaderResources(0, Some(&[(*inp).srv.clone()]));
    ctx.Draw(3, 0);
}

// The blur of the source: halved twice, then a gaussian at a quarter size. A few hundred thousand texels: well
// under a millisecond, so it runs for every new image of the screen.
pub unsafe fn r_bd_blur() {
    let r = rr();
    r.bd_dirty = false;
    if r.bd_src.srv.is_none() {
        return;
    }
    let (sw, sh) = (r.bd_src.w, r.bd_src.h);
    let hw = (sw + 1) / 2;
    let hh = (sh + 1) / 2;
    let qw = (hw + 1) / 2;
    let qh = (hh + 1) / 2;
    if !rtex_fit(&mut r.bd_half, hw, hh, true)
        || !rtex_fit(&mut r.bd_quarter, qw, qh, true)
        || !rtex_fit(&mut r.bd_quarter_t, qw, qh, true)
        || !rtex_fit(&mut r.bd_quarter_b, qw, qh, true)
    {
        return;
    }
    let ctx = r.ctx.as_ref().unwrap();
    let bf = [0.0f32; 4];
    ctx.OMSetBlendState(None, Some(&bf), 0xFFFFFFFF);
    ctx.RSSetState(r.raster.as_ref());
    ctx.IASetInputLayout(None);
    ctx.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx.VSSetShader(r.fx_vs.as_ref(), None);
    ctx.PSSetConstantBuffers(0, Some(&[r.fx_cbuf.clone()]));
    ctx.PSSetSamplers(0, Some(&[r.samp_linear.clone()]));
    r_fx(&r.fx_down, &r.bd_src, &r.bd_half, 0.0, 0.0, 0.0);
    r_fx(&r.fx_down, &r.bd_half, &r.bd_quarter, 0.0, 0.0, 0.0);
    r_fx(&r.fx_blur, &r.bd_quarter, &r.bd_quarter_t, 1.0, 0.0, r.bd_sigma / 4.0);
    r_fx(&r.fx_blur, &r.bd_quarter_t, &r.bd_quarter_b, 0.0, 1.0, r.bd_sigma / 4.0);
    ctx.PSSetShaderResources(0, Some(&[None]));
}

pub unsafe fn r_glyph_dirty(x: i32, y: i32, w: i32, h: i32) {
    R.dirty_x0 = min(R.dirty_x0, x);
    R.dirty_y0 = min(R.dirty_y0, y);
    R.dirty_x1 = max(R.dirty_x1, x + w);
    R.dirty_y1 = max(R.dirty_y1, y + h);
}

pub unsafe fn r_upload_icon(x: i32, y: i32, size: i32, bgra: *const u32) {
    if !R.ok {
        return;
    }
    let b = D3D11_BOX { left: x as u32, top: y as u32, front: 0, right: (x + size) as u32, bottom: (y + size) as u32, back: 1 };
    let r = rr();
    r.ctx.as_ref().unwrap().UpdateSubresource(r.icon_tex.as_ref().unwrap(), 0, Some(&b), bgra as *const c_void, size as u32 * 4, 0);
}

pub unsafe fn r_end_and_present() -> bool {
    let t = R.t;
    if !R.ok || t.is_null() || !(*t).ok {
        return false;
    }
    let r = rr();
    let ctx = r.ctx.clone().unwrap();

    if r.dirty_x1 > r.dirty_x0 && r.dirty_y1 > r.dirty_y0 {
        let b = D3D11_BOX { left: r.dirty_x0 as u32, top: r.dirty_y0 as u32, front: 0, right: r.dirty_x1 as u32, bottom: r.dirty_y1 as u32, back: 1 };
        ctx.UpdateSubresource(
            r.glyph_tex.as_ref().unwrap(),
            0,
            Some(&b),
            r.glyph_cpu.add(r.dirty_y0 as usize * GLYPH_ATLAS as usize + r.dirty_x0 as usize) as *const c_void,
            GLYPH_ATLAS as u32,
            0,
        );
    }
    r.dirty_x0 = i32::MAX;
    r.dirty_y0 = i32::MAX;
    r.dirty_x1 = 0;
    r.dirty_y1 = 0;

    let mut m: D3D11_MAPPED_SUBRESOURCE = zeroed();
    if ctx.Map(r.cbuf.as_ref().unwrap(), 0, D3D11_MAP_WRITE_DISCARD, 0, Some(&mut m)).is_ok() {
        let c = core::slice::from_raw_parts_mut(m.pData as *mut f32, 20);
        c[0] = 1.0 / (*t).w as f32;
        c[1] = 1.0 / (*t).h as f32;
        c[2] = if r.scale > 0.0 { r.scale } else { 1.0 };
        c[3] = 0.0;
        // DirectWrite's alpha correction for gamma 1.8 (the ps: g + g(1 - g)(..)) and the text contrast boost
        c[4] = 0.1469 / 4.0;
        c[5] = -0.8911 / 4.0;
        c[6] = 1.4644 / 4.0;
        c[7] = -0.3234 / 4.0;
        c[8] = 1.0;
        c[9] = 0.0;
        c[10] = 0.0;
        c[11] = 0.0;
        c[12..20].copy_from_slice(&r.bd);
        ctx.Unmap(r.cbuf.as_ref().unwrap(), 0);
    }

    if r.bd_dirty {
        r_bd_blur();
    }

    let clear = [0.0f32; 4];
    ctx.OMSetRenderTargets(Some(&[(*t).rtv.clone()]), None);
    ctx.ClearRenderTargetView((*t).rtv.as_ref().unwrap(), &clear);
    let vp = D3D11_VIEWPORT { TopLeftX: 0.0, TopLeftY: 0.0, Width: (*t).w as f32, Height: (*t).h as f32, MinDepth: 0.0, MaxDepth: 1.0 };
    ctx.RSSetViewports(Some(&[vp]));
    ctx.RSSetState(r.raster.as_ref());
    let bf = [0.0f32; 4];
    ctx.OMSetBlendState(r.blend.as_ref(), Some(&bf), 0xFFFFFFFF);
    ctx.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ctx.IASetInputLayout(r.layout.as_ref());
    let stride = size_of::<RInst>() as u32;
    let offset = 0u32;
    ctx.IASetVertexBuffers(0, 1, Some(&r.ibuf), Some(&stride), Some(&offset));
    ctx.VSSetShader(r.vs.as_ref(), None);
    ctx.VSSetConstantBuffers(0, Some(&[r.cbuf.clone()]));
    ctx.PSSetShader(r.ps.as_ref(), None);
    ctx.PSSetConstantBuffers(0, Some(&[r.cbuf.clone()]));
    let srvs = [r.glyph_srv.clone(), r.icon_srv.clone(), r.bd_quarter_b.srv.clone(), r.pv.srv.clone()];
    ctx.PSSetShaderResources(0, Some(&srvs));
    let samps = [if r.scale != 1.0 { r.samp_linear.clone() } else { r.samp_point.clone() }, r.samp_linear.clone()];
    ctx.PSSetSamplers(0, Some(&samps));

    let mut done = 0;
    while done < r.count {
        let n = min(r.count - done, r.ibuf_cap as i32);
        if ctx.Map(r.ibuf.as_ref().unwrap(), 0, D3D11_MAP_WRITE_DISCARD, 0, Some(&mut m)).is_err() {
            break;
        }
        memcpy(m.pData as *mut RInst, r.inst.add(done as usize), n as usize * size_of::<RInst>());
        ctx.Unmap(r.ibuf.as_ref().unwrap(), 0);
        ctx.DrawInstanced(4, n as u32, 0, 0);
        done += n;
    }

    let Some(swap) = (*t).swap.as_ref() else {
        return true; // offscreen (r_target_tex): the picture stays in its texture
    };
    let hr = swap.Present(1, DXGI_PRESENT(0));
    if hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET {
        log_msg!("device lost 0x{:08x}", hr.0 as u32);
        r.ok = false;
        return false;
    }
    true
}
