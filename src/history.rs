// Launch history (history.tsv): how often and how lately each app, command, file or folder was opened
// (frecency), and which queries led to it (query memory).

use crate::*;

const FREC_HALF_LIFE_DAYS: f64 = 14.0;
const QMEM_HALF_LIFE_DAYS: f64 = 30.0;
pub const QMEM_MAX_QUERY: i32 = 24;
const QMEM_KEEP: i32 = 1500;

// What a history or tag entry is about; the letter is its mark in history.tsv and tags.tsv.
pub const IT_APP: u8 = b'a';
pub const IT_CMD: u8 = b'c';
pub const IT_FILE: u8 = b'f';
pub const IT_DIR: u8 = b'd';

#[repr(C)]
pub struct HistEntry {
    pub hash: u64,
    pub key: *mut WCHAR, // app id or full path
    pub score: f64,
    pub last: i64,
    pub count: u32,
    pub type_: u8,
    pub used: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct QueryMem {
    pub item_hash: u64,
    pub query: [WCHAR; QMEM_MAX_QUERY as usize + 1], // normalized
    pub weight: f64,
    pub last: i64,
}

const HIST_CAP: i32 = 8192;

#[repr(C)]
pub struct Hist {
    pub arena: Arena,
    pub entries: *mut HistEntry, // open addressing by hash
    pub count: i32,
    pub qmem: *mut QueryMem,
    pub qcount: i32,
    pub qcap: i32,
    pub dirty: bool,
}

pub static mut H: Hist = unsafe { zeroed() };

pub fn item_hash_of(type_: u8, key_hash: u64) -> u64 {
    hash_u64(key_hash ^ (type_ as u64).wrapping_mul(0x9E3779B97F4A7C15))
}
pub unsafe fn item_hash(type_: u8, key: *const WCHAR) -> u64 {
    item_hash_of(type_, hash_wstr_i(key, -1))
}

fn decay(score: f64, last: i64, now: i64, half_life_days: f64) -> f64 {
    let dt = (now - last) as f64 / 86400.0;
    if dt <= 0.0 { score } else { score * 0.5f64.powf(dt / half_life_days) }
}

unsafe fn hist_slot(type_: u8, key: *const WCHAR, h: u64, create: bool) -> *mut HistEntry {
    if H.entries.is_null() || key.is_null() {
        return null_mut();
    }
    for p in 0..HIST_CAP as u64 {
        let e = &mut *H.entries.add((h.wrapping_add(p) & (HIST_CAP as u64 - 1)) as usize);
        if e.used != 0 && e.hash == h && e.type_ == type_ && _wcsicmp(e.key, key) == 0 {
            return e;
        }
        if e.used != 0 {
            continue;
        }
        if !create || H.count >= HIST_CAP * 3 / 4 {
            return null_mut();
        }
        e.used = 1;
        e.hash = h;
        e.type_ = type_;
        e.key = wdup(&raw mut H.arena, key, -1);
        H.count += 1;
        return e;
    }
    null_mut()
}

// h is item_hash(type, key).
pub unsafe fn hist_frecency(type_: u8, key: *const WCHAR, h: u64, now: i64) -> f64 {
    let e = hist_slot(type_, key, h, false);
    if e.is_null() { 0.0 } else { decay((*e).score, (*e).last, now, FREC_HALF_LIFE_DAYS) }
}

pub fn frec_bonus(f: f64) -> f32 {
    (1800.0 * (1.0 - (-f / 6.0).exp())) as f32
}

// How strongly this query (or one it extends or that extends it) led to the item before.
pub unsafe fn qmem_bonus(h: u64, qnorm: *const WCHAR, qlen: i32, now: i64) -> f32 {
    if qlen == 0 || H.qcount == 0 {
        return 0.0;
    }
    let mut best = 0.0f64;
    let ql = min(qlen, QMEM_MAX_QUERY);
    for i in 0..H.qcount as usize {
        let q = &*H.qmem.add(i);
        if q.item_hash != h {
            continue;
        }
        let ml = wlen(q.query.as_ptr());
        let rel;
        if ml == ql && wmem_eq(q.query.as_ptr(), qnorm, ql) {
            rel = 1.0;
        } else if ml > ql && wmem_eq(q.query.as_ptr(), qnorm, ql) {
            rel = 0.8;
        } else if ml < ql && wmem_eq(q.query.as_ptr(), qnorm, ml) {
            rel = 0.55;
        } else {
            continue;
        }
        let w = decay(q.weight, q.last, now, QMEM_HALF_LIFE_DAYS);
        best = max(best, rel * (1.0 - (-w / 1.5).exp()));
    }
    (3500.0 * best) as f32
}

unsafe fn qmem_add(h: u64, query: *const WCHAR, len: i32) -> *mut QueryMem {
    if !grow(&raw mut H.qmem, &raw mut H.qcap, H.qcount + 1, 256) {
        return null_mut();
    }
    let q = H.qmem.add(H.qcount as usize);
    H.qcount += 1;
    zero(q);
    (*q).item_hash = h;
    let len = min(len, QMEM_MAX_QUERY);
    memcpy((*q).query.as_mut_ptr(), query, len as usize * 2);
    q
}

pub unsafe fn history_record(type_: u8, key: *const WCHAR, qnorm: *const WCHAR, qlen: i32) {
    let now = unix_now();
    let h = item_hash(type_, key);
    let e = hist_slot(type_, key, h, true);
    if !e.is_null() {
        (*e).score = decay((*e).score, (*e).last, now, FREC_HALF_LIFE_DAYS) + 1.0;
        (*e).last = now;
        (*e).count += 1;
    }
    if qlen > 0 {
        let ql = min(qlen, QMEM_MAX_QUERY);
        let mut q: *mut QueryMem = null_mut();
        let mut i = 0;
        while i < H.qcount && q.is_null() {
            let m = H.qmem.add(i as usize);
            if (*m).item_hash == h && wlen((*m).query.as_ptr()) == ql && wmem_eq((*m).query.as_ptr(), qnorm, ql) {
                q = m;
            }
            i += 1;
        }
        if q.is_null() {
            q = qmem_add(h, qnorm, ql);
        }
        if q.is_null() {
            return;
        }
        (*q).weight = decay((*q).weight, (*q).last, now, QMEM_HALF_LIFE_DAYS) + 1.0;
        (*q).last = now;
    }
    H.dirty = true;
}

// The file is gone: it stops ranking high.
pub unsafe fn history_forget(type_: u8, key: *const WCHAR) {
    let e = hist_slot(type_, key, item_hash(type_, key), false);
    if !e.is_null() {
        (*e).score = 0.0;
        H.dirty = true;
    }
}

pub unsafe fn history_clear() {
    if !H.entries.is_null() {
        memset(H.entries, 0, size_of::<HistEntry>() * HIST_CAP as usize);
    }
    H.count = 0;
    H.qcount = 0;
    arena_reset(&raw mut H.arena);
    H.dirty = true;
}

unsafe extern "C" fn cmp_qmem_desc(a: *const c_void, b: *const c_void) -> i32 {
    let x = &*(a as *const QueryMem);
    let y = &*(b as *const QueryMem);
    if x.last < y.last {
        1
    } else if x.last > y.last {
        -1
    } else {
        0
    }
}

// "%.4f" as printf gives it.
pub fn f4(v: f64) -> Ff {
    Ff(v, 4)
}

// Entries that have decayed to nothing are dropped; so are the oldest queries past QMEM_KEEP.
pub unsafe fn history_save() {
    if !H.dirty {
        return;
    }
    H.dirty = false;
    let now = unix_now();
    let mut b = Buf::new();
    b.str("# MixLauncher history v1\n");
    for i in 0..HIST_CAP as usize {
        let e = &*H.entries.add(i);
        if e.used == 0 || decay(e.score, e.last, now, FREC_HALF_LIFE_DAYS) < 0.02 {
            continue;
        }
        buf_printf!(b, "H\t{}\t{}\t{}\t{}\t", e.type_ as char, f4(e.score), e.last, e.count);
        b.put_w(e.key);
        b.put(b"\n");
    }
    if H.qcount > QMEM_KEEP {
        qsort(H.qmem as *mut c_void, H.qcount as usize, size_of::<QueryMem>(), cmp_qmem_desc);
        H.qcount = QMEM_KEEP;
    }
    for i in 0..H.qcount as usize {
        let q = &*H.qmem.add(i);
        if decay(q.weight, q.last, now, QMEM_HALF_LIFE_DAYS) < 0.02 {
            continue;
        }
        buf_printf!(b, "Q\t{:016x}\t{}\t{}\t", q.item_hash, f4(q.weight), q.last);
        b.put_w(q.query.as_ptr());
        b.put(b"\n");
    }
    buf_save_data(&mut b, w!("history.tsv"));
}

// H <type> <score> <last> <count> <key>  /  Q <item hash> <weight> <last> <query>
pub unsafe fn history_load() {
    arena_init(&raw mut H.arena, 64 << 20);
    H.entries = calloc(HIST_CAP as usize, size_of::<HistEntry>()) as *mut HistEntry;
    let text = data_read(w!("history.tsv"));
    if text.is_null() {
        return;
    }
    let mut cur = text;
    let mut wbuf = [0u16; 2048];
    loop {
        let line = next_line(&mut cur);
        if line.is_null() {
            break;
        }
        if *line == b'H' && *line.add(1) == b'\t' {
            let type_ = *line.add(2);
            let mut p = line.add(4);
            let score = strtod(p, &mut p);
            if *p != b'\t' {
                continue;
            }
            let last = _strtoi64(p.add(1), &mut p, 10);
            if *p != b'\t' {
                continue;
            }
            let count = strtoul(p.add(1), &mut p, 10);
            if *p != b'\t' {
                continue;
            }
            utf8_to_w(p.add(1), -1, wbuf.as_mut_ptr(), wbuf.len() as i32);
            let e = if wbuf[0] != 0 { hist_slot(type_, wbuf.as_ptr(), item_hash(type_, wbuf.as_ptr()), true) } else { null_mut() };
            if !e.is_null() {
                (*e).score = score;
                (*e).last = last;
                (*e).count = count;
            }
        } else if *line == b'Q' && *line.add(1) == b'\t' {
            let mut p = line.add(2);
            let h = _strtoui64(p, &mut p, 16);
            if *p != b'\t' {
                continue;
            }
            let w = strtod(p.add(1), &mut p);
            if *p != b'\t' {
                continue;
            }
            let last = _strtoi64(p.add(1), &mut p, 10);
            if *p != b'\t' {
                continue;
            }
            utf8_to_w(p.add(1), -1, wbuf.as_mut_ptr(), wbuf.len() as i32);
            let q = if wbuf[0] != 0 { qmem_add(h, wbuf.as_ptr(), wlen(wbuf.as_ptr())) } else { null_mut() };
            if !q.is_null() {
                (*q).weight = w;
                (*q).last = last;
            }
        }
    }
    free(text as *mut c_void);
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct RecentFile {
    pub path: *const WCHAR,
    pub folder: bool,
    pub score: f64,
}

unsafe extern "C" fn cmp_recent(a: *const c_void, b: *const c_void) -> i32 {
    let x = &*(a as *const RecentFile);
    let y = &*(b as *const RecentFile);
    if x.score < y.score {
        1
    } else if x.score > y.score {
        -1
    } else {
        0
    }
}

// The files and folders opened most (and most lately), for the empty launcher.
pub unsafe fn history_top_files(out: *mut RecentFile, max_: i32) -> i32 {
    let now = unix_now();
    let mut tmp: [RecentFile; 256] = zeroed();
    let mut n = 0usize;
    let mut i = 0;
    while i < HIST_CAP as usize && n < 256 {
        let e = &*H.entries.add(i);
        i += 1;
        if e.used == 0 || (e.type_ != IT_FILE && e.type_ != IT_DIR) {
            continue;
        }
        let v = decay(e.score, e.last, now, FREC_HALF_LIFE_DAYS);
        if v < 0.25 {
            continue;
        }
        tmp[n].path = e.key;
        tmp[n].folder = e.type_ == IT_DIR;
        tmp[n].score = v;
        n += 1;
    }
    qsort(tmp.as_mut_ptr() as *mut c_void, n, size_of::<RecentFile>(), cmp_recent);
    let n = min(n as i32, max_);
    memcpy(out, tmp.as_ptr(), size_of::<RecentFile>() * n as usize);
    n
}
