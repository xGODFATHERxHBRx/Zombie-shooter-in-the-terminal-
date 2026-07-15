/*
 * ZOMBIE SHOOTER - Terminal Zombie Shooting Game
 * 
 * Features:
 *  - Aiming mechanics with visible crosshair and trajectory line
 *  - 3 zombie types: Normal, Fast, Tank
 *  - Wave system with progressive difficulty
 *  - Combo kill system for bonus points
 *  - Terminal bell sound effects
 *  - 2-Player co-op mode
 *  - High score leaderboard (persistent file)
 *  - Power-ups: Shotgun, Fast Fire, Shield
 *  - Debian package ready
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <math.h>
#include <pwd.h>

/* ========================== CONFIGURATION ========================== */

#define WIDTH          60
#define HEIGHT         22
#define MAX_BULLETS_PP 8   /* Bullets per player */
#define MAX_BULLETS    (MAX_BULLETS_PP * 2)
#define MAX_ZOMBIES    35
#define MAX_POWERUPS   10
#define CROSSHAIR_MIN_Y  2
#define CROSSHAIR_MAX_Y  (HEIGHT - 4)
#define SCORE_FILE     ".zombie_shooter_scores"
#define HIGH_SCORE_COUNT 10

/* ========================== ANSI COLORS ============================ */

#define COLOR_RESET   "\x1b[0m"
#define COLOR_RED     "\x1b[31m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_YELLOW  "\x1b[33m"
#define COLOR_BLUE    "\x1b[34m"
#define COLOR_MAGENTA "\x1b[35m"
#define COLOR_CYAN    "\x1b[36m"
#define COLOR_WHITE   "\x1b[37m"
#define COLOR_BG_RED  "\x1b[41m"
#define COLOR_BG_GREEN "\x1b[42m"
#define COLOR_BG_YELLOW "\x1b[43m"
#define COLOR_BG_BLUE  "\x1b[44m"

/* ======================== TERMINAL SETTINGS ======================== */

struct termios orig_termios;

void reset_terminal_mode() {
    tcsetattr(0, TCSANOW, &orig_termios);
    printf("\x1b[?25h\x1b[0m");
    fflush(stdout);
}

void set_conio_terminal_mode() {
    struct termios new_termios;
    tcgetattr(0, &orig_termios);
    atexit(reset_terminal_mode);
    new_termios = orig_termios;
    new_termios.c_lflag &= ~(ICANON | ECHO);
    new_termios.c_cc[VMIN] = 0;
    new_termios.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &new_termios);
    printf("\x1b[?25l");
    fflush(stdout);
}

void handle_signal(int sig) { (void)sig; exit(0); }

int kbhit() {
    struct timeval tv = {0L, 0L};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(0, &fds);
    return select(1, &fds, NULL, NULL, &tv) > 0;
}

int getch() {
    unsigned char c;
    if (read(0, &c, 1) == 1) return c;
    return 0;
}

/* ======================== GAME STRUCTURES ========================== */

typedef struct {
    float x, y;
    float dx, dy;
    int active;
    int player_id;       /* 0 = P1, 1 = P2 */
    int trail_x[20];
    int trail_y[20];
    int trail_len;
} Bullet;

typedef struct {
    int x, y;
    int active;
    int hp;
    int max_hp;
    int type;            /* 0=normal, 1=fast, 2=tank */
    int move_counter;
    int move_interval;
} Zombie;

typedef struct {
    int x, y;
    int type;            /* 0=shotgun(S), 1=fastfire(F), 2=shield(D) */
    int active;
    int timer;           /* Frames until it disappears */
} Powerup;

typedef struct {
    int score;
    int wave;
} ScoreEntry;

/* ======================== GLOBAL STATE ============================= */

/* Player 1 */
int p1_crosshair_x, p1_crosshair_y;
int p1_aim_line_x[50], p1_aim_line_y[50], p1_aim_line_len;

/* Player 2 (co-op mode) */
int p2_active;
int p2_crosshair_x, p2_crosshair_y;
int p2_aim_line_x[50], p2_aim_line_y[50], p2_aim_line_len;

/* Shared state */
int player_x, player_y;
int score;
int lives;
int wave;
int zombies_killed;
int max_kills_for_wave;
int zombies_in_wave;
int game_over;
int frame_counter;
int explosion_timer, explosion_x, explosion_y;
int screen_shake;
int hit_marker_timer;
int combo_counter, combo_timer;
int shield_active;
int fast_fire_timer;
int shotgun_shots;

Bullet bullets[MAX_BULLETS];
Zombie zombies[MAX_ZOMBIES];
Powerup powerups[MAX_POWERUPS];

/* ======================== UTILITY ================================== */

int clamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

float length(float dx, float dy) {
    return sqrtf(dx * dx + dy * dy);
}

/* ======================== HIGH SCORES ============================== */

void get_score_file_path(char *buf, size_t sz) {
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_dir) {
        snprintf(buf, sz, "%s/%s", pw->pw_dir, SCORE_FILE);
    } else {
        snprintf(buf, sz, "%s", SCORE_FILE);
    }
}

int load_scores(ScoreEntry *entries) {
    char path[512];
    get_score_file_path(path, sizeof(path));
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int count = 0;
    while (count < HIGH_SCORE_COUNT && 
           fscanf(f, "%d:%d", &entries[count].score, &entries[count].wave) == 2) {
        count++;
    }
    fclose(f);
    return count;
}

void save_score(int score_val, int wave_val) {
    ScoreEntry entries[HIGH_SCORE_COUNT];
    int count = load_scores(entries);

    /* Insert new score */
    int pos = count;
    for (int i = 0; i < count; i++) {
        if (score_val > entries[i].score) {
            pos = i;
            break;
        }
    }
    if (pos < HIGH_SCORE_COUNT) {
        /* Shift down */
        for (int i = count; i > pos && i < HIGH_SCORE_COUNT; i--) {
            if (i > 0) entries[i] = entries[i-1];
        }
        entries[pos].score = score_val;
        entries[pos].wave = wave_val;
        if (count < HIGH_SCORE_COUNT) count++;
    }

    char path[512];
    get_score_file_path(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f) return;
    for (int i = 0; i < count && i < HIGH_SCORE_COUNT; i++) {
        fprintf(f, "%d:%d\n", entries[i].score, entries[i].wave);
    }
    fclose(f);
}

