// Launch history (history.tsv): how often and how lately each app, command, file or folder was opened
// (frecency), and which queries led to it (query memory).

#define FREC_HALF_LIFE_DAYS 14.0
#define QMEM_HALF_LIFE_DAYS 30.0
#define QMEM_MAX_QUERY 24
#define QMEM_KEEP 1500

// What a history or tag entry is about; the letter is its mark in history.tsv and tags.tsv.
enum { IT_APP = 'a', IT_CMD = 'c', IT_FILE = 'f', IT_DIR = 'd' };

typedef struct HistEntry {
    u64 hash;
    WCHAR *key;  // app id or full path
    f64 score;
    i64 last;
    u32 count;
    u8 type;
    u8 used;
} HistEntry;

typedef struct QueryMem {
    u64 item_hash;
    WCHAR query[QMEM_MAX_QUERY + 1];  // normalized
    f64 weight;
    i64 last;
} QueryMem;

#define HIST_CAP 8192
static struct {
    Arena arena;
    HistEntry *entries;  // open addressing by hash
    int count;
    QueryMem *qmem;
    int qcount, qcap;
    bool dirty;
} H;

static u64 item_hash_of(u8 type, u64 key_hash) { return hash_u64(key_hash ^ ((u64)type * 0x9E3779B97F4A7C15ull)); }
static u64 item_hash(u8 type, const WCHAR *key) { return item_hash_of(type, hash_wstr_i(key, -1)); }

static f64 decay(f64 score, i64 last, i64 now, f64 half_life_days)
{
    f64 dt = (f64)(now - last) / 86400.0;
    return dt <= 0 ? score : score * pow(0.5, dt / half_life_days);
}

static HistEntry *hist_slot(u8 type, const WCHAR *key, u64 h, bool create)
{
    if (!H.entries || !key) return NULL;
    for (int p = 0; p < HIST_CAP; p++) {
        HistEntry *e = &H.entries[(h + p) & (HIST_CAP - 1)];
        if (e->used && e->hash == h && e->type == type && !_wcsicmp(e->key, key)) return e;
        if (e->used) continue;
        if (!create || H.count >= HIST_CAP * 3 / 4) return NULL;
        e->used = 1;
        e->hash = h;
        e->type = type;
        e->key = wdup(&H.arena, key, -1);
        H.count++;
        return e;
    }
    return NULL;
}

// h is item_hash(type, key).
static f64 hist_frecency(u8 type, const WCHAR *key, u64 h, i64 now)
{
    HistEntry *e = hist_slot(type, key, h, false);
    return e ? decay(e->score, e->last, now, FREC_HALF_LIFE_DAYS) : 0;
}

static f32 frec_bonus(f64 f) { return (f32)(1800.0 * (1.0 - exp(-f / 6.0))); }

// How strongly this query (or one it extends or that extends it) led to the item before.
static f32 qmem_bonus(u64 h, const WCHAR *qnorm, int qlen, i64 now)
{
    if (!qlen || !H.qcount) return 0;
    f64 best = 0;
    int ql = MIN(qlen, QMEM_MAX_QUERY);
    for (int i = 0; i < H.qcount; i++) {
        QueryMem *q = &H.qmem[i];
        if (q->item_hash != h) continue;
        int ml = wlen(q->query);
        f64 rel;
        if (ml == ql && wmem_eq(q->query, qnorm, ql)) rel = 1.0;
        else if (ml > ql && wmem_eq(q->query, qnorm, ql)) rel = 0.8;
        else if (ml < ql && wmem_eq(q->query, qnorm, ml)) rel = 0.55;
        else continue;
        f64 w = decay(q->weight, q->last, now, QMEM_HALF_LIFE_DAYS);
        best = MAX(best, rel * (1.0 - exp(-w / 1.5)));
    }
    return (f32)(3500.0 * best);
}

static QueryMem *qmem_add(u64 h, const WCHAR *query, int len)
{
    if (!GROW(H.qmem, H.qcap, H.qcount + 1, 256)) return NULL;
    QueryMem *q = &H.qmem[H.qcount++];
    memset(q, 0, sizeof *q);
    q->item_hash = h;
    len = MIN(len, QMEM_MAX_QUERY);
    memcpy(q->query, query, (size_t)len * sizeof(WCHAR));
    return q;
}

static void history_record(u8 type, const WCHAR *key, const WCHAR *qnorm, int qlen)
{
    i64 now = unix_now();
    u64 h = item_hash(type, key);
    HistEntry *e = hist_slot(type, key, h, true);
    if (e) {
        e->score = decay(e->score, e->last, now, FREC_HALF_LIFE_DAYS) + 1.0;
        e->last = now;
        e->count++;
    }
    if (qlen > 0) {
        int ql = MIN(qlen, QMEM_MAX_QUERY);
        QueryMem *q = NULL;
        for (int i = 0; i < H.qcount && !q; i++)
            if (H.qmem[i].item_hash == h && wlen(H.qmem[i].query) == ql && wmem_eq(H.qmem[i].query, qnorm, ql)) q = &H.qmem[i];
        if (!q) q = qmem_add(h, qnorm, ql);
        if (!q) return;
        q->weight = decay(q->weight, q->last, now, QMEM_HALF_LIFE_DAYS) + 1.0;
        q->last = now;
    }
    H.dirty = true;
}

