#include "T5AppApi.h"
#include "T5HardwareTakeover.h"
#include "display_output_client.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FPS_LOGICAL_W 540
#define FPS_LOGICAL_H 960
#define FPS_VIEW_TOP 74
#define FPS_VIEW_BOTTOM 798
#define FPS_VIEW_H (FPS_VIEW_BOTTOM - FPS_VIEW_TOP)
#define FPS_HORIZON (FPS_VIEW_TOP + FPS_VIEW_H / 2)
#define FPS_TARGET_FRAME_MS 33u
#define FPS_MAP_W 16
#define FPS_MAP_H 16
#define FPS_ENEMY_COUNT 5
#define FPS_PI 3.14159265358979323846f
#define FPS_TWO_PI 6.28318530717958647692f
#define FPS_FOV_PLANE 0.66f
#define FPS_PLAYER_RADIUS 0.18f
#define FPS_EXIT_HOLD_MS 1200u
#define FPS_FIRE_COOLDOWN_MS 240u

typedef enum {
    FPS_STATE_TITLE = 0,
    FPS_STATE_PLAYING,
    FPS_STATE_WON,
    FPS_STATE_LOST
} fps_state_t;

typedef struct {
    float x;
    float y;
    uint8_t health;
    uint32_t next_attack_ms;
    uint32_t hit_until_ms;
    bool alive;
} fps_enemy_t;

static const char *const g_map[FPS_MAP_H] = {
    "################",
    "#..............#",
    "#.##.######.##.#",
    "#.#..........#.#",
    "#.#.##.##.##.#.#",
    "#...#......#...#",
    "###.#.####.#.###",
    "#...#......#...#",
    "#.###.####.###.#",
    "#..............#",
    "#.###.####.###.#",
    "#...#......#...#",
    "###.#.####.#.###",
    "#...#......#...#",
    "#..............#",
    "################"
};

static const float g_enemy_starts[FPS_ENEMY_COUNT][2] = {
    {13.5f, 1.5f},
    {8.5f, 3.5f},
    {5.5f, 7.5f},
    {12.5f, 9.5f},
    {2.5f, 14.5f}
};

static const t5_app_api_v1 *g_app;
static const display_client_api *g_video;
static display_client_surface g_surface;
static fps_enemy_t g_enemies[FPS_ENEMY_COUNT];
static float g_depth[FPS_LOGICAL_W];
static float g_player_x;
static float g_player_y;
static float g_player_angle;
static int g_health;
static uint32_t g_score;
static uint32_t g_prev_buttons;
static uint32_t g_next_fire_ms;
static uint32_t g_muzzle_until_ms;
static uint32_t g_damage_until_ms;
static uint32_t g_back_hold_start_ms;
static bool g_back_holding;
static uint32_t g_last_frame_ms;
static fps_state_t g_state;

static int fps_iabs(int value) { return value < 0 ? -value : value; }
static int fps_min_i(int a, int b) { return a < b ? a : b; }
static int fps_max_i(int a, int b) { return a > b ? a : b; }
static float fps_absf(float value) { return value < 0.0f ? -value : value; }
static float fps_minf(float a, float b) { return a < b ? a : b; }
static float fps_maxf(float a, float b) { return a > b ? a : b; }
static float fps_clampf(float value, float lo, float hi) {
    return value < lo ? lo : (value > hi ? hi : value);
}

static float fps_wrap_angle(float angle) {
    while (angle > FPS_PI) angle -= FPS_TWO_PI;
    while (angle < -FPS_PI) angle += FPS_TWO_PI;
    return angle;
}

/* The native app ABI deliberately does not expose libm. These bounded
 * approximations are accurate enough for camera movement and projection. */
static float fps_sin(float angle) {
    angle = fps_wrap_angle(angle);
    const float angle2 = angle * angle;
    return angle * (1.0f - angle2 *
                    (0.16666667f - angle2 *
                     (0.0083333310f - angle2 * 0.00019840874f)));
}

static float fps_cos(float angle) {
    return fps_sin(angle + 1.57079632679489661923f);
}

static float fps_fast_length(float x, float y) {
    const float ax = fps_absf(x);
    const float ay = fps_absf(y);
    const float hi = fps_maxf(ax, ay);
    const float lo = fps_minf(ax, ay);
    return hi + lo * 0.41421356f;
}

static bool fps_time_reached(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}

static bool fps_api_has(size_t offset, size_t member_size) {
    return g_app && g_app->struct_size >= offset + member_size;
}

static void fps_log(const char *message) {
    if (fps_api_has(offsetof(t5_app_api_v1, log_message), sizeof(g_app->log_message)) &&
        g_app->log_message) {
        g_app->log_message(message);
    }
}

/* The video surface is physical landscape 960x540. The game uses the same
 * portrait mapping as the model viewer and GameBoy:
 * logical (x,y) -> panel (y, 539-x). */
static void fps_pixel(uint8_t *buffer, int x, int y, bool black) {
    if (!buffer || x < 0 || y < 0 || x >= FPS_LOGICAL_W || y >= FPS_LOGICAL_H) return;
    const int panel_x = y;
    const int panel_y = (FPS_LOGICAL_W - 1) - x;
    const size_t offset =
        (size_t)panel_y * g_surface.stride_bytes + (size_t)(panel_x >> 3);
    const uint8_t mask = (uint8_t)(0x80u >> (panel_x & 7));
    if (black) buffer[offset] |= mask;
    else buffer[offset] &= (uint8_t)~mask;
}