void display_high_scores() {
    ScoreEntry entries[HIGH_SCORE_COUNT];
    int count = load_scores(entries);
    if (count == 0) {
        printf(COLOR_CYAN "  ┌────────────────────────────────────┐\n" COLOR_RESET);
        printf(COLOR_CYAN "  │" COLOR_RESET " No high scores yet!                 " COLOR_CYAN "│\n" COLOR_RESET);
        printf(COLOR_CYAN "  └────────────────────────────────────┘\n" COLOR_RESET);
        return;
    }
    printf(COLOR_CYAN "  ┌────────────────────────────────────┐\n" COLOR_RESET);
    printf(COLOR_CYAN "  │" COLOR_RESET "        HIGH SCORES               " COLOR_CYAN "│\n" COLOR_RESET);
    printf(COLOR_CYAN "  ├────────────────────────────────────┤\n" COLOR_RESET);
    int show = count < 5 ? count : 5;
    for (int i = 0; i < show; i++) {
        printf(COLOR_CYAN "  │" COLOR_RESET);
        if (i == 0) printf(COLOR_YELLOW " 🥇" COLOR_RESET);
        else if (i == 1) printf(COLOR_WHITE " 🥈" COLOR_RESET);
        else if (i == 2) printf(COLOR_RED " 🥉" COLOR_RESET);
        else printf("    ");
        printf(" %3d.  " COLOR_GREEN "%06d" COLOR_RESET " pts  "
               COLOR_MAGENTA "W%d" COLOR_RESET "     ", i+1, entries[i].score, entries[i].wave);
        printf(COLOR_CYAN "│\n" COLOR_RESET);
    }
    printf(COLOR_CYAN "  └────────────────────────────────────┘\n" COLOR_RESET);
}

/* ======================== AIM LINE ================================= */

void calculate_aim_line(int *line_x, int *line_y, int *line_len,
                         int from_x, int from_y, int to_x, int to_y) {
    int dx = to_x - from_x;
    int dy = to_y - from_y;
    int steps = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
    if (steps == 0) { *line_len = 0; return; }
    float x_step = (float)dx / steps;
    float y_step = (float)dy / steps;

    int count = 0;
    float cx = (float)from_x;
    float cy = (float)from_y;
    for (int i = 1; i < steps; i++) {
        cx += x_step;
        cy += y_step;
        int ix = (int)(cx + 0.5f);
        int iy = (int)(cy + 0.5f);
        if ((ix == from_x && iy == from_y) || (ix == to_x && iy == to_y)) continue;
        if (ix >= 1 && ix < WIDTH - 1 && iy >= 1 && iy < HEIGHT - 1) {
            line_x[count] = ix;
            line_y[count] = iy;
            count++;
            if (count >= 50) break;
        }
    }
    *line_len = count;
}

void calculate_all_aim_lines() {
    calculate_aim_line(p1_aim_line_x, p1_aim_line_y, &p1_aim_line_len,
                       player_x, player_y, p1_crosshair_x, p1_crosshair_y);
    if (p2_active) {
        calculate_aim_line(p2_aim_line_x, p2_aim_line_y, &p2_aim_line_len,
                           player_x, player_y, p2_crosshair_x, p2_crosshair_y);
    }
}

/* ======================== INITIALIZATION =========================== */

void init_game() {
    player_x = WIDTH / 2;
    player_y = HEIGHT - 3;
    p1_crosshair_x = WIDTH / 2;
    p1_crosshair_y = HEIGHT / 3;
    p2_crosshair_x = WIDTH / 2 + 5;
    p2_crosshair_y = HEIGHT / 3;

    score = 0;
    lives = 3;
    wave = 1;
    zombies_killed = 0;
    max_kills_for_wave = 8;
    zombies_in_wave = 0;
    game_over = 0;
    frame_counter = 0;
    explosion_timer = 0;
    screen_shake = 0;
    hit_marker_timer = 0;
    combo_counter = 0;
    combo_timer = 0;
    shield_active = 0;
    fast_fire_timer = 0;
    shotgun_shots = 0;

    for (int i = 0; i < MAX_BULLETS; i++) {
        bullets[i].active = 0;
        bullets[i].trail_len = 0;
    }
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        zombies[i].active = 0;
    }
    for (int i = 0; i < MAX_POWERUPS; i++) {
        powerups[i].active = 0;
    }

    calculate_all_aim_lines();
}

/* ======================== SPAWNING ================================= */

void spawn_zombie() {
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        if (!zombies[i].active) {
            zombies[i].x = 2 + (rand() % (WIDTH - 4));
            zombies[i].y = 1;
            zombies[i].active = 1;
            zombies[i].hp = 1;
            zombies[i].max_hp = 1;
            zombies_in_wave++;

            int r = rand() % 100;
            if (wave >= 3 && r < 15) {
                zombies[i].type = 2;
                zombies[i].hp = 3;
                zombies[i].max_hp = 3;
                zombies[i].move_interval = 8;
            } else if (wave >= 2 && r < 35) {
                zombies[i].type = 1;
                zombies[i].move_interval = 3;
            } else {
                zombies[i].type = 0;
                zombies[i].move_interval = 5 + (wave > 5 ? 0 : 5 - wave);
                if (zombies[i].move_interval < 3) zombies[i].move_interval = 3;
            }
            zombies[i].move_counter = 0;

            if (wave > 5 && zombies[i].type == 0) {
                if ((rand() % 100) < 30) {
                    zombies[i].hp = 2;
                    zombies[i].max_hp = 2;
                }
            }
            break;
        }
    }
}

void spawn_powerup(int x, int y) {
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (!powerups[i].active) {
            powerups[i].x = x;
            powerups[i].y = y;
            powerups[i].type = rand() % 3;
            powerups[i].active = 1;
            powerups[i].timer = 150; /* ~5 seconds */
            break;
        }
    }
}

