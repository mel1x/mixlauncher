// "Support the author": the DonationAlerts QR (drawn from its modules), a link and crypto wallets to copy.

use crate::*;

static k_don_qr: [u64; 49] = [
    0x1fd2bd08f807f, 0x105fd5bfcf641, 0x1758b389c715d, 0x174b0c6a5d95d,
    0x17428eff8915d, 0x1047c0476a541, 0x1fd555555557f, 0x00067d45dcb00,
    0x07d588fdec87d, 0x0beb0f323b796, 0x1c1ceb57e46db, 0x098615aa0b6a3,
    0x0e938814dadc0, 0x08ca0d9e00bb5, 0x1e04e37e96962, 0x11c48c0a50182,
    0x06d91fd32f652, 0x08e138129db34, 0x138646f924be5, 0x09fa9f409fd1d,
    0x1c57269d5c759, 0x09e11648f8f35, 0x1bf5eaff4d9f1, 0x091255c66e717,
    0x0d53ecd76af58, 0x07199e46f9518, 0x11f5607da71f8, 0x1822378a949a6,
    0x07f9e8f83524d, 0x026ab56bb39b7, 0x1d4cdab15d2f6, 0x1a3095c5a6824,
    0x1d29068589ff0, 0x08a1855811c34, 0x134fe196ed376, 0x0196f57a2a69f,
    0x152d088fa905c, 0x0abb17d2a6091, 0x16f5e20ce0be2, 0x10167df8b510e,
    0x0fffce7de1ec7, 0x09130f44ef700, 0x1d55c854b3e7f, 0x1912154695541,
    0x17f1e87c8155d, 0x13f11c840eb5d, 0x00effa16dd55d, 0x106a0c786fa41,
    0x1e2984a961b7f,
];
const DON_QR_N: i32 = 49;
pub const DON_LINK_URL: &str = "https://dalink.to/mel1xy";

struct Wallet {
    net: &'static str,
    addr: &'static str,
}

static k_wallets: [Wallet; 8] = [
    Wallet { net: "TRC20", addr: "TD1yryydTBLre9ATmhhMjcdBxUZSuLhcYd" },
    Wallet { net: "ERC20", addr: "0x10B2945C3222F9Fb27CDcDBbEfF298961Db10e96" },
    Wallet { net: "BEP20", addr: "0x10B2945C3222F9Fb27CDcDBbEfF298961Db10e96" },
    Wallet { net: "ERC20", addr: "0x10B2945C3222F9Fb27CDcDBbEfF298961Db10e96" },
    Wallet { net: "Bitcoin", addr: "bc1qrhtwhv75jrh8fz6y8qc38evkxrvg7ge0nu4z3w" },
    Wallet { net: "Litecoin", addr: "LVtmyVY3YbMMKzjxhkVu5bgNFY5T7hhbf2" },
    Wallet { net: "ERC20", addr: "0x10B2945C3222F9Fb27CDcDBbEfF298961Db10e96" },
    Wallet { net: "TRC20", addr: "TD1yryydTBLre9ATmhhMjcdBxUZSuLhcYd" },
];

const LOGO_USDT: i32 = 0;
const LOGO_ETH: i32 = 1;
const LOGO_BTC: i32 = 2;
const LOGO_LTC: i32 = 3;
const LOGO_USDC: i32 = 4;
const LOGO_TRX: i32 = 5;

// One row per coin; USDT has a chip per network, the others copy their single wallet.
struct Coin {
    name: &'static str,
    top: u32,
    bottom: u32,
    logo: i32,
    wallet: i32,
    nets: i32,
}

static k_coins: [Coin; 6] = [
    Coin { name: "USDT", top: RGBA(84, 196, 160, 255), bottom: RGBA(22, 126, 98, 255), logo: LOGO_USDT, wallet: 0, nets: 3 },
    Coin { name: "ETH", top: RGBA(146, 166, 252, 255), bottom: RGBA(76, 92, 212, 255), logo: LOGO_ETH, wallet: 3, nets: 1 },
    Coin { name: "BTC", top: RGBA(255, 190, 80, 255), bottom: RGBA(238, 118, 8, 255), logo: LOGO_BTC, wallet: 4, nets: 1 },
    Coin { name: "Litecoin", top: RGBA(112, 152, 222, 255), bottom: RGBA(38, 76, 146, 255), logo: LOGO_LTC, wallet: 5, nets: 1 },
    Coin { name: "USDC", top: RGBA(86, 164, 242, 255), bottom: RGBA(28, 92, 182, 255), logo: LOGO_USDC, wallet: 6, nets: 1 },
    Coin { name: "TRON", top: RGBA(255, 86, 104, 255), bottom: RGBA(196, 0, 32, 255), logo: LOGO_TRX, wallet: 7, nets: 1 },
];

