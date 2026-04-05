/*
 * Minimal QR code encoder – byte mode, EC level L, versions 1-4.
 * No external dependencies.
 */
#include "qrcode.h"
#include <string.h>

/* ---- GF(256) arithmetic (primitive polynomial 0x11D) ---- */

static uint8_t gf_exp[512];
static uint8_t gf_log[256];
static int gf_ready;

static void gf_init(void)
{
    if (gf_ready) return;
    int v = 1;
    for (int i = 0; i < 255; i++) {
        gf_exp[i] = (uint8_t)v;
        gf_log[v] = (uint8_t)i;
        v <<= 1;
        if (v >= 256) v ^= 0x11D;
    }
    for (int i = 255; i < 512; i++)
        gf_exp[i] = gf_exp[i - 255];
    gf_ready = 1;
}

static uint8_t gf_mul(uint8_t a, uint8_t b)
{
    if (a == 0 || b == 0) return 0;
    return gf_exp[gf_log[a] + gf_log[b]];
}

/* ---- Reed-Solomon EC generation ---- */

static void rs_compute_ec(const uint8_t *data, int dlen, int eclen, uint8_t *ec)
{
    /* Build generator polynomial g[0..eclen-1] (coeff of x^0..x^(eclen-1)).
     * Leading x^eclen coefficient = 1 (implicit). */
    uint8_t g[32];
    memset(g, 0, sizeof(g));
    g[0] = 1;
    for (int i = 0; i < eclen; i++) {
        for (int j = eclen - 1; j >= 1; j--)
            g[j] = g[j - 1] ^ gf_mul(g[j], gf_exp[i]);
        g[0] = gf_mul(g[0], gf_exp[i]);
    }

    /* Polynomial division: remainder of data·x^eclen ÷ g(x) */
    uint8_t r[32];
    memset(r, 0, eclen);
    for (int i = 0; i < dlen; i++) {
        uint8_t fb = data[i] ^ r[eclen - 1];
        for (int j = eclen - 1; j >= 1; j--)
            r[j] = r[j - 1] ^ gf_mul(fb, g[j]);
        r[0] = gf_mul(fb, g[0]);
    }

    /* Output highest-degree first */
    for (int i = 0; i < eclen; i++)
        ec[i] = r[eclen - 1 - i];
}

/* ---- Version parameters (EC level L, single block) ---- */

typedef struct { int size, data_cw, ec_cw, align; } VerInfo;

static const VerInfo VER[] = {
    /* V1 */ { 21, 19,  7,  0 },
    /* V2 */ { 25, 34, 10, 18 },
    /* V3 */ { 29, 55, 15, 22 },
    /* V4 */ { 33, 80, 20, 26 },
};
#define NUM_VER 4

/* ---- Internal working grid ---- */

#define S QR_MAX_MODULES

static uint8_t g[S][S];   /* 0 = white, 1 = black */
static uint8_t fm[S][S];  /* 1 = function-pattern module (not for data) */

static void set_fn(int r, int c, int sz, int black)
{
    if (r >= 0 && r < sz && c >= 0 && c < sz) {
        g[r][c] = black ? 1 : 0;
        fm[r][c] = 1;
    }
}

/* ---- Function patterns ---- */

static void place_finder(int r0, int c0, int sz)
{
    for (int dr = 0; dr < 7; dr++)
        for (int dc = 0; dc < 7; dc++) {
            int blk = (dr == 0 || dr == 6 || dc == 0 || dc == 6 ||
                       (dr >= 2 && dr <= 4 && dc >= 2 && dc <= 4));
            set_fn(r0 + dr, c0 + dc, sz, blk);
        }
}

static void place_patterns(int sz, int align)
{
    /* Three finder patterns */
    place_finder(0, 0, sz);
    place_finder(0, sz - 7, sz);
    place_finder(sz - 7, 0, sz);

    /* Separators (white border around finders) */
    for (int i = 0; i < 8; i++) {
        set_fn(7, i, sz, 0);              /* top-left horizontal */
        set_fn(i, 7, sz, 0);              /* top-left vertical */
        set_fn(7, sz - 8 + i, sz, 0);     /* top-right horizontal */
        set_fn(i, sz - 8, sz, 0);         /* top-right vertical */
        set_fn(sz - 8, i, sz, 0);         /* bottom-left horizontal */
        set_fn(sz - 8 + i, 7, sz, 0);     /* bottom-left vertical */
    }

    /* Timing patterns */
    for (int i = 8; i < sz - 8; i++) {
        set_fn(6, i, sz, i % 2 == 0);
        set_fn(i, 6, sz, i % 2 == 0);
    }

    /* Alignment pattern (versions 2-4 have exactly one) */
    if (align) {
        for (int dr = -2; dr <= 2; dr++)
            for (int dc = -2; dc <= 2; dc++) {
                int blk = (dr == -2 || dr == 2 || dc == -2 || dc == 2 ||
                           (dr == 0 && dc == 0));
                set_fn(align + dr, align + dc, sz, blk);
            }
    }

    /* Dark module */
    set_fn(sz - 8, 8, sz, 1);

    /* Reserve format-information areas */
    for (int i = 0; i < 6; i++) fm[8][i] = 1;
    fm[8][7] = 1;  fm[8][8] = 1;  fm[7][8] = 1;
    for (int i = 0; i < 6; i++) fm[i][8] = 1;
    for (int i = 0; i < 7; i++) fm[sz - 1 - i][8] = 1;
    for (int i = 0; i < 8; i++) fm[8][sz - 8 + i] = 1;
}

