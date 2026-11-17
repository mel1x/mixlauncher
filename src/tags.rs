// Tags (tags.tsv): the user's own search words for an app, command, file or folder, matched like names.

use crate::*;

pub const TAG_INPUT_MAX: i32 = 120;

#[repr(C)]
pub struct Tag {
    pub text: *mut WCHAR,
    pub norm: *mut WCHAR,
    pub ws: *mut u8,
    pub len: i32,
}

#[repr(C)]
pub struct TagItem {
    pub type_: u8,        // IT_*
    pub key: *mut WCHAR,  // app id or full path
    pub hash: u64,        // item_hash(type, key), shared with the history
    pub tags: *mut Tag,
    pub ntags: i32,
    pub cap: i32,
    pub file: FileItem, // files and folders: how the item shows as a search result
}

#[repr(C)]
pub struct Tags {
    pub items: *mut *mut TagItem,
    pub n: i32,
    pub cap: i32,
}

pub static mut TG: Tags = unsafe { zeroed() };

pub unsafe fn app_item_type(a: *const App) -> u8 {
    if (*a).kind == APP_CMD { IT_CMD } else { IT_APP }
}
pub unsafe fn file_item_type(f: *const FileItem) -> u8 {
    if ((*f).flags & FI_FOLDER) != 0 { IT_DIR } else { IT_FILE }
}

// h is item_hash(type, key), 0 to have it computed.
pub unsafe fn tags_find(type_: u8, key: *const WCHAR, h: u64) -> *mut TagItem {
    if TG.n == 0 || key.is_null() {
        return null_mut();
    }
    let h = if h == 0 { item_hash(type_, key) } else { h };
    for i in 0..TG.n as usize {
        let t = *TG.items.add(i);
        if (*t).hash == h && (*t).type_ == type_ && _wcsicmp((*t).key, key) == 0 {
            return t;
        }
    }
    null_mut()
}

unsafe fn tag_item_free(ti: *mut TagItem) {
    for i in 0..(*ti).ntags as usize {
        let t = &*(*ti).tags.add(i);
        free(t.text as *mut c_void);
        free(t.norm as *mut c_void);
        free(t.ws as *mut c_void);
    }
    free((*ti).tags as *mut c_void);
    free((*ti).file.dir as *mut c_void);
    free((*ti).key as *mut c_void);
    free(ti as *mut c_void);
}

unsafe fn tags_item(type_: u8, key: *const WCHAR) -> *mut TagItem {
    let ti = tags_find(type_, key, 0);
    if !ti.is_null() {
        return ti;
    }
    if !grow(&raw mut TG.items, &raw mut TG.cap, TG.n + 1, 32) {
        return null_mut();
    }
    let ti = calloc(1, size_of::<TagItem>()) as *mut TagItem;
    if ti.is_null() {
        return null_mut();
    }
    (*ti).key = wdup_heap(key);
    if (*ti).key.is_null() {
        free(ti as *mut c_void);
        return null_mut();
    }
    (*ti).type_ = type_;
    (*ti).hash = item_hash(type_, key);
    if type_ == IT_FILE || type_ == IT_DIR {
        let f = &mut (*ti).file;
        let mut name: *const WCHAR = null();
        let dl = path_split((*ti).key, &mut name);
        f.full = (*ti).key;
        f.name = name as *mut WCHAR;
        f.dir = malloc((dl as usize + 1) * 2) as *mut WCHAR;
        if !f.dir.is_null() {
            wcopy(f.dir, dl + 1, f.full);
        }
        f.flags = if type_ == IT_DIR { FI_FOLDER } else { 0 };
        f.icon_key = icon_key_for_file(f.full, type_ == IT_DIR);
    }
    *TG.items.add(TG.n as usize) = ti;
    TG.n += 1;
    ti
}

unsafe fn tag_push(ti: *mut TagItem, mut s: *const WCHAR, mut len: i32) -> bool {
    while len > 0 && (*s == ' ' as u16 || *s == '\t' as u16) {
        s = s.add(1);
        len -= 1;
    }
    while len > 0 && (*s.add(len as usize - 1) == ' ' as u16 || *s.add(len as usize - 1) == '\t' as u16) {
        len -= 1;
    }
    if len <= 0 {
        return false;
    }
    let len = min(len, TAG_INPUT_MAX);
    let mut norm = [0u16; TAG_INPUT_MAX as usize + 1];
    norm_str(norm.as_mut_ptr(), s, len);
    let t_ = &mut *ti;
    for i in 0..t_.ntags as usize {
        let t = &*t_.tags.add(i);
        if t.len == len && wmem_eq(t.norm, norm.as_ptr(), len) {
            return false;
        }
    }
    if !grow(&mut t_.tags, &mut t_.cap, t_.ntags + 1, 4) {
        return false;
    }
    let t = &mut *t_.tags.add(t_.ntags as usize);
    t.text = malloc((len as usize + 1) * 2) as *mut WCHAR;
    t.norm = wdup_heap(norm.as_ptr());
    t.ws = malloc(len as usize + 1) as *mut u8;
    if t.text.is_null() || t.norm.is_null() || t.ws.is_null() {
        free(t.text as *mut c_void);
        free(t.norm as *mut c_void);
        free(t.ws as *mut c_void);
        return false;
    }
    wcopy(t.text, len + 1, s);
    word_starts(t.text, len, t.ws);
    t.len = len;
    t_.ntags += 1;
    true
}

