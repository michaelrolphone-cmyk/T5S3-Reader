#include "T5AppApi.h"

/*
 * Minimal Mahjong draw/discard demo for T5S3-Reader native ELF apps.
 *
 * Uses only the API calls demonstrated by examples/native_apps/hello.c:
 *   clear(), draw_text(), fill_rect(), present(), poll().
 *
 * Rules intentionally kept small:
 *   - 136-tile wall (no flowers/seasons)
 *   - 14-tile hand: tap one tile to discard, then auto-draw
 *   - standard 4 melds + pair win detection
 *   - seven-pairs win detection
 *   - no opponents, calls, riichi, dora, scoring, or kan handling
 */

enum {
    TILE_TYPES = 34,
    WALL_SIZE = 136,
    HAND_MAX = 14,
    DISCARD_MAX = 24,

    SCREEN_W = 960,
    SCREEN_H = 540,

    NEW_X = 780,
    NEW_Y = 25,
    NEW_W = 150,
    NEW_H = 58,

    HAND_X = 22,
    HAND_Y = 315,
    TILE_W = 60,
    TILE_H = 105,
    TILE_GAP = 5,

    DISCARD_X = 25,
    DISCARD_Y = 188,
    DISCARD_W = 54,
    DISCARD_H = 62,
    DISCARD_GAP = 5,
    DISCARD_VISIBLE = 12
};

static unsigned char g_wall[WALL_SIZE];
static unsigned char g_hand[HAND_MAX];
static unsigned char g_discards[DISCARD_MAX];
static int g_wall_pos;
static int g_hand_count;
static int g_discard_count;
static int g_turn_count;
static unsigned int g_rng = 0x6d2b79f5u;

static const char *tile_label(unsigned char tile) {
    static const char *const labels[TILE_TYPES] = {
        "1M", "2M", "3M", "4M", "5M", "6M", "7M", "8M", "9M",
        "1P", "2P", "3P", "4P", "5P", "6P", "7P", "8P", "9P",
        "1S", "2S", "3S", "4S", "5S", "6S", "7S", "8S", "9S",
        "E", "S", "W", "N", "WH", "G", "R"
    };
    return tile < TILE_TYPES ? labels[tile] : "?";
}

static unsigned int rng_next(void) {
    /* xorshift32: tiny, deterministic, and libc-free. */
    unsigned int x = g_rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_rng = x ? x : 0x6d2b79f5u;
    return g_rng;
}

static void sort_hand(void) {
    int i;
    for (i = 1; i < g_hand_count; ++i) {
        unsigned char value = g_hand[i];
        int j = i - 1;
        while (j >= 0 && g_hand[j] > value) {
            g_hand[j + 1] = g_hand[j];
            --j;
        }
        g_hand[j + 1] = value;
    }
}

static void build_and_shuffle_wall(void) {
    int i;
    for (i = 0; i < WALL_SIZE; ++i) {
        g_wall[i] = (unsigned char)(i / 4);
    }

    for (i = WALL_SIZE - 1; i > 0; --i) {
        int j = (int)(rng_next() % (unsigned int)(i + 1));
        unsigned char tmp = g_wall[i];
        g_wall[i] = g_wall[j];
        g_wall[j] = tmp;
    }
}

static int draw_one(void) {
    if (g_hand_count >= HAND_MAX || g_wall_pos >= WALL_SIZE) return 0;
    g_hand[g_hand_count++] = g_wall[g_wall_pos++];
    sort_hand();
    return 1;
}

static void new_game(void) {
    int i;

    /* Ensure NEW does not repeat the same wall even without a clock API. */
    g_rng ^= 0x9e3779b9u + (unsigned int)(g_turn_count + 1) * 0x85ebca6bu;
    (void)rng_next();

    g_wall_pos = 0;
    g_hand_count = 0;
    g_discard_count = 0;
    g_turn_count = 0;

    build_and_shuffle_wall();

    /* Deal 13, then make the normal draw to reach a playable 14 tiles. */
    for (i = 0; i < 13; ++i) (void)draw_one();
    (void)draw_one();
}