static void fps_vspan(uint8_t *buffer, int x, int y0, int y1) {
    if (x < 0 || x >= FPS_LOGICAL_W || y1 < 0 || y0 >= FPS_LOGICAL_H) return;
    y0 = fps_max_i(y0, 0);
    y1 = fps_min_i(y1, FPS_LOGICAL_H - 1);
    if (y0 > y1) return;

    uint8_t *row = buffer +
        (size_t)((FPS_LOGICAL_W - 1) - x) * g_surface.stride_bytes;
    const int first_byte = y0 >> 3;
    const int last_byte = y1 >> 3;
    const uint8_t first_mask = (uint8_t)(0xffu >> (y0 & 7));
    const uint8_t last_mask = (uint8_t)(0xffu << (7 - (y1 & 7)));

    if (first_byte == last_byte) {
        row[first_byte] |= (uint8_t)(first_mask & last_mask);
        return;
    }
    row[first_byte] |= first_mask;
    for (int byte = first_byte + 1; byte < last_byte; ++byte) row[byte] = 0xffu;
    row[last_byte] |= last_mask;
}

static void fps_clear_vspan(uint8_t *buffer, int x, int y0, int y1) {
    if (x < 0 || x >= FPS_LOGICAL_W || y1 < 0 || y0 >= FPS_LOGICAL_H) return;
    y0 = fps_max_i(y0, 0);
    y1 = fps_min_i(y1, FPS_LOGICAL_H - 1);
    if (y0 > y1) return;

    uint8_t *row = buffer +
        (size_t)((FPS_LOGICAL_W - 1) - x) * g_surface.stride_bytes;
    const int first_byte = y0 >> 3;
    const int last_byte = y1 >> 3;
    const uint8_t first_mask = (uint8_t)(0xffu >> (y0 & 7));
    const uint8_t last_mask = (uint8_t)(0xffu << (7 - (y1 & 7)));

    if (first_byte == last_byte) {
        row[first_byte] &= (uint8_t)~(first_mask & last_mask);
        return;
    }
    row[first_byte] &= (uint8_t)~first_mask;
    for (int byte = first_byte + 1; byte < last_byte; ++byte) row[byte] = 0x00u;
    row[last_byte] &= (uint8_t)~last_mask;
}

static void fps_view_vspan(uint8_t *buffer, int x, int y0, int y1) {
    y0 = fps_max_i(y0, FPS_VIEW_TOP);
    y1 = fps_min_i(y1, FPS_VIEW_BOTTOM);
    if (y0 <= y1) fps_vspan(buffer, x, y0, y1);
}

static void fps_view_pixel(uint8_t *buffer, int x, int y) {
    if (y >= FPS_VIEW_TOP && y <= FPS_VIEW_BOTTOM) fps_pixel(buffer, x, y, true);
}

static void fps_hline(uint8_t *buffer, int x0, int x1, int y) {
    if (y < 0 || y >= FPS_LOGICAL_H) return;
    x0 = fps_max_i(x0, 0);
    x1 = fps_min_i(x1, FPS_LOGICAL_W - 1);
    for (int x = x0; x <= x1; ++x) fps_pixel(buffer, x, y, true);
}

static void fps_rect(uint8_t *buffer, int x, int y, int width, int height) {
    if (width <= 0 || height <= 0) return;
    for (int px = x; px < x + width; ++px) fps_vspan(buffer, px, y, y + height - 1);
}