// The logos no font has (Tether, Ethereum, TRON) as shapes in a unit box, rasterized into the glyph atlas.
fn pt_in_poly(p: &[f32], n: usize, x: f32, y: f32) -> bool {
    let mut inside = false;
    let mut j = n - 1;
    for i in 0..n {
        let (xi, yi, xj, yj) = (p[i * 2], p[i * 2 + 1], p[j * 2], p[j * 2 + 1]);
        if (yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi {
            inside = !inside;
        }
        j = i;
    }
    inside
}

fn near_seg(ax: f32, ay: f32, bx: f32, by: f32, x: f32, y: f32, half: f32) -> bool {
    let dx = bx - ax;
    let dy = by - ay;
    let t = clamp(((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy), 0.0, 1.0);
    let ex = ax + dx * t - x;
    let ey = ay + dy * t - y;
    ex * ex + ey * ey <= half * half
}

fn logo_inside(id: i32, x: f32, y: f32) -> bool {
    match id {
        LOGO_USDT => {
            if (0.10..=0.90).contains(&x) && (0.14..=0.33).contains(&y) {
                return true;
            }
            if (0.40..=0.60).contains(&x) && (0.33..=0.90).contains(&y) {
                return true;
            }
            let ox = (x - 0.5) / 0.43;
            let oy = (y - 0.50) / 0.13;
            let ix = (x - 0.5) / 0.33;
            let iy = (y - 0.50) / 0.065;
            ox * ox + oy * oy <= 1.0 && ix * ix + iy * iy >= 1.0 && y >= 0.40
        }
        LOGO_ETH => {
            let top = [0.5, 0.0, 0.82, 0.545, 0.5, 0.735, 0.18, 0.545];
            let bot = [0.5, 0.80, 0.82, 0.605, 0.5, 1.0, 0.18, 0.605];
            pt_in_poly(&top, 4, x, y) || pt_in_poly(&bot, 4, x, y)
        }
        LOGO_TRX => {
            let (ax, ay, bx, by, cx, cy, dx, dy, w) = (0.06, 0.10, 0.96, 0.36, 0.44, 0.96, 0.66, 0.46, 0.055);
            near_seg(ax, ay, bx, by, x, y, w)
                || near_seg(bx, by, cx, cy, x, y, w)
                || near_seg(cx, cy, ax, ay, x, y, w)
                || near_seg(ax, ay, dx, dy, x, y, w)
                || near_seg(dx, dy, cx, cy, x, y, w)
                || near_seg(dx, dy, bx, by, x, y, w * 0.8)
        }
        _ => false,
    }
}

#[derive(Clone, Copy)]
struct LogoCache {
    id: i32,
    px: i32,
    gen_: u32,
    x: i32,
    y: i32,
}
static mut g_logo_cache: [LogoCache; 16] = [LogoCache { id: 0, px: 0, gen_: 0, x: 0, y: 0 }; 16];

unsafe fn logo_region(id: i32, px: i32, ox: &mut i32, oy: &mut i32) -> bool {
    let cache = &mut *(&raw mut g_logo_cache);
    let mut free_slot: i32 = -1;
    for (i, c) in cache.iter().enumerate() {
        if c.px == px && c.id == id && c.gen_ == g_atlas_gen {
            *ox = c.x;
            *oy = c.y;
            return true;
        }
        if free_slot < 0 && (c.px == 0 || c.gen_ != g_atlas_gen) {
            free_slot = i as i32;
        }
    }
    if free_slot < 0 {
        free_slot = 0;
    }
    if R.glyph_cpu.is_null() || !atlas_alloc_pub(px, px, ox, oy) {
        F.overflow = true;
        return false;
    }
    let ss_ = 4;
    for yy in 0..px {
        for xx in 0..px {
            let mut hit = 0;
            for sy in 0..ss_ {
                for sx in 0..ss_ {
                    if logo_inside(id, (xx as f32 + (sx as f32 + 0.5) / ss_ as f32) / px as f32, (yy as f32 + (sy as f32 + 0.5) / ss_ as f32) / px as f32) {
                        hit += 1;
                    }
                }
            }
            *R.glyph_cpu.add((*oy + yy) as usize * GLYPH_ATLAS as usize + (*ox + xx) as usize) = (hit * 255 / (ss_ * ss_)) as u8;
        }
    }
    r_glyph_dirty(*ox, *oy, px, px);
    cache[free_slot as usize] = LogoCache { id, px, gen_: g_atlas_gen, x: *ox, y: *oy };
    true
}

// Squircle with the coin's vertical gradient and its white logo.
unsafe fn draw_coin_icon(i: usize, x: f32, y: f32, sz: f32) {
    let x = x.floor();
    let y = y.floor();
    let rad = (sz * 0.3 + 0.5).floor();
    let saved = R.clip;
    for r in 0..sz as i32 {
        let y0 = max(y + r as f32, saved[1]);
        let y1 = min(y + r as f32 + 1.0, saved[3]);
        if y1 <= y0 {
            continue;
        }
        r_set_clip(saved[0], y0, saved[2], y1);
        r_rect(x, y, sz, sz, color_mix(k_coins[i].top, k_coins[i].bottom, (r as f32 + 0.5) / sz), rad);
    }
    R.clip = saved;
    let white = RGBA(255, 255, 255, 255);
    let cx = x + sz * 0.5;
    let cy = y + sz * 0.5;
    let logo = k_coins[i].logo;
    if logo == LOGO_USDT || logo == LOGO_ETH || logo == LOGO_TRX {
        let px = (sz * if logo == LOGO_ETH { 0.62 } else { 0.58 } + 0.5) as i32;
        let (mut ox, mut oy) = (0, 0);
        if logo_region(logo, px, &mut ox, &mut oy) {
            let inv = 1.0 / GLYPH_ATLAS as f32;
            let lx = (cx - px as f32 * 0.5 + 0.5).floor();
            let ly = (cy - px as f32 * 0.5 + 0.5).floor();
            r_glyph(lx, ly, px as f32, px as f32, ox as f32 * inv, oy as f32 * inv, (ox + px) as f32 * inv, (oy + px) as f32 * inv, white);
        }
        return;
    }
    if logo == LOGO_USDC {
        let ring = (sz * 0.62 + 0.5).floor();
        r_rect_ex((cx - ring * 0.5 + 0.5).floor(), (cy - ring * 0.5 + 0.5).floor(), ring, ring, white, ring * 0.5, max(1.0, (sz * 0.06 + 0.5).floor()), 0.0);
    }
    let sym = if logo == LOGO_BTC {
        w!("\u{20BF}")
    } else if logo == LOGO_LTC {
        w!("\u{0141}")
    } else {
        w!("$")
    };
    let fs = sz * if logo == LOGO_USDC { 0.40 } else { 0.56 };
    let w = text_width(FONT_TEXT_SEMIBOLD, fs, sym, -1);
    text_draw(FONT_TEXT_SEMIBOLD, fs, (cx - w * 0.5 + 0.5).floor(), text_baseline(FONT_TEXT_SEMIBOLD, fs, cy), sym, -1, white);
}

pub const DON_HOV_LINK: usize = 8;
pub const DON_HOV_CLOSE: usize = 9;

pub unsafe fn don_open() {
    end_edit(true);
    SW.pop_open = false;
    let d = &mut sw().don;
    d.open = true;
    d.closing = false;
    d.t = if anims_enabled() { 0.0 } else { 1.0 };
    d.open_time = time_now();
    d.scroll = 0.0;
    d.scroll_target = 0.0;
    d.scroll_v = 0.0;
    d.sb_t = 0.0;
    d.sb_prev = 0.0;
    d.sb_seen = 0.0;
    d.flash = -1;
    d.hov = [0.0; 10];
    d.cp = [0.0; 8];
    settings_invalidate();
}

pub unsafe fn don_close() {
    if !SW.don.open {
        return;
    }
    if anims_enabled() {
        SW.don.closing = true;
    } else {
        SW.don.open = false;
    }
    settings_invalidate();
}

pub unsafe fn don_active() -> bool {
    SW.don.open && !SW.don.closing
}

// Confetti in the canvas-confetti manner: fired from both top corners, braked, then drifting down with wobble and spin.
#[derive(Clone, Copy)]
struct Confetti {
    x: f32,
    y: f32,
    vx: f32,
    vy: f32,
    fall: f32,
    sway: f32,
    size: f32,
    rot: f32,
    rot_v: f32,
    wob: f32,
    wob_v: f32,
    age: f32,
    life: f32,
    color: u32,
}
static mut g_conf: [Confetti; 320] = unsafe { zeroed() };
static mut g_nconf: usize = 0;
static mut g_conf_seed: u32 = 0x9E3779B9;

unsafe fn conf_rand() -> f32 {
    g_conf_seed = g_conf_seed.wrapping_mul(1664525).wrapping_add(1013904223);
    (g_conf_seed >> 8) as f32 / 16777216.0
}

pub unsafe fn confetti_burst() {
    if !anims_enabled() {
        return;
    }
    let colors = [RGBA(255, 94, 58, 255), RGBA(255, 184, 0, 255), RGBA(46, 196, 182, 255), RGBA(58, 134, 255, 255), RGBA(255, 77, 141, 255), RGBA(150, 92, 255, 255)];
    g_conf_seed ^= (time_now() * 1000.0) as u64 as u32;
    let conf = &mut *(&raw mut g_conf);
    for i in 0..220 {
        if g_nconf == conf.len() {
            g_nconf -= 1;
            conf.copy_within(1..=g_nconf, 0);
        }
        let ci = g_nconf;
        g_nconf += 1;
        let left = (i & 1) != 0;
        // up to 50 degrees above the horizon, toward the middle; varied speeds spread them over the window
        let r = conf_rand();
        let a = (-8.0 + conf_rand() * 58.0) * 0.0174533;
        let sp = SSC(500.0 + (1.0 - r * r) * 2600.0);
        let c = &mut conf[ci];
        c.x = if left { 0.0 } else { SW.W as f32 };
        c.y = SSC(150.0 + conf_rand() * 20.0);
        c.vx = a.cos() * sp * if left { 1.0 } else { -1.0 };
        c.vy = -a.sin() * sp;
        c.fall = SSC(130.0 + conf_rand() * 140.0);
        c.sway = SSC(5.0 + conf_rand() * 12.0);
        c.size = SSC(6.0 + conf_rand() * 4.0);
        c.rot = conf_rand() * 6.283;
        c.rot_v = (conf_rand() - 0.5) * 10.0;
        c.wob = conf_rand() * 6.283;
        c.wob_v = 4.0 + conf_rand() * 4.0;
        c.age = -conf_rand() * 0.08;
        c.life = 3.2 + conf_rand() * 1.6;
        c.color = colors[(conf_rand() * colors.len() as f32) as usize % colors.len()];
    }
    settings_invalidate();
}

pub unsafe fn confetti_animate(dt: f32) -> bool {
    let conf = &mut *(&raw mut g_conf);
    let mut n = 0;
    let k = (-6.3 * dt).exp(); // canvas-confetti: velocity *= 0.9 per 60 Hz frame
    for i in 0..g_nconf {
        let mut c = conf[i];
        c.age += dt;
        if c.age >= c.life || c.y > SW.H as f32 + SSC(20.0) {
            continue;
        }
        if c.age > 0.0 {
            c.vx *= k;
            c.vy *= k;
            c.x += c.vx * dt;
            c.y += (c.vy + c.fall) * dt;
            c.wob += c.wob_v * dt;
            c.rot += c.rot_v * dt;
        }
        conf[n] = c;
        n += 1;
    }
    g_nconf = n;
    n > 0
}

pub unsafe fn confetti_draw() {
    let conf = &*(&raw const g_conf);
    for c in &conf[..g_nconf] {
        if c.age < 0.0 {
            continue;
        }
        let fade = clamp((c.life - c.age) / 0.6, 0.0, 1.0);
        let tumble = c.wob.cos();
        let x = c.x + c.wob.sin() * c.sway;
        let col = color_mix(c.color, RGBA(0, 0, 0, 255), (1.0 - tumble.abs()) * 0.3);
        r_rect_rot(x, c.y, c.size, c.size, c.rot, max(0.12, tumble.abs()), color_alpha(col, fade), SSC(1.0));
    }
}

pub unsafe fn don_copy(w: i32) {
    let a = wstr(k_wallets[w as usize].addr);
    clipboard_set(a.as_ptr(), -1);
    SW.don.flash = w;
    SW.don.flash_until = time_now() + 1.4;
    SetTimer(SW.hwnd, SETTINGS_TIMER_FLASH, 1500, None);
    settings_invalidate();
}

const DON_STAGGER: f32 = 0.035;
const DON_ITEM_DUR: f32 = 0.32;
const DON_ITEMS: i32 = 11;

// Entrance of the k-th block of the modal (header, QR, link, rows...), staggered after opening.
unsafe fn don_item(k: i32) -> f32 {
    if !anims_enabled() {
        return 1.0;
    }
    let t = (time_now() - SW.don.open_time) as f32 - DON_STAGGER * k as f32 - 0.05;
    ease_out_cubic(t / anim_sec((DON_ITEM_DUR * 1000.0) as i32))
}

pub unsafe fn don_animate(dt: f32) -> bool {
    let d = &mut sw().don;
    if !d.open {
        return false;
    }
    let mut anim = false;
    if d.closing {
        let dd = anim_sec(130);
        d.t = if dd > 0.0 { d.t - dt / dd } else { 0.0 };
        if d.t <= 0.0 {
            d.t = 0.0;
            d.open = false;
            d.closing = false;
        }
        return true;
    }
    if d.t < 1.0 {
        let dd = anim_sec(240);
        d.t = if dd > 0.0 { min(1.0, d.t + dt / dd) } else { 1.0 };
        anim = true;
    }
    if time_now() - d.open_time < anim_sec(((DON_ITEM_DUR + DON_STAGGER * DON_ITEMS as f32 + 0.05) * 1000.0) as i32) as f64 {
        anim = true;
    }
    let ms = max(0.0, d.content_h - d.view_h);
    d.scroll_target = clamp(d.scroll_target, 0.0, ms);
    let target = d.scroll_target;
    anim |= spring_step(&mut d.scroll, &mut d.scroll_v, target, anim_omega(g_cfg.anim_scroll_ms), dt);
    let now = time_now();
    if d.scroll != d.sb_prev {
        d.sb_seen = now;
    }
    d.sb_prev = d.scroll;
    let recent = now - d.sb_seen < 0.9;
    anim |= approach(&mut d.sb_t, if recent && ms > 0.0 { 1.0 } else { 0.0 }, if recent { 20.0 } else { 7.0 }, dt);
    if recent && ms > 0.0 {
        anim = true;
    }
    let flash_on = d.flash >= 0 && now < d.flash_until;
    for i in 0..8 {
        anim |= approach(&mut d.cp[i], if flash_on && d.flash == i as i32 { 1.0 } else { 0.0 }, 16.0, dt);
    }
    for i in 0..10 {
        let want = if i == DON_HOV_LINK {
            DON_LINK
        } else if i == DON_HOV_CLOSE {
            DON_CLOSE
        } else {
            i as i32
        };
        let target = if SW.hover.type_ == HT_DON && SW.hover.idx == want { 1.0 } else { 0.0 };
        anim |= approach(&mut d.hov[i], target, 20.0, dt);
    }
    anim
}

pub unsafe fn draw_donate(t: &Theme, c: &SColors) {
    let d = &mut sw().don;
    let e = if d.closing { d.t * d.t } else { ease_out_quart(d.t) };
    R.opacity = e;
    r_rect(0.0, 0.0, SW.W as f32, SW.H as f32, c.overlay, 0.0);
    let pw = SSR(380.0);
    let m = SSR(16.0);
    let mut qr_sz = SSR(184.0);
    let row_h = SSR(54.0);
    let row_gap = SSR(8.0);
    let link_h = SSR(32.0);
    qr_sz = (qr_sz / (DON_QR_N + 7) as f32).floor() * (DON_QR_N + 7) as f32;
    let content = SSR(20.0) + SSR(24.0) + SSR(16.0) + qr_sz + SSR(16.0) + link_h + SSR(22.0) + SSR(20.0) + (row_h + row_gap) * 6.0 - row_gap + SSR(18.0);
    d.content_h = content;
    let ph = min(content, SW.H as f32 - m * 2.0);
    d.view_h = ph;
    let ms = max(0.0, content - ph);
    d.scroll = clamp(d.scroll, 0.0, ms);
    let px = ((SW.W as f32 - pw) * 0.5).floor();
    let py = ((SW.H as f32 - ph) * 0.5 + (1.0 - e) * SSR(if d.closing { 8.0 } else { 22.0 })).floor();
    box_set(&mut d.panel, px, py, pw, ph);
    r_panel(px, py, pw, ph, SSR(16.0), if t.dark { RGBA(36, 36, 39, 255) } else { RGBA(252, 252, 253, 255) }, c.pop_border, c.shadow, SSR(14.0), SSR(30.0));

    r_set_clip(px, py + 1.0, px + pw, py + ph - 1.0);
    let x0 = px + SSR(20.0);
    let x1 = px + pw - SSR(20.0);
    let mut y = py + SSR(20.0) - (d.scroll + 0.5).floor();
    let mut k = 0;
    let closing = d.closing;
    // Closing: content first, then the empty panel (a fading white QR reads as a gray flash).
    let mut block = |k: &mut i32| -> f32 {
        let ek = if closing {
            1.0
        } else {
            let v = don_item(*k);
            *k += 1;
            v
        };
        R.opacity = if closing { clamp(e * 2.0 - 1.0, 0.0, 1.0) } else { e * ek };
        ((1.0 - ek) * SSR(10.0) + 0.5).floor()
    };

    {
        let dy = block(&mut k);
        let hy = y + dy;
        text_draw(FONT_TEXT_SEMIBOLD, SSC(17.0), x0, text_baseline(FONT_TEXT_SEMIBOLD, SSC(17.0), hy + SSR(12.0)), tr!("Поддержать автора", "Support the author"), -1, t.text);
        let cb = SSR(30.0);
        let hv = d.hov[DON_HOV_CLOSE];
        box_set(&mut d.close, x1 - cb + SSR(6.0), y - SSR(3.0), cb, cb);
        if hv > 0.0 {
            r_rect(d.close[0], d.close[1] + dy, cb, cb, color_alpha(c.ctl_hover, hv), SSR(8.0));
        }
        text_draw_icon(0xE8BB, SSC(10.0), d.close[0] + cb * 0.5, d.close[1] + dy + cb * 0.5, color_mix(t.dim, t.text, hv));
    }
    y += SSR(24.0) + SSR(16.0);

    // QR: always dark on white
    {
        let dy = block(&mut k);
        let md = (qr_sz / (DON_QR_N + 7) as f32).floor();
        let tile = md * (DON_QR_N + 7) as f32;
        let qo = ((tile - md * DON_QR_N as f32) * 0.5).floor();
        let qx = (px + (pw - tile) * 0.5).floor();
        let qy = y + ((qr_sz - tile) * 0.5).floor() + dy;
        r_rect(qx, qy, tile, tile, RGBA(255, 255, 255, 255), SSR(10.0));
        if !t.dark {
            r_border(qx, qy, tile, tile, RGBA(0, 0, 0, 20), SSR(10.0));
        }
        for r in 0..DON_QR_N {
            let bits = k_don_qr[r as usize];
            let mut col = 0;
            while col < DON_QR_N {
                if (bits >> col) & 1 == 0 {
                    col += 1;
                    continue;
                }
                let mut run = col;
                while run < DON_QR_N && (bits >> run) & 1 != 0 {
                    run += 1;
                }
                r_rect(qx + qo + md * col as f32, qy + qo + md * r as f32, md * (run - col) as f32, md, RGBA(17, 17, 19, 255), 0.0);
                col = run;
            }
        }
    }
    y += qr_sz + SSR(16.0);

    {
        let dy = block(&mut k);
        let hv = d.hov[DON_HOV_LINK];
        let fs = SSC(13.5);
        let cy = y + dy + link_h * 0.5;
        let lbl = w!("DonationAlerts");
        let lw = text_width(FONT_TEXT_SEMIBOLD, fs, lbl, -1);
        let bw = (SSR(16.0) + lw + SSR(8.0) + SSR(12.0) + SSR(14.0)).floor();
        let bx = (px + (pw - bw) * 0.5).floor();
        box_set(&mut d.link, bx, y, bw, link_h);
        r_rect(bx, y + dy, bw, link_h, color_mix(c.ctl, c.ctl_hover, hv), link_h * 0.5);
        text_draw(FONT_TEXT_SEMIBOLD, fs, bx + SSR(16.0), text_baseline(FONT_TEXT_SEMIBOLD, fs, cy), lbl, -1, t.text);
        text_draw_icon(0xE8A7, SSC(11.0), bx + SSR(16.0) + lw + SSR(8.0) + SSR(6.0), cy, color_mix(t.dim, t.text, hv));
    }
    y += link_h + SSR(22.0);

    {
        let dy = block(&mut k);
        text_draw(
            FONT_TEXT_SEMIBOLD,
            SSC(12.5),
            x0 + SSR(2.0),
            text_baseline(FONT_TEXT_SEMIBOLD, SSC(12.5), y + dy + SSR(6.0)),
            tr!("Криптовалюта", "Crypto"),
            -1,
            color_mix(t.dim, t.text, 0.35),
        );
    }
    y += SSR(20.0);

    let ok_col = if t.dark { RGBA(74, 210, 130, 255) } else { RGBA(24, 140, 72, 255) };
    for i in 0..6 {
        let dy = block(&mut k);
        let base_op = R.opacity;
        let ry = y + dy;
        let cy = ry + row_h * 0.5;
        let w0 = k_coins[i].wallet;
        let nets = k_coins[i].nets;
        r_rect(x0, ry, x1 - x0, row_h, c.ctl, SSR(12.0));
        if nets == 1 {
            box_set(&mut d.copy[w0 as usize], x0, y, x1 - x0, row_h);
        }
        let isz = SSR(34.0);
        let ix = x0 + SSR(10.0);
        draw_coin_icon(i, ix, cy - isz * 0.5, isz);
        let tx = ix + isz + SSR(12.0);
        let nfs = SSC(14.5);
        let afs = SSC(12.0);
        let name = wstr(k_coins[i].name);
        text_draw(FONT_TEXT_SEMIBOLD, nfs, tx, text_baseline(FONT_TEXT_SEMIBOLD, nfs, cy - SSR(9.0)), name.as_ptr(), -1, t.text);
        if nets > 1 {
            text_draw(FONT_TEXT, afs, tx, text_baseline(FONT_TEXT, afs, cy + SSR(10.0)), w!("Tether"), -1, t.faint);
            let mut xr = x1 - SSR(10.0);
            let chh = SSR(30.0);
            let cfs = SSC(12.5);
            for n in (0..nets).rev() {
                let wi = (w0 + n) as usize;
                let hv = d.hov[wi];
                let cp = d.cp[wi];
                let lbl = wstr(k_wallets[wi].net);
                let lw = text_width(FONT_TEXT, cfs, lbl.as_ptr(), -1);
                let cw = (SSR(10.0) + lw + SSR(6.0) + SSR(10.0) + SSR(10.0)).floor();
                let chx = xr - cw;
                let chy = (cy - chh * 0.5).floor();
                box_set(&mut d.copy[wi], chx, chy - dy, cw, chh);
                if hv > 0.0 {
                    r_rect(chx, chy, cw, chh, color_alpha(c.ctl_hover, hv), SSR(8.0));
                }
                if cp > 0.0 {
                    r_rect(chx, chy, cw, chh, color_alpha(ok_col, 0.16 * cp), SSR(8.0));
                }
                let col = color_mix(color_mix(t.dim, t.text, hv), ok_col, cp);
                text_draw(FONT_TEXT, cfs, chx + SSR(10.0), text_baseline(FONT_TEXT, cfs, cy), lbl.as_ptr(), -1, col);
                let gx = chx + cw - SSR(10.0) - SSR(5.0);
                let slide = SSR(7.0);
                r_set_clip(chx, max(chy, py + 1.0), chx + cw, min(chy + chh, py + ph - 1.0));
                R.opacity = base_op * (1.0 - cp);
                text_draw_icon(0xE8A7, SSC(9.0), gx, cy - cp * slide, col);
                R.opacity = base_op * cp;
                text_draw_icon(0xE73E, SSC(10.0), gx, cy + (1.0 - cp) * slide, ok_col);
                R.opacity = base_op;
                r_set_clip(px, py + 1.0, px + pw, py + ph - 1.0);
                xr = chx - SSR(2.0);
            }
        } else {
            let wl = &k_wallets[w0 as usize];
            let a: Vec<char> = wl.addr.chars().collect();
            let n = a.len();
            let first: String = a[..6].iter().collect();
            let last: String = a[n - 4..].iter().collect();
            let shortaddr = wstr(&format!("{}  \u{00B7}  {}\u{2026}{}", wl.net, first, last));
            text_draw(FONT_TEXT, afs, tx, text_baseline(FONT_TEXT, afs, cy + SSR(10.0)), shortaddr.as_ptr(), -1, t.faint);
            let hv = d.hov[w0 as usize];
            let cp = d.cp[w0 as usize];
            let l0 = tr!("Копировать", "Copy");
            let l1 = tr!("Скопировано", "Copied");
            let hfs = SSC(12.5);
            let phh = SSR(30.0);
            let slide = SSR(9.0);
            let w0p = (SSR(11.0) + text_width(FONT_TEXT, hfs, l0, -1) + SSR(8.0) + SSR(14.0) + SSR(10.0)).floor();
            let w1p = (SSR(11.0) + text_width(FONT_TEXT, hfs, l1, -1) + SSR(8.0) + SSR(14.0) + SSR(10.0)).floor();
            let pw2 = (w0p + (w1p - w0p) * cp + 0.5).floor();
            let hx = x1 - SSR(10.0) - pw2;
            let hy = (cy - phh * 0.5).floor();
            if hv > 0.0 {
                r_rect(hx, hy, pw2, phh, color_alpha(c.ctl_hover, hv * (1.0 - cp)), SSR(8.0));
            }
            if cp > 0.0 {
                r_rect(hx, hy, pw2, phh, color_alpha(ok_col, 0.16 * cp), SSR(8.0));
            }
            r_set_clip(hx, max(hy, py + 1.0), hx + pw2, min(hy + phh, py + ph - 1.0));
            let gx = hx + pw2 - SSR(10.0) - SSR(7.0);
            let base = text_baseline(FONT_TEXT, hfs, cy);
            let c0 = color_mix(t.dim, t.text, hv);
            R.opacity = base_op * (1.0 - cp);
            text_draw(FONT_TEXT, hfs, hx + SSR(11.0), base - cp * slide, l0, -1, c0);
            text_draw_icon(0xE8C8, SSC(12.0), gx, cy - cp * slide, c0);
            R.opacity = base_op * cp;
            text_draw(FONT_TEXT, hfs, hx + SSR(11.0), base + (1.0 - cp) * slide, l1, -1, ok_col);
            text_draw_icon(0xE73E, SSC(12.0), gx, cy + (1.0 - cp) * slide, ok_col);
            R.opacity = base_op;
            r_set_clip(px, py + 1.0, px + pw, py + ph - 1.0);
        }
        y += row_h + row_gap;
    }
    R.opacity = e;
    r_clip_all();
    if ms > 0.0 && d.sb_t > 0.0 {
        let vh = ph - SSR(24.0);
        let bar_h = max(SSR(28.0), vh * ph / content);
        let bw = SSR(3.0);
        r_rect(px + pw - SSR(6.0) - bw, (py + SSR(12.0) + (vh - bar_h) * d.scroll / ms).floor(), bw, bar_h.floor(), color_alpha(t.faint, d.sb_t), bw * 0.5);
    }
    R.opacity = 1.0;
}