void fire_bullet_for_player(int player_id, float target_x, float target_y) {
    /* Find a bullet slot for this player */
    int start_idx = player_id * MAX_BULLETS_PP;
    int end_idx = start_idx + MAX_BULLETS_PP;

    /* Check if shotgun mode */
    if (shotgun_shots > 0 && player_id == 0) {
        shotgun_shots--;
        /* Fire 3 bullets in spread */
        float dx = target_x - (float)player_x;
        float dy = target_y - (float)player_y;
        float len = length(dx, dy);
        if (len < 0.5f) { dx = 0.0f; dy = -1.0f; len = 1.0f; }
        dx /= len; dy /= len;

        /* Spread: -0.2, 0, +0.2 radians */
        float angles[] = {-0.2f, 0.0f, 0.2f};
        int bullets_fired = 0;
        for (int a = 0; a < 3 && bullets_fired < 3; a++) {
            for (int b = start_idx; b < end_idx && b < MAX_BULLETS; b++) {
                if (!bullets[b].active) {
                    float ca = cosf(angles[a]), sa = sinf(angles[a]);
                    bullets[b].x = (float)player_x;
                    bullets[b].y = (float)player_y;
                    bullets[b].dx = dx * ca - dy * sa;
                    bullets[b].dy = dx * sa + dy * ca;
                    bullets[b].active = 1;
                    bullets[b].player_id = player_id;
                    bullets[b].trail_len = 0;
                    bullets_fired++;
                    break;
                }
            }
        }
        /* Terminal bell sound */
        printf("\a"); fflush(stdout);
        return;
    }

    for (int i = start_idx; i < end_idx && i < MAX_BULLETS; i++) {
        if (!bullets[i].active) {
            float dx = target_x - (float)player_x;
            float dy = target_y - (float)player_y;
            float len = length(dx, dy);
            if (len < 0.5f) { dx = 0.0f; dy = -1.0f; len = 1.0f; }

            bullets[i].x = (float)player_x;
            bullets[i].y = (float)player_y;
            bullets[i].dx = dx / len;
            bullets[i].dy = dy / len;
            bullets[i].active = 1;
            bullets[i].player_id = player_id;
            bullets[i].trail_len = 0;

            /* Terminal bell sound */
            printf("\a"); fflush(stdout);
            break;
        }
    }
}

void fire_p1() {
    fire_bullet_for_player(0, (float)p1_crosshair_x, (float)p1_crosshair_y);
}

void fire_p2() {
    if (p2_active)
        fire_bullet_for_player(1, (float)p2_crosshair_x, (float)p2_crosshair_y);
}

/* ======================== INPUT ==================================== */

void process_input() {
    while (kbhit()) {
        int ch = getch();
        if (ch == 'q' || ch == 'Q') {
            game_over = 1;
        } else if (ch == ' ') {
            fire_p1();
        } else if (ch == 'w' || ch == 'W') {
            if (p1_crosshair_y > CROSSHAIR_MIN_Y) p1_crosshair_y--;
        } else if (ch == 's' || ch == 'S') {
            if (p1_crosshair_y < CROSSHAIR_MAX_Y) p1_crosshair_y++;
        } else if (ch == 'a' || ch == 'A') {
            if (p1_crosshair_x > 2) p1_crosshair_x--;
        } else if (ch == 'd' || ch == 'D') {
            if (p1_crosshair_x < WIDTH - 3) p1_crosshair_x++;
        } else if (ch == 'i' || ch == 'I') {
            if (p2_active && p2_crosshair_y > CROSSHAIR_MIN_Y) p2_crosshair_y--;
        } else if (ch == 'k' || ch == 'K') {
            if (p2_active && p2_crosshair_y < CROSSHAIR_MAX_Y) p2_crosshair_y++;
        } else if (ch == 'j' || ch == 'J') {
            if (p2_active && p2_crosshair_x > 2) p2_crosshair_x--;
        } else if (ch == 'l' || ch == 'L') {
            if (p2_active && p2_crosshair_x < WIDTH - 3) p2_crosshair_x++;
        } else if (ch == ']' || ch == '}' || ch == 'u' || ch == 'U') {
            fire_p2();
        } else if (ch == '\x1b') {
            int ch2 = getch();
            if (ch2 == '[') {
                int ch3 = getch();
                if (ch3 == 'A') { /* Up arrow */
                    if (p2_active) {
                        if (p2_crosshair_y > CROSSHAIR_MIN_Y) p2_crosshair_y--;
                    } else {
                        if (p1_crosshair_y > CROSSHAIR_MIN_Y) p1_crosshair_y--;
                    }
                } else if (ch3 == 'B') { /* Down arrow */
                    if (p2_active) {
                        if (p2_crosshair_y < CROSSHAIR_MAX_Y) p2_crosshair_y++;
                    } else {
                        if (p1_crosshair_y < CROSSHAIR_MAX_Y) p1_crosshair_y++;
                    }
                } else if (ch3 == 'D') { /* Left arrow */
                    if (p2_active) {
                        if (p2_crosshair_x > 2) p2_crosshair_x--;
                    } else {
                        if (p1_crosshair_x > 2) p1_crosshair_x--;
                    }
                } else if (ch3 == 'C') { /* Right arrow */
                    if (p2_active) {
                        if (p2_crosshair_x < WIDTH - 3) p2_crosshair_x++;
                    } else {
                        if (p1_crosshair_x < WIDTH - 3) p1_crosshair_x++;
                    }
                }
            }
        }
    }

    p1_crosshair_x = clamp(p1_crosshair_x, 2, WIDTH - 3);
    p1_crosshair_y = clamp(p1_crosshair_y, CROSSHAIR_MIN_Y, CROSSHAIR_MAX_Y);
    if (p2_active) {
        p2_crosshair_x = clamp(p2_crosshair_x, 2, WIDTH - 3);
        p2_crosshair_y = clamp(p2_crosshair_y, CROSSHAIR_MIN_Y, CROSSHAIR_MAX_Y);
    }

    calculate_all_aim_lines();
}

/* ======================== UPDATE =================================== */