static void fps_line(uint8_t *buffer, int x0, int y0, int x1, int y1) {
    int dx = fps_iabs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -fps_iabs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    for (int guard = 0; guard < 1600; ++guard) {
        fps_pixel(buffer, x0, y0, true);
        if (x0 == x1 && y0 == y1) break;
        const int twice = error * 2;
        if (twice >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

static const uint8_t *fps_glyph(char ch) {
    static const uint8_t blank[5] = {0, 0, 0, 0, 0};
    static const uint8_t digits[10][5] = {
        {0x3e,0x51,0x49,0x45,0x3e},{0x00,0x42,0x7f,0x40,0x00},
        {0x62,0x51,0x49,0x49,0x46},{0x22,0x49,0x49,0x49,0x36},
        {0x18,0x14,0x12,0x7f,0x10},{0x2f,0x49,0x49,0x49,0x31},
        {0x3e,0x49,0x49,0x49,0x32},{0x01,0x71,0x09,0x05,0x03},
        {0x36,0x49,0x49,0x49,0x36},{0x26,0x49,0x49,0x49,0x3e}
    };
    static const uint8_t letters[26][5] = {
        {0x7e,0x09,0x09,0x09,0x7e},{0x7f,0x49,0x49,0x49,0x36},
        {0x3e,0x41,0x41,0x41,0x22},{0x7f,0x41,0x41,0x22,0x1c},
        {0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x09,0x01},
        {0x3e,0x41,0x49,0x49,0x7a},{0x7f,0x08,0x08,0x08,0x7f},
        {0x00,0x41,0x7f,0x41,0x00},{0x20,0x40,0x41,0x3f,0x01},
        {0x7f,0x08,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
        {0x7f,0x02,0x0c,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},
        {0x3e,0x41,0x41,0x41,0x3e},{0x7f,0x09,0x09,0x09,0x06},
        {0x3e,0x41,0x51,0x21,0x5e},{0x7f,0x09,0x19,0x29,0x46},
        {0x26,0x49,0x49,0x49,0x32},{0x01,0x01,0x7f,0x01,0x01},
        {0x3f,0x40,0x40,0x40,0x3f},{0x1f,0x20,0x40,0x20,0x1f},
        {0x7f,0x20,0x18,0x20,0x7f},{0x63,0x14,0x08,0x14,0x63},
        {0x03,0x04,0x78,0x04,0x03},{0x61,0x51,0x49,0x45,0x43}
    };
    static const uint8_t dash[5] = {0x08,0x08,0x08,0x08,0x08};
    static const uint8_t dot[5] = {0x00,0x60,0x60,0x00,0x00};
    static const uint8_t slash[5] = {0x20,0x10,0x08,0x04,0x02};
    static const uint8_t colon[5] = {0x00,0x36,0x36,0x00,0x00};
    static const uint8_t plus[5] = {0x08,0x08,0x3e,0x08,0x08};
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
    if (ch >= 'A' && ch <= 'Z') return letters[ch - 'A'];
    if (ch >= '0' && ch <= '9') return digits[ch - '0'];
    if (ch == '-') return dash;
    if (ch == '.') return dot;
    if (ch == '/') return slash;
    if (ch == ':') return colon;
    if (ch == '+') return plus;
    return blank;
}

static void fps_text(uint8_t *buffer, int x, int y, const char *text, int scale) {
    if (!text || scale <= 0) return;
    for (const char *p = text; *p; ++p) {
        const uint8_t *glyph = fps_glyph(*p);
        for (int cx = 0; cx < 5; ++cx) {
            for (int cy = 0; cy < 7; ++cy) {
                if ((glyph[cx] & (1u << cy)) == 0u) continue;
                fps_rect(buffer, x + cx * scale, y + cy * scale, scale, scale);
            }
        }
        x += 6 * scale;
    }
}

static void fps_frame(uint8_t *buffer, int x, int y, int width, int height) {
    fps_hline(buffer, x, x + width - 1, y);
    fps_hline(buffer, x, x + width - 1, y + height - 1);
    fps_vspan(buffer, x, y, y + height - 1);
    fps_vspan(buffer, x + width - 1, y, y + height - 1);
}

static bool fps_cell_open(int x, int y) {
    return x >= 0 && y >= 0 && x < FPS_MAP_W && y < FPS_MAP_H &&
           g_map[y][x] != '#';
}

static bool fps_position_open(float x, float y, float radius) {
    return fps_cell_open((int)(x - radius), (int)(y - radius)) &&
           fps_cell_open((int)(x + radius), (int)(y - radius)) &&
           fps_cell_open((int)(x - radius), (int)(y + radius)) &&
           fps_cell_open((int)(x + radius), (int)(y + radius));
}

static void fps_try_move(float dx, float dy) {
    const float next_x = g_player_x + dx;
    const float next_y = g_player_y + dy;
    if (fps_position_open(next_x, g_player_y, FPS_PLAYER_RADIUS)) g_player_x = next_x;
    if (fps_position_open(g_player_x, next_y, FPS_PLAYER_RADIUS)) g_player_y = next_y;
}

static bool fps_line_of_sight(float ax, float ay, float bx, float by) {
    const float dx = bx - ax;
    const float dy = by - ay;
    const float distance = fps_fast_length(dx, dy);
    int steps = (int)(distance * 7.0f);
    if (steps < 1) steps = 1;
    if (steps > 112) steps = 112;
    for (int step = 1; step < steps; ++step) {
        const float fraction = (float)step / (float)steps;
        const int x = (int)(ax + dx * fraction);
        const int y = (int)(ay + dy * fraction);
        if (!fps_cell_open(x, y)) return false;
    }
    return true;
}

static int fps_alive_count(void) {
    int alive = 0;
    for (int i = 0; i < FPS_ENEMY_COUNT; ++i) if (g_enemies[i].alive) ++alive;
    return alive;
}

static void fps_reset_game(void) {
    g_player_x = 1.5f;
    g_player_y = 1.5f;
    g_player_angle = 0.0f;
    g_health = 100;
    g_score = 0;
    g_next_fire_ms = 0;
    g_muzzle_until_ms = 0;
    g_damage_until_ms = 0;
    g_back_hold_start_ms = 0;
    g_back_holding = false;
    for (int i = 0; i < FPS_ENEMY_COUNT; ++i) {
        g_enemies[i].x = g_enemy_starts[i][0];
        g_enemies[i].y = g_enemy_starts[i][1];
        g_enemies[i].health = 2u;
        g_enemies[i].next_attack_ms = 0;
        g_enemies[i].hit_until_ms = 0;
        g_enemies[i].alive = true;
    }
    g_state = FPS_STATE_PLAYING;
}

static void fps_update_enemies(float dt, uint32_t now) {
    for (int i = 0; i < FPS_ENEMY_COUNT; ++i) {
        fps_enemy_t *enemy = &g_enemies[i];
        if (!enemy->alive) continue;
        const float dx = g_player_x - enemy->x;
        const float dy = g_player_y - enemy->y;
        const float distance = fps_fast_length(dx, dy);
        const bool sees_player =
            distance < 7.0f && fps_line_of_sight(enemy->x, enemy->y, g_player_x, g_player_y);

        if (sees_player && distance > 1.05f) {
            const float speed = 0.46f * dt;
            const float inverse = distance > 0.001f ? 1.0f / distance : 0.0f;
            const float move_x = dx * inverse * speed;
            const float move_y = dy * inverse * speed;
            const float next_x = enemy->x + move_x;
            const float next_y = enemy->y + move_y;
            if (fps_position_open(next_x, enemy->y, 0.16f)) enemy->x = next_x;
            if (fps_position_open(enemy->x, next_y, 0.16f)) enemy->y = next_y;
        }

        if (sees_player && distance < 1.18f && fps_time_reached(now, enemy->next_attack_ms)) {
            enemy->next_attack_ms = now + 850u;
            g_health -= 12;
            g_damage_until_ms = now + 180u;
            if (g_health <= 0) {
                g_health = 0;
                g_state = FPS_STATE_LOST;
                return;
            }
        }
    }
}

static void fps_fire(uint32_t now) {
    if (!fps_time_reached(now, g_next_fire_ms)) return;
    g_next_fire_ms = now + FPS_FIRE_COOLDOWN_MS;
    g_muzzle_until_ms = now + 90u;

    const float dir_x = fps_cos(g_player_angle);
    const float dir_y = fps_sin(g_player_angle);
    const float plane_x = -dir_y * FPS_FOV_PLANE;
    const float plane_y = dir_x * FPS_FOV_PLANE;
    const float determinant = plane_x * dir_y - dir_x * plane_y;
    if (fps_absf(determinant) < 0.001f) return;
    const float inv_det = 1.0f / determinant;

    int target = -1;
    float best_depth = 1000.0f;
    for (int i = 0; i < FPS_ENEMY_COUNT; ++i) {
        fps_enemy_t *enemy = &g_enemies[i];
        if (!enemy->alive) continue;
        const float rel_x = enemy->x - g_player_x;
        const float rel_y = enemy->y - g_player_y;
        const float camera_x = inv_det * (dir_y * rel_x - dir_x * rel_y);
        const float camera_depth = inv_det * (-plane_y * rel_x + plane_x * rel_y);
        if (camera_depth <= 0.15f) continue;
        const int screen_x =
            (int)((float)FPS_LOGICAL_W * 0.5f * (1.0f + camera_x / camera_depth));
        const int hit_radius = (int)(80.0f / camera_depth) + 8;
        if (fps_iabs(screen_x - FPS_LOGICAL_W / 2) > hit_radius) continue;
        if (!fps_line_of_sight(g_player_x, g_player_y, enemy->x, enemy->y)) continue;
        if (camera_depth < best_depth) {
            best_depth = camera_depth;
            target = i;
        }
    }

    if (target >= 0) {
        fps_enemy_t *enemy = &g_enemies[target];
        enemy->hit_until_ms = now + 160u;
        if (enemy->health > 0u) --enemy->health;
        if (enemy->health == 0u) {
            enemy->alive = false;
            g_score += 100u;
            if (fps_alive_count() == 0) g_state = FPS_STATE_WON;
        } else {
            g_score += 25u;
        }
    }
}

static void fps_draw_wall_column(uint8_t *buffer, int x, int y0, int y1,
                                 float distance, int texture, bool side) {
    y0 = fps_max_i(y0, FPS_VIEW_TOP);
    y1 = fps_min_i(y1, FPS_VIEW_BOTTOM);
    if (y0 > y1) return;
    fps_clear_vspan(buffer, x, y0, y1);

    if (distance < 1.35f) {
        for (int y = y0; y <= y1; ++y) {
            const bool mortar = ((y + texture * 3) & 31) < 2;
            const bool seam = texture == 0;
            if (!mortar && !seam) fps_pixel(buffer, x, y, true);
        }
    } else if (distance < 3.4f) {
        for (int y = y0; y <= y1; ++y) {
            const bool black = ((((y >> 3) + (texture >> 1) + (side ? 1 : 0)) & 1) == 0);
            if (black) fps_pixel(buffer, x, y, true);
        }
    } else if (distance < 6.5f) {
        for (int y = y0; y <= y1; y += 3) {
            if ((((y >> 3) + texture + (side ? 1 : 0)) & 1) == 0)
                fps_pixel(buffer, x, y, true);
        }
        fps_pixel(buffer, x, y0, true);
        fps_pixel(buffer, x, y1, true);
    } else {
        for (int y = y0; y <= y1; y += 6) fps_pixel(buffer, x, y, true);
    }
}

static void fps_cast_walls(uint8_t *buffer) {
    const float dir_x = fps_cos(g_player_angle);
    const float dir_y = fps_sin(g_player_angle);
    const float plane_x = -dir_y * FPS_FOV_PLANE;
    const float plane_y = dir_x * FPS_FOV_PLANE;

    for (int x = 0; x < FPS_LOGICAL_W; x += 2) {
        const float camera = 2.0f * (float)x / (float)FPS_LOGICAL_W - 1.0f;
        const float ray_x = dir_x + plane_x * camera;
        const float ray_y = dir_y + plane_y * camera;
        int map_x = (int)g_player_x;
        int map_y = (int)g_player_y;
        const float delta_x = fps_absf(ray_x) < 0.00001f ? 100000.0f : fps_absf(1.0f / ray_x);
        const float delta_y = fps_absf(ray_y) < 0.00001f ? 100000.0f : fps_absf(1.0f / ray_y);
        int step_x;
        int step_y;
        float side_x;
        float side_y;

        if (ray_x < 0.0f) {
            step_x = -1;
            side_x = (g_player_x - (float)map_x) * delta_x;
        } else {
            step_x = 1;
            side_x = ((float)map_x + 1.0f - g_player_x) * delta_x;
        }
        if (ray_y < 0.0f) {
            step_y = -1;
            side_y = (g_player_y - (float)map_y) * delta_y;
        } else {
            step_y = 1;
            side_y = ((float)map_y + 1.0f - g_player_y) * delta_y;
        }

        bool side = false;
        for (int step = 0; step < 32; ++step) {
            if (side_x < side_y) {
                side_x += delta_x;
                map_x += step_x;
                side = false;
            } else {
                side_y += delta_y;
                map_y += step_y;
                side = true;
            }
            if (!fps_cell_open(map_x, map_y)) break;
        }

        float distance = side ? side_y - delta_y : side_x - delta_x;
        distance = fps_clampf(distance, 0.08f, 100.0f);
        int line_height = (int)((float)FPS_VIEW_H / distance);
        if (line_height > FPS_VIEW_H * 3) line_height = FPS_VIEW_H * 3;
        const int y0 = FPS_HORIZON - line_height / 2;
        const int y1 = FPS_HORIZON + line_height / 2;

        const float wall_hit = side
            ? g_player_x + distance * ray_x
            : g_player_y + distance * ray_y;
        const int wall_cell = (int)wall_hit;
        int texture = (int)((wall_hit - (float)wall_cell) * 8.0f);
        if (texture < 0) texture = -texture;
        texture &= 7;

        fps_draw_wall_column(buffer, x, y0, y1, distance, texture, side);
        g_depth[x] = distance;
        if (x + 1 < FPS_LOGICAL_W) {
            fps_draw_wall_column(buffer, x + 1, y0, y1, distance, texture, side);
            g_depth[x + 1] = distance;
        }
    }
}

static void fps_draw_enemy(uint8_t *buffer, const fps_enemy_t *enemy,
                           float dir_x, float dir_y, float plane_x, float plane_y,
                           uint32_t now) {
    const float determinant = plane_x * dir_y - dir_x * plane_y;
    if (fps_absf(determinant) < 0.001f) return;
    const float inv_det = 1.0f / determinant;
    const float rel_x = enemy->x - g_player_x;
    const float rel_y = enemy->y - g_player_y;
    const float transform_x = inv_det * (dir_y * rel_x - dir_x * rel_y);
    const float transform_y = inv_det * (-plane_y * rel_x + plane_x * rel_y);
    if (transform_y <= 0.16f) return;

    const int center_x =
        (int)((float)FPS_LOGICAL_W * 0.5f * (1.0f + transform_x / transform_y));
    int sprite_h = (int)((float)FPS_VIEW_H * 0.72f / transform_y);
    sprite_h = fps_min_i(sprite_h, FPS_VIEW_H * 2);
    if (sprite_h < 8) return;
    const int sprite_w = fps_max_i(sprite_h / 2, 6);
    const int top = FPS_HORIZON - sprite_h / 2;
    const int bottom = FPS_HORIZON + sprite_h / 2;
    const int left = center_x - sprite_w / 2;
    const int right = center_x + sprite_w / 2;
    const int head_radius = fps_max_i(sprite_w / 4, 2);
    const int head_y = top + head_radius + sprite_h / 12;
    const int torso_top = head_y + head_radius;
    const int torso_bottom = top + (sprite_h * 3) / 4;
    const bool hit = !fps_time_reached(now, enemy->hit_until_ms);

    for (int x = left; x <= right; ++x) {
        if (x < 0 || x >= FPS_LOGICAL_W || transform_y >= g_depth[x]) continue;
        const int dx = x - center_x;
        const int adx = fps_iabs(dx);

        if (adx <= head_radius) {
            const int extent = head_radius - adx / 2;
            if (hit) {
                fps_view_pixel(buffer, x, head_y - extent);
                fps_view_pixel(buffer, x, head_y + extent);
            } else {
                fps_view_vspan(buffer, x, head_y - extent, head_y + extent);
            }
        }

        const int torso_half = fps_max_i(2, sprite_w / 2 -
            ((fps_max_i(0, torso_bottom - torso_top)) > 0
                ? fps_iabs(dx) / 4 : 0));
        if (adx <= torso_half) {
            if (hit) {
                if (x == center_x - torso_half || x == center_x + torso_half)
                    fps_view_vspan(buffer, x, torso_top, torso_bottom);
                else {
                    fps_view_pixel(buffer, x, torso_top);
                    fps_view_pixel(buffer, x, torso_bottom);
                }
            } else if (((x + center_x) & 3) != 0) {
                fps_view_vspan(buffer, x, torso_top, torso_bottom);
            }
        }

        const int leg_top = torso_bottom;
        const int leg_bottom = fps_min_i(bottom, FPS_VIEW_BOTTOM);
        const int leg_offset = fps_max_i(sprite_w / 6, 1);
        const int leg_width = fps_max_i(sprite_w / 10, 1);
        if (fps_iabs(dx - leg_offset) <= leg_width ||
            fps_iabs(dx + leg_offset) <= leg_width) {
            if (!hit || ((x + now / 40u) & 1u) == 0u)
                fps_view_vspan(buffer, x, leg_top, leg_bottom);
        }
    }
}

static void fps_draw_enemies(uint8_t *buffer, uint32_t now) {
    const float dir_x = fps_cos(g_player_angle);
    const float dir_y = fps_sin(g_player_angle);
    const float plane_x = -dir_y * FPS_FOV_PLANE;
    const float plane_y = dir_x * FPS_FOV_PLANE;
    int order[FPS_ENEMY_COUNT];
    float distance[FPS_ENEMY_COUNT];

    for (int i = 0; i < FPS_ENEMY_COUNT; ++i) {
        order[i] = i;
        const float dx = g_enemies[i].x - g_player_x;
        const float dy = g_enemies[i].y - g_player_y;
        distance[i] = dx * dx + dy * dy;
    }
    for (int i = 0; i < FPS_ENEMY_COUNT - 1; ++i) {
        int farthest = i;
        for (int j = i + 1; j < FPS_ENEMY_COUNT; ++j)
            if (distance[order[j]] > distance[order[farthest]]) farthest = j;
        const int swap = order[i];
        order[i] = order[farthest];
        order[farthest] = swap;
    }
    for (int i = 0; i < FPS_ENEMY_COUNT; ++i) {
        const fps_enemy_t *enemy = &g_enemies[order[i]];
        if (enemy->alive)
            fps_draw_enemy(buffer, enemy, dir_x, dir_y, plane_x, plane_y, now);
    }
}

static void fps_draw_world(uint8_t *buffer, uint32_t now) {
    /* Minimal perspective grid keeps the 1-bit floor readable without
     * obscuring enemies or adding a second framebuffer. */
    fps_hline(buffer, 0, FPS_LOGICAL_W - 1, FPS_HORIZON);
    static const int floor_rows[] = {FPS_HORIZON + 42, FPS_HORIZON + 96,
                                     FPS_HORIZON + 166, FPS_HORIZON + 252,
                                     FPS_VIEW_BOTTOM};
    for (unsigned i = 0; i < sizeof(floor_rows) / sizeof(floor_rows[0]); ++i)
        fps_hline(buffer, 0, FPS_LOGICAL_W - 1, floor_rows[i]);
    for (int x = 0; x <= FPS_LOGICAL_W; x += 90)
        fps_line(buffer, FPS_LOGICAL_W / 2, FPS_HORIZON, x, FPS_VIEW_BOTTOM);

    fps_cast_walls(buffer);
    fps_draw_enemies(buffer, now);

    const int cx = FPS_LOGICAL_W / 2;
    const int cy = FPS_HORIZON;
    fps_hline(buffer, cx - 14, cx - 4, cy);
    fps_hline(buffer, cx + 4, cx + 14, cy);
    fps_vspan(buffer, cx, cy - 14, cy - 4);
    fps_vspan(buffer, cx, cy + 4, cy + 14);

    /* Blocky first-person weapon silhouette. */
    fps_rect(buffer, cx - 18, FPS_VIEW_BOTTOM - 58, 36, 58);
    fps_rect(buffer, cx - 8, FPS_VIEW_BOTTOM - 94, 16, 40);
    fps_rect(buffer, cx - 4, FPS_VIEW_BOTTOM - 108, 8, 16);
    if (!fps_time_reached(now, g_muzzle_until_ms)) {
        fps_line(buffer, cx, FPS_VIEW_BOTTOM - 112, cx - 20, FPS_VIEW_BOTTOM - 140);
        fps_line(buffer, cx, FPS_VIEW_BOTTOM - 112, cx + 20, FPS_VIEW_BOTTOM - 140);
        fps_line(buffer, cx - 20, FPS_VIEW_BOTTOM - 140, cx, FPS_VIEW_BOTTOM - 132);
        fps_line(buffer, cx + 20, FPS_VIEW_BOTTOM - 140, cx, FPS_VIEW_BOTTOM - 132);
    }
}

static void fps_draw_hud(uint8_t *buffer, uint32_t now) {
    char line[72];
    fps_frame(buffer, 8, 814, FPS_LOGICAL_W - 16, 136);
    fps_text(buffer, 20, 826, "RISC STRIKE", 2);
    snprintf(line, sizeof(line), "HEALTH %d", g_health);
    fps_text(buffer, 20, 860, line, 2);
    snprintf(line, sizeof(line), "TARGETS %d  SCORE %lu",
             fps_alive_count(), (unsigned long)g_score);
    fps_text(buffer, 20, 894, line, 2);
    fps_text(buffer, 20, 928, "A FIRE  B+L/R STRAFE  HOLD B EXIT", 1);

    if (!fps_time_reached(now, g_damage_until_ms)) {
        fps_frame(buffer, 2, FPS_VIEW_TOP + 2, FPS_LOGICAL_W - 4,
                  FPS_VIEW_BOTTOM - FPS_VIEW_TOP - 4);
        fps_frame(buffer, 6, FPS_VIEW_TOP + 6, FPS_LOGICAL_W - 12,
                  FPS_VIEW_BOTTOM - FPS_VIEW_TOP - 12);
    }

    if (g_back_holding) {
        const uint32_t held = now - g_back_hold_start_ms;
        const int width = (int)fps_min_i((int)(held * 180u / FPS_EXIT_HOLD_MS), 180);
        fps_frame(buffer, 348, 924, 180, 16);
        if (width > 2) fps_rect(buffer, 350, 926, width - 2, 12);
    }
}

static void fps_draw_title(uint8_t *buffer) {
    fps_frame(buffer, 20, 86, FPS_LOGICAL_W - 40, 788);
    fps_text(buffer, 72, 142, "RISC STRIKE", 5);
    fps_text(buffer, 126, 226, "3D FIRST PERSON SHOOTER", 2);

    /* Stylized corridor preview. */
    fps_frame(buffer, 86, 306, 368, 304);
    fps_line(buffer, 86, 306, 220, 410);
    fps_line(buffer, 454, 306, 320, 410);
    fps_line(buffer, 86, 610, 220, 506);
    fps_line(buffer, 454, 610, 320, 506);
    fps_frame(buffer, 220, 410, 100, 96);
    fps_rect(buffer, 252, 432, 36, 74);
    fps_hline(buffer, 80, 460, 458);
    fps_vspan(buffer, 270, 438, 478);
    fps_hline(buffer, 250, 290, 458);

    fps_text(buffer, 144, 660, "A  START / FIRE", 2);
    fps_text(buffer, 96, 704, "D-PAD  MOVE / TURN", 2);
    fps_text(buffer, 66, 748, "B + LEFT/RIGHT  STRAFE", 2);
    fps_text(buffer, 120, 792, "HOLD B  EXIT", 2);
    fps_text(buffer, 174, 842, "VERSION 1.0.0", 1);
}

static void fps_draw_end(uint8_t *buffer, bool won) {
    fps_frame(buffer, 24, 130, FPS_LOGICAL_W - 48, 650);
    fps_text(buffer, won ? 102 : 132, 220, won ? "LEVEL CLEAR" : "GAME OVER", 5);
    char line[48];
    snprintf(line, sizeof(line), "SCORE %lu", (unsigned long)g_score);
    fps_text(buffer, 156, 346, line, 3);
    fps_text(buffer, 128, 500, "A  PLAY AGAIN", 3);
    fps_text(buffer, 158, 568, "B  EXIT", 3);
    fps_text(buffer, 92, 706, won ? "ALL TARGETS ELIMINATED" : "THE TARGETS GOT YOU", 2);
}

static bool fps_render(uint32_t now) {
    if (!g_video->can_submit()) return false;
    size_t bytes = 0;
    uint8_t *buffer = g_video->backbuffer(&bytes);
    const size_t required = (size_t)g_surface.stride_bytes * g_surface.height;
    if (!buffer || bytes < required) return false;
    memset(buffer, 0x00, bytes);

    if (g_state == FPS_STATE_TITLE) {
        fps_draw_title(buffer);
    } else if (g_state == FPS_STATE_PLAYING) {
        fps_draw_world(buffer, now);
        fps_draw_hud(buffer, now);
    } else {
        fps_draw_end(buffer, g_state == FPS_STATE_WON);
    }
    return g_video->submit(0, g_surface.height);
}

static bool fps_back_requested(uint32_t buttons, uint32_t now) {
    const bool back = (buttons & T5_APP_BUTTON_BACK) != 0u;
    const bool directional =
        (buttons & (T5_APP_BUTTON_LEFT | T5_APP_BUTTON_RIGHT |
                    T5_APP_BUTTON_UP | T5_APP_BUTTON_DOWN)) != 0u;
    if (!back || directional) {
        g_back_holding = false;
        g_back_hold_start_ms = 0u;
        return false;
    }
    if (g_state != FPS_STATE_PLAYING) return true;
    if (!g_back_holding) {
        g_back_holding = true;
        g_back_hold_start_ms = now;
        return false;
    }
    return now - g_back_hold_start_ms >= FPS_EXIT_HOLD_MS;
}

static bool fps_update_input(uint32_t buttons, uint32_t down,
                             uint32_t now, float dt) {
    if (fps_back_requested(buttons, now)) return false;

    if (g_state == FPS_STATE_TITLE || g_state == FPS_STATE_WON ||
        g_state == FPS_STATE_LOST) {
        if (down & T5_APP_BUTTON_CONFIRM) fps_reset_game();
        return true;
    }

    const bool strafe = (buttons & T5_APP_BUTTON_BACK) != 0u;
    const float forward = ((buttons & T5_APP_BUTTON_UP) ? 1.0f : 0.0f) -
                          ((buttons & T5_APP_BUTTON_DOWN) ? 1.0f : 0.0f);
    const float horizontal = ((buttons & T5_APP_BUTTON_RIGHT) ? 1.0f : 0.0f) -
                             ((buttons & T5_APP_BUTTON_LEFT) ? 1.0f : 0.0f);

    if (strafe) {
        const float side_speed = horizontal * 1.45f * dt;
        fps_try_move(-fps_sin(g_player_angle) * side_speed,
                     fps_cos(g_player_angle) * side_speed);
    } else {
        g_player_angle = fps_wrap_angle(g_player_angle + horizontal * 2.2f * dt);
    }

    if (forward != 0.0f) {
        const float move = forward * 1.8f * dt;
        fps_try_move(fps_cos(g_player_angle) * move,
                     fps_sin(g_player_angle) * move);
    }

    if (down & T5_APP_BUTTON_CONFIRM) fps_fire(now);
    return true;
}

static void fps_show_video_error(void) {
    if (g_surface.width != 960u || g_surface.height != 540u ||
        g_surface.stride_bytes < 120u ||
        g_surface.pixel_format != DISPLAY_CLIENT_MONO1) {
        return;
    }
    size_t bytes = 0;
    uint8_t *buffer = g_video->backbuffer(&bytes);
    if (!buffer || bytes < (size_t)g_surface.stride_bytes * g_surface.height) return;
    memset(buffer, 0x00, bytes);
    fps_text(buffer, 50, 350, "RISC STRIKE", 4);
    fps_text(buffer, 54, 430, "UNSUPPORTED VIDEO SURFACE", 2);
    fps_text(buffer, 114, 500, "B  EXIT", 2);
    if (g_video->can_submit()) (void)g_video->submit(0, g_surface.height);
}

__attribute__((visibility("default"))) uint32_t app_hardware_takeover(void) {
    return T5_HARDWARE_TAKEOVER_DISPLAY;
}

__attribute__((visibility("default"))) void app_main(void) {
    g_app = t5_app_get_api(T5_APP_ABI_VERSION);
    g_video = display_client_get_api();
    if (!g_app || !g_video ||
        !g_app->poll || !g_app->millis || !g_video->start ||
        !g_video->backbuffer || !g_video->can_submit ||
        !g_video->submit || !g_video->stop) {
        return;
    }

    const bool can_configure_back =
        fps_api_has(offsetof(t5_app_api_v1, set_back_exits_app),
                    sizeof(g_app->set_back_exits_app)) &&
        g_app->set_back_exits_app;
    if (can_configure_back) g_app->set_back_exits_app(false);

    memset(&g_surface, 0, sizeof(g_surface));
    if (!g_video->start(&g_surface)) {
        if (can_configure_back) g_app->set_back_exits_app(true);
        return;
    }

    if (g_surface.width != 960u || g_surface.height != 540u ||
        g_surface.stride_bytes != 120u ||
        g_surface.pixel_format != DISPLAY_CLIENT_MONO1 ||
        (g_surface.flags & DISPLAY_CLIENT_ONE_IS_BLACK) == 0u) {
        fps_show_video_error();
        const uint32_t deadline = g_app->millis() + 2500u;
        while (!fps_time_reached(g_app->millis(), deadline)) {
            t5_app_input_t input = {0};
            if (!g_app->poll(&input, 20u) || input.exit_requested ||
                (input.buttons & T5_APP_BUTTON_BACK)) break;
        }
        g_video->stop();
        if (can_configure_back) g_app->set_back_exits_app(true);
        return;
    }

    fps_log("Risc Strike 1.0.0 started");
    g_state = FPS_STATE_TITLE;
    g_prev_buttons = 0u;
    g_back_hold_start_ms = 0u;
    g_back_holding = false;
    g_last_frame_ms = g_app->millis();
    (void)fps_render(g_last_frame_ms);

    bool running = true;
    uint32_t queued_down = 0u;
    while (running) {
        t5_app_input_t input = {0};
        if (!g_app->poll(&input, 4u)) break;
        if (input.exit_requested) break;

        const uint32_t now = g_app->millis();
        const uint32_t buttons = input.buttons;
        queued_down |= buttons & ~g_prev_buttons;
        g_prev_buttons = buttons;

        const uint32_t elapsed_ms = now - g_last_frame_ms;
        if (elapsed_ms < FPS_TARGET_FRAME_MS) continue;

        const float dt = fps_clampf((float)elapsed_ms * 0.001f, 0.0f, 0.075f);
        const uint32_t frame_down = queued_down;
        queued_down = 0u;
        g_last_frame_ms = now;
        running = fps_update_input(buttons, frame_down, now, dt);
        if (!running) break;
        if (g_state == FPS_STATE_PLAYING) fps_update_enemies(dt, now);
        (void)fps_render(now);
    }

    fps_log("Risc Strike stopped");
    g_video->stop();
    if (can_configure_back) g_app->set_back_exits_app(true);
}
