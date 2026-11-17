// Fuzzy matching of a query against names (apps, files, tags). Both sides are normalized with norm_str, and
// ws marks where words start in the original (spaces, punctuation, camelCase, digits).

use crate::*;

pub const SCORE_EXACT: i32 = 10000;
pub const SCORE_PREFIX: i32 = 9000;
pub const SCORE_WORD_PREFIX: i32 = 8000;
pub const SCORE_ACRONYM: i32 = 7000;
pub const SCORE_STEM: i32 = 6400;
pub const SCORE_SUBSTRING: i32 = 6000;
pub const SCORE_FUZZY_MAX: i32 = 5000;

pub unsafe fn norm_char(c: WCHAR) -> WCHAR {
    if c < 0x80 {
        return wlower_ascii(c);
    }
    if (0x410..=0x42F).contains(&c) {
        return c + 32;
    }
    if c == 0x401 || c == 0x451 {
        return 0x435;
    }
    if (0x430..=0x44F).contains(&c) {
        return c;
    }
    if (0x400..=0x40F).contains(&c) {
        return c + 80;
    }
    if c == 0x00A0 {
        return ' ' as u16;
    }
    CharLowerW(c as usize as *mut u16) as usize as u16
}

pub unsafe fn norm_str(dst: *mut WCHAR, src: *const WCHAR, len: i32) {
    for i in 0..len as usize {
        *dst.add(i) = norm_char(*src.add(i));
    }
    *dst.add(len as usize) = 0;
}

pub fn is_sep(c: WCHAR) -> bool {
    matches!(c, 0x20 | 0x2D | 0x5F | 0x2E | 0x28 | 0x29 | 0x5B | 0x5D | 0x2F | 0x5C | 0x26 | 0x2B | 0x2C | 0x3A | 0x27 | 0x22 | 0x2013 | 0x2014)
}

fn is_upper(c: WCHAR) -> bool {
    (b'A' as u16..=b'Z' as u16).contains(&c) || (0x410..=0x42F).contains(&c) || c == 0x401
}
fn is_lower(c: WCHAR) -> bool {
    (b'a' as u16..=b'z' as u16).contains(&c) || (0x430..=0x44F).contains(&c) || c == 0x451
}
fn is_digit(c: WCHAR) -> bool {
    (b'0' as u16..=b'9' as u16).contains(&c)
}

pub unsafe fn word_starts(s: *const WCHAR, len: i32, ws: *mut u8) {
    for i in 0..len as usize {
        let c = *s.add(i);
        if i == 0 {
            *ws = !is_sep(c) as u8;
            continue;
        }
        let p = *s.add(i - 1);
        *ws.add(i) = (!is_sep(c) && (is_sep(p) || (is_lower(p) && is_upper(c)) || (!is_digit(p) && is_digit(c)) || (is_digit(p) && !is_digit(c)))) as u8;
    }
}

#[inline]
pub unsafe fn wmem_eq(a: *const WCHAR, b: *const WCHAR, n: i32) -> bool {
    core::slice::from_raw_parts(a, n as usize) == core::slice::from_raw_parts(b, n as usize)
}

unsafe fn find_sub(s: *const WCHAR, n: i32, q: *const WCHAR, m: i32, from: i32) -> i32 {
    let mut i = from;
    while i + m <= n {
        if *s.add(i as usize) == *q && wmem_eq(s.add(i as usize), q, m) {
            return i;
        }
        i += 1;
    }
    -1
}

// Where the query starts a word other than the first, -1 if nowhere.
unsafe fn word_prefix_at(s: *const WCHAR, n: i32, ws: *const u8, q: *const WCHAR, m: i32) -> i32 {
    let mut i = find_sub(s, n, q, m, 1);
    while i >= 0 {
        if *ws.add(i as usize) != 0 {
            return i;
        }
        i = find_sub(s, n, q, m, i + 1);
    }
    -1
}

// The query's letters start words of s in order ("vsc" -> Visual Studio Code); marks them in mask if given.
unsafe fn acronym_match(s: *const WCHAR, n: i32, ws: *const u8, q: *const WCHAR, m: i32, mask: *mut u8) -> bool {
    let mut j = 0;
    let mut i = 0;
    while i < n && j < m {
        if *ws.add(i as usize) != 0 && *s.add(i as usize) == *q.add(j as usize) {
            j += 1;
        }
        i += 1;
    }
    if j < m {
        return false;
    }
    if !mask.is_null() {
        let mut k = 0;
        let mut i = 0;
        while i < n && k < m {
            if *ws.add(i as usize) != 0 && *s.add(i as usize) == *q.add(k as usize) {
                *mask.add(i as usize) = 1;
                k += 1;
            }
            i += 1;
        }
    }
    true
}

unsafe fn trim_spaces(q: &mut *const WCHAR, m: &mut i32) {
    while *m > 0 && *(*q).add(*m as usize - 1) == ' ' as u16 {
        *m -= 1;
    }
    while *m > 0 && **q == ' ' as u16 {
        *q = (*q).add(1);
        *m -= 1;
    }
}

