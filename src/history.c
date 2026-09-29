// history.c — frecency (decayed launch counts) and query memory ("for 'ch' I pick Chrome").

#define FREC_HALF_LIFE_DAYS 14.0
#define QMEM_HALF_LIFE_DAYS 30.0
#define QMEM_MAX_QUERY 24

typedef struct HistEntry {
    u64 hash;          // type + case-insensitive key
    WCHAR *key;        // app id or file path, original case
    f64 score;         // decayed count at time `last`
    i64 last;          // unix seconds
    u32 count;
    u8 type;           // 'a' app, 'c' command, 'f' file, 'd' folder
    u8 used;
} HistEntry;

typedef struct QueryMem {
    u64 item_hash;
    WCHAR query[QMEM_MAX_QUERY + 1];
    f64 weight;
    i64 last;
} QueryMem;

#define HIST_CAP 8192
static struct {
    Arena arena;              // strings; compacted on save when it grows
    HistEntry *entries;       // open addressing, HIST_CAP
    int count;
    QueryMem *qmem;
    int qcount, qcap;
    bool dirty;
} H;

static u64 hist_hash(u8 type, const WCHAR *key)
{
    return hash_u64(hash_wstr_i(key, -1) ^ ((u64)type * 0x9E3779B97F4A7C15ull));
}

static f64 decay(f64 score, i64 last, i64 now, f64 half_life_days)
{
    f64 dt = (f64)(now - last) / 86400.0;
    if (dt <= 0) return score;
    return score * pow(0.5, dt / half_life_days);
}

static HistEntry *hist_find(u8 type, const WCHAR *key)
{
    if (!H.entries || !key) return NULL;
    u64 h = hist_hash(type, key);
    for (int p = 0; p < HIST_CAP; p++) {
        HistEntry *e = &H.entries[(h + p) & (HIST_CAP - 1)];
        if (!e->used) return NULL;
        if (e->hash == h && e->type == type && !_wcsicmp(e->key, key)) return e;
    }
    return NULL;
}

static HistEntry *hist_insert(u8 type, const WCHAR *key)
{
    if (H.count >= HIST_CAP * 3 / 4) return NULL;
    u64 h = hist_hash(type, key);
    for (int p = 0; p < HIST_CAP; p++) {
        HistEntry *e = &H.entries[(h + p) & (HIST_CAP - 1)];
        if (!e->used) {
            e->used = 1;
            e->hash = h;
            e->type = type;
            e->key = wdup(&H.arena, key, -1);
            H.count++;
            return e;
        }
        if (e->hash == h && e->type == type && !_wcsicmp(e->key, key)) return e;
    }
    return NULL;
}

static f64 hist_frecency(u8 type, const WCHAR *key, i64 now)
{
    HistEntry *e = hist_find(type, key);
    return e ? decay(e->score, e->last, now, FREC_HALF_LIFE_DAYS) : 0;
}

static f32 frec_bonus(f64 f) { return (f32)(1800.0 * (1.0 - exp(-f / 6.0))); }

static f32 qmem_bonus(u8 type, const WCHAR *key, const WCHAR *qnorm, int qlen, i64 now)
{
    if (!qlen || !H.qcount) return 0;
    u64 ih = hist_hash(type, key);
    f64 best = 0;
    int ql = MIN(qlen, QMEM_MAX_QUERY);
    for (int i = 0; i < H.qcount; i++) {
        QueryMem *q = &H.qmem[i];
        if (q->item_hash != ih) continue;
        int ml = wlen(q->query);
        f64 rel;
        if (ml == ql && wmem_eq(q->query, qnorm, ql)) rel = 1.0;
        else if (ml > ql && wmem_eq(q->query, qnorm, ql)) rel = 0.8;         // typed less than last time
        else if (ml < ql && wmem_eq(q->query, qnorm, ml)) rel = 0.55;        // typed more than last time
        else continue;
        f64 w = decay(q->weight, q->last, now, QMEM_HALF_LIFE_DAYS);
        f64 v = rel * (1.0 - exp(-w / 1.5));
        if (v > best) best = v;
    }
    return (f32)(3500.0 * best);
}

static void history_record(u8 type, const WCHAR *key, const WCHAR *qnorm, int qlen)
{
    i64 now = unix_now();
    HistEntry *e = hist_insert(type, key);
    if (e) {
        e->score = decay(e->score, e->last, now, FREC_HALF_LIFE_DAYS) + 1.0;
        e->last = now;
        e->count++;
    }
    if (qlen > 0) {
        int ql = MIN(qlen, QMEM_MAX_QUERY);
        u64 ih = hist_hash(type, key);
        QueryMem *found = NULL;
        for (int i = 0; i < H.qcount; i++)
            if (H.qmem[i].item_hash == ih && wlen(H.qmem[i].query) == ql && wmem_eq(H.qmem[i].query, qnorm, ql)) found = &H.qmem[i];
        if (!found) {
            if (H.qcount == H.qcap) {
                int nc = H.qcap ? H.qcap * 2 : 256;
                QueryMem *nq = (QueryMem *)realloc(H.qmem, sizeof(QueryMem) * nc);
                if (!nq) return;
                H.qmem = nq;
                H.qcap = nc;
            }
            found = &H.qmem[H.qcount++];
            memset(found, 0, sizeof *found);
            found->item_hash = ih;
            memcpy(found->query, qnorm, ql * sizeof(WCHAR));
            found->query[ql] = 0;
        }
        found->weight = decay(found->weight, found->last, now, QMEM_HALF_LIFE_DAYS) + 1.0;
        found->last = now;
    }
    H.dirty = true;
}

