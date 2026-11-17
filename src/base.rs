// base.rs: types, memory, strings, hashing, time, files and the log, threads and windows, monitors, the clipboard.

use crate::*;
use core::fmt;

pub type WCHAR = u16;
pub type u8_ = u8;

pub const MAX_PATH: usize = 260;

// The version: res/version.h, read by build.rs
const fn ver_num(s: &str) -> i32 {
    let b = s.as_bytes();
    let mut i = 0;
    let mut n = 0;
    while i < b.len() {
        n = n * 10 + (b[i] - b'0') as i32;
        i += 1;
    }
    n
}
pub const ML_VER_MAJOR: i32 = ver_num(env!("ML_VER_MAJOR"));
pub const ML_VER_MINOR: i32 = ver_num(env!("ML_VER_MINOR"));
pub const ML_VER_PATCH: i32 = ver_num(env!("ML_VER_PATCH"));
pub const ML_VER_STR: &str = env!("ML_VER_STR");

pub static mut g_lang_ru: bool = false;

#[macro_export]
macro_rules! tr {
    ($ru:literal, $en:literal) => {
        if unsafe { $crate::base::g_lang_ru } { windows_sys::w!($ru) } else { windows_sys::w!($en) }
    };
}

#[macro_export]
macro_rules! countof {
    ($a:expr) => {
        ($a).len() as i32
    };
}

pub static mut g_hwnd: HWND = null_mut(); // the launcher window, the target of every worker's messages
pub static mut g_pinned: bool = false; // --pin: a test copy, no hotkey, tray, taskbar or single-instance lock
pub static mut g_other_monitor: bool = false; // --other-monitor: opens away from the cursor and traces keys to the log
pub static mut g_data_dir: [WCHAR; MAX_PATH] = [0; MAX_PATH];
pub static mut g_exe_path: [WCHAR; MAX_PATH] = [0; MAX_PATH];

// Other instances, the installer and tools\stop-running.ps1 post these by number: add new ones at the end.
pub const WM_APP_TOGGLE: u32 = WM_APP + 1;
pub const WM_APP_SHOW: u32 = WM_APP + 2;
pub const WM_APP_APPS_READY: u32 = WM_APP + 3;
pub const WM_APP_ICONS_READY: u32 = WM_APP + 4;
pub const WM_APP_FILES_READY: u32 = WM_APP + 5;
pub const WM_APP_TRAY: u32 = WM_APP + 6;
pub const WM_APP_EXIT: u32 = WM_APP + 7;
pub const WM_APP_LAUNCH_FAILED: u32 = WM_APP + 8;
pub const WM_APP_CLICK_OUTSIDE: u32 = WM_APP + 9;
pub const WM_APP_SETTINGS: u32 = WM_APP + 10;
pub const WM_APP_KEYCAP: u32 = WM_APP + 11;
pub const WM_APP_TASKBAR: u32 = WM_APP + 12;
pub const WM_APP_MEDIA: u32 = WM_APP + 13;
pub const WM_APP_UPDATE: u32 = WM_APP + 14;
pub const WM_APP_AUTOSTART: u32 = WM_APP + 15;
pub const WM_APP_PRTSC: u32 = WM_APP + 16;
pub const WM_APP_SEARCH: u32 = WM_APP + 17;
pub const WM_APP_SCREENSHOT: u32 = WM_APP + 18;

#[inline]
pub fn min<T: PartialOrd>(a: T, b: T) -> T {
    if a < b { a } else { b }
}
#[inline]
pub fn max<T: PartialOrd>(a: T, b: T) -> T {
    if a > b { a } else { b }
}
#[inline]
pub fn clamp<T: PartialOrd>(v: T, lo: T, hi: T) -> T {
    if v < lo {
        lo
    } else if v > hi {
        hi
    } else {
        v
    }
}

// ---- the C runtime (ucrtbase)