void update() {
    frame_counter++;
    int zombie_move_frame = (frame_counter % 2 == 0);

    /* Timers */
    if (combo_timer > 0) { combo_timer--; if (combo_timer == 0) combo_counter = 0; }
    if (explosion_timer > 0) explosion_timer--;
    if (screen_shake > 0) screen_shake--;
    if (hit_marker_timer > 0) hit_marker_timer--;
    if (fast_fire_timer > 0) fast_fire_timer--;

    /* Auto-fire in fast fire mode */
    if (fast_fire_timer > 0 && frame_counter % 5 == 0) {
        fire_p1();
    }

    /* Powerup timers */
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (powerups[i].active) {
            powerups[i].timer--;
            if (powerups[i].timer <= 0) powerups[i].active = 0;
        }
    }

    /* === 1. Spawn zombies === */
    int spawn_rate = 40 - (wave * 2);
    if (spawn_rate < 12) spawn_rate = 12;
    if (frame_counter % spawn_rate == 0 && zombies_in_wave < max_kills_for_wave + 5) {
        spawn_zombie();
    }

    /* === 2. Move zombies === */
    if (zombie_move_frame) {
        for (int i = 0; i < MAX_ZOMBIES; i++) {
            if (!zombies[i].active) continue;
            zombies[i].move_counter++;
            if (zombies[i].move_counter >= zombies[i].move_interval) {
                zombies[i].move_counter = 0;
                zombies[i].y++;
                if (zombies[i].y >= HEIGHT - 2) {
                    zombies[i].active = 0;
                    if (shield_active) {
                        shield_active = 0;
                        screen_shake = 3;
                    } else {
                        lives--;
                        screen_shake = 6;
                    }
                    combo_counter = 0;
                    if (lives <= 0) game_over = 1;
                }
            }
        }
    }

    /* === 3. Move bullets === */
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) continue;

        if (bullets[i].trail_len < 20) {
            bullets[i].trail_x[bullets[i].trail_len] = (int)(bullets[i].x + 0.5f);
            bullets[i].trail_y[bullets[i].trail_len] = (int)(bullets[i].y + 0.5f);
            bullets[i].trail_len++;
        }

        bullets[i].x += bullets[i].dx * 1.8f;
        bullets[i].y += bullets[i].dy * 1.8f;

        int bx = (int)(bullets[i].x + 0.5f);
        int by = (int)(bullets[i].y + 0.5f);

        if (bx < 1 || bx >= WIDTH - 1 || by < 1 || by >= HEIGHT - 1) {
            bullets[i].active = 0;
            continue;
        }

        /* Collision with zombies */
        int hit = 0;
        for (int z = 0; z < MAX_ZOMBIES; z++) {
            if (!zombies[z].active) continue;
            int dist = abs(bx - zombies[z].x) + abs(by - zombies[z].y);
            if (dist <= 1) {
                zombies[z].hp--;
                if (zombies[z].hp <= 0) {
                    zombies[z].active = 0;
                    zombies_killed++;
                    combo_counter++;
                    combo_timer = 30;
                    int combo_bonus = combo_counter > 1 ? combo_counter : 0;
                    score += 10 + (zombies[z].type * 10) + combo_bonus * 5;
                    explosion_x = zombies[z].x;
                    explosion_y = zombies[z].y;
                    explosion_timer = 4;

                    /* Terminal bell tone (shorter for kill) */
                    printf("\a"); fflush(stdout);

                    /* Powerup drop chance */
                    if ((rand() % 100) < 20) {
                        spawn_powerup(zombies[z].x, zombies[z].y);
                    }
                } else {
                    hit_marker_timer = 3;
                }
                bullets[i].active = 0;
                hit = 1;
                break;
            }
        }
        if (hit) continue;

        int crosshair_x = (bullets[i].player_id == 0) ? p1_crosshair_x : p2_crosshair_x;
        int crosshair_y = (bullets[i].player_id == 0) ? p1_crosshair_y : p2_crosshair_y;
        int dist_to_target = abs(bx - crosshair_x) + abs(by - crosshair_y);
        if (dist_to_target <= 1) bullets[i].active = 0;
        if (bullets[i].trail_len > 40) bullets[i].active = 0;
    }

    /* === 4. Collect powerups === */
    for (int p = 0; p < MAX_POWERUPS; p++) {
        if (!powerups[p].active) continue;
        /* Check player 1 proximity */
        int dist_p1 = abs(powerups[p].x - player_x) + abs(powerups[p].y - player_y);
        if (dist_p1 <= 2) {
            /* P1 collected */
            if (powerups[p].type == 0) {
                shotgun_shots = 3;  /* Next 3 shots are shotgun */
            } else if (powerups[p].type == 1) {
                fast_fire_timer = 150;  /* ~5 seconds */
            } else if (powerups[p].type == 2) {
                shield_active = 1;
            }
            powerups[p].active = 0;
            score += 5;
        }
    }

    /* === 5. Wave progression === */
    if (zombies_killed >= max_kills_for_wave) {
        wave++;
        zombies_killed = 0;
        max_kills_for_wave = 8 + (wave * 3);
        if (max_kills_for_wave > 30) max_kills_for_wave = 30;
        zombies_in_wave = 0;
        for (int i = 0; i < MAX_ZOMBIES; i++) zombies[i].active = 0;
        score += wave * 50;
        if (wave % 3 == 0 && lives < 5) lives++;
    }
}

/* ======================== RENDERING ================================ */

void print_cell(char c, int color_code) {
    switch (color_code) {
        case 1:  printf(COLOR_RED "%c" COLOR_RESET, c); break;
        case 2:  printf(COLOR_GREEN "%c" COLOR_RESET, c); break;
        case 3:  printf(COLOR_YELLOW "%c" COLOR_RESET, c); break;
        case 4:  printf(COLOR_CYAN "%c" COLOR_RESET, c); break;
        case 5:  printf(COLOR_BLUE "%c" COLOR_RESET, c); break;
        case 6:  printf(COLOR_MAGENTA "%c" COLOR_RESET, c); break;
        case 7:  printf(COLOR_WHITE "%c" COLOR_RESET, c); break;
        default: printf("%c", c); break;
    }
}