static void history_forget(u8 type, const WCHAR *key)
{
    HistEntry *e = hist_find(type, key);
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

static void history_save(void)
{
    if (!H.dirty) return;
    H.dirty = false;
    i64 now = unix_now();
    Buf b = { 0 };
    buf_put(&b, "# MixLauncher history v1\n", 25);
    for (int i = 0; i < HIST_CAP; i++) {
        HistEntry *e = &H.entries[i];
        if (!e->used) continue;
        f64 v = decay(e->score, e->last, now, FREC_HALF_LIFE_DAYS);
        if (v < 0.02) continue;  // forgotten
        buf_printf(&b, "H\t%c\t%.4f\t%lld\t%u\t", e->type, e->score, (long long)e->last, e->count);
        buf_put_w(&b, e->key);
        buf_put(&b, "\n", 1);
    }
    if (H.qcount > 1500) {
        qsort(H.qmem, H.qcount, sizeof(QueryMem), cmp_qmem_desc);
        H.qcount = 1500;
    }
    for (int i = 0; i < H.qcount; i++) {
        QueryMem *q = &H.qmem[i];
        if (decay(q->weight, q->last, now, QMEM_HALF_LIFE_DAYS) < 0.02) continue;
        buf_printf(&b, "Q\t%016llx\t%.4f\t%lld\t", (unsigned long long)q->item_hash, q->weight, (long long)q->last);
        buf_put_w(&b, q->query);
        buf_put(&b, "\n", 1);
    }
    WCHAR path[MAX_PATH];
    data_path(path, L"history.tsv");
    if (!write_file_atomic(path, b.data ? b.data : "", (DWORD)b.len)) log_msg("history save failed");
    free(b.data);
}

static void history_load(void)
{
    arena_init(&H.arena, 64ull << 20);
    H.entries = (HistEntry *)calloc(HIST_CAP, sizeof(HistEntry));
    WCHAR path[MAX_PATH];
    data_path(path, L"history.tsv");
    char *text = read_file(path, NULL);
    if (!text) return;
    char *cur = text, *line;
    WCHAR wbuf[2048];
    while ((line = next_line(&cur)) != NULL) {
        if (line[0] == 'H' && line[1] == '\t') {
            char type = line[2];
            char *p = line + 4;
            f64 score = strtod(p, &p);
            if (*p != '\t') continue;
            i64 last = _strtoi64(p + 1, &p, 10);
            if (*p != '\t') continue;
            u32 count = (u32)strtoul(p + 1, &p, 10);
            if (*p != '\t') continue;
            utf8_to_w(p + 1, -1, wbuf, countof(wbuf));
            if (!wbuf[0]) continue;
            HistEntry *e = hist_insert((u8)type, wbuf);
            if (e) {
                e->score = score;
                e->last = last;
                e->count = count;
            }
        } else if (line[0] == 'Q' && line[1] == '\t') {
            char *p = line + 2;
            u64 ih = _strtoui64(p, &p, 16);
            if (*p != '\t') continue;
            f64 w = strtod(p + 1, &p);
            if (*p != '\t') continue;
            i64 last = _strtoi64(p + 1, &p, 10);
            if (*p != '\t') continue;
            utf8_to_w(p + 1, -1, wbuf, countof(wbuf));
            int ql = MIN(wlen(wbuf), QMEM_MAX_QUERY);
            if (!ql) continue;
            if (H.qcount == H.qcap) {
                int nc = H.qcap ? H.qcap * 2 : 256;
                QueryMem *nq = (QueryMem *)realloc(H.qmem, sizeof(QueryMem) * nc);
                if (!nq) break;
                H.qmem = nq;
                H.qcap = nc;
            }
            QueryMem *q = &H.qmem[H.qcount++];
            memset(q, 0, sizeof *q);
            q->item_hash = ih;
            memcpy(q->query, wbuf, ql * sizeof(WCHAR));
            q->weight = w;
            q->last = last;
        }
    }
    free(text);
}

// Most-used files, for the empty-query view.
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

static int history_top_files(RecentFile *out, int max)
{
    i64 now = unix_now();
    RecentFile tmp[256];
    int n = 0;
    for (int i = 0; i < HIST_CAP && n < countof(tmp); i++) {
        HistEntry *e = &H.entries[i];
        if (!e->used || (e->type != 'f' && e->type != 'd')) continue;
        f64 v = decay(e->score, e->last, now, FREC_HALF_LIFE_DAYS);
        if (v < 0.25) continue;
        tmp[n].path = e->key;
        tmp[n].folder = e->type == 'd';
        tmp[n].score = v;
        n++;
    }
    qsort(tmp, n, sizeof(RecentFile), cmp_recent);
    n = MIN(n, max);
    memcpy(out, tmp, sizeof(RecentFile) * n);
    return n;
}