#[allow(improper_ctypes)]
unsafe extern "C" {
    pub fn malloc(n: usize) -> *mut c_void;
    pub fn calloc(n: usize, s: usize) -> *mut c_void;
    pub fn realloc(p: *mut c_void, n: usize) -> *mut c_void;
    pub fn free(p: *mut c_void);
    pub fn qsort(base: *mut c_void, n: usize, size: usize, cmp: unsafe extern "C" fn(*const c_void, *const c_void) -> i32);
    pub fn wcslen(s: *const u16) -> usize;
    pub fn wcscmp(a: *const u16, b: *const u16) -> i32;
    pub fn wcsncmp(a: *const u16, b: *const u16, n: usize) -> i32;
    pub fn _wcsicmp(a: *const u16, b: *const u16) -> i32;
    pub fn _wcsnicmp(a: *const u16, b: *const u16, n: usize) -> i32;
    pub fn wcsstr(a: *const u16, b: *const u16) -> *mut u16;
    pub fn wcschr(s: *const u16, c: u16) -> *mut u16;
    pub fn wcsrchr(s: *const u16, c: u16) -> *mut u16;
    pub fn towlower(c: u16) -> u16;
    pub fn towupper(c: u16) -> u16;
    pub fn _wtoi(s: *const u16) -> i32;
    pub fn wcstol(s: *const u16, end: *mut *mut u16, base: i32) -> i32;
    pub fn strlen(s: *const u8) -> usize;
    pub fn strcmp(a: *const u8, b: *const u8) -> i32;
    pub fn strncmp(a: *const u8, b: *const u8, n: usize) -> i32;
    pub fn _stricmp(a: *const u8, b: *const u8) -> i32;
    pub fn _strnicmp(a: *const u8, b: *const u8, n: usize) -> i32;
    pub fn strchr(s: *const u8, c: i32) -> *mut u8;
    pub fn strstr(a: *const u8, b: *const u8) -> *mut u8;
    pub fn strtod(s: *const u8, end: *mut *mut u8) -> f64;
    pub fn strtol(s: *const u8, end: *mut *mut u8, base: i32) -> i32;
    pub fn strtoul(s: *const u8, end: *mut *mut u8, base: i32) -> u32;
    pub fn _strtoi64(s: *const u8, end: *mut *mut u8, base: i32) -> i64;
    pub fn _strtoui64(s: *const u8, end: *mut *mut u8, base: i32) -> u64;
    pub fn atoi(s: *const u8) -> i32;
}

#[inline]
pub unsafe fn memcpy<T>(dst: *mut T, src: *const T, bytes: usize) {
    core::ptr::copy_nonoverlapping(src as *const u8, dst as *mut u8, bytes);
}
#[inline]
pub unsafe fn memmove<T>(dst: *mut T, src: *const T, bytes: usize) {
    core::ptr::copy(src as *const u8, dst as *mut u8, bytes);
}
#[inline]
pub unsafe fn memset<T>(dst: *mut T, v: u8, bytes: usize) {
    core::ptr::write_bytes(dst as *mut u8, v, bytes);
}
#[inline]
pub unsafe fn zero<T>(p: *mut T) {
    core::ptr::write_bytes(p, 0, 1);
}
#[inline]
pub unsafe fn memcmp<T>(a: *const T, b: *const T, bytes: usize) -> i32 {
    let a = core::slice::from_raw_parts(a as *const u8, bytes);
    let b = core::slice::from_raw_parts(b as *const u8, bytes);
    match a.cmp(b) {
        core::cmp::Ordering::Less => -1,
        core::cmp::Ordering::Equal => 0,
        core::cmp::Ordering::Greater => 1,
    }
}

pub unsafe fn wmemchr(s: *const u16, c: u16, n: usize) -> *const u16 {
    for i in 0..n {
        if *s.add(i) == c {
            return s.add(i);
        }
    }
    null()
}

// ---- memory

#[repr(C)]
pub struct Arena {
    pub base: *mut u8,
    pub cap: usize,
    pub committed: usize,
    pub pos: usize,
}

pub unsafe fn arena_init(a: *mut Arena, reserve: usize) {
    (*a).base = VirtualAlloc(null(), reserve, MEM_RESERVE, PAGE_READWRITE) as *mut u8;
    (*a).cap = if (*a).base.is_null() { 0 } else { reserve };
    (*a).committed = 0;
    (*a).pos = 0;
}

pub unsafe fn arena_push(a: *mut Arena, size: usize) -> *mut c_void {
    let a = &mut *a;
    let p = (a.pos + 15) & !15usize;
    let end = p + size;
    if end > a.committed {
        let nc = (end + 0xFFFF) & !0xFFFFusize;
        if nc > a.cap || VirtualAlloc(a.base.add(a.committed) as *const c_void, nc - a.committed, MEM_COMMIT, PAGE_READWRITE).is_null() {
            FatalAppExitW(0, w!("MixLauncher: arena out of memory"));
        }
        a.committed = nc;
    }
    a.pos = end;
    a.base.add(p) as *mut c_void
}

pub unsafe fn arena_push_zero(a: *mut Arena, size: usize) -> *mut c_void {
    let p = arena_push(a, size);
    memset(p, 0, size);
    p
}

pub unsafe fn arena_array<T>(a: *mut Arena, n: i32) -> *mut T {
    arena_push_zero(a, size_of::<T>() * n as usize) as *mut T
}

pub unsafe fn arena_reset(a: *mut Arena) {
    (*a).pos = 0;
}

pub unsafe fn arena_release(a: *mut Arena) {
    if !(*a).base.is_null() {
        VirtualFree((*a).base as *mut c_void, 0, MEM_RELEASE);
    }
    zero(a);
}

// Grows a malloc'd array to room for `need` items, doubling from `first`. False when out of memory.
pub unsafe fn grow<T>(p: *mut *mut T, cap: *mut i32, need: i32, first: i32) -> bool {
    if need <= *cap {
        return true;
    }
    let mut nc = if *cap != 0 { *cap } else { first };
    while nc < need {
        nc *= 2;
    }
    let n = realloc(*p as *mut c_void, size_of::<T>() * nc as usize);
    if n.is_null() {
        return false;
    }
    *p = n as *mut T;
    *cap = nc;
    true
}