void render() {
    char grid[HEIGHT][WIDTH];
    int col[HEIGHT][WIDTH];

    /* Base grid with border */
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            if (y == 0 || y == HEIGHT - 1 || x == 0 || x == WIDTH - 1) {
                grid[y][x] = ((x + y) % 5 == 0) ? '#' : '=';
                col[y][x] = 4;
            } else {
                if ((x + y + frame_counter / 3) % 17 == 0) {
                    grid[y][x] = '.'; col[y][x] = 2;
                } else if ((x - y + frame_counter / 2) % 23 == 0) {
                    grid[y][x] = ','; col[y][x] = 2;
                } else {
                    grid[y][x] = ' '; col[y][x] = 0;
                }
            }
        }
    }

    /* === Player 1 aim line (yellow) === */
    for (int i = 0; i < p1_aim_line_len; i++) {
        int ax = p1_aim_line_x[i], ay = p1_aim_line_y[i];
        if (ay > 0 && ay < HEIGHT - 1 && ax > 0 && ax < WIDTH - 1) {
            int blocked = 0;
            for (int z = 0; z < MAX_ZOMBIES; z++) {
                if (zombies[z].active && zombies[z].x == ax && abs(zombies[z].y - ay) <= 1)
                    { blocked = 1; break; }
            }
            if (!blocked) {
                grid[ay][ax] = (i % 3 == 0) ? '~' : '.';
                col[ay][ax] = 3;
            }
        }
    }

    /* === Player 2 aim line (cyan) === */
    if (p2_active) {
        for (int i = 0; i < p2_aim_line_len; i++) {
            int ax = p2_aim_line_x[i], ay = p2_aim_line_y[i];
            if (ay > 0 && ay < HEIGHT - 1 && ax > 0 && ax < WIDTH - 1) {
                int blocked = 0;
                for (int z = 0; z < MAX_ZOMBIES; z++) {
                    if (zombies[z].active && zombies[z].x == ax && abs(zombies[z].y - ay) <= 1)
                        { blocked = 1; break; }
                }
                if (!blocked) {
                    grid[ay][ax] = (i % 3 == 0) ? '~' : ',';
                    col[ay][ax] = 4;
                }
            }
        }
    }

    /* === Draw zombies === */
    for (int i = 0; i < MAX_ZOMBIES; i++) {
        if (!zombies[i].active) continue;
        int zx = zombies[i].x, zy = zombies[i].y;
        if (zx < 1 || zx >= WIDTH - 1 || zy < 1 || zy >= HEIGHT - 1) continue;
        switch (zombies[i].type) {
            case 0: grid[zy][zx] = 'Z'; col[zy][zx] = (zombies[i].hp > 1) ? 3 : 2; break;
            case 1: grid[zy][zx] = 'z'; col[zy][zx] = 1; break;
            case 2: grid[zy][zx] = 'Z'; col[zy][zx] = 6; break;
        }
    }

    /* === Draw powerups === */
    for (int i = 0; i < MAX_POWERUPS; i++) {
        if (!powerups[i].active) continue;
        int px = powerups[i].x, py = powerups[i].y;
        if (px < 1 || px >= WIDTH - 1 || py < 1 || py >= HEIGHT - 1) continue;
        if (powerups[i].type == 0) { grid[py][px] = 'S'; col[py][px] = 3; }  /* Shotgun */
        else if (powerups[i].type == 1) { grid[py][px] = 'F'; col[py][px] = 1; } /* Fast fire */
        else if (powerups[i].type == 2) { grid[py][px] = 'D'; col[py][px] = 4; } /* Shield */
    }

    /* === Draw explosion === */
    if (explosion_timer > 0) {
        int ex = explosion_x, ey = explosion_y;
        if (ex > 1 && ex < WIDTH - 2 && ey > 1 && ey < HEIGHT - 2) {
            for (int dy = -2; dy <= 2; dy++) {
                for (int dx = -2; dx <= 2; dx++) {
                    int bx = ex + dx, by = ey + dy;
                    if (bx > 0 && bx < WIDTH - 1 && by > 0 && by < HEIGHT - 1) {
                        if (abs(dx) + abs(dy) <= 2 && (rand() % 100) < 60) {
                            grid[by][bx] = (abs(dx) + abs(dy) <= 1) ? '*' : '%';
                            col[by][bx] = (abs(dx) + abs(dy) <= 1) ? 1 : 3;
                        }
                    }
                }
            }
        }
    }

    /* === Player 1 crosshair (red) === */
    if (p1_crosshair_x > 0 && p1_crosshair_x < WIDTH - 1 && p1_crosshair_y > 0 && p1_crosshair_y < HEIGHT - 1) {
        int blink = (frame_counter / 8) % 2;
        if (blink) {
            grid[p1_crosshair_y][p1_crosshair_x] = '+';
            col[p1_crosshair_y][p1_crosshair_x] = 1;
            if (p1_crosshair_x > 1) { grid[p1_crosshair_y][p1_crosshair_x - 1] = '-'; col[p1_crosshair_y][p1_crosshair_x - 1] = 1; }
            if (p1_crosshair_x < WIDTH - 2) { grid[p1_crosshair_y][p1_crosshair_x + 1] = '-'; col[p1_crosshair_y][p1_crosshair_x + 1] = 1; }
            if (p1_crosshair_y > 1) { grid[p1_crosshair_y - 1][p1_crosshair_x] = '|'; col[p1_crosshair_y - 1][p1_crosshair_x] = 1; }
            if (p1_crosshair_y < HEIGHT - 2) { grid[p1_crosshair_y + 1][p1_crosshair_x] = '|'; col[p1_crosshair_y + 1][p1_crosshair_x] = 1; }
        } else {
            grid[p1_crosshair_y][p1_crosshair_x] = 'x';
            col[p1_crosshair_y][p1_crosshair_x] = 7;
        }
    }

    /* === Player 2 crosshair (blue) === */
    if (p2_active && p2_crosshair_x > 0 && p2_crosshair_x < WIDTH - 1 && p2_crosshair_y > 0 && p2_crosshair_y < HEIGHT - 1) {
        int blink = (frame_counter / 8) % 2;
        if (blink) {
            grid[p2_crosshair_y][p2_crosshair_x] = '+';
            col[p2_crosshair_y][p2_crosshair_x] = 5;
            if (p2_crosshair_x > 1) { grid[p2_crosshair_y][p2_crosshair_x - 1] = '-'; col[p2_crosshair_y][p2_crosshair_x - 1] = 5; }
            if (p2_crosshair_x < WIDTH - 2) { grid[p2_crosshair_y][p2_crosshair_x + 1] = '-'; col[p2_crosshair_y][p2_crosshair_x + 1] = 5; }
            if (p2_crosshair_y > 1) { grid[p2_crosshair_y - 1][p2_crosshair_x] = '|'; col[p2_crosshair_y - 1][p2_crosshair_x] = 5; }
            if (p2_crosshair_y < HEIGHT - 2) { grid[p2_crosshair_y + 1][p2_crosshair_x] = '|'; col[p2_crosshair_y + 1][p2_crosshair_x] = 5; }
        } else {
            grid[p2_crosshair_y][p2_crosshair_x] = 'x';
            col[p2_crosshair_y][p2_crosshair_x] = 7;
        }
    }

    /* === Draw bullets === */
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!bullets[i].active) continue;
        for (int t = 0; t < bullets[i].trail_len; t++) {
            int tx = bullets[i].trail_x[t], ty = bullets[i].trail_y[t];
            if (tx > 0 && tx < WIDTH - 1 && ty > 0 && ty < HEIGHT - 1) {
                if (t == bullets[i].trail_len - 1) continue;
                grid[ty][tx] = '.';
                col[ty][tx] = (bullets[i].player_id == 0) ? 3 : 4;
            }
        }
        int bx = (int)(bullets[i].x + 0.5f);
        int by = (int)(bullets[i].y + 0.5f);
        if (bx > 0 && bx < WIDTH - 1 && by > 0 && by < HEIGHT - 1) {
            grid[by][bx] = '*';
            col[by][bx] = (bullets[i].player_id == 0) ? 3 : 4;
        }
    }

    /* === Draw player === */
    if (player_x > 1 && player_x < WIDTH - 2) {
        grid[player_y][player_x] = 'W';
        col[player_y][player_x] = 2;
        if (player_x > 2) { grid[player_y][player_x - 1] = '['; col[player_y][player_x - 1] = 2; }
        if (player_x < WIDTH - 3) { grid[player_y][player_x + 1] = ']'; col[player_y][player_x + 1] = 2; }
        if (player_y < HEIGHT - 2) { grid[player_y + 1][player_x] = '_'; col[player_y + 1][player_x] = 4; }
        /* Shield visual */
        if (shield_active && player_y > 0) {
            grid[player_y - 1][player_x] = '^';
            col[player_y - 1][player_x] = 4;
        }
    }

    /* === Hit marker === */
    if (hit_marker_timer > 0 && p1_crosshair_x > 0 && p1_crosshair_x < WIDTH - 1 && p1_crosshair_y > 0 && p1_crosshair_y < HEIGHT - 1) {
        grid[p1_crosshair_y][p1_crosshair_x] = '!';
        col[p1_crosshair_y][p1_crosshair_x] = 1;
    }

    /* === RENDER === */
    printf("\x1b[H");

    /* HUD Header */
    printf(COLOR_RED " ╔══════════════════════ ZOMBIE APOCALYPSE ═══════════════════════╗\n" COLOR_RESET);
    printf(COLOR_RED " ║" COLOR_RESET);
    printf(COLOR_YELLOW " Wave: %2d " COLOR_RESET, wave);
    printf(COLOR_CYAN "|" COLOR_RESET);
    printf(COLOR_GREEN " Score: %05d " COLOR_RESET, score);
    printf(COLOR_CYAN "|" COLOR_RESET);
    printf(" Lives: ");
    for (int l = 0; l < lives && l < 5; l++) printf(COLOR_RED "♥ " COLOR_RESET);
    for (int l = lives; l < 5; l++) printf("  ");
    printf(COLOR_CYAN "|" COLOR_RESET);
    printf(COLOR_YELLOW " Kills: %02d " COLOR_RESET, zombies_killed);
    if (combo_counter > 1) printf(COLOR_MAGENTA "🔥%dx COMBO! " COLOR_RESET, combo_counter);
    /* Powerup indicators */
    if (shotgun_shots > 0) printf(COLOR_YELLOW "SHT%d " COLOR_RESET, shotgun_shots);
    if (fast_fire_timer > 0) printf(COLOR_RED "FST " COLOR_RESET);
    if (shield_active) printf(COLOR_CYAN "🛡 " COLOR_RESET);
    if (p2_active) printf(COLOR_BLUE "2P" COLOR_RESET);
    printf(COLOR_RED "║\n" COLOR_RESET);

    /* Progress bar */
    int progress = max_kills_for_wave > 0 ? (zombies_killed * 100) / max_kills_for_wave : 0;
    if (progress > 100) progress = 100;
    printf(COLOR_RED " ║" COLOR_RESET " ");
    printf(COLOR_RED "WAVE: " COLOR_RESET);
    printf("[");
    int bars = progress / 10;
    for (int b = 0; b < 10; b++) {
        printf(b < bars ? COLOR_RED "█" COLOR_RESET : COLOR_WHITE "▒" COLOR_RESET);
    }
    printf("] %3d%%", progress);
    printf("    " COLOR_CYAN "[Z:%02d/%02d]" COLOR_RESET, zombies_in_wave, max_kills_for_wave + 5);
    printf(COLOR_RED " ║\n" COLOR_RESET);
    printf(COLOR_RED " ╚" COLOR_RESET);
    for (int i = 0; i < 56; i++) printf(COLOR_RED "═" COLOR_RESET);
    printf(COLOR_RED "╝\n" COLOR_RESET);

    /* Game grid */
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) print_cell(grid[y][x], col[y][x]);
        printf("\n");
    }

    /* Controls footer */
    printf(COLOR_CYAN " ───────────────────────────────────────────────────────────── \n" COLOR_RESET);
    if (p2_active) {
        printf(COLOR_GREEN " P1:WASD+SPACE " COLOR_RESET);
        printf(COLOR_BLUE " P2:IJKL+] " COLOR_RESET);
        printf(COLOR_YELLOW " Both:Shoot " COLOR_RESET);
        printf(COLOR_RED   " [Q]:Quit\n" COLOR_RESET);
    } else {
        printf(COLOR_GREEN " [WASD/Arrows]:" COLOR_RESET " Aim  ");
        printf(COLOR_YELLOW "[SPACE]:" COLOR_RESET " Shoot  ");
        printf(COLOR_RED   "[Q]:" COLOR_RESET " Quit\n");
    }
    printf(COLOR_CYAN " P1" COLOR_RESET "(%2d,%2d)", p1_crosshair_x, p1_crosshair_y);
    if (p2_active) printf(COLOR_BLUE " P2" COLOR_RESET "(%2d,%2d)", p2_crosshair_x, p2_crosshair_y);
    printf(COLOR_CYAN " ─ Player" COLOR_RESET "(%2d,%2d)\n", player_x, player_y);
    fflush(stdout);
}