// The file is gone: it stops ranking high.
static void history_forget(u8 type, const WCHAR *key)
{
    HistEntry *e = hist_slot(type, key, item_hash(type, key), false);
    if (e) {
        e->score = 0;
        H.dirty = true;
    }
}

static void history_clear(void)
{
    if (H.entries) memset(H.entries, 0, sizeof(HistEntry) * HIST_CAP);
    H.count = 0;
    H.qcount = 0;
    arena_reset(&H.arena);
    H.dirty = true;
}

static int cmp_qmem_desc(const void *a, const void *b)
{
    const QueryMem *x = (const QueryMem *)a, *y = (const QueryMem *)b;
    return x->last < y->last ? 1 : x->last > y->last ? -1 : 0;
}

// Entries that have decayed to nothing are dropped; so are the oldest queries past QMEM_KEEP.
static void history_save(void)
{
    if (!H.dirty) return;
    H.dirty = false;
    i64 now = unix_now();
    Buf b = { 0 };
    buf_str(&b, "# MixLauncher history v1\n");
    for (int i = 0; i < HIST_CAP; i++) {
        HistEntry *e = &H.entries[i];
        if (!e->used || decay(e->score, e->last, now, FREC_HALF_LIFE_DAYS) < 0.02) continue;
        buf_printf(&b, "H\t%c\t%.4f\t%lld\t%u\t", e->type, e->score, (long long)e->last, e->count);
        buf_put_w(&b, e->key);
        buf_put(&b, "\n", 1);
    }
    if (H.qcount > QMEM_KEEP) {
        qsort(H.qmem, H.qcount, sizeof(QueryMem), cmp_qmem_desc);
        H.qcount = QMEM_KEEP;
    }
    for (int i = 0; i < H.qcount; i++) {
        QueryMem *q = &H.qmem[i];
        if (decay(q->weight, q->last, now, QMEM_HALF_LIFE_DAYS) < 0.02) continue;
        buf_printf(&b, "Q\t%016llx\t%.4f\t%lld\t", (unsigned long long)q->item_hash, q->weight, (long long)q->last);
        buf_put_w(&b, q->query);
        buf_put(&b, "\n", 1);
    }
    buf_save_data(&b, L"history.tsv");
}

// H <type> <score> <last> <count> <key>  /  Q <item hash> <weight> <last> <query>
static void history_load(void)
{
    arena_init(&H.arena, 64ull << 20);
    H.entries = (HistEntry *)calloc(HIST_CAP, sizeof(HistEntry));
    char *text = data_read(L"history.tsv");
    if (!text) return;
    char *cur = text, *line;
    WCHAR wbuf[2048];
    while ((line = next_line(&cur)) != NULL) {
        if (line[0] == 'H' && line[1] == '\t') {
            u8 type = (u8)line[2];
            char *p = line + 4;
            f64 score = strtod(p, &p);
            if (*p != '\t') continue;
            i64 last = _strtoi64(p + 1, &p, 10);
            if (*p != '\t') continue;
            u32 count = (u32)strtoul(p + 1, &p, 10);
            if (*p != '\t') continue;
            utf8_to_w(p + 1, -1, wbuf, countof(wbuf));
            HistEntry *e = wbuf[0] ? hist_slot(type, wbuf, item_hash(type, wbuf), true) : NULL;
            if (e) {
                e->score = score;
                e->last = last;
                e->count = count;
            }
        } else if (line[0] == 'Q' && line[1] == '\t') {
            char *p = line + 2;
            u64 h = _strtoui64(p, &p, 16);
            if (*p != '\t') continue;
            f64 w = strtod(p + 1, &p);
            if (*p != '\t') continue;
            i64 last = _strtoi64(p + 1, &p, 10);
            if (*p != '\t') continue;
            utf8_to_w(p + 1, -1, wbuf, countof(wbuf));
            QueryMem *q = wbuf[0] ? qmem_add(h, wbuf, wlen(wbuf)) : NULL;
            if (q) {
                q->weight = w;
                q->last = last;
            }
        }
    }
    free(text);
}

typedef struct RecentFile {
    const WCHAR *path;
    bool folder;
    f64 score;
} RecentFile;

static int cmp_recent(const void *a, const void *b)
{
    const RecentFile *x = (const RecentFile *)a, *y = (const RecentFile *)b;
    return x->score < y->score ? 1 : x->score > y->score ? -1 : 0;
}

// The files and folders opened most (and most lately), for the empty launcher.
static int history_top_files(RecentFile *out, int max)
{
    i64 now = unix_now();
    RecentFile tmp[256];
    int n = 0;
    for (int i = 0; i < HIST_CAP && n < countof(tmp); i++) {
        HistEntry *e = &H.entries[i];
        if (!e->used || (e->type != IT_FILE && e->type != IT_DIR)) continue;
        f64 v = decay(e->score, e->last, now, FREC_HALF_LIFE_DAYS);
        if (v < 0.25) continue;
        tmp[n].path = e->key;
        tmp[n].folder = e->type == IT_DIR;
        tmp[n].score = v;
        n++;
    }
    qsort(tmp, n, sizeof(RecentFile), cmp_recent);
    n = MIN(n, max);
    memcpy(out, tmp, sizeof(RecentFile) * n);
    return n;
}