/* ---- Data placement (zigzag) ---- */

static void place_data(int sz, const uint8_t *bits, int nbits)
{
    int idx = 0;
    int upward = 1;

    for (int right = sz - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5;  /* skip timing column */

        for (int i = 0; i < sz; i++) {
            int row = upward ? (sz - 1 - i) : i;
            for (int dc = 0; dc <= 1; dc++) {
                int col = right - dc;
                if (col < 0 || fm[row][col]) continue;
                if (idx < nbits)
                    g[row][col] = bits[idx];
                idx++;
            }
        }
        upward = !upward;
    }
}

/* ---- Masking ---- */

static int mask_cond(int mask, int r, int c)
{
    switch (mask) {
    case 0: return (r + c) % 2 == 0;
    case 1: return r % 2 == 0;
    case 2: return c % 3 == 0;
    case 3: return (r + c) % 3 == 0;
    case 4: return (r / 2 + c / 3) % 2 == 0;
    case 5: return (r * c) % 2 + (r * c) % 3 == 0;
    case 6: return ((r * c) % 2 + (r * c) % 3) % 2 == 0;
    case 7: return ((r + c) % 2 + (r * c) % 3) % 2 == 0;
    }
    return 0;
}

/* ---- Format information ---- */

static uint16_t compute_format_bits(int mask)
{
    /* EC level L = 01, then 3-bit mask */
    uint16_t data = (1u << 3) | (unsigned)mask;
    uint16_t bits = data << 10;
    /* BCH(15,5) with generator 0x537 */
    for (int i = 4; i >= 0; i--)
        if (bits & (1u << (i + 10)))
            bits ^= (0x537u << i);
    bits |= (data << 10);
    bits ^= 0x5412u;
    return bits;
}

/* Bit positions for format info copy 1 (around top-left finder) */
static const int F1R[] = {8,8,8,8,8,8,8,8, 7,5,4,3,2,1,0};
static const int F1C[] = {0,1,2,3,4,5,7,8, 8,8,8,8,8,8,8};

static void write_format(int sz, int mask, uint8_t out[][S])
{
    uint16_t bits = compute_format_bits(mask);
    for (int i = 0; i < 15; i++) {
        int v = (bits >> i) & 1;
        /* Copy 1 */
        out[F1R[i]][F1C[i]] = v;
        /* Copy 2: bits 0-6 → bottom-left column, bits 7-14 → top-right row */
        if (i < 7)
            out[sz - 1 - i][8] = v;
        else
            out[8][sz - 15 + i] = v;
    }
}

/* ---- Penalty evaluation ---- */

static int penalty_score(int sz, const uint8_t q[][S])
{
    int score = 0;

    /* Rule 1: runs of same color in rows & columns */
    for (int r = 0; r < sz; r++) {
        int cnt = 1;
        for (int c = 1; c < sz; c++) {
            if (q[r][c] == q[r][c - 1]) { cnt++; }
            else { if (cnt >= 5) score += cnt - 2; cnt = 1; }
        }
        if (cnt >= 5) score += cnt - 2;
    }
    for (int c = 0; c < sz; c++) {
        int cnt = 1;
        for (int r = 1; r < sz; r++) {
            if (q[r][c] == q[r - 1][c]) { cnt++; }
            else { if (cnt >= 5) score += cnt - 2; cnt = 1; }
        }
        if (cnt >= 5) score += cnt - 2;
    }

    /* Rule 2: 2×2 same-color blocks */
    for (int r = 0; r < sz - 1; r++)
        for (int c = 0; c < sz - 1; c++)
            if (q[r][c] == q[r][c + 1] &&
                q[r][c] == q[r + 1][c] &&
                q[r][c] == q[r + 1][c + 1])
                score += 3;

    /* Rule 3: finder-like patterns */
    for (int r = 0; r < sz; r++)
        for (int c = 0; c <= sz - 11; c++) {
            if (q[r][c]==1&&q[r][c+1]==0&&q[r][c+2]==1&&q[r][c+3]==1&&
                q[r][c+4]==1&&q[r][c+5]==0&&q[r][c+6]==1&&
                q[r][c+7]==0&&q[r][c+8]==0&&q[r][c+9]==0&&q[r][c+10]==0)
                score += 40;
            if (q[r][c]==0&&q[r][c+1]==0&&q[r][c+2]==0&&q[r][c+3]==0&&
                q[r][c+4]==1&&q[r][c+5]==0&&q[r][c+6]==1&&q[r][c+7]==1&&
                q[r][c+8]==1&&q[r][c+9]==0&&q[r][c+10]==1)
                score += 40;
        }
    for (int c = 0; c < sz; c++)
        for (int r = 0; r <= sz - 11; r++) {
            if (q[r][c]==1&&q[r+1][c]==0&&q[r+2][c]==1&&q[r+3][c]==1&&
                q[r+4][c]==1&&q[r+5][c]==0&&q[r+6][c]==1&&
                q[r+7][c]==0&&q[r+8][c]==0&&q[r+9][c]==0&&q[r+10][c]==0)
                score += 40;
            if (q[r][c]==0&&q[r+1][c]==0&&q[r+2][c]==0&&q[r+3][c]==0&&
                q[r+4][c]==1&&q[r+5][c]==0&&q[r+6][c]==1&&q[r+7][c]==1&&
                q[r+8][c]==1&&q[r+9][c]==0&&q[r+10][c]==1)
                score += 40;
        }

    /* Rule 4: dark/light proportion */
    int dark = 0;
    for (int r = 0; r < sz; r++)
        for (int c = 0; c < sz; c++)
            dark += q[r][c];
    int total = sz * sz;
    int pct = dark * 100 / total;
    int prev5 = (pct / 5) * 5;
    int next5 = prev5 + 5;
    int d1 = prev5 - 50; if (d1 < 0) d1 = -d1;
    int d2 = next5 - 50; if (d2 < 0) d2 = -d2;
    score += (d1 < d2 ? d1 : d2) / 5 * 10;

    return score;
}