/* ======================== MENU ===================================== */

void show_menu() {
    printf("\x1b[2J\x1b[H");
    printf(COLOR_RED "\n");
    printf("      ███████╗ ██████╗ ███╗   ███╗██████╗ ██╗███████╗     █████╗ ██████╗  \n");
    printf("      ╚══███╔╝██╔══██╗████╗ ████║██╔══██╗██║╚══███╔╝    ██╔══██╗██╔══██╗ \n");
    printf("        ███╔╝ ███████║██╔████╔██║██████╔╝██║  ███╔╝     ███████║██████╔╝ \n");
    printf("       ███╔╝  ██╔══██║██║╚██╔╝██║██╔══██╗██║ ███╔╝      ██╔══██║██╔═══╝  \n");
    printf("      ███████╗██║  ██║██║ ╚═╝ ██║██████╔╝██║███████╗    ██║  ██║██║      \n");
    printf("      ╚══════╝╚═╝  ╚═╝╚═╝     ╚═╝╚═════╝ ╚═╝╚══════╝    ╚═╝  ╚═╝╚═╝      \n");
    printf(COLOR_RESET);
    printf(COLOR_RED "╔═══════════════════════════════════════════════════════════════╗\n" COLOR_RESET);
    printf(COLOR_RED "║" COLOR_RESET);
    printf(COLOR_GREEN "        ████████╗███████╗██████╗ ███╗   ███╗██╗███╗   ██╗ █████╗ ██╗         " COLOR_RESET);
    printf(COLOR_RED "║\n" COLOR_RESET);
    printf(COLOR_RED "║" COLOR_RESET);
    printf(COLOR_GREEN "        ╚══██╔══╝██╔════╝██╔══██╗████╗ ████║██║████╗  ██║██╔══██╗██║         " COLOR_RESET);
    printf(COLOR_RED "║\n" COLOR_RESET);
    printf(COLOR_RED "║" COLOR_RESET);
    printf(COLOR_GREEN "           ██║   █████╗  ██████╔╝██╔████╔██║██║██╔██╗ ██║███████║██║         " COLOR_RESET);
    printf(COLOR_RED "║\n" COLOR_RESET);
    printf(COLOR_RED "║" COLOR_RESET);
    printf(COLOR_GREEN "           ██║   ██╔══╝  ██╔══██╗██║╚██╔╝██║██║██║╚██╗██║██╔══██║██║         " COLOR_RESET);
    printf(COLOR_RED "║\n" COLOR_RESET);
    printf(COLOR_RED "║" COLOR_RESET);
    printf(COLOR_GREEN "           ██║   ███████╗██║  ██║██║ ╚═╝ ██║██║██║ ╚████║██║  ██║███████╗    " COLOR_RESET);
    printf(COLOR_RED "║\n" COLOR_RESET);
    printf(COLOR_RED "║" COLOR_RESET);
    printf(COLOR_GREEN "           ╚═╝   ╚══════╝╚═╝  ╚═╝╚═╝     ╚═╝╚═╝╚═╝  ╚═══╝╚═╝  ╚═╝╚══════╝    " COLOR_RESET);
    printf(COLOR_RED "║\n" COLOR_RESET);
    printf(COLOR_RED "╚═══════════════════════════════════════════════════════════════╝\n" COLOR_RESET);
    printf("\n");
    printf(COLOR_WHITE "  ☠  The undead horde is approaching! Defend your position!  ☠\n\n" COLOR_RESET);
    printf(COLOR_RED "  ⚠  AIMING MECHANICS:\n" COLOR_RESET);
    printf(COLOR_YELLOW "     • Move the crosshair around the screen to aim\n" COLOR_RESET);
    printf(COLOR_YELLOW "     • Dotted lines show your bullet trajectory\n" COLOR_RESET);
    printf(COLOR_YELLOW "     • Bullets travel along the aim line toward your crosshair\n\n" COLOR_RESET);
    printf(COLOR_GREEN "  Controls:\n" COLOR_RESET);
    printf("    " COLOR_GREEN "W/A/S/D" COLOR_RESET " or " COLOR_GREEN "[Arrows]" COLOR_RESET " : Aim\n");
    printf("    " COLOR_YELLOW "[SPACEBAR]" COLOR_RESET "            : FIRE!\n");
    printf("    " COLOR_RED "Q" COLOR_RESET "                    : Quit\n\n");
    printf(COLOR_BLUE "  2-Player Co-op:\n" COLOR_RESET);
    printf("    " COLOR_BLUE "P1: WASD+SPACE" COLOR_RESET "  " COLOR_BLUE "P2: IJKL+]" COLOR_RESET "\n");
    printf("    " COLOR_BLUE "Arrows also control P2 in 2P mode\n\n" COLOR_RESET);
    printf(COLOR_MAGENTA "  Zombie Types:\n" COLOR_RESET);
    printf(COLOR_GREEN "    Z" COLOR_RESET " - Regular (1 HP)\n");
    printf(COLOR_RED "    z" COLOR_RESET " - Fast (1 HP, moves quickly)\n");
    printf(COLOR_MAGENTA "    Z" COLOR_RESET " - Tank (3 HP)\n\n");
    printf(COLOR_CYAN "  Power-ups:\n" COLOR_RESET);
    printf(COLOR_YELLOW "    S" COLOR_RESET " - Shotgun (3-bullet spread!)\n");
    printf(COLOR_RED "    F" COLOR_RESET " - Fast Fire (auto-fire!)\n");
    printf(COLOR_CYAN "    D" COLOR_RESET " - Shield (absorbs one hit!)\n\n");

    printf(COLOR_YELLOW "  🏆 High scores are saved automatically!\n\n" COLOR_RESET);

    printf(COLOR_RED "  ╔════════════════════════════════════════════════════════╗\n" COLOR_RESET);
    printf(COLOR_RED "  ║" COLOR_RESET);
    printf(COLOR_GREEN "  1P: Press " COLOR_YELLOW "1" COLOR_GREEN "  |  2P Co-op: Press " COLOR_BLUE "2" COLOR_GREEN "  |  " COLOR_RED "Q: Quit   " COLOR_RESET);
    printf(COLOR_RED "║\n" COLOR_RESET);
    printf(COLOR_RED "  ╚════════════════════════════════════════════════════════╝\n" COLOR_RESET);
    printf("\n");
    display_high_scores();
    fflush(stdout);

    while (kbhit()) getch();
    while (1) {
        if (kbhit()) {
            int ch = getch();
            if (ch == '2') {
                p2_active = 1;
            } else if (ch == 'q' || ch == 'Q') {
                reset_terminal_mode();
                exit(0);
            }
            /* '1' or any other key starts 1P */
            break;
        }
        usleep(50000);
    }
    printf("\x1b[2J\x1b[H");
    fflush(stdout);
}

