// Tags: the user's own search words for an app, a command, a file or a folder ("Add tag" in the context
// menu). A tagged item is found by its tag the way it is found by its name: a prefix, a word, or the
// tag typed in the wrong keyboard layout.

#define TAG_INPUT_MAX 120

typedef struct Tag {
    WCHAR *text, *norm;
    u8 *ws;
    int len;
} Tag;

typedef struct TagItem {
    u8 type;  // 'a' app, 'c' command, 'f' file, 'd' folder
    WCHAR *key;  // app id or full path
    u64 hash;
    Tag *tags;
    int ntags, cap;
    FileItem file;  // files and folders: how the item shows as a search result
} TagItem;

static struct {
    TagItem **items;
    int n, cap;
} TG;

static u64 tag_hash(u8 type, const WCHAR *key) { return hash_wstr_i(key, -1) ^ ((u64)type * 0x9E3779B97F4A7C15ull); }

static TagItem *tags_find(u8 type, const WCHAR *key)
{
    if (!TG.n || !key) return NULL;
    u64 h = tag_hash(type, key);
    for (int i = 0; i < TG.n; i++)
        if (TG.items[i]->hash == h && TG.items[i]->type == type && !_wcsicmp(TG.items[i]->key, key)) return TG.items[i];
    return NULL;
}

static void tag_item_free(TagItem *ti)
{
    for (int i = 0; i < ti->ntags; i++) {
        free(ti->tags[i].text);
        free(ti->tags[i].norm);
        free(ti->tags[i].ws);
    }
    free(ti->tags);
    free(ti->file.dir);
    free(ti->key);
    free(ti);
}

static TagItem *tags_item(u8 type, const WCHAR *key)
{
    TagItem *ti = tags_find(type, key);
    if (ti) return ti;
    if (TG.n == TG.cap) {
        int nc = TG.cap ? TG.cap * 2 : 32;
        TagItem **ni = (TagItem **)realloc(TG.items, sizeof(TagItem *) * nc);
        if (!ni) return NULL;
        TG.items = ni;
        TG.cap = nc;
    }
    ti = (TagItem *)calloc(1, sizeof(TagItem));
    if (!ti || !(ti->key = wdup_heap(key))) {
        free(ti);
        return NULL;
    }
    ti->type = type;
    ti->hash = tag_hash(type, key);
    if (type == 'f' || type == 'd') {
        FileItem *f = &ti->file;
        f->full = ti->key;
        const WCHAR *slash = wcsrchr(f->full, '\\');
        bool root = slash && slash[1] == 0;
        int dl = slash && !root ? (int)(slash - f->full) : 0;
        f->name = slash && !root ? (WCHAR *)slash + 1 : f->full;
        f->dir = (WCHAR *)malloc((size_t)(dl + 1) * sizeof(WCHAR));
        if (f->dir) {
            memcpy(f->dir, f->full, (size_t)dl * sizeof(WCHAR));
            f->dir[dl] = 0;
        }
        f->flags = type == 'd' ? FI_FOLDER : 0;
        f->icon_key = icon_key_for_file(f->full, type == 'd');
    }
    TG.items[TG.n++] = ti;
    return ti;
}

static bool tag_push(TagItem *ti, const WCHAR *s, int len)
{
    while (len > 0 && (s[0] == ' ' || s[0] == '\t')) s++, len--;
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) len--;
    if (len <= 0) return false;
    len = MIN(len, TAG_INPUT_MAX);
    WCHAR norm[TAG_INPUT_MAX + 1];
    norm_str(norm, s, len);
    for (int i = 0; i < ti->ntags; i++)
        if (ti->tags[i].len == len && wmem_eq(ti->tags[i].norm, norm, len)) return false;
    if (ti->ntags == ti->cap) {
        int nc = ti->cap ? ti->cap * 2 : 4;
        Tag *nt = (Tag *)realloc(ti->tags, sizeof(Tag) * nc);
        if (!nt) return false;
        ti->tags = nt;
        ti->cap = nc;
    }
    Tag *t = &ti->tags[ti->ntags];
    t->text = (WCHAR *)malloc((size_t)(len + 1) * sizeof(WCHAR));
    t->norm = wdup_heap(norm);
    t->ws = (u8 *)malloc((size_t)len + 1);
    if (!t->text || !t->norm || !t->ws) {
        free(t->text);
        free(t->norm);
        free(t->ws);
        return false;
    }
    memcpy(t->text, s, (size_t)len * sizeof(WCHAR));
    t->text[len] = 0;
    word_starts(t->text, len, t->ws);
    t->len = len;
    ti->ntags++;
    return true;
}