/* ---- Public API ---- */

static uint8_t s_tmp[S][S];
static uint8_t s_best[S][S];

int qr_encode(const char *text, int len, uint8_t *modules, int *out_size)
{
    gf_init();

    /* Pick smallest version that can hold the data */
    int vi = -1;
    for (int i = 0; i < NUM_VER; i++) {
        int cap = (VER[i].data_cw * 8 - 4 - 8) / 8; /* byte mode overhead: 4-bit mode + 8-bit count */
        if (len <= cap) { vi = i; break; }
    }
    if (vi < 0) return 0;

    const VerInfo *v = &VER[vi];
    int sz = v->size;

    /* ---- Encode data into codewords (byte mode) ---- */
    uint8_t data_cw[80];
    memset(data_cw, 0, sizeof(data_cw));
    int bp = 0;

#define WR(val, cnt) do { \
    for (int _i = (cnt) - 1; _i >= 0; _i--) { \
        data_cw[bp / 8] |= (((val) >> _i) & 1) << (7 - bp % 8); \
        bp++; \
    } \
} while (0)

    WR(4, 4);                                        /* mode: byte */
    WR(len, 8);                                      /* character count */
    for (int i = 0; i < len; i++)
        WR((uint8_t)text[i], 8);                     /* data */

    /* Terminator (up to 4 bits) */
    { int rem = v->data_cw * 8 - bp;
      int t = rem < 4 ? rem : 4;
      if (t > 0) WR(0, t); }

    /* Pad to byte boundary */
    if (bp % 8) WR(0, 8 - bp % 8);

    /* Pad codewords 0xEC / 0x11 */
    { int done = bp / 8;
      for (int i = done; i < v->data_cw; i++)
          data_cw[i] = ((i - done) % 2 == 0) ? 0xEC : 0x11; }

#undef WR

    /* ---- Error correction ---- */
    uint8_t ec_cw[20];
    rs_compute_ec(data_cw, v->data_cw, v->ec_cw, ec_cw);

    /* ---- Interleave (single block → just concatenate) ---- */
    int total_cw = v->data_cw + v->ec_cw;
    uint8_t all_cw[100];
    memcpy(all_cw, data_cw, v->data_cw);
    memcpy(all_cw + v->data_cw, ec_cw, v->ec_cw);

    /* Convert to bit array */
    uint8_t bits[800];
    for (int i = 0; i < total_cw; i++)
        for (int b = 0; b < 8; b++)
            bits[i * 8 + b] = (all_cw[i] >> (7 - b)) & 1;

    /* ---- Build grid ---- */
    memset(g, 0, sizeof(g));
    memset(fm, 0, sizeof(fm));
    place_patterns(sz, v->align);
    place_data(sz, bits, total_cw * 8);

    /* ---- Select best mask ---- */
    int best_mask = 0, best_pen = 0x7FFFFFFF;
    for (int m = 0; m < 8; m++) {
        memcpy(s_tmp, g, sizeof(s_tmp));
        for (int r = 0; r < sz; r++)
            for (int c = 0; c < sz; c++)
                if (!fm[r][c] && mask_cond(m, r, c))
                    s_tmp[r][c] ^= 1;
        write_format(sz, m, s_tmp);
        int pen = penalty_score(sz, s_tmp);
        if (pen < best_pen) {
            best_pen = pen;
            best_mask = m;
            memcpy(s_best, s_tmp, sizeof(s_best));
        }
    }

    /* ---- Output ---- */
    for (int r = 0; r < sz; r++)
        for (int c = 0; c < sz; c++)
            modules[r * sz + c] = s_best[r][c];

    *out_size = sz;
    return vi + 1;
}