// The next space-separated token of q from *i: its start and length, 0 at the end.
unsafe fn next_token(q: *const WCHAR, m: i32, i: &mut i32, start: &mut i32) -> i32 {
    while *i < m && *q.add(*i as usize) == ' ' as u16 {
        *i += 1;
    }
    *start = *i;
    while *i < m && *q.add(*i as usize) != ' ' as u16 {
        *i += 1;
    }
    *i - *start
}

unsafe fn score_fuzzy(s: *const WCHAR, n: i32, ws: *const u8, q: *const WCHAR, m: i32) -> i32 {
    let mut start = -1;
    for i in 0..n {
        if *ws.add(i as usize) != 0 && *s.add(i as usize) == *q {
            start = i;
            break;
        }
    }
    if start < 0 {
        return 0;
    }
    let (mut j, mut prev, mut gaps, mut bonus) = (0, -2, 0, 0);
    let mut i = start;
    while i < n && j < m {
        if *s.add(i as usize) != *q.add(j as usize) {
            i += 1;
            continue;
        }
        if *ws.add(i as usize) == 0 && prev != i - 1 {
            let mut k = i + 1;
            while k < n && k < i + 12 && !(*ws.add(k as usize) != 0 && *s.add(k as usize) == *q.add(j as usize)) {
                k += 1;
            }
            if k < n && k < i + 12 {
                i = k;
            }
        }
        if *ws.add(i as usize) != 0 {
            bonus += 30;
        }
        if prev == i - 1 {
            bonus += 20;
        }
        if prev >= 0 {
            gaps += i - prev - 1;
        }
        prev = i;
        j += 1;
        i += 1;
    }
    if j < m {
        return 0;
    }
    if m >= 3 && gaps > 3 * m + 6 {
        return 0;
    }
    let sc = 1000 + bonus * 40 / m - gaps * 25 - start * 10 - min(n, 200);
    clamp(sc, 1000, SCORE_FUZZY_MAX)
}

// Word forms: the token minus an ending of up to 3 letters (4+ left) must start a word ("удалить" -> "удаление").
unsafe fn stem_at(s: *const WCHAR, n: i32, ws: *const u8, q: *const WCHAR, m: i32) -> i32 {
    if m < 5 {
        return -1;
    }
    let k = max(4, m - 3);
    let mut i = find_sub(s, n, q, k, 0);
    while i >= 0 {
        if *ws.add(i as usize) != 0 {
            return i;
        }
        i = find_sub(s, n, q, k, i + 1);
    }
    -1
}

unsafe fn score_stem(s: *const WCHAR, n: i32, ws: *const u8, q: *const WCHAR, m: i32) -> i32 {
    let i = stem_at(s, n, ws, q, m);
    if i < 0 { 0 } else { SCORE_STEM - min(i, 100) * 2 - min((n - m).abs(), 200) / 2 }
}

// Every way score_token can match needs the token's first letters (up to 4: the shortest stem) in s in order,
// so one pass rules out most names before the five scans below.
#[inline]
unsafe fn may_match(s: *const WCHAR, n: i32, q: *const WCHAR, m: i32) -> bool {
    let k = min(m, 4) as usize;
    let q = core::slice::from_raw_parts(q, k);
    let mut j = 0;
    for &c in core::slice::from_raw_parts(s, n as usize) {
        if c == q[j] {
            j += 1;
            if j == k {
                return true;
            }
        }
    }
    false
}

pub unsafe fn score_token(s: *const WCHAR, n: i32, ws: *const u8, q: *const WCHAR, m: i32) -> i32 {
    if m <= 0 {
        return 1;
    }
    if !may_match(s, n, q, m) {
        return 0;
    }
    if m > n {
        return score_stem(s, n, ws, q, m);
    }
    let len_pen = min(n - m, 200);
    if m == n && wmem_eq(s, q, n) {
        return SCORE_EXACT;
    }
    if wmem_eq(s, q, m) {
        return SCORE_PREFIX - len_pen;
    }
    let wp = word_prefix_at(s, n, ws, q, m);
    if wp >= 0 {
        return SCORE_WORD_PREFIX - min(wp, 100) * 4 - len_pen;
    }
    if m >= 2 && acronym_match(s, n, ws, q, m, null_mut()) {
        return SCORE_ACRONYM - min(n, 200);
    }
    let st = score_stem(s, n, ws, q, m);
    if st != 0 {
        return st;
    }
    let at = find_sub(s, n, q, m, 0);
    if at >= 0 {
        return SCORE_SUBSTRING - min(at, 200) * 2 - len_pen;
    }
    score_fuzzy(s, n, ws, q, m)
}