// ---- strings

pub unsafe fn wlen(s: *const WCHAR) -> i32 {
    if s.is_null() { 0 } else { wcslen(s) as i32 }
}

pub unsafe fn wdup(a: *mut Arena, s: *const WCHAR, len: i32) -> *mut WCHAR {
    if s.is_null() {
        return null_mut();
    }
    let len = if len < 0 { wlen(s) } else { len };
    let d = arena_push(a, (len as usize + 1) * 2) as *mut WCHAR;
    memcpy(d, s, len as usize * 2);
    *d.add(len as usize) = 0;
    d
}

pub unsafe fn wdup_heap(s: *const WCHAR) -> *mut WCHAR {
    if s.is_null() {
        return null_mut();
    }
    let n = wlen(s);
    let d = malloc((n as usize + 1) * 2) as *mut WCHAR;
    if !d.is_null() {
        memcpy(d, s, (n as usize + 1) * 2);
    }
    d
}

pub unsafe fn wcopy(dst: *mut WCHAR, cap: i32, src: *const WCHAR) {
    if cap <= 0 {
        return;
    }
    let n = min(wlen(src), cap - 1);
    if n != 0 {
        memcpy(dst, src, n as usize * 2);
    }
    *dst.add(n as usize) = 0;
}

// The processor runs x86-64-v2 code (SSE4.2, POPCNT): the -v2 build (pgo.bat v2).
pub fn cpu_v2() -> bool {
    use std::arch::is_x86_feature_detected as has;
    has!("sse3") && has!("ssse3") && has!("sse4.1") && has!("sse4.2") && has!("popcnt") && has!("cmpxchg16b")
}

// The processor (and Windows, for the AVX state) runs x86-64-v3 code: the -v3 build (pgo.bat v3).
pub fn cpu_v3() -> bool {
    use std::arch::is_x86_feature_detected as has;
    has!("avx") && has!("avx2") && has!("bmi1") && has!("bmi2") && has!("fma") && has!("f16c") && has!("lzcnt") && has!("movbe") && has!("xsave")
}

// "%.Nf" (N up to 6) without core's float formatting: the value's exact binary fraction rounded half to even, as
// printf does it. Leaves out about 14 KB of float printing code.
pub struct Ff(pub f64, pub u32);
impl fmt::Display for Ff {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let (v, prec) = (self.0, self.1.min(6));
        if !v.is_finite() {
            return f.write_str(if v.is_nan() { "nan" } else if v > 0.0 { "inf" } else { "-inf" });
        }
        let bits = v.to_bits();
        let neg = (bits >> 63) != 0;
        let e = ((bits >> 52) & 0x7FF) as i32;
        let frac = bits & ((1u64 << 52) - 1);
        // v = mant * 2^exp exactly
        let (mant, exp) = if e == 0 { (frac, -1074) } else { (frac | (1u64 << 52), e - 1075) };
        let scale = 10u128.pow(prec);
        // q = mant * 10^prec * 2^exp, rounded half to even; values past 2^64 do not occur here (counts, ms)
        let num = mant as u128 * scale;
        let q: u128 = if exp >= 0 {
            if exp > 30 { u128::MAX } else { num << exp }
        } else if -exp >= 127 {
            0
        } else {
            let sh = (-exp) as u32;
            let (q, r) = (num >> sh, num & ((1u128 << sh) - 1));
            let half = 1u128 << (sh - 1);
            if r > half || (r == half && (q & 1) == 1) { q + 1 } else { q }
        };
        // digits from the right into a small buffer, then padded as a string ("{:>8}" is "%8.0f")
        let mut buf = [0u8; 48];
        let mut i = buf.len();
        let mut x = q;
        let mut nd = 0;
        loop {
            i -= 1;
            buf[i] = b'0' + (x % 10) as u8;
            x /= 10;
            nd += 1;
            if nd == prec {
                i -= 1;
                buf[i] = b'.';
            }
            if x == 0 && nd > prec || i < 2 {
                break;
            }
        }
        if neg {
            i -= 1;
            buf[i] = b'-';
        }
        f.pad(unsafe { core::str::from_utf8_unchecked(&buf[i..]) })
    }
}