static int sets_possible(unsigned char counts[TILE_TYPES]) {
    int i;

    for (i = 0; i < TILE_TYPES; ++i) {
        if (counts[i] != 0) break;
    }
    if (i == TILE_TYPES) return 1;

    /* Triplet. */
    if (counts[i] >= 3) {
        counts[i] -= 3;
        if (sets_possible(counts)) {
            counts[i] += 3;
            return 1;
        }
        counts[i] += 3;
    }

    /* Sequence, only inside the three numbered suits. */
    if (i < 27 && (i % 9) <= 6 &&
        counts[i + 1] != 0 && counts[i + 2] != 0) {
        --counts[i];
        --counts[i + 1];
        --counts[i + 2];
        if (sets_possible(counts)) {
            ++counts[i];
            ++counts[i + 1];
            ++counts[i + 2];
            return 1;
        }
        ++counts[i];
        ++counts[i + 1];
        ++counts[i + 2];
    }

    return 0;
}

static int is_winning_hand(void) {
    unsigned char counts[TILE_TYPES];
    int i;
    int pairs = 0;

    if (g_hand_count != HAND_MAX) return 0;

    for (i = 0; i < TILE_TYPES; ++i) counts[i] = 0;
    for (i = 0; i < HAND_MAX; ++i) ++counts[g_hand[i]];

    /* Seven distinct pairs. */
    for (i = 0; i < TILE_TYPES; ++i) {
        if (counts[i] == 2) {
            ++pairs;
        } else if (counts[i] != 0) {
            pairs = -99;
            break;
        }
    }
    if (pairs == 7) return 1;

    /* Standard hand: choose a pair, then decompose the rest into four melds. */
    for (i = 0; i < TILE_TYPES; ++i) {
        if (counts[i] >= 2) {
            counts[i] -= 2;
            if (sets_possible(counts)) {
                counts[i] += 2;
                return 1;
            }
            counts[i] += 2;
        }
    }

    return 0;
}

static void push_discard(unsigned char tile) {
    int i;

    if (g_discard_count < DISCARD_MAX) {
        g_discards[g_discard_count++] = tile;
        return;
    }

    /* Keep the newest DISCARD_MAX tiles without allocating memory. */
    for (i = 1; i < DISCARD_MAX; ++i) {
        g_discards[i - 1] = g_discards[i];
    }
    g_discards[DISCARD_MAX - 1] = tile;
}

static void discard_tile(int index) {
    int i;
    unsigned char tile;

    if (index < 0 || index >= g_hand_count) return;
    if (g_hand_count != HAND_MAX) return;

    tile = g_hand[index];
    push_discard(tile);

    for (i = index + 1; i < g_hand_count; ++i) {
        g_hand[i - 1] = g_hand[i];
    }
    --g_hand_count;
    ++g_turn_count;

    (void)draw_one();
}

static void uint_to_text(char *out, const char *prefix, unsigned int value) {
    char rev[10];
    int n = 0;
    int p = 0;

    while (prefix[p] != '\0') {
        out[p] = prefix[p];
        ++p;
    }

    if (value == 0) {
        out[p++] = '0';
    } else {
        while (value != 0 && n < (int)sizeof(rev)) {
            rev[n++] = (char)('0' + (value % 10u));
            value /= 10u;
        }
        while (n > 0) out[p++] = rev[--n];
    }
    out[p] = '\0';
}

static void draw_border(const t5_app_api_v1 *api, int x, int y, int w, int h) {
    api->fill_rect(x, y, w, 2, true);
    api->fill_rect(x, y + h - 2, w, 2, true);
    api->fill_rect(x, y, 2, h, true);
    api->fill_rect(x + w - 2, y, 2, h, true);
}

static void draw_small_tile(const t5_app_api_v1 *api,
                            int x, int y, unsigned char tile) {
    draw_border(api, x, y, DISCARD_W, DISCARD_H);
    api->draw_text(x + 13, y + 21, tile_label(tile));
}