static void tags_save(void)
{
    Buf b = { 0 };
    buf_put(&b, "# MixLauncher tags v1: type, tag, app id or path\n", 49);
    for (int i = 0; i < TG.n; i++)
        for (int k = 0; k < TG.items[i]->ntags; k++) {
            buf_printf(&b, "%c\t", TG.items[i]->type);
            buf_put_w(&b, TG.items[i]->tags[k].text);
            buf_put(&b, "\t", 1);
            buf_put_w(&b, TG.items[i]->key);
            buf_put(&b, "\n", 1);
        }
    WCHAR path[MAX_PATH];
    data_path(path, L"tags.tsv");
    if (!write_file_atomic(path, b.data ? b.data : "", (DWORD)b.len)) log_msg("tags save failed");
    free(b.data);
}

static void tags_load(void)
{
    WCHAR path[MAX_PATH];
    data_path(path, L"tags.tsv");
    char *text = read_file(path, NULL);
    if (!text) return;
    char *cur = text, *line;
    WCHAR tag[TAG_INPUT_MAX + 1], key[2048];
    while ((line = next_line(&cur)) != NULL) {
        u8 type = (u8)line[0];
        if ((type != 'a' && type != 'c' && type != 'f' && type != 'd') || line[1] != '\t') continue;
        char *t2 = strchr(line + 2, '\t');
        if (!t2) continue;
        *t2 = 0;
        utf8_to_w(line + 2, -1, tag, countof(tag));
        utf8_to_w(t2 + 1, -1, key, countof(key));
        if (!tag[0] || !key[0]) continue;
        TagItem *ti = tags_item(type, key);
        if (ti) tag_push(ti, tag, wlen(tag));
    }
    free(text);
}

// "a, b,c" -> three tags. Returns whether anything was added.
static bool tags_add(u8 type, const WCHAR *key, const WCHAR *input)
{
    TagItem *ti = tags_item(type, key);
    if (!ti) return false;
    bool added = false;
    for (const WCHAR *s = input; *s;) {
        const WCHAR *e = s;
        while (*e && *e != ',' && *e != ';') e++;
        added |= tag_push(ti, s, (int)(e - s));
        s = *e ? e + 1 : e;
    }
    if (!ti->ntags) {
        TG.items[--TG.n] = NULL;
        tag_item_free(ti);
    }
    if (added) tags_save();
    return added;
}

static void tags_clear(u8 type, const WCHAR *key)
{
    TagItem *ti = tags_find(type, key);
    if (!ti) return;
    for (int i = 0; i < TG.n; i++)
        if (TG.items[i] == ti) {
            TG.items[i] = TG.items[--TG.n];
            break;
        }
    tag_item_free(ti);
    tags_save();
}

// The best match of the query against the item's tags, 0 if none is close enough. A tag only counts as a
// prefix, a word, an acronym or a substring: fuzzy matches on short tags would find everything.
static int tags_score(const TagItem *ti, const WCHAR *qn, int ql, const WCHAR *alt, bool has_alt, bool *is_alt)
{
    int best = 0, min = SCORE_SUBSTRING - 400;
    *is_alt = false;
    for (int i = 0; ti && i < ti->ntags; i++) {
        const Tag *t = &ti->tags[i];
        int s = score_query(t->norm, t->len, t->ws, qn, ql);
        if (s >= min && s > best) {
            best = s;
            *is_alt = false;
        }
        if (has_alt) {
            int sa = score_query(t->norm, t->len, t->ws, alt, ql);
            if (sa >= min && sa * 9 / 10 > best) {
                best = sa * 9 / 10;
                *is_alt = true;
            }
        }
    }
    return best;
}

// "1 тег", "3 тега", "5 тегов"
static void tags_count_label(WCHAR *out, int cap, int n)
{
    const WCHAR *w;
    if (g_lang_ru) {
        int m10 = n % 10, m100 = n % 100;
        w = m10 == 1 && m100 != 11 ? L"тег" : m10 >= 2 && m10 <= 4 && (m100 < 12 || m100 > 14) ? L"тега" : L"тегов";
    } else {
        w = n == 1 ? L"tag" : L"tags";
    }
    _snwprintf(out, cap, L"%d %ls", n, w);
    out[cap - 1] = 0;
}