// A wide C string for format!: "%s" of a WCHAR *.
pub struct Ws(pub *const WCHAR);
impl fmt::Display for Ws {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.0.is_null() {
            return f.write_str("(null)");
        }
        unsafe {
            let n = wcslen(self.0);
            let s = core::slice::from_raw_parts(self.0, n);
            for c in char::decode_utf16(s.iter().copied()) {
                f.write_char_(c.unwrap_or('\u{FFFD}'))?;
            }
        }
        Ok(())
    }
}
// "%.*s": n characters of a wide string.
pub struct Wn(pub *const WCHAR, pub i32);
impl fmt::Display for Wn {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        unsafe {
            let s = core::slice::from_raw_parts(self.0, max(self.1, 0) as usize);
            for c in char::decode_utf16(s.iter().copied()) {
                f.write_char_(c.unwrap_or('\u{FFFD}'))?;
            }
        }
        Ok(())
    }
}
// "%s" of a char * (UTF-8).
pub struct Cs(pub *const u8);
impl fmt::Display for Cs {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if self.0.is_null() {
            return f.write_str("(null)");
        }
        unsafe {
            let s = core::slice::from_raw_parts(self.0, strlen(self.0));
            f.write_str(&String::from_utf8_lossy(s))
        }
    }
}
trait WriteChar {
    fn write_char_(&mut self, c: char) -> fmt::Result;
}
impl WriteChar for fmt::Formatter<'_> {
    fn write_char_(&mut self, c: char) -> fmt::Result {
        let mut b = [0u8; 4];
        self.write_str(c.encode_utf8(&mut b))
    }
}

struct WOut {
    buf: *mut WCHAR,
    cap: usize,
    len: usize,
    cut: bool,
}
impl fmt::Write for WOut {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        for u in s.encode_utf16() {
            if self.len + 1 < self.cap {
                unsafe { *self.buf.add(self.len) = u };
                self.len += 1;
            } else {
                self.cut = true;
            }
        }
        Ok(())
    }
}

// swprintf into a fixed buffer, always terminated. Returns the length, or -1 when the text was cut.
pub unsafe fn wfmt_args(buf: *mut WCHAR, cap: i32, a: fmt::Arguments) -> i32 {
    if cap <= 0 {
        return -1;
    }
    let mut o = WOut { buf, cap: cap as usize, len: 0, cut: false };
    let _ = fmt::write(&mut o, a);
    *buf.add(o.len) = 0;
    if o.cut { -1 } else { o.len as i32 }
}

#[macro_export]
macro_rules! wfmt {
    ($buf:expr, $cap:expr, $($a:tt)*) => {
        $crate::base::wfmt_args(($buf) as *mut u16, ($cap) as i32, format_args!($($a)*))
    };
}

// Joins wide strings into a fixed buffer, always terminated: the hot paths' "%s%s".
pub unsafe fn wjoin(buf: *mut WCHAR, cap: i32, parts: &[(*const WCHAR, i32)]) -> i32 {
    if cap <= 0 {
        return -1;
    }
    let mut n = 0i32;
    let mut cut = false;
    for &(p, l) in parts {
        let l = if l < 0 { wlen(p) } else { l };
        let take = min(l, cap - 1 - n);
        if take < l {
            cut = true;
        }
        if take > 0 {
            memcpy(buf.add(n as usize), p, take as usize * 2);
            n += take;
        }
    }
    *buf.add(n as usize) = 0;
    if cut { -1 } else { n }
}

pub fn wlower_ascii(c: WCHAR) -> WCHAR {
    if (b'A' as u16..=b'Z' as u16).contains(&c) { c + 32 } else { c }
}

pub unsafe fn wstarts_with_i(mut s: *const WCHAR, mut prefix: *const WCHAR) -> bool {
    while *prefix != 0 {
        if *s == 0 || towlower(*s) != towlower(*prefix) {
            return false;
        }
        s = s.add(1);
        prefix = prefix.add(1);
    }
    true
}

pub unsafe fn wends_with_i(s: *const WCHAR, n: i32, suffix: *const WCHAR) -> bool {
    let m = wlen(suffix);
    m <= n && _wcsnicmp(s.add((n - m) as usize), suffix, m as usize) == 0
}

// Replaces [a, b) of a text field (len chars, room for cap - 1) with n typed or pasted chars: line breaks and
// tabs become spaces, other control chars are dropped, what does not fit is cut. Returns the caret after it.
pub unsafe fn text_splice(s: *mut WCHAR, len: *mut i32, cap: i32, mut a: i32, mut b: i32, ins: *const WCHAR, n: i32) -> i32 {
    if a > b {
        core::mem::swap(&mut a, &mut b);
    }
    let room = max(cap - 1 - (*len - (b - a)), 0);
    let mut k = 0;
    let mut last = 0;
    while last < n && k < room {
        let c = *ins.add(last as usize);
        if if c >= 0x20 { c != 0x7F } else { c == b'\r' as u16 || c == b'\n' as u16 || c == b'\t' as u16 } {
            k += 1;
        }
        last += 1;
    }
    memmove(s.add((a + k) as usize), s.add(b as usize), (*len - b) as usize * 2);
    let mut j = a;
    for i in 0..last {
        let c = *ins.add(i as usize);
        if c == b'\r' as u16 || c == b'\n' as u16 || c == b'\t' as u16 {
            *s.add(j as usize) = b' ' as u16;
            j += 1;
        } else if c >= 0x20 && c != 0x7F {
            *s.add(j as usize) = c;
            j += 1;
        }
    }
    *len += k - (b - a);
    *s.add(*len as usize) = 0;
    a + k
}

