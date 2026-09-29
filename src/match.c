// match.c — query normalization and fuzzy scoring. Pure functions, safe on any thread.

enum {
    SCORE_EXACT = 10000,
    SCORE_PREFIX = 9000,
    SCORE_WORD_PREFIX = 8000,
    SCORE_ACRONYM = 7000,
    SCORE_SUBSTRING = 6000,
    SCORE_FUZZY_MAX = 5000,
};

static WCHAR norm_char(WCHAR c)
{
    if (c < 0x80) return wlower_ascii(c);
    if (c >= 0x410 && c <= 0x42F) return (WCHAR)(c + 32);        // А-Я
    if (c == 0x401 || c == 0x451) return 0x435;                  // Ё ё -> е
    if (c >= 0x430 && c <= 0x44F) return c;
    if (c >= 0x400 && c <= 0x40F) return (WCHAR)(c + 80);
    if (c == 0x00A0) return ' ';
    return (WCHAR)(uintptr_t)CharLowerW((LPWSTR)(uintptr_t)c);
}

static void norm_str(WCHAR *dst, const WCHAR *src, int len)
{
    for (int i = 0; i < len; i++) dst[i] = norm_char(src[i]);
    dst[len] = 0;
}

static bool is_sep(WCHAR c)
{
    return c == ' ' || c == '-' || c == '_' || c == '.' || c == '(' || c == ')' || c == '[' || c == ']' || c == '/' ||
           c == '\\' || c == '&' || c == '+' || c == ',' || c == ':' || c == '\'' || c == '"' || c == 0x2013 || c == 0x2014;
}

static bool is_upper(WCHAR c) { return (c >= 'A' && c <= 'Z') || (c >= 0x410 && c <= 0x42F) || c == 0x401; }
static bool is_lower(WCHAR c) { return (c >= 'a' && c <= 'z') || (c >= 0x430 && c <= 0x44F) || c == 0x451; }
static bool is_digit(WCHAR c) { return c >= '0' && c <= '9'; }

static void word_starts(const WCHAR *s, int len, u8 *ws)
{
    for (int i = 0; i < len; i++) {
        WCHAR c = s[i];
        if (i == 0) {
            ws[i] = !is_sep(c);
            continue;
        }
        WCHAR p = s[i - 1];
        ws[i] = !is_sep(c) && (is_sep(p) || (is_lower(p) && is_upper(c)) || (!is_digit(p) && is_digit(c)) || (is_digit(p) && !is_digit(c)));
    }
}

static bool wmem_eq(const WCHAR *a, const WCHAR *b, int n) { return memcmp(a, b, (size_t)n * sizeof(WCHAR)) == 0; }

static int find_sub(const WCHAR *s, int n, const WCHAR *q, int m, int from)
{
    for (int i = from; i + m <= n; i++)
        if (s[i] == q[0] && wmem_eq(s + i, q, m)) return i;
    return -1;
}

static int score_fuzzy(const WCHAR *s, int n, const u8 *ws, const WCHAR *q, int m)
{
    int start = -1;
    for (int i = 0; i < n; i++)
        if (ws[i] && s[i] == q[0]) {
            start = i;
            break;
        }
    if (start < 0) return 0;
    int j = 0, prev = -2, gaps = 0, bonus = 0;
    for (int i = start; i < n && j < m; i++) {
        if (s[i] != q[j]) continue;
        if (!ws[i] && prev != i - 1) {
            int k = i + 1;
            while (k < n && k < i + 12 && !(ws[k] && s[k] == q[j])) k++;
            if (k < n && k < i + 12) i = k;
        }
        if (ws[i]) bonus += 30;
        if (prev == i - 1) bonus += 20;
        if (prev >= 0) gaps += i - prev - 1;
        prev = i;
        j++;
    }
    if (j < m) return 0;
    if (m >= 3 && gaps > 3 * m + 6) return 0;
    int sc = 1000 + bonus * 40 / m - gaps * 25 - start * 10 - MIN(n, 200);
    return CLAMP(sc, 1000, SCORE_FUZZY_MAX);
}