// <type> <tag> <key>, one line per tag
unsafe fn tags_save() {
    let mut b = Buf::new();
    b.str("# MixLauncher tags v1: type, tag, app id or path\n");
    for i in 0..TG.n as usize {
        let ti = &**TG.items.add(i);
        for k in 0..ti.ntags as usize {
            buf_printf!(b, "{}\t", ti.type_ as char);
            b.put_w((*ti.tags.add(k)).text);
            b.put(b"\t");
            b.put_w(ti.key);
            b.put(b"\n");
        }
    }
    buf_save_data(&mut b, w!("tags.tsv"));
}

pub unsafe fn tags_load() {
    let text = data_read(w!("tags.tsv"));
    if text.is_null() {
        return;
    }
    let mut cur = text;
    let mut tag = [0u16; TAG_INPUT_MAX as usize + 1];
    let mut key = vec![0u16; 2048];
    loop {
        let line = next_line(&mut cur);
        if line.is_null() {
            break;
        }
        let type_ = *line;
        if (type_ != IT_APP && type_ != IT_CMD && type_ != IT_FILE && type_ != IT_DIR) || *line.add(1) != b'\t' {
            continue;
        }
        let t2 = strchr(line.add(2), b'\t' as i32);
        if t2.is_null() {
            continue;
        }
        *t2 = 0;
        utf8_to_w(line.add(2), -1, tag.as_mut_ptr(), tag.len() as i32);
        utf8_to_w(t2.add(1), -1, key.as_mut_ptr(), 2048);
        if tag[0] == 0 || key[0] == 0 {
            continue;
        }
        let ti = tags_item(type_, key.as_ptr());
        if !ti.is_null() {
            tag_push(ti, tag.as_ptr(), wlen(tag.as_ptr()));
        }
    }
    free(text as *mut c_void);
}

// "a, b,c" -> three tags. Returns whether anything was added.
pub unsafe fn tags_add(type_: u8, key: *const WCHAR, input: *const WCHAR) -> bool {
    let ti = tags_item(type_, key);
    if ti.is_null() {
        return false;
    }
    let mut added = false;
    let mut s = input;
    while *s != 0 {
        let mut e = s;
        while *e != 0 && *e != ',' as u16 && *e != ';' as u16 {
            e = e.add(1);
        }
        added |= tag_push(ti, s, e.offset_from(s) as i32);
        s = if *e != 0 { e.add(1) } else { e };
    }
    if (*ti).ntags == 0 {
        // a new item that got nothing
        TG.n -= 1;
        *TG.items.add(TG.n as usize) = null_mut();
        tag_item_free(ti);
    }
    if added {
        tags_save();
    }
    added
}

pub unsafe fn tags_clear(type_: u8, key: *const WCHAR) {
    let ti = tags_find(type_, key, 0);
    if ti.is_null() {
        return;
    }
    for i in 0..TG.n as usize {
        if *TG.items.add(i) == ti {
            TG.n -= 1;
            *TG.items.add(i) = *TG.items.add(TG.n as usize);
            break;
        }
    }
    tag_item_free(ti);
    tags_save();
}

// Best tag match, 0 if none: prefix, word, acronym or substring only (fuzzy hits on short tags match everything).
pub unsafe fn tags_score(ti: *const TagItem, qn: *const WCHAR, ql: i32, alt: *const WCHAR, has_alt: bool, is_alt: &mut bool) -> i32 {
    let mut best = 0;
    let mn = SCORE_SUBSTRING - 400;
    *is_alt = false;
    if ti.is_null() {
        return 0;
    }
    for i in 0..(*ti).ntags as usize {
        let t = &*(*ti).tags.add(i);
        let s = score_query(t.norm, t.len, t.ws, qn, ql);
        if s >= mn && s > best {
            best = s;
            *is_alt = false;
        }
        if has_alt {
            let sa = score_query(t.norm, t.len, t.ws, alt, ql);
            if sa >= mn && sa * 9 / 10 > best {
                best = sa * 9 / 10;
                *is_alt = true;
            }
        }
    }
    best
}

// "1 тег", "3 тега", "5 тегов"
pub unsafe fn tags_count_label(out: *mut WCHAR, cap: i32, n: i32) {
    let w = if g_lang_ru {
        let m10 = n % 10;
        let m100 = n % 100;
        if m10 == 1 && m100 != 11 {
            "тег"
        } else if (2..=4).contains(&m10) && (m100 < 12 || m100 > 14) {
            "тега"
        } else {
            "тегов"
        }
    } else if n == 1 {
        "tag"
    } else {
        "tags"
    };
    wfmt!(out, cap, "{} {}", n, w);
}