/* ======================== GAME OVER ================================ */

void show_game_over() {
    printf("\x1b[2J\x1b[H");
    printf("\x1b[H");
    for (int i = 0; i < 10; i++) {
        printf(COLOR_BG_RED "                                                                                \n" COLOR_RESET);
    }
    fflush(stdout);
    usleep(300000);

    /* Auto-save score */
    save_score(score, wave);

    printf("\x1b[2J\x1b[H");
    printf(COLOR_RED "\n");
    printf("   ▄████  ▄▄▄       ███▄ ▄███▓▓█████     ▒█████   ██▒   █▓▓█████  ██▀███  \n");
    printf("  ██▒ ▀█▒▒████▄    ▓██▒▀█▀ ██▒▓█   ▀    ▒██▒  ██▒▓██░   █▒▓█   ▀ ▓██ ▒ ██▒\n");
    printf(" ▒██░▄▄▄░▒██  ▀█▄  ▓██    ▓██░▒███      ▒██░  ██▒ ▓██  █▒░▒███   ▓██ ░▄█ ▒\n");
    printf(" ░▓█  ██▓░██▄▄▄▄██ ▒██    ▒██ ▒▓█  ▄    ▒██   ██░  ▒██ █░░▒▓█  ▄ ▒██▀▀█▄  \n");
    printf(" ░▒▓███▀▒ ▓█   ▓██▒▒██▒   ░██▒░▒████▒   ░ ████▓▒░   ▒▀█░  ░▒████▒░██▓ ▒██▒\n");
    printf("  ░▒   ▒  ▒▒   ▓▒█░░ ▒░   ░  ░░░ ▒░ ░   ░ ▒░▒░▒░    ░ ▐░  ░░ ▒░ ░░ ▒▓ ░▒▓░\n");
    printf("   ░   ░   ▒   ▒▒ ░░  ░      ░ ░ ░  ░     ░ ▒ ▒░    ░ ░░   ░ ░  ░  ░▒ ░ ▒░\n");
    printf(" ░ ░   ░   ░   ▒   ░      ░      ░      ░ ░ ░ ▒       ░░     ░     ░░   ░ \n");
    printf("       ░       ░  ░       ░      ░  ░       ░ ░        ░     ░  ░   ░     \n");
    printf("                                                            ░               \n");
    printf(COLOR_RESET);
    printf("\n");
    printf(COLOR_RED "  ╔══════════════════════════════════════════════════╗\n" COLOR_RESET);
    printf(COLOR_RED "  ║" COLOR_RESET);
    printf(COLOR_YELLOW "          FINAL STATS                    " COLOR_RESET);
    printf(COLOR_RED "║\n" COLOR_RESET);
    printf(COLOR_RED "  ║" COLOR_RESET);
    printf("  Score : " COLOR_GREEN "%05d" COLOR_RESET " points                ", score);
    printf(COLOR_RED "║\n" COLOR_RESET);
    printf(COLOR_RED "  ║" COLOR_RESET);
    printf("  Waves : " COLOR_MAGENTA "%d" COLOR_RESET " survived               ", wave);
    printf(COLOR_RED "║\n" COLOR_RESET);
    printf(COLOR_RED "  ╚══════════════════════════════════════════════════╝\n" COLOR_RESET);
    printf("\n");

    /* Display high scores */
    display_high_scores();

    printf("\n  Press any key to continue...\n");
    fflush(stdout);

    while (kbhit()) getch();
    while (1) {
        if (kbhit()) { getch(); break; }
        usleep(50000);
    }
}