// Score one query token against a normalized name.
static int score_token(const WCHAR *s, int n, const u8 *ws, const WCHAR *q, int m)
{
    if (m <= 0) return 1;
    if (m > n) return 0;
    int len_pen = MIN(n - m, 200);
    if (m == n && wmem_eq(s, q, n)) return SCORE_EXACT;
    if (wmem_eq(s, q, m)) return SCORE_PREFIX - len_pen;
    for (int i = find_sub(s, n, q, m, 1); i >= 0; i = find_sub(s, n, q, m, i + 1))
        if (ws[i]) return SCORE_WORD_PREFIX - MIN(i, 100) * 4 - len_pen;
    if (m >= 2) {
        int j = 0;
        for (int i = 0; i < n && j < m; i++)
            if (ws[i] && s[i] == q[j]) j++;
        if (j == m) return SCORE_ACRONYM - MIN(n, 200);
    }
    int at = find_sub(s, n, q, m, 0);
    if (at >= 0) return SCORE_SUBSTRING - MIN(at, 200) * 2 - len_pen;
    return score_fuzzy(s, n, ws, q, m);
}

static int score_query(const WCHAR *s, int n, const u8 *ws, const WCHAR *q, int m)
{
    while (m > 0 && q[m - 1] == ' ') m--;
    while (m > 0 && q[0] == ' ') {
        q++;
        m--;
    }
    if (m <= 0) return 1;
    int whole = score_token(s, n, ws, q, m);
    bool has_space = false;
    for (int i = 0; i < m; i++)
        if (q[i] == ' ') has_space = true;
    if (!has_space) return whole;
    int worst = INT32_MAX, tokens = 0;
    for (int i = 0; i < m;) {
        while (i < m && q[i] == ' ') i++;
        int b = i;
        while (i < m && q[i] != ' ') i++;
        if (i > b) {
            int sc = score_token(s, n, ws, q + b, i - b);
            if (!sc) {
                worst = 0;
                break;
            }
            worst = MIN(worst, sc);
            tokens++;
        }
    }
    int multi = (tokens && worst) ? worst - 300 : 0;
    return MAX(whole, multi);
}

// Wrong keyboard layout: convert between QWERTY and ЙЦУКЕН positions.
static const WCHAR k_layout_en[] = L"`qwertyuiop[]asdfghjkl;'zxcvbnm,./";
static const WCHAR k_layout_ru[] = L"\x0451\x0439\x0446\x0443\x043a\x0435\x043d\x0433\x0448\x0449\x0437\x0445\x044a"
                                   L"\x0444\x044b\x0432\x0430\x043f\x0440\x043e\x043b\x0434\x0436\x044d"
                                   L"\x044f\x0447\x0441\x043c\x0438\x0442\x044c\x0431\x044e.";

static bool layout_convert(const WCHAR *q, int m, WCHAR *out)
{
    bool has_ru = false, has_en = false;
    for (int i = 0; i < m; i++) {
        if (q[i] >= 0x430 && q[i] <= 0x451) has_ru = true;
        else if (q[i] >= 'a' && q[i] <= 'z') has_en = true;
    }
    if (has_ru == has_en) {  // mixed or none: ambiguous, skip
        out[0] = 0;
        return false;
    }
    const WCHAR *from = has_ru ? k_layout_ru : k_layout_en;
    const WCHAR *to = has_ru ? k_layout_en : k_layout_ru;
    int n = wlen(from);
    for (int i = 0; i < m; i++) {
        WCHAR c = q[i];
        WCHAR r = c;
        for (int k = 0; k < n; k++)
            if (from[k] == c) {
                r = to[k];
                break;
            }
        // normalized text maps ё to е, keep that invariant
        out[i] = norm_char(r);
    }
    out[m] = 0;
    return true;
}