pub unsafe fn utf8_to_w(s: *const u8, n: i32, out: *mut WCHAR, cap: i32) -> i32 {
    let mut r = MultiByteToWideChar(CP_UTF8, 0, s, n, out, cap - 1);
    if r < 0 {
        r = 0;
    }
    *out.add(r as usize) = 0;
    r
}

pub unsafe fn w_to_utf8(s: *const WCHAR, n: i32, out: *mut u8, cap: i32) -> i32 {
    let mut r = WideCharToMultiByte(CP_UTF8, 0, s, n, out, cap - 1, null(), null_mut());
    if r < 0 {
        r = 0;
    }
    *out.add(r as usize) = 0;
    r
}

// A Rust string as a zero-terminated wide one.
pub fn wstr(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(core::iter::once(0)).collect()
}

// A wide C string as a Rust one.
pub unsafe fn wto_string(s: *const WCHAR) -> String {
    if s.is_null() {
        return String::new();
    }
    String::from_utf16_lossy(core::slice::from_raw_parts(s, wcslen(s)))
}

// ---- hashing

pub const FNV_BASIS: u64 = 1469598103934665603;
pub const FNV_PRIME: u64 = 1099511628211;

pub unsafe fn hash_wstr(s: *const WCHAR, len: i32) -> u64 {
    let mut h = FNV_BASIS;
    let len = if len < 0 { wlen(s) } else { len };
    for i in 0..len {
        h = (h ^ *s.add(i as usize) as u64).wrapping_mul(FNV_PRIME);
    }
    if h != 0 { h } else { 1 }
}

pub unsafe fn hash_wstr_i(s: *const WCHAR, len: i32) -> u64 {
    let mut h = FNV_BASIS;
    let len = if len < 0 { wlen(s) } else { len };
    for i in 0..len {
        // as CharLowerW gives it (history.tsv keeps these hashes), without the call for ASCII and Cyrillic
        let mut c = *s.add(i as usize);
        if c < 0x80 {
            c = wlower_ascii(c);
        } else if (0x400..=0x45F).contains(&c) {
            c = if c >= 0x430 { c } else { c + if c >= 0x410 { 32 } else { 80 } };
        } else {
            c = CharLowerW(c as usize as *mut u16) as usize as u16;
        }
        h = (h ^ c as u64).wrapping_mul(FNV_PRIME);
    }
    if h != 0 { h } else { 1 }
}

pub fn hash_u64(mut x: u64) -> u64 {
    x ^= x >> 33;
    x = x.wrapping_mul(0xff51afd7ed558ccd);
    x ^= x >> 33;
    x = x.wrapping_mul(0xc4ceb9fe1a85ec53);
    x ^= x >> 33;
    x
}

// ---- time

pub static mut g_qpc_inv: f64 = 0.0;

pub unsafe fn time_init() {
    let mut f = 0i64;
    QueryPerformanceFrequency(&mut f);
    g_qpc_inv = 1.0 / f as f64;
}

pub unsafe fn time_now() -> f64 {
    let mut c = 0i64;
    QueryPerformanceCounter(&mut c);
    c as f64 * g_qpc_inv
}

pub unsafe fn filetime_now() -> i64 {
    let mut ft: FILETIME = zeroed();
    GetSystemTimeAsFileTime(&mut ft);
    ((ft.dwHighDateTime as i64) << 32) | ft.dwLowDateTime as i64
}

pub unsafe fn unix_now() -> i64 {
    filetime_now() / 10000000 - 11644473600
}

// ---- files and the log

pub unsafe fn data_path(out: *mut WCHAR, file: *const WCHAR) {
    wfmt!(out, MAX_PATH, "{}\\{}", Ws(g_data_dir.as_ptr()), Ws(file));
}

pub static mut g_log_lock: SRWLOCK = SRWLOCK { Ptr: null_mut() };

#[macro_export]
macro_rules! log_msg {
    ($($a:tt)*) => {
        $crate::base::log_write(&format!($($a)*))
    };
}

