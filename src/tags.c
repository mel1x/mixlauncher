// Tags (tags.tsv): the user's own search words for an app, command, file or folder, matched like names.

#define TAG_INPUT_MAX 120

typedef struct Tag {
    WCHAR *text, *norm;
    u8 *ws;
    int len;
} Tag;

typedef struct TagItem {
    u8 type;     // IT_*
    WCHAR *key;  // app id or full path
    u64 hash;    // item_hash(type, key), shared with the history
    Tag *tags;
    int ntags, cap;
    FileItem file;  // files and folders: how the item shows as a search result
} TagItem;

static struct {
    TagItem **items;
    int n, cap;
} TG;

static u8 app_item_type(const App *a) { return a->kind == APP_CMD ? IT_CMD : IT_APP; }
static u8 file_item_type(const FileItem *f) { return (f->flags & FI_FOLDER) ? IT_DIR : IT_FILE; }

// h is item_hash(type, key), 0 to have it computed.
static TagItem *tags_find(u8 type, const WCHAR *key, u64 h)
{
    if (!TG.n || !key) return NULL;
    if (!h) h = item_hash(type, key);
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
    TagItem *ti = tags_find(type, key, 0);
    if (ti) return ti;
    if (!GROW(TG.items, TG.cap, TG.n + 1, 32)) return NULL;
    ti = (TagItem *)calloc(1, sizeof(TagItem));
    if (!ti || !(ti->key = wdup_heap(key))) {
        free(ti);
        return NULL;
    }
    ti->type = type;
    ti->hash = item_hash(type, key);
    if (type == IT_FILE || type == IT_DIR) {
        FileItem *f = &ti->file;
        const WCHAR *name;
        int dl = path_split(ti->key, &name);
        f->full = ti->key;
        f->name = (WCHAR *)name;
        f->dir = (WCHAR *)malloc((size_t)(dl + 1) * sizeof(WCHAR));
        if (f->dir) wcopy(f->dir, dl + 1, f->full);
        f->flags = type == IT_DIR ? FI_FOLDER : 0;
        f->icon_key = icon_key_for_file(f->full, type == IT_DIR);
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
    if (!GROW(ti->tags, ti->cap, ti->ntags + 1, 4)) return false;
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
    wcopy(t->text, len + 1, s);
    word_starts(t->text, len, t->ws);
    t->len = len;
    ti->ntags++;
    return true;
}

// <type> <tag> <key>, one line per tag
static void tags_save(void)
{
    Buf b = { 0 };
    buf_str(&b, "# MixLauncher tags v1: type, tag, app id or path\n");
    for (int i = 0; i < TG.n; i++)
        for (int k = 0; k < TG.items[i]->ntags; k++) {
            buf_printf(&b, "%c\t", TG.items[i]->type);
            buf_put_w(&b, TG.items[i]->tags[k].text);
            buf_put(&b, "\t", 1);
            buf_put_w(&b, TG.items[i]->key);
            buf_put(&b, "\n", 1);
        }
    buf_save_data(&b, L"tags.tsv");
}

static void tags_load(void)
{
    char *text = data_read(L"tags.tsv");
    if (!text) return;
    char *cur = text, *line;
    WCHAR tag[TAG_INPUT_MAX + 1], key[2048];
    while ((line = next_line(&cur)) != NULL) {
        u8 type = (u8)line[0];
        if ((type != IT_APP && type != IT_CMD && type != IT_FILE && type != IT_DIR) || line[1] != '\t') continue;
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
    if (!ti->ntags) {  // a new item that got nothing
        TG.items[--TG.n] = NULL;
        tag_item_free(ti);
    }
    if (added) tags_save();
    return added;
}

static void tags_clear(u8 type, const WCHAR *key)
{
    TagItem *ti = tags_find(type, key, 0);
    if (!ti) return;
    for (int i = 0; i < TG.n; i++)
        if (TG.items[i] == ti) {
            TG.items[i] = TG.items[--TG.n];
            break;
        }
    tag_item_free(ti);
    tags_save();
}

// Best tag match, 0 if none: prefix, word, acronym or substring only (fuzzy hits on short tags match everything).
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
    wfmt(out, cap, L"%d %ls", n, w);
}