/* ============================ MAIN ================================= */

int main() {
    srand((unsigned int)time(NULL));
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    set_conio_terminal_mode();

    while (1) {
        p2_active = 0;
        show_menu();
        init_game();

        while (!game_over) {
            process_input();
            update();
            render();
            usleep(30000);
        }

        show_game_over();

        /* Play again? */
        printf("\x1b[2J\x1b[H");
        printf(COLOR_RED "  ╔══════════════════════════════════╗\n" COLOR_RESET);
        printf(COLOR_RED "  ║" COLOR_RESET);
        printf(COLOR_GREEN "     Play again? (y/n):           " COLOR_RESET);
        printf(COLOR_RED "║\n" COLOR_RESET);
        printf(COLOR_RED "  ╚══════════════════════════════════╝\n" COLOR_RESET);
        printf("\n  ");
        fflush(stdout);

        while (kbhit()) getch();
        char ans = ' ';
        while (1) {
            if (kbhit()) {
                ans = getch();
                if (ans == 'y' || ans == 'Y' || ans == 'n' || ans == 'N' || ans == 'q' || ans == 'Q')
                    break;
            }
            usleep(50000);
        }
        if (ans == 'n' || ans == 'N' || ans == 'q' || ans == 'Q') break;
    }
    return 0;
}