pub unsafe fn log_write(text: &str) {
    let mut buf: Vec<u8> = text.as_bytes().to_vec();
    buf.truncate(1024 - 3);
    buf.extend_from_slice(b"\r\n");

    let mut path = [0u16; MAX_PATH];
    data_path(path.as_mut_ptr(), w!("log.txt"));
    AcquireSRWLockExclusive(&raw mut g_log_lock);
    let mut f = CreateFileW(path.as_ptr(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, null(), OPEN_ALWAYS, 0, null_mut());
    let mut sz = 0i64;
    if f != INVALID_HANDLE_VALUE && GetFileSizeEx(f, &mut sz) != 0 && sz > 256 * 1024 {
        CloseHandle(f);
        f = CreateFileW(path.as_ptr(), GENERIC_WRITE, FILE_SHARE_READ, null(), CREATE_ALWAYS, 0, null_mut());
    }
    if f != INVALID_HANDLE_VALUE {
        let mut st: SYSTEMTIME = zeroed();
        GetLocalTime(&mut st);
        let ts = format!("{:04}-{:02}-{:02} {:02}:{:02}:{:02} ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        let mut w = 0u32;
        WriteFile(f, ts.as_ptr(), ts.len() as u32, &mut w, null_mut());
        WriteFile(f, buf.as_ptr(), buf.len() as u32, &mut w, null_mut());
        CloseHandle(f);
    }
    ReleaseSRWLockExclusive(&raw mut g_log_lock);
}

// The whole file, zero-terminated (two zeros, for UTF-16 text too).
pub unsafe fn read_file(path: *const WCHAR, out_size: *mut u32) -> *mut u8 {
    let f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, null(), OPEN_EXISTING, 0, null_mut());
    if f == INVALID_HANDLE_VALUE {
        return null_mut();
    }
    let mut sz = 0i64;
    if GetFileSizeEx(f, &mut sz) == 0 || sz > 64 * 1024 * 1024 {
        CloseHandle(f);
        return null_mut();
    }
    let n = sz as u32;
    let mut got = 0u32;
    let buf = malloc(n as usize + 2) as *mut u8;
    if !buf.is_null() && ReadFile(f, buf, n, &mut got, null_mut()) == 0 {
        got = 0;
    }
    CloseHandle(f);
    if buf.is_null() {
        return null_mut();
    }
    *buf.add(got as usize) = 0;
    *buf.add(got as usize + 1) = 0;
    if !out_size.is_null() {
        *out_size = got;
    }
    buf
}

pub unsafe fn write_file_atomic(path: *const WCHAR, data: *const c_void, size: u32) -> bool {
    let mut tmp = [0u16; MAX_PATH + 8];
    wfmt!(tmp.as_mut_ptr(), tmp.len(), "{}.tmp", Ws(path));
    let f = CreateFileW(tmp.as_ptr(), GENERIC_WRITE, 0, null(), CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, null_mut());
    if f == INVALID_HANDLE_VALUE {
        return false;
    }
    let mut w = 0u32;
    let ok = WriteFile(f, data as *const u8, size, &mut w, null_mut()) != 0 && w == size;
    CloseHandle(f);
    if !ok {
        DeleteFileW(tmp.as_ptr());
        return false;
    }
    MoveFileExW(tmp.as_ptr(), path, MOVEFILE_REPLACE_EXISTING) != 0
}

pub unsafe fn data_read(file: *const WCHAR) -> *mut u8 {
    let mut path = [0u16; MAX_PATH];
    data_path(path.as_mut_ptr(), file);
    read_file(path.as_ptr(), null_mut())
}

// Splits text into lines in place: each call returns the next one without its line break, NULL at the end.
pub unsafe fn next_line(cursor: *mut *mut u8) -> *mut u8 {
    let s = *cursor;
    if s.is_null() || *s == 0 {
        return null_mut();
    }
    let mut e = s;
    while *e != 0 && *e != b'\n' {
        e = e.add(1);
    }
    *cursor = if *e != 0 { e.add(1) } else { e };
    if e > s && *e.sub(1) == b'\r' {
        *e.sub(1) = 0;
    }
    *e = 0;
    s
}

pub unsafe fn utf8_bom(s: *const u8) -> bool {
    *s == 0xEF && *s.add(1) == 0xBB && *s.add(2) == 0xBF
}

// ---- a growing byte buffer, for files written in one go

#[derive(Default)]
pub struct Buf {
    pub data: Vec<u8>,
}

impl Buf {
    pub fn new() -> Buf {
        Buf { data: Vec::new() }
    }
    pub fn put(&mut self, p: &[u8]) {
        if self.data.capacity() == 0 {
            self.data.reserve(4096);
        }
        self.data.extend_from_slice(p);
    }
    pub fn str(&mut self, s: &str) {
        self.put(s.as_bytes());
    }
    // buf_printf: at most 511 bytes, as its 512-byte buffer
    pub fn printf(&mut self, a: fmt::Arguments) {
        let s = format!("{}", a);
        let b = s.as_bytes();
        self.put(&b[..min(b.len(), 511)]);
    }
    pub unsafe fn put_w(&mut self, s: *const WCHAR) {
        let mut tmp = [0u8; 4 * 1024];
        let mut n = w_to_utf8(s, -1, tmp.as_mut_ptr(), tmp.len() as i32);
        if n > 0 && tmp[n as usize - 1] == 0 {
            n -= 1; // the count includes the terminator
        }
        self.put(&tmp[..n as usize]);
    }
    pub fn len(&self) -> usize {
        self.data.len()
    }
}

#[macro_export]
macro_rules! buf_printf {
    ($b:expr, $($a:tt)*) => {
        ($b).printf(format_args!($($a)*))
    };
}

// Writes the buffer (atomically) and frees it.
pub unsafe fn buf_save(b: &mut Buf, path: *const WCHAR) -> bool {
    let ok = write_file_atomic(path, b.data.as_ptr() as *const c_void, b.data.len() as u32);
    b.data = Vec::new();
    ok
}

pub unsafe fn buf_save_data(b: &mut Buf, file: *const WCHAR) -> bool {
    let mut path = [0u16; MAX_PATH];
    data_path(path.as_mut_ptr(), file);
    let ok = buf_save(b, path.as_ptr());
    if !ok {
        log_msg!("could not save {}", Ws(file));
    }
    ok
}

// ---- threads and windows

pub type ThreadFn = unsafe extern "system" fn(*mut c_void) -> u32;

pub unsafe fn thread_start(f: ThreadFn, arg: *mut c_void) -> bool {
    let t = CreateThread(null(), 0, Some(f), arg, 0, null_mut());
    if !t.is_null() {
        CloseHandle(t);
    }
    !t.is_null()
}

pub unsafe fn pump_messages() {
    let mut msg: MSG = zeroed();
    while PeekMessageW(&mut msg, null_mut(), 0, 0, PM_REMOVE) != 0 {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

pub type WndProc = unsafe extern "system" fn(HWND, u32, WPARAM, LPARAM) -> LRESULT;

// Registering a class that exists already just fails, so callers need no guard.
pub unsafe fn window_class(name: *const WCHAR, proc_: WndProc, style: u32, icon: HICON, icon_sm: HICON) {
    let mut wc: WNDCLASSEXW = zeroed();
    wc.cbSize = size_of::<WNDCLASSEXW>() as u32;
    wc.style = style;
    wc.lpfnWndProc = Some(proc_);
    wc.hInstance = GetModuleHandleW(null());
    wc.hIcon = icon;
    wc.hIconSm = icon_sm;
    wc.hCursor = LoadCursorW(null_mut(), IDC_ARROW);
    wc.lpszClassName = name;
    RegisterClassExW(&wc);
}

pub unsafe fn message_window(cls: *const WCHAR, proc_: WndProc) -> HWND {
    window_class(cls, proc_, 0, null_mut(), null_mut());
    CreateWindowExW(0, cls, w!(""), 0, 0, 0, 0, 0, HWND_MESSAGE, null_mut(), GetModuleHandleW(null()), null())
}

pub unsafe fn window_is_ours(h: HWND) -> bool {
    let mut pid = 0u32;
    if !h.is_null() {
        GetWindowThreadProcessId(h, &mut pid);
    }
    pid == GetCurrentProcessId()
}

pub unsafe fn track_mouse_leave(h: HWND) {
    let mut tme = TRACKMOUSEEVENT { cbSize: size_of::<TRACKMOUSEEVENT>() as u32, dwFlags: TME_LEAVE, hwndTrack: h, dwHoverTime: 0 };
    TrackMouseEvent(&mut tme);
}

pub unsafe fn reg_dword(root: HKEY, key: *const WCHAR, name: *const WCHAR, out: *mut u32) -> bool {
    let mut sz = 4u32;
    RegGetValueW(root, key, name, RRF_RT_REG_DWORD, null_mut(), out as *mut c_void, &mut sz) == ERROR_SUCCESS
}

// A 32-bit top-down DIB.
pub unsafe fn dib32(w: i32, h: i32) -> BITMAPINFO {
    let mut bi: BITMAPINFO = zeroed();
    bi.bmiHeader.biSize = size_of::<BITMAPINFOHEADER>() as u32;
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB as u32;
    bi
}

// DWM attributes and values by name: older SDKs lack them.
pub const DWA_NO_TRANSITIONS: u32 = 3;
pub const DWA_DARK: u32 = 20;
pub const DWA_CORNER: u32 = 33;
pub const DWA_BORDER_COLOR: u32 = 34;
pub const DWA_BACKDROP: u32 = 38;
pub const DWC_SQUARE: u32 = 1;
pub const DWC_ROUND: u32 = 2;
pub const DWC_ROUND_SMALL: u32 = 3;
pub const DWB_NONE: u32 = 1;
pub const DWB_MICA: u32 = 2;
pub const DWB_ACRYLIC: u32 = 3;
pub const DW_COLOR_NONE: u32 = 0xFFFFFFFE;
pub const DW_COLOR_DEFAULT: u32 = 0xFFFFFFFF;

pub unsafe fn dwm_set(h: HWND, attr: u32, v: u32) -> bool {
    DwmSetWindowAttribute(h, attr as _, &v as *const u32 as *const c_void, 4) >= 0
}

// A system backdrop under the whole window (the frame extended over the client area).
pub unsafe fn dwm_backdrop(h: HWND, kind: u32) -> bool {
    let m = MARGINS { cxLeftWidth: -1, cxRightWidth: -1, cyTopHeight: -1, cyBottomHeight: -1 };
    DwmExtendFrameIntoClientArea(h, &m);
    dwm_set(h, DWA_BACKDROP, kind)
}

// ---- monitors

type PfnGetDpiForMonitor = unsafe extern "system" fn(HMONITOR, i32, *mut u32, *mut u32) -> HRESULT;
static mut MDPI_F: Option<PfnGetDpiForMonitor> = None;
static mut MDPI_TRIED: bool = false;

pub unsafe fn monitor_dpi(m: HMONITOR) -> u32 {
    if !MDPI_TRIED {
        MDPI_TRIED = true;
        let shcore = LoadLibraryW(w!("shcore.dll"));
        if !shcore.is_null() {
            MDPI_F = core::mem::transmute(GetProcAddress(shcore, s!("GetDpiForMonitor")));
        }
    }
    let mut x = 96u32;
    let mut y = 96u32;
    let ok = match MDPI_F {
        Some(f) => f(m, 0, &mut x, &mut y) >= 0,
        None => false,
    };
    if !ok {
        let dc = GetDC(null_mut());
        x = GetDeviceCaps(dc, LOGPIXELSX as i32) as u32;
        ReleaseDC(null_mut(), dc);
    }
    x
}

pub unsafe fn monitor_work(m: HMONITOR) -> RECT {
    let mut mi: MONITORINFO = zeroed();
    mi.cbSize = size_of::<MONITORINFO>() as u32;
    GetMonitorInfoW(m, &mut mi);
    mi.rcWork
}

unsafe extern "system" fn find_other_monitor(m: HMONITOR, _dc: HDC, _r: *mut RECT, lp: LPARAM) -> BOOL {
    let io = lp as *mut HMONITOR;
    if m == *io {
        return TRUE;
    }
    *io.add(1) = m;
    FALSE
}

// Where our windows open: the cursor's monitor, or another one for a test copy.
pub unsafe fn open_monitor() -> HMONITOR {
    let mut pt = POINT { x: 0, y: 0 };
    GetCursorPos(&mut pt);
    let mut io: [HMONITOR; 2] = [MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY), null_mut()];
    if g_pinned || g_other_monitor {
        EnumDisplayMonitors(null_mut(), null(), Some(find_other_monitor), io.as_mut_ptr() as LPARAM);
    }
    if !io[1].is_null() { io[1] } else { io[0] }
}

// ---- the clipboard

pub unsafe fn clipboard_set(s: *const WCHAR, n: i32) {
    let n = if n < 0 { wlen(s) } else { n };
    if OpenClipboard(g_hwnd) == 0 {
        return;
    }
    EmptyClipboard();
    let g = GlobalAlloc(GMEM_MOVEABLE, (n as usize + 1) * 2);
    let p = if g.is_null() { null_mut() } else { GlobalLock(g) as *mut WCHAR };
    if !p.is_null() {
        memcpy(p, s, n as usize * 2);
        *p.add(n as usize) = 0;
        GlobalUnlock(g);
        if SetClipboardData(13 /*CF_UNICODETEXT*/, g as HANDLE).is_null() {
            GlobalFree(g);
        }
    } else if !g.is_null() {
        GlobalFree(g);
    }
    CloseClipboard();
}

// The clipboard text, up to cap - 1 characters. Returns its length.
pub unsafe fn clipboard_get(out: *mut WCHAR, cap: i32) -> i32 {
    let mut n = 0i32;
    if cap <= 0 {
        return 0;
    }
    *out = 0;
    if OpenClipboard(g_hwnd) == 0 {
        return 0;
    }
    let h = GetClipboardData(13 /*CF_UNICODETEXT*/);
    let p = if h.is_null() { null() } else { GlobalLock(h as _) as *const WCHAR };
    if !p.is_null() {
        while *p.add(n as usize) != 0 && n < cap - 1 {
            n += 1;
        }
        memcpy(out, p, n as usize * 2);
        GlobalUnlock(h as _);
    }
    *out.add(n as usize) = 0;
    CloseClipboard();
    n
}

// LOWORD / HIWORD / GET_X_LPARAM and friends
#[inline]
pub fn loword(v: usize) -> u32 {
    (v & 0xFFFF) as u32
}
#[inline]
pub fn hiword(v: usize) -> u32 {
    ((v >> 16) & 0xFFFF) as u32
}
#[inline]
pub fn get_x_lparam(lp: LPARAM) -> i32 {
    (lp & 0xFFFF) as u16 as i16 as i32
}
#[inline]
pub fn get_y_lparam(lp: LPARAM) -> i32 {
    ((lp >> 16) & 0xFFFF) as u16 as i16 as i32
}
#[inline]
pub fn makelparam(lo: i32, hi: i32) -> LPARAM {
    ((lo as u32 & 0xFFFF) | ((hi as u32 & 0xFFFF) << 16)) as i32 as LPARAM
}
#[inline]
pub fn succeeded(hr: HRESULT) -> bool {
    hr >= 0
}
#[inline]
pub fn failed(hr: HRESULT) -> bool {
    hr < 0
}
#[inline]
pub fn primarylangid(l: u16) -> u16 {
    l & 0x3FF
}
pub const LANG_RUSSIAN: u16 = 0x19;