// The query as a whole, or every word of it somewhere in s (scored by the weakest, a little below a whole match).
pub unsafe fn score_query(s: *const WCHAR, n: i32, ws: *const u8, q: *const WCHAR, m: i32) -> i32 {
    let mut q = q;
    let mut m = m;
    trim_spaces(&mut q, &mut m);
    if m <= 0 {
        return 1;
    }
    let whole = score_token(s, n, ws, q, m);
    if wmemchr(q, ' ' as u16, m as usize).is_null() {
        return whole;
    }
    let mut worst = i32::MAX;
    let (mut i, mut b) = (0, 0);
    loop {
        let len = next_token(q, m, &mut i, &mut b);
        if len <= 0 {
            break;
        }
        worst = min(worst, score_token(s, n, ws, q.add(b as usize), len));
        if worst == 0 {
            break;
        }
    }
    max(whole, if worst > 0 && worst < i32::MAX { worst - 300 } else { 0 })
}

// The two layouts key for key, as UTF-16 at compile time
const fn utf16<const N: usize>(s: &str) -> [u16; N] {
    let b = s.as_bytes();
    let mut out = [0u16; N];
    let (mut i, mut k) = (0, 0);
    while i < b.len() {
        // ASCII or a 2-byte sequence (Cyrillic) is all these tables hold
        if b[i] < 0x80 {
            out[k] = b[i] as u16;
            i += 1;
        } else {
            out[k] = (((b[i] & 0x1F) as u16) << 6) | (b[i + 1] & 0x3F) as u16;
            i += 2;
        }
        k += 1;
    }
    assert!(k == N);
    out
}
const LAYOUT_KEYS: usize = 34;
static k_layout_en: [u16; LAYOUT_KEYS] = utf16("`qwertyuiop[]asdfghjkl;'zxcvbnm,./");
static k_layout_ru: [u16; LAYOUT_KEYS] = utf16("ёйцукенгшщзхъфывапролджэячсмитьбю.");

// The query as typed in the other layout (ru <-> en, "ghbdtn" -> "привет"). False when it mixes both or has neither.
pub unsafe fn layout_convert(q: *const WCHAR, m: i32, out: *mut WCHAR) -> bool {
    let mut has_ru = false;
    let mut has_en = false;
    for i in 0..m as usize {
        let c = *q.add(i);
        if (0x430..=0x451).contains(&c) {
            has_ru = true;
        } else if (b'a' as u16..=b'z' as u16).contains(&c) {
            has_en = true;
        }
    }
    if has_ru == has_en {
        *out = 0;
        return false;
    }
    let (from, to) = if has_ru { (&k_layout_ru, &k_layout_en) } else { (&k_layout_en, &k_layout_ru) };
    for i in 0..m as usize {
        let c = *q.add(i);
        let r = from.iter().position(|&f| f == c).map_or(c, |k| to[k]);
        *out.add(i) = norm_char(r); // normalized text has е for ё
    }
    *out.add(m as usize) = 0;
    true
}

// Marks the characters of s that a query token matched, in the same order of tries as score_token.
unsafe fn match_token_mask(s: *const WCHAR, n: i32, ws: *const u8, q: *const WCHAR, m: i32, mask: *mut u8) {
    if m <= 0 {
        return;
    }
    if m > n {
        let st = stem_at(s, n, ws, q, m);
        if st >= 0 {
            memset(mask.add(st as usize), 1, max(4, m - 3) as usize);
        }
        return;
    }
    let mut at = if wmem_eq(s, q, m) { 0 } else { word_prefix_at(s, n, ws, q, m) };
    if at < 0 && m >= 2 && acronym_match(s, n, ws, q, m, mask) {
        return;
    }
    let st = if at < 0 { stem_at(s, n, ws, q, m) } else { -1 };
    if st >= 0 {
        memset(mask.add(st as usize), 1, max(4, m - 3) as usize);
        return;
    }
    if at < 0 {
        at = find_sub(s, n, q, m, 0);
    }
    if at >= 0 {
        memset(mask.add(at as usize), 1, m as usize);
        return;
    }
    let mut tmp = [0u8; 512];
    let mut j = 0;
    let mut i = 0;
    while i < n && i < 512 && j < m {
        if *s.add(i as usize) == *q.add(j as usize) && (j != 0 || *ws.add(i as usize) != 0) {
            tmp[i as usize] = 1;
            j += 1;
        }
        i += 1;
    }
    if j == m {
        let mut i = 0;
        while i < n && i < 512 {
            *mask.add(i as usize) |= tmp[i as usize];
            i += 1;
        }
    }
}

// Marks the matched characters for highlighting; returns how many.
pub unsafe fn match_mask(s: *const WCHAR, n: i32, ws: *const u8, q: *const WCHAR, m: i32, mask: *mut u8) -> i32 {
    let mut q = q;
    let mut m = m;
    trim_spaces(&mut q, &mut m);
    memset(mask, 0, n as usize);
    if m <= 0 {
        return 0;
    }
    if score_token(s, n, ws, q, m) > 0 {
        match_token_mask(s, n, ws, q, m, mask);
    } else {
        let (mut i, mut b) = (0, 0);
        loop {
            let len = next_token(q, m, &mut i, &mut b);
            if len <= 0 {
                break;
            }
            match_token_mask(s, n, ws, q.add(b as usize), len, mask);
        }
    }
    let mut k = 0;
    for i in 0..n as usize {
        k += *mask.add(i) as i32;
    }
    k
}