static void draw_hand_tile(const t5_app_api_v1 *api,
                           int x, int y, unsigned char tile) {
    draw_border(api, x, y, TILE_W, TILE_H);
    api->draw_text(x + 14, y + 37, tile_label(tile));
}

static void render_game(const t5_app_api_v1 *api, int full_refresh) {
    char wall_text[20];
    char turn_text[20];
    int i;
    int start;
    int winning = is_winning_hand();

    api->clear();

    api->draw_text(25, 28, "MAHJONG - native ELF demo");
    api->draw_text(25, 70, "Tap a tile to discard; a new tile is drawn automatically.");

    draw_border(api, NEW_X, NEW_Y, NEW_W, NEW_H);
    api->draw_text(825, 47, "NEW");

    uint_to_text(wall_text, "Wall: ", (unsigned int)(WALL_SIZE - g_wall_pos));
    uint_to_text(turn_text, "Turns: ", (unsigned int)g_turn_count);
    api->draw_text(25, 112, wall_text);
    api->draw_text(180, 112, turn_text);

    if (winning) {
        api->draw_text(370, 112, "MAHJONG! Winning hand - tap NEW.");
    } else if (g_wall_pos >= WALL_SIZE && g_hand_count < HAND_MAX) {
        api->draw_text(370, 112, "Wall empty - tap NEW.");
    } else {
        api->draw_text(370, 112, "M/P/S = suits; E/S/W/N + WH/G/R = honors");
    }

    api->draw_text(25, 153, "Recent discards");
    start = g_discard_count > DISCARD_VISIBLE
              ? g_discard_count - DISCARD_VISIBLE
              : 0;
    for (i = start; i < g_discard_count; ++i) {
        int visible = i - start;
        int x = DISCARD_X + visible * (DISCARD_W + DISCARD_GAP);
        draw_small_tile(api, x, DISCARD_Y, g_discards[i]);
    }

    api->draw_text(25, 278, "Your hand");
    for (i = 0; i < g_hand_count; ++i) {
        int x = HAND_X + i * (TILE_W + TILE_GAP);
        draw_hand_tile(api, x, HAND_Y, g_hand[i]);
    }

    api->draw_text(25, 458, "Back / PWR / Home exits to the reader.");
    api->draw_text(25, 500, "Demo rules: 4 melds + pair, or seven pairs. No scoring/calls/opponents.");

    api->present(full_refresh ? true : false);
}

static int hit_rect(int x, int y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

static int hand_tile_at(int x, int y) {
    int i;

    if (y < HAND_Y || y >= HAND_Y + TILE_H) return -1;

    for (i = 0; i < g_hand_count; ++i) {
        int tx = HAND_X + i * (TILE_W + TILE_GAP);
        if (x >= tx && x < tx + TILE_W) return i;
    }
    return -1;
}

__attribute__((visibility("default"))) void app_main(void) {
    const t5_app_api_v1 *api = t5_app_get_api(T5_APP_ABI_VERSION);
    t5_app_input_t input;

    if (!api || api->struct_size < sizeof(*api)) return;

    /* A little per-load variation if the loader places the API differently. */
    g_rng ^= (unsigned int)(unsigned long)api;
    new_game();
    render_game(api, 1);

    while (api->poll(&input, 20)) {
        /* Poll timing becomes additional entropy for subsequent NEW games. */
        (void)rng_next();

        if (input.exit_requested) return;

        if (input.tapped) {
            int tile_index;

            if (hit_rect(input.touch_x, input.touch_y,
                         NEW_X, NEW_Y, NEW_W, NEW_H)) {
                g_rng ^= ((unsigned int)input.touch_x << 16) ^
                         (unsigned int)input.touch_y ^
                         (unsigned int)g_turn_count;
                new_game();
                render_game(api, 1);
                continue;
            }

            if (is_winning_hand()) continue;

            tile_index = hand_tile_at(input.touch_x, input.touch_y);
            if (tile_index >= 0) {
                discard_tile(tile_index);
                /* Periodic full refresh limits e-paper ghosting. */
                render_game(api, (g_turn_count % 8) == 0);
            }
        }
    }
}
