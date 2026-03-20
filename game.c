/*
 * game.c - DOS Text Adventure
 * Single-file MS-DOS text adventure compiled with Open Watcom C.
 * Compile for DOS:  wcl -bt=dos game.c -o game.exe
 * Compile for test: cc -DTEST_BUILD game.c -o game_test -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <math.h>

/* =========================================================
 * ANSI colour helpers (work on Windows console with ANSI support)
 * ========================================================= */
#define COL_RED    "\x1b[31m"
#define COL_GREEN  "\x1b[32m"
#define COL_YELLOW "\x1b[33m"
#define COL_WHITE  "\x1b[37m"
#define COL_RESET  "\x1b[0m"

/* =========================================================
 * Constants
 * ========================================================= */

#define MAX_ORCS          10
#define MAX_ELVES          6   /* leprechauns */
#define MAX_ANIMALS       60
#define NUM_TREASURE_TYPES 6
#define ORC_MIN_DIST      20
#define SWORD_VISIBLE_DIST 40
#define TREASURE_VISIBLE_DIST 40
#define MAX_HIGH_SCORES   10
#define HIGHSCORE_FILE    "hiscores.dat"
#define PLAYER_NAME_LEN   24

static const char *treasure_names[NUM_TREASURE_TYPES] = {
    "gold", "silver", "diamonds", "rubies", "emeralds", "sapphires"
};

/* Points per treasure type — total across all 6 = 1000 */
static const int treasure_values[NUM_TREASURE_TYPES] = {
    100,  /* gold      */
    75,   /* silver    */
    300,  /* diamonds  */
    250,  /* rubies    */
    175,  /* emeralds  */
    100   /* sapphires */
};

/* =========================================================
 * High score table
 * ========================================================= */
typedef struct {
    char name[PLAYER_NAME_LEN];
    int  score;
    char title[64];
} HighScore;

static HighScore hi_scores[MAX_HIGH_SCORES];
static int       hi_count = 0;
static char      player_name[PLAYER_NAME_LEN] = "Unknown";

/* =========================================================
 * Enumerations
 * ========================================================= */

typedef enum {
    TERRAIN_OPEN      = 0,
    TERRAIN_FOREST    = 1,
    TERRAIN_MEADOW    = 2,
    TERRAIN_RIVER     = 3,
    TERRAIN_BRIDGE    = 4,
    TERRAIN_FARM      = 5,
    TERRAIN_FARMHOUSE = 6,
    TERRAIN_BARN      = 7,
    TERRAIN_GATE      = 8,
    TERRAIN_WALL      = 9,
    TERRAIN_MEADOW2   = 10,  /* second meadow */
    TERRAIN_POND      = 11,  /* pond inside meadow2 — impassable */
    TERRAIN_TOWER     = 12,  /* ruined tower — impassable */
    TERRAIN_GRAVEYARD = 13   /* graveyard — walkable but spooky */
} Terrain;

typedef enum {
    ANIMAL_BIRD,
    ANIMAL_RABBIT,
    ANIMAL_COW,
    ANIMAL_HORSE,
    ANIMAL_DEER,
    ANIMAL_WOLF
} AnimalType;

typedef enum {
    CMD_NORTH,
    CMD_SOUTH,
    CMD_EAST,
    CMD_WEST,
    CMD_NORTHEAST,
    CMD_SOUTHEAST,
    CMD_SOUTHWEST,
    CMD_NORTHWEST,
    CMD_ATTACK,
    CMD_FIGHT,
    CMD_HELLO,
    CMD_PET,
    CMD_TALK,
    CMD_DRINK,
    CMD_PICKUP_SWORD,
    CMD_PICKUP_FLASK,
    CMD_PICKUP_TREASURE,
    CMD_LOOK,
    CMD_SCORE,
    CMD_INVENTORY,
    CMD_HELP,
    CMD_QUIT,
    CMD_TURN_LEFT,
    CMD_TURN_RIGHT,
    CMD_MAP,
    CMD_FLEE,
    CMD_BUY,
    CMD_RUN_NORTH,
    CMD_RUN_SOUTH,
    CMD_RUN_EAST,
    CMD_RUN_WEST,
    CMD_RUN_NORTHEAST,
    CMD_RUN_SOUTHEAST,
    CMD_RUN_SOUTHWEST,
    CMD_RUN_NORTHWEST,
    CMD_RUN_PROMPT,
    CMD_REST,
    CMD_DODGE,
    CMD_SAVE,
    CMD_LOAD,
    CMD_HIGHSCORES,
    CMD_UNKNOWN
} Command;

typedef enum {
    MODE_ASCII,
    MODE_TEXT
} DisplayMode;

/* =========================================================
 * Structs
 * ========================================================= */

typedef struct {
    Terrain terrain;
    int     has_sword;    /* 1 if a sword lies here */
    int     has_flask;    /* 1 if a flask lies here */
    int     has_treasure; /* index into treasure_names[], -1 if none */
    int     wall_style;   /* 0-3: moss, ivy, aged stone, damaged — only used for TERRAIN_WALL */
    int     open_feature; /* 0=none, 1=wildflowers, 2=mud, 3=bushes — only on TERRAIN_OPEN */
    int     signpost;     /* 0=none, 1=farm, 2=gate, 3=pond, 4=forest, 5=merchant */
    int     has_scroll;   /* 1 = Malachar's scroll lies here */
    int     has_shield;   /* 1 = a shield lies here */
} Cell;

typedef struct {
    int x, y;
    int hp;
    int has_sword;
    int flask_count;
    int treasure_mask;
    int score;
    /* stats for summary */
    int elves_encountered;
    int orcs_killed;
    int flasks_found;
    int animals_interacted;
    /* fleeing state */
    int fleeing;
    int flee_dir_x;
    int flee_dir_y;
    /* facing direction: 0=North, 1=East, 2=South, 3=West */
    int facing;
    int has_key;        /* 1 = player carries the ancient key */
    int dodging;        /* 1 = player used dodge this turn, halves orc damage */
    int has_shield;     /* 1 = player carries a shield, halves orc damage */
} Player;

typedef struct {
    int x, y;
    int hp;
    int alive;
    int chasing;
    int chase_dist;
    int in_melee;    /* 1 = locked adjacent to player, attacking each turn */
    int spotted;     /* 1 = warning already shouted for this orc */
    int chase_ox;    /* x position where chase began */
    int chase_oy;    /* y position where chase began */
    int is_warchief; /* 1 = this orc is the warchief (100 HP, special messages) */
} Orc;

typedef struct {
    int x, y;
    int alive;
    int patrol_cx;
    int patrol_cy;
    int turn_counter;
} Elf;

typedef struct {
    int       x, y;
    AnimalType type;
    int       alive;
    int       turn_counter;
    int       wolf_menace_turns;     /* wolf only: turns spent watching player */
    int       wolf_menace_threshold; /* wolf only: random 2-4, set on first spot */
} Animal;

typedef struct {
    int x, y;
    int alive;       /* 1 = dragon exists in pond */
    int surface_turn; /* turn on which dragon next surfaces (0 = not scheduled) */
} Dragon;

typedef struct {
    Player  player;
    Orc     orcs[MAX_ORCS];
    Elf     elves[MAX_ELVES];
    Animal  animals[MAX_ANIMALS];
    int     num_animals;
    Cell    grid[30][30];
    int     treasure_type[5];
    DisplayMode mode;
    int     turn;
    int     game_over; /* 0=running, 1=won, 2=dead */
    int     skip_render; /* 1 = suppress render() this turn (e.g. after 'look') */
    Dragon  dragon;
    /* Shuffle-deck indices for animal sayings (0-14); reset when all used */
    int     meadow_deck[15];
    int     meadow_deck_pos;
    int     farm_deck[15];
    int     farm_deck_pos;
    /* Shuffle-deck for leprechaun taunts */
    int     lep_deck[15];
    int     lep_deck_pos;
    /* Sword block origins [15 swords, each 4x4] */
    int     sword_ox[15];
    int     sword_oy[15];
    int     num_swords;
    /* Treasure block origins [5 treasures, each 5x5] */
    int     treasure_ox[5];
    int     treasure_oy[5];
    /* Merchant */
    int     merchant_x;
    int     merchant_y;
    int     merchant_alive;  /* 1 = still at post */
    /* Locked chest: which treasure index (0-4) needs the key */
    int     locked_treasure; /* -1 = none */
    /* Key location */
    int     key_x;
    int     key_y;
    int     key_taken;
    /* Gatekeeper riddle state */
    int     riddle_answered;      /* 1 = player already solved it */
    int     riddle_wrong_streak;  /* consecutive wrong answers at gate */
} GameState;

/* =========================================================
 * Global game state
 * ========================================================= */

GameState g;

/* =========================================================
 * Forward declarations
 * ========================================================= */

/* World generation */
void gen_world(void);
void gen_forest(void);
void gen_meadow(void);
void gen_meadow2(void);
void gen_river(void);
void gen_bridges(void);
void gen_farm(void);
void place_gate(void);
void place_wall(void);
void place_items(void);
void place_npcs(void);
void place_landmarks(void);

/* Rendering */
void render(void);
void render_ascii(void);
void render_prose(void);
int  visibility_radius(void);

/* Input */
Command read_command(void);

/* Player actions */
void move_player(int dx, int dy);
void describe_view(void);
void do_map(void);
void do_attack(void);
void do_drink(void);
void do_flee(void);
void do_buy(void);
void do_rest(void);
void do_dodge(void);
void do_save(void);
void do_load(void);
void do_pickup_sword(void);
void do_pickup_flask(void);
void do_pickup_treasure(void);
void do_interact(Command cmd);
void do_look(void);
void do_score(void);
void do_inventory(void);
void do_help(void);
void process_command(Command cmd);

/* NPC update */
void update_orcs(void);
void update_elves(void);
void update_animals(void);
void update_dragon(void);
void update_npcs(void);

/* HP helper */
int clamp_hp(int hp);
int popcount_mask(int mask);

/* Direction helpers */
static const char *compass_dir(int px, int py, int tx, int ty);
static const char *dist_label(int steps);
static const char *wall_style_desc(int style);
static const char *score_title(int s);

/* End game */
void show_summary(void);
void show_death(const char *msg);
void check_triggers(void);

/* High scores */
void load_high_scores(void);
void save_high_scores(void);
void add_high_score(const char *name, int score, const char *title);
void show_high_scores(void);

/* Initialization */
void init_game(void);
void show_intro(void);
void choose_display_mode(void);
void game_loop(void);

/* =========================================================
 * HP helper
 * ========================================================= */

int clamp_hp(int hp) {
    if (hp < 0)   return 0;
    if (hp > 120) return 120;
    return hp;
}

/* =========================================================
 * World generation stubs (implemented in later tasks)
 * ========================================================= */

void gen_forest(void) {
    static int fx[900], fy[900];
    int fsize = 0;
    int count = 0;
    int sx, sy;
    int attempts;

    /* Pick a random seed: must be TERRAIN_OPEN, inside the wall (rows 1-28, cols 1-28) */
    for (attempts = 0; attempts < 10000; attempts++) {
        sx = 1 + rand() % 28;
        sy = 1 + rand() % 27; /* rows 1-27, keep away from player start row */
        if (g.grid[sy][sx].terrain == TERRAIN_OPEN &&
            !(sx == 15 && sy == 28)) {
            break;
        }
    }
    if (attempts >= 10000) return;

    g.grid[sy][sx].terrain = TERRAIN_FOREST;
    fx[fsize] = sx; fy[fsize] = sy; fsize++;
    count = 1;

    while (count < 100 && fsize > 0) {
        int fi = rand() % fsize;
        int cx = fx[fi], cy = fy[fi];
        int nx[4], ny[4], nc = 0;
        int ddx[] = {0, 0, 1, -1};
        int ddy[] = {-1, 1, 0, 0};
        int d;
        for (d = 0; d < 4; d++) {
            int ex = cx + ddx[d];
            int ey = cy + ddy[d];
            if (ex < 1 || ex > 28 || ey < 1 || ey > 28) continue;
            if (ex == 15 && ey == 28) continue;
            if (g.grid[ey][ex].terrain == TERRAIN_OPEN) {
                nx[nc] = ex; ny[nc] = ey; nc++;
            }
        }
        if (nc == 0) {
            fx[fi] = fx[fsize-1]; fy[fi] = fy[fsize-1]; fsize--;
        } else {
            int pick = rand() % nc;
            g.grid[ny[pick]][nx[pick]].terrain = TERRAIN_FOREST;
            fx[fsize] = nx[pick]; fy[fsize] = ny[pick]; fsize++;
            count++;
        }
    }
}
void gen_meadow(void) {
    static int fx[900], fy[900];
    int fsize = 0;
    int count = 0;
    int sx, sy;
    int attempts;

    /* Pick a random seed: must be TERRAIN_OPEN, inside the wall */
    for (attempts = 0; attempts < 10000; attempts++) {
        sx = 1 + rand() % 28;
        sy = 1 + rand() % 27;
        if (g.grid[sy][sx].terrain == TERRAIN_OPEN &&
            !(sx == 15 && sy == 28)) {
            break;
        }
    }
    if (attempts >= 10000) return;

    g.grid[sy][sx].terrain = TERRAIN_MEADOW;
    fx[fsize] = sx; fy[fsize] = sy; fsize++;
    count = 1;

    while (count < 200 && fsize > 0) {
        int fi = rand() % fsize;
        int cx = fx[fi], cy = fy[fi];
        int nx[4], ny[4], nc = 0;
        int ddx[] = {0, 0, 1, -1};
        int ddy[] = {-1, 1, 0, 0};
        int d;
        for (d = 0; d < 4; d++) {
            int ex = cx + ddx[d];
            int ey = cy + ddy[d];
            if (ex < 1 || ex > 28 || ey < 1 || ey > 28) continue;
            if (ex == 15 && ey == 28) continue;
            if (g.grid[ey][ex].terrain == TERRAIN_OPEN) {
                nx[nc] = ex; ny[nc] = ey; nc++;
            }
        }
        if (nc == 0) {
            fx[fi] = fx[fsize-1]; fy[fi] = fy[fsize-1]; fsize--;
        } else {
            int pick = rand() % nc;
            g.grid[ny[pick]][nx[pick]].terrain = TERRAIN_MEADOW;
            fx[fsize] = nx[pick]; fy[fsize] = ny[pick]; fsize++;
            count++;
        }
    }
}

void gen_meadow2(void) {
    /* Flood-fill a second irregular meadow: 200-400 cells.
     * Then carve a small irregular pond (8-20 cells) inside it. */
    static int fx2[900], fy2[900];
    int fsize = 0;
    int count = 0;
    int sx, sy;
    int attempts;
    int target = 200 + rand() % 201; /* 200..400 */

    /* Pick a seed on TERRAIN_OPEN, not too close to existing meadow */
    for (attempts = 0; attempts < 10000; attempts++) {
        sx = 1 + rand() % 28;
        sy = 1 + rand() % 27;
        if (g.grid[sy][sx].terrain != TERRAIN_OPEN) continue;
        if (sx == 15 && sy == 28) continue;
        int near = 0, r2, c2;
        for (r2 = sy-3; r2 <= sy+3 && !near; r2++)
            for (c2 = sx-3; c2 <= sx+3 && !near; c2++)
                if (c2>=0&&c2<30&&r2>=0&&r2<30 && g.grid[r2][c2].terrain == TERRAIN_MEADOW)
                    near = 1;
        if (near) continue;
        break;
    }
    if (attempts >= 10000) return;

    g.grid[sy][sx].terrain = TERRAIN_MEADOW2;
    fx2[fsize] = sx; fy2[fsize] = sy; fsize++;
    count = 1;

    while (count < target && fsize > 0) {
        int fi = rand() % fsize;
        int cx = fx2[fi], cy = fy2[fi];
        int nx[4], ny[4], nc = 0;
        int ddx[] = {0, 0, 1, -1};
        int ddy[] = {-1, 1, 0, 0};
        int d;
        for (d = 0; d < 4; d++) {
            int ex = cx + ddx[d];
            int ey = cy + ddy[d];
            if (ex < 1 || ex > 28 || ey < 1 || ey > 28) continue;
            if (ex == 15 && ey == 28) continue;
            if (g.grid[ey][ex].terrain == TERRAIN_OPEN) {
                nx[nc] = ex; ny[nc] = ey; nc++;
            }
        }
        if (nc == 0) {
            fx2[fi] = fx2[fsize-1]; fy2[fi] = fy2[fsize-1]; fsize--;
        } else {
            int pick = rand() % nc;
            g.grid[ny[pick]][nx[pick]].terrain = TERRAIN_MEADOW2;
            fx2[fsize] = nx[pick]; fy2[fsize] = ny[pick]; fsize++;
            count++;
        }
    }

    /* Carve a small irregular pond (8-20 cells) near the centre of the meadow */
    if (count < 20) return;
    {
        int pond_target = 8 + rand() % 13; /* 8..20 */
        /* Find an interior meadow2 cell (3+ meadow2 neighbours) to seed the pond */
        int pseed_x = fx2[fsize/2], pseed_y = fy2[fsize/2];
        int pi;
        for (pi = 0; pi < fsize; pi++) {
            int cx2 = fx2[pi], cy2 = fy2[pi];
            int nbcount = 0, d2;
            int ddx2[] = {0,0,1,-1}, ddy2[] = {-1,1,0,0};
            for (d2 = 0; d2 < 4; d2++) {
                int ex2 = cx2+ddx2[d2], ey2 = cy2+ddy2[d2];
                if (ex2<0||ex2>=30||ey2<0||ey2>=30) continue;
                if (g.grid[ey2][ex2].terrain == TERRAIN_MEADOW2) nbcount++;
            }
            if (nbcount >= 3) { pseed_x = cx2; pseed_y = cy2; break; }
        }

        static int pfx[64], pfy[64];
        int pfsize = 0, pcount = 0;
        g.grid[pseed_y][pseed_x].terrain = TERRAIN_POND;
        pfx[pfsize] = pseed_x; pfy[pfsize] = pseed_y; pfsize++;
        pcount = 1;

        while (pcount < pond_target && pfsize > 0) {
            int pfi = rand() % pfsize;
            int pcx = pfx[pfi], pcy = pfy[pfi];
            int pnx[4], pny[4], pnc = 0;
            int ddx3[] = {0,0,1,-1}, ddy3[] = {-1,1,0,0};
            int d3;
            for (d3 = 0; d3 < 4; d3++) {
                int ex3 = pcx+ddx3[d3], ey3 = pcy+ddy3[d3];
                if (ex3<1||ex3>28||ey3<1||ey3>28) continue;
                if (g.grid[ey3][ex3].terrain == TERRAIN_MEADOW2) {
                    pnx[pnc] = ex3; pny[pnc] = ey3; pnc++;
                }
            }
            if (pnc == 0) {
                pfx[pfi] = pfx[pfsize-1]; pfy[pfi] = pfy[pfsize-1]; pfsize--;
            } else {
                int ppick = rand() % pnc;
                g.grid[pny[ppick]][pnx[ppick]].terrain = TERRAIN_POND;
                pfx[pfsize] = pnx[ppick]; pfy[pfsize] = pny[ppick]; pfsize++;
                pcount++;
            }
        }

        /* Place dragon at the pond seed cell */
        g.dragon.x = pseed_x;
        g.dragon.y = pseed_y;
        g.dragon.alive = 1;
        g.dragon.surface_turn = g.turn + 3 + rand() % 8;
    }
}

void gen_river(void) {
    /* Sine curve river: period=300, amplitude=10, 2 squares wide.
     * For each column x (0-29), compute center row and mark 2 cells.
     * Random phase offset gives a different angle each game.
     * Clamp rows to [1, 28] to avoid gate row (0) and player start (29).
     * Never overwrite TERRAIN_GATE cells.
     */
    int base_row = rand() % 10 + 10; /* 10..19, at least 5 from row 0 and row 28 */
    double phase = (rand() % 628) / 100.0; /* 0 to ~2*PI */
    int x;
    for (x = 0; x < 30; x++) {
        double center_d = base_row + 10.0 * sin(2.0 * 3.14159265358979 * x / 300.0 + phase);
        int center = (int)center_d;
        int r;
        if (center < 5)  center = 5;
        if (center > 22) center = 22; /* leave room for center+1, stay 5 from south wall */
        for (r = center; r <= center + 1; r++) {
            if (r < 1 || r > 28) continue;
            if (x < 1 || x > 28) continue;
            if (g.grid[r][x].terrain == TERRAIN_GATE ||
                g.grid[r][x].terrain == TERRAIN_WALL) continue;
            g.grid[r][x].terrain = TERRAIN_RIVER;
        }
    }
}
void gen_bridges(void) {
    /* Place 6 evenly spaced bridges over the river.
     * Bridge columns: 2, 7, 12, 17, 22, 27 (spacing of 5 across 30 cols).
     * Each bridge is 2 wide (col, col+1) and 4 tall in the crossing direction.
     * Scan the bridge column for the first TERRAIN_RIVER row, then mark
     * rows [river_start-1 .. river_start+2] (4 rows) as TERRAIN_BRIDGE.
     * All row/col values are clamped to [0, 29].
     */
    static const int bridge_cols[6] = {2, 7, 12, 17, 22, 27};
    int b;
    for (b = 0; b < 6; b++) {
        int col = bridge_cols[b];
        int river_start = -1;
        int row;

        /* Find the first river row in this column */
        for (row = 0; row < 30; row++) {
            if (g.grid[row][col].terrain == TERRAIN_RIVER) {
                river_start = row;
                break;
            }
        }
        if (river_start < 0) continue; /* no river in this column, skip */

        /* Mark a 2-wide, 4-tall area as TERRAIN_BRIDGE */
        int r, c;
        for (r = river_start - 1; r <= river_start + 2; r++) {
            if (r < 0 || r > 29) continue;
            for (c = col; c <= col + 1; c++) {
                if (c < 0 || c > 29) continue;
                g.grid[r][c].terrain = TERRAIN_BRIDGE;
            }
        }
    }
}
void gen_farm(void) {
    /* Farm bounding box: 12 wide x 8 tall = 96 squares (fits inside 28x28 interior).
     * Farmhouse: 2 wide x 3 tall = 6 sq at offset (1,1) from farm origin.
     * Barn: 3 wide x 3 tall = 9 sq, 5 cols right of farmhouse (gap of 3).
     *   barn_x = ox+1+2+3 = ox+6; barn right edge = ox+8. Max ox = 19.
     * All coords stay within cols 1-28, rows 1-28 (inside the wall).
     */
    int attempt;
    for (attempt = 0; attempt < 200; attempt++) {
        /* ox: 1..19 so right edge ox+11 <= 28 (inside wall col 28) */
        int ox = 1 + rand() % 19;
        /* oy: 1..19 so bottom edge oy+7 <= 28 */
        int oy = 1 + rand() % 19;

        int fh_x   = ox + 1;
        int fh_y   = oy + 1;
        int barn_x = fh_x + 5;  /* ox+6 */
        int barn_y = fh_y;

        int ok = 1;
        int r, c;

        /* Check 12x8 farm rectangle */
        for (r = oy; r < oy + 8 && ok; r++) {
            for (c = ox; c < ox + 12 && ok; c++) {
                if (c < 1 || c > 28 || r < 1 || r > 28) { ok = 0; break; }
                Terrain t = g.grid[r][c].terrain;
                if (t == TERRAIN_RIVER  || t == TERRAIN_BRIDGE ||
                    t == TERRAIN_GATE   || t == TERRAIN_FOREST ||
                    t == TERRAIN_MEADOW || t == TERRAIN_WALL) {
                    ok = 0;
                }
                if (c == 15 && r == 28) ok = 0;
            }
        }
        if (!ok) continue;

        /* Mark 12x8 as TERRAIN_FARM */
        for (r = oy; r < oy + 8; r++)
            for (c = ox; c < ox + 12; c++)
                g.grid[r][c].terrain = TERRAIN_FARM;

        /* Farmhouse: 2x3 */
        for (r = fh_y; r < fh_y + 3; r++)
            for (c = fh_x; c < fh_x + 2; c++)
                g.grid[r][c].terrain = TERRAIN_FARMHOUSE;

        /* Barn: 3x3 */
        for (r = barn_y; r < barn_y + 3; r++)
            for (c = barn_x; c < barn_x + 3; c++)
                g.grid[r][c].terrain = TERRAIN_BARN;

        return; /* success */
    }
    /* All attempts failed — gen_world retry will handle */
}

void place_gate(void) {
    int col;
    for (col = 8; col <= 22; col++) {
        g.grid[0][col].terrain = TERRAIN_GATE;
    }
}

void place_wall(void) {
    /* Surround the 30x30 grid with a stone wall on all four edges.
     * Gate occupies row 0 cols 8-22, so those cells stay as TERRAIN_GATE.
     * Each wall cell gets a random style:
     *   0 = moss-covered   1 = ivy-draped
     *   2 = aged bare stone  3 = damaged but strong
     */
    int i;
    /* Top row (row 0): skip gate cols 8-22 */
    for (i = 0; i < 30; i++) {
        if (i >= 8 && i <= 22) continue;
        g.grid[0][i].terrain    = TERRAIN_WALL;
        g.grid[0][i].wall_style = rand() % 4;
    }
    /* Bottom row (row 29) */
    for (i = 0; i < 30; i++) {
        g.grid[29][i].terrain    = TERRAIN_WALL;
        g.grid[29][i].wall_style = rand() % 4;
    }
    /* Left col (col 0), rows 1-28 */
    for (i = 1; i < 29; i++) {
        g.grid[i][0].terrain    = TERRAIN_WALL;
        g.grid[i][0].wall_style = rand() % 4;
    }
    /* Right col (col 29), rows 1-28 */
    for (i = 1; i < 29; i++) {
        g.grid[i][29].terrain    = TERRAIN_WALL;
        g.grid[i][29].wall_style = rand() % 4;
    }
}

void place_items(void) {
    /* Build list of accessible top-left origins for 4x4 blocks (swords)
     * and 5x5 blocks (treasures), staying inside the wall (cols/rows 1-28). */
    int x, y, i, j;

    /* Initialise all item flags */
    for (y = 0; y < 30; y++)
        for (x = 0; x < 30; x++) {
            g.grid[y][x].has_sword    = 0;
            g.grid[y][x].has_flask    = 0;
            g.grid[y][x].has_treasure = -1;
        }

    g.num_swords = 0;

    /* Select 5 treasure types */
    {
        int order[NUM_TREASURE_TYPES];
        for (i = 0; i < NUM_TREASURE_TYPES; i++) order[i] = i;
        for (i = NUM_TREASURE_TYPES - 1; i > 0; i--) {
            j = rand() % (i + 1);
            int tmp = order[i]; order[i] = order[j]; order[j] = tmp;
        }
        for (i = 0; i < 5; i++) g.treasure_type[i] = order[i];
    }

    /* Helper: check if a WxH block at (ox,oy) is clear of impassable terrain
     * and existing items, and inside the wall */
    #define BLOCK_OK(ox, oy, W, H) block_ok_fn(ox, oy, W, H)
    /* (defined inline below as a lambda-style check) */

    /* Place 15 swords as 4x4 blocks */
    {
        int placed = 0;
        int attempts = 0;
        while (placed < 15 && attempts < 100000) {
            attempts++;
            int ox = 1 + rand() % 24; /* 1..24 so ox+3 <= 28 */
            int oy = 1 + rand() % 24;
            /* Skip player start area */
            if (ox <= 15 && 15 <= ox+3 && oy <= 28 && 28 <= oy+3) continue;
            /* Check 4x4 block */
            int ok = 1;
            int r, c;
            for (r = oy; r < oy+4 && ok; r++) {
                for (c = ox; c < ox+4 && ok; c++) {
                    Terrain t = g.grid[r][c].terrain;
                    if (t == TERRAIN_RIVER || t == TERRAIN_FARMHOUSE ||
                        t == TERRAIN_BARN  || t == TERRAIN_WALL ||
                        t == TERRAIN_GATE  || t == TERRAIN_POND) ok = 0;
                    if (g.grid[r][c].has_sword || g.grid[r][c].has_treasure >= 0) ok = 0;
                }
            }
            if (!ok) continue;
            /* Place sword block */
            for (r = oy; r < oy+4; r++)
                for (c = ox; c < ox+4; c++)
                    g.grid[r][c].has_sword = 1;
            g.sword_ox[placed] = ox;
            g.sword_oy[placed] = oy;
            placed++;
            g.num_swords = placed;
        }
    }

    /* Place 20 flasks (single cells) */
    {
        /* Build accessible cell list */
        static int ax[900], ay[900];
        int acount = 0;
        for (y = 1; y < 29; y++) {
            for (x = 1; x < 29; x++) {
                if (x == 15 && y == 28) continue;
                Terrain t = g.grid[y][x].terrain;
                if (t == TERRAIN_OPEN   || t == TERRAIN_FOREST ||
                    t == TERRAIN_MEADOW || t == TERRAIN_MEADOW2 ||
                    t == TERRAIN_FARM   || t == TERRAIN_BRIDGE) {
                    if (!g.grid[y][x].has_sword && g.grid[y][x].has_treasure == -1) {
                        ax[acount] = x; ay[acount] = y; acount++;
                    }
                }
            }
        }
        int placed = 0;
        int attempts = 0;
        while (placed < 20 && acount > 0 && attempts < 100000) {
            attempts++;
            int idx = rand() % acount;
            int cx = ax[idx], cy = ay[idx];
            if (!g.grid[cy][cx].has_flask) {
                g.grid[cy][cx].has_flask = 1;
                placed++;
            }
        }
    }

    /* Place 8 shields (single cells) */
    {
        static int sx2[900], sy2[900];
        int scount = 0;
        for (y = 1; y < 29; y++) {
            for (x = 1; x < 29; x++) {
                if (x == 15 && y == 28) continue;
                Terrain t = g.grid[y][x].terrain;
                if (t == TERRAIN_OPEN   || t == TERRAIN_FOREST ||
                    t == TERRAIN_MEADOW || t == TERRAIN_MEADOW2 ||
                    t == TERRAIN_FARM   || t == TERRAIN_BRIDGE) {
                    if (!g.grid[y][x].has_sword && !g.grid[y][x].has_flask &&
                        g.grid[y][x].has_treasure == -1) {
                        sx2[scount] = x; sy2[scount] = y; scount++;
                    }
                }
            }
        }
        int placed = 0;
        int attempts = 0;
        while (placed < 8 && scount > 0 && attempts < 100000) {
            attempts++;
            int idx = rand() % scount;
            int cx = sx2[idx], cy = sy2[idx];
            if (!g.grid[cy][cx].has_shield) {
                g.grid[cy][cx].has_shield = 1;
                placed++;
            }
        }
    }

    /* Place 5 treasures as 5x5 blocks */
    {
        int placed = 0;
        int attempts = 0;
        while (placed < 5 && attempts < 100000) {
            attempts++;
            int ox = 1 + rand() % 23; /* 1..23 so ox+4 <= 28 */
            int oy = 1 + rand() % 23;
            if (ox <= 15 && 15 <= ox+4 && oy <= 28 && 28 <= oy+4) continue;
            int ok = 1;
            int r, c;
            for (r = oy; r < oy+5 && ok; r++) {
                for (c = ox; c < ox+5 && ok; c++) {
                    Terrain t = g.grid[r][c].terrain;
                    if (t == TERRAIN_RIVER || t == TERRAIN_FARMHOUSE ||
                        t == TERRAIN_BARN  || t == TERRAIN_WALL ||
                        t == TERRAIN_GATE  || t == TERRAIN_POND) ok = 0;
                    if (g.grid[r][c].has_sword || g.grid[r][c].has_treasure >= 0) ok = 0;
                }
            }
            if (!ok) continue;
            for (r = oy; r < oy+5; r++)
                for (c = ox; c < ox+5; c++)
                    g.grid[r][c].has_treasure = placed;
            g.treasure_ox[placed] = ox;
            g.treasure_oy[placed] = oy;
            placed++;
        }
    }

    /* Pick one treasure to be locked — randomly chosen */
    if (5 > 0) {
        g.locked_treasure = rand() % 5;
    }

    /* Place the ancient key on a random accessible cell (not near player start) */
    {
        int attempts2 = 0;
        g.key_taken = 0;
        g.key_x = -1; g.key_y = -1;
        while (attempts2 < 100000) {
            int kx = 1 + rand() % 28;
            int ky = 1 + rand() % 27;
            int ddx = kx - 15; if (ddx < 0) ddx = -ddx;
            int ddy = ky - 28; if (ddy < 0) ddy = -ddy;
            if (ddx < 5 && ddy < 5) { attempts2++; continue; }
            Terrain t = g.grid[ky][kx].terrain;
            if (t == TERRAIN_OPEN || t == TERRAIN_MEADOW || t == TERRAIN_MEADOW2 ||
                t == TERRAIN_FOREST || t == TERRAIN_FARM || t == TERRAIN_BRIDGE) {
                if (!g.grid[ky][kx].has_sword && g.grid[ky][kx].has_treasure < 0
                    && !g.grid[ky][kx].has_flask) {
                    g.key_x = kx; g.key_y = ky;
                    break;
                }
            }
            attempts2++;
        }
    }

    /* Place Malachar's scroll on a random accessible cell */
    {
        int attempts3 = 0;
        while (attempts3 < 100000) {
            int sx2 = 1 + rand() % 28;
            int sy2 = 1 + rand() % 27;
            int ddx = sx2 - 15; if (ddx < 0) ddx = -ddx;
            int ddy = sy2 - 28; if (ddy < 0) ddy = -ddy;
            if (ddx < 5 && ddy < 5) { attempts3++; continue; }
            Terrain t = g.grid[sy2][sx2].terrain;
            if (t == TERRAIN_OPEN || t == TERRAIN_MEADOW || t == TERRAIN_MEADOW2 ||
                t == TERRAIN_FOREST || t == TERRAIN_FARM || t == TERRAIN_BRIDGE) {
                if (!g.grid[sy2][sx2].has_sword && g.grid[sy2][sx2].has_treasure < 0
                    && !g.grid[sy2][sx2].has_flask && !g.grid[sy2][sx2].has_scroll
                    && !(sx2 == g.key_x && sy2 == g.key_y)) {
                    g.grid[sy2][sx2].has_scroll = 1;
                    break;
                }
            }
            attempts3++;
        }
    }
}
void place_npcs(void) {
    int i, j, x, y, attempts;

    /* ---- Place 10 orcs ---- */
    for (i = 0; i < MAX_ORCS; i++) {
        g.orcs[i].alive = 0;
    }
    {
        int placed = 0;
        for (attempts = 0; attempts < 10000 && placed < MAX_ORCS; attempts++) {
            x = rand() % 30;
            y = rand() % 30;
            Terrain t = g.grid[y][x].terrain;
            /* Must be accessible, not farmhouse/barn/river, not player start */
            if (t == TERRAIN_FARMHOUSE || t == TERRAIN_BARN ||
                t == TERRAIN_RIVER    || t == TERRAIN_GATE) continue;
            if (t == TERRAIN_OPEN   || t == TERRAIN_FOREST ||
                t == TERRAIN_MEADOW || t == TERRAIN_MEADOW2 ||
                t == TERRAIN_FARM   || t == TERRAIN_BRIDGE) {
                /* skip player start */
                if (x == 15 && y == 28) continue;
                /* check minimum distance of 40 from all placed orcs */
                int ok = 1;
                for (j = 0; j < placed; j++) {
                    int dx = x - g.orcs[j].x;
                    int dy = y - g.orcs[j].y;
                    if (dx * dx + dy * dy < ORC_MIN_DIST * ORC_MIN_DIST) { ok = 0; break; }
                }
                if (!ok) continue;
                g.orcs[placed].x          = x;
                g.orcs[placed].y          = y;
                g.orcs[placed].hp         = 50;
                g.orcs[placed].alive      = 1;
                g.orcs[placed].chasing    = 0;
                g.orcs[placed].chase_dist = 0;
                g.orcs[placed].spotted    = 0;
                g.orcs[placed].chase_ox   = x;
                g.orcs[placed].chase_oy   = y;
                g.orcs[placed].is_warchief = 0;
                placed++;
            }
        }
    }
    /* Designate orc 0 as the Warchief */
    if (g.orcs[0].alive) {
        g.orcs[0].hp = 100;
        g.orcs[0].is_warchief = 1;
    }

    /* ---- Place 6 elves (non-forest, visible areas) ---- */
    for (i = 0; i < MAX_ELVES; i++) {
        g.elves[i].alive = 0;
    }
    {
        int placed = 0;
        for (attempts = 0; attempts < 10000 && placed < MAX_ELVES; attempts++) {
            x = rand() % 30;
            y = rand() % 30;
            Terrain t = g.grid[y][x].terrain;
            /* Elves go in visible (non-forest) areas */
            if (t == TERRAIN_OPEN   || t == TERRAIN_MEADOW ||
                t == TERRAIN_MEADOW2 || t == TERRAIN_FARM  ||
                t == TERRAIN_BRIDGE || t == TERRAIN_GATE) {
                if (x == 15 && y == 28) continue;
                /* Enforce 20-unit minimum separation between leprechauns */
                int ok = 1;
                for (j = 0; j < placed; j++) {
                    int ddx = x - g.elves[j].x;
                    int ddy = y - g.elves[j].y;
                    if (ddx < 0) ddx = -ddx;
                    if (ddy < 0) ddy = -ddy;
                    int d = ddx > ddy ? ddx : ddy;
                    if (d < 20) { ok = 0; break; }
                }
                if (!ok) continue;
                g.elves[placed].x            = x;
                g.elves[placed].y            = y;
                g.elves[placed].alive        = 1;
                g.elves[placed].patrol_cx    = x;
                g.elves[placed].patrol_cy    = y;
                g.elves[placed].turn_counter = 0;
                placed++;
            }
        }
    }

    /* ---- Place animals ---- */
    g.num_animals = 0;

    /* Collect meadow squares (both meadow types) */
    static int mx[900], my[900];
    int mcount = 0;
    for (y = 0; y < 30; y++) {
        for (x = 0; x < 30; x++) {
            if (g.grid[y][x].terrain == TERRAIN_MEADOW ||
                g.grid[y][x].terrain == TERRAIN_MEADOW2) {
                mx[mcount] = x;
                my[mcount] = y;
                mcount++;
            }
        }
    }

    /* Collect farm squares */
    static int farmx[900], farmy[900];
    int farmcount = 0;
    for (y = 0; y < 30; y++) {
        for (x = 0; x < 30; x++) {
            if (g.grid[y][x].terrain == TERRAIN_FARM) {
                farmx[farmcount] = x;
                farmy[farmcount] = y;
                farmcount++;
            }
        }
    }

    /* Place ~10 birds on meadow */
    if (mcount > 0) {
        int count = 10;
        if (count > mcount) count = mcount;
        for (i = 0; i < count && g.num_animals < MAX_ANIMALS; i++) {
            int idx = rand() % mcount;
            Animal *a = &g.animals[g.num_animals++];
            a->x            = mx[idx];
            a->y            = my[idx];
            a->type         = ANIMAL_BIRD;
            a->alive        = 1;
            a->turn_counter = 0;
        }
    }

    /* Place ~10 rabbits on meadow */
    if (mcount > 0) {
        int count = 10;
        if (count > mcount) count = mcount;
        for (i = 0; i < count && g.num_animals < MAX_ANIMALS; i++) {
            int idx = rand() % mcount;
            Animal *a = &g.animals[g.num_animals++];
            a->x            = mx[idx];
            a->y            = my[idx];
            a->type         = ANIMAL_RABBIT;
            a->alive        = 1;
            a->turn_counter = 0;
        }
    }

    /* Place 20 cows on farm squares */
    if (farmcount > 0) {
        int count = 20;
        if (count > farmcount) count = farmcount;
        for (i = 0; i < count && g.num_animals < MAX_ANIMALS; i++) {
            int idx = rand() % farmcount;
            Animal *a = &g.animals[g.num_animals++];
            a->x            = farmx[idx];
            a->y            = farmy[idx];
            a->type         = ANIMAL_COW;
            a->alive        = 1;
            a->turn_counter = 0;
        }
    }

    /* Place 5 horses on farm squares */
    if (farmcount > 0) {
        int count = 5;
        if (count > farmcount) count = farmcount;
        for (i = 0; i < count && g.num_animals < MAX_ANIMALS; i++) {
            int idx = rand() % farmcount;
            Animal *a = &g.animals[g.num_animals++];
            a->x            = farmx[idx];
            a->y            = farmy[idx];
            a->type         = ANIMAL_HORSE;
            a->alive        = 1;
            a->turn_counter = 0;
        }
    }

    /* Collect open-ground squares for deer and wolf */
    {
        static int ox[900], oy[900];
        int ocount = 0;
        for (y = 0; y < 30; y++)
            for (x = 0; x < 30; x++)
                if (g.grid[y][x].terrain == TERRAIN_OPEN) {
                    ox[ocount] = x; oy[ocount] = y; ocount++;
                }

        /* Place 4 deer on open ground */
        if (ocount > 0) {
            int count = 4;
            if (count > ocount) count = ocount;
            for (i = 0; i < count && g.num_animals < MAX_ANIMALS; i++) {
                int idx = rand() % ocount;
                Animal *a = &g.animals[g.num_animals++];
                a->x                 = ox[idx];
                a->y                 = oy[idx];
                a->type              = ANIMAL_DEER;
                a->alive             = 1;
                a->turn_counter      = 0;
                a->wolf_menace_turns = 0;
            }
        }

        /* Place 1 wolf on open ground, away from player start */
        if (ocount > 0) {
            for (attempts = 0; attempts < 1000 && g.num_animals < MAX_ANIMALS; attempts++) {
                int idx = rand() % ocount;
                int wx = ox[idx], wy = oy[idx];
                int ddx = wx - 15, ddy = wy - 28;
                if (ddx < 0) ddx = -ddx;
                if (ddy < 0) ddy = -ddy;
                if (ddx < 10 && ddy < 10) continue; /* keep away from start */
                Animal *a = &g.animals[g.num_animals++];
                a->x                 = wx;
                a->y                 = wy;
                a->type              = ANIMAL_WOLF;
                a->alive             = 1;
                a->turn_counter      = 0;
                a->wolf_menace_turns = 0;
                a->wolf_menace_threshold = 0; /* set randomly on first spot */
                break;
            }
        }
    }

    /* ---- Place merchant on a farm cell ---- */
    g.merchant_alive = 0;
    {
        int mx2[900], my2[900], mc = 0;
        int fx2, fy2;
        for (fy2 = 0; fy2 < 30; fy2++)
            for (fx2 = 0; fx2 < 30; fx2++)
                if (g.grid[fy2][fx2].terrain == TERRAIN_FARM) {
                    mx2[mc] = fx2; my2[mc] = fy2; mc++;
                }
        if (mc > 0) {
            int idx = rand() % mc;
            g.merchant_x = mx2[idx];
            g.merchant_y = my2[idx];
            g.merchant_alive = 1;
        }
    }
}

void place_landmarks(void) {
    int x, y, r, c, attempts;

    /* --- Ruined tower: 2x2 impassable block --- */
    for (attempts = 0; attempts < 10000; attempts++) {
        int ox = 2 + rand() % 25;
        int oy = 2 + rand() % 25;
        int ok = 1;
        for (r = oy; r < oy+2 && ok; r++)
            for (c = ox; c < ox+2 && ok; c++) {
                Terrain t = g.grid[r][c].terrain;
                if (t != TERRAIN_OPEN && t != TERRAIN_MEADOW && t != TERRAIN_MEADOW2) ok = 0;
                if (g.grid[r][c].has_sword || g.grid[r][c].has_treasure >= 0) ok = 0;
            }
        if (!ok) continue;
        for (r = oy; r < oy+2; r++)
            for (c = ox; c < ox+2; c++)
                g.grid[r][c].terrain = TERRAIN_TOWER;
        break;
    }

    /* --- Graveyard: 3x3 walkable area --- */
    for (attempts = 0; attempts < 10000; attempts++) {
        int ox = 2 + rand() % 24;
        int oy = 2 + rand() % 24;
        int ok = 1;
        for (r = oy; r < oy+3 && ok; r++)
            for (c = ox; c < ox+3 && ok; c++) {
                Terrain t = g.grid[r][c].terrain;
                if (t != TERRAIN_OPEN) ok = 0;
                if (g.grid[r][c].has_sword || g.grid[r][c].has_treasure >= 0) ok = 0;
                /* keep away from player start */
                int ddx = c - 15; if (ddx < 0) ddx = -ddx;
                int ddy = r - 28; if (ddy < 0) ddy = -ddy;
                if (ddx < 4 && ddy < 4) ok = 0;
            }
        if (!ok) continue;
        for (r = oy; r < oy+3; r++)
            for (c = ox; c < ox+3; c++)
                g.grid[r][c].terrain = TERRAIN_GRAVEYARD;
        break;
    }

    /* --- Signposts: placed on open ground near player start (rows 20-27) ---
     * Each points toward a landmark. One per destination.
     * First signpost is guaranteed within 3 squares of player start (15,28). */
    static const int sign_dest[5] = {1, 2, 3, 4, 5}; /* farm,gate,pond,forest,merchant */
    static const char *sign_labels[6] = {
        "", "FARM", "GATE", "POND", "FOREST", "MERCHANT"
    };
    int placed = 0;
    int si;
    (void)sign_labels;

    /* Place first signpost very close to player start */
    for (attempts = 0; attempts < 5000; attempts++) {
        x = 13 + rand() % 5;  /* cols 13-17 */
        y = 25 + rand() % 3;  /* rows 25-27 */
        if (x == 15 && y == 28) continue;
        Terrain t = g.grid[y][x].terrain;
        if (t != TERRAIN_OPEN && t != TERRAIN_MEADOW && t != TERRAIN_FARM) continue;
        if (g.grid[y][x].signpost != 0) continue;
        if (g.grid[y][x].has_sword || g.grid[y][x].has_treasure >= 0) continue;
        g.grid[y][x].signpost = sign_dest[0];
        placed++;
        break;
    }

    /* Place remaining signposts in rows 18-27 */
    for (si = 1; si < 5 && placed < 5; si++) {
        for (attempts = 0; attempts < 5000; attempts++) {
            x = 2 + rand() % 26;
            y = 18 + rand() % 10;
            if (x == 15 && y == 28) continue;
            Terrain t = g.grid[y][x].terrain;
            if (t != TERRAIN_OPEN && t != TERRAIN_MEADOW && t != TERRAIN_FARM) continue;
            if (g.grid[y][x].signpost != 0) continue;
            if (g.grid[y][x].has_sword || g.grid[y][x].has_treasure >= 0) continue;
            g.grid[y][x].signpost = sign_dest[si];
            placed++;
            break;
        }
    }
}

void gen_world(void) {
    int retry;
    for (retry = 0; retry < 50; retry++) {
        int x, y;
        int forest_count, meadow_count, bridge_count;
        Terrain start_terrain;

        memset(g.grid, 0, sizeof(g.grid));

        /* Order: gate first (fixed), then large biomes on open grid,
         * then river+bridges (cuts through), then farm last.
         * This gives flood-fill the maximum open space to work with. */
        place_gate();
        place_wall();
        gen_forest();
        gen_meadow();
        gen_meadow2();
        gen_river();
        gen_bridges();
        gen_farm();
        place_items();
        place_npcs();
        place_landmarks();

        /* Scatter open-ground features: wildflowers, mud, bushes */
        {
            int fx, fy;
            for (fy = 0; fy < 30; fy++) {
                for (fx = 0; fx < 30; fx++) {
                    if (g.grid[fy][fx].terrain != TERRAIN_OPEN) continue;
                    int r = rand() % 100;
                    if      (r < 15) g.grid[fy][fx].open_feature = 1; /* wildflowers */
                    else if (r < 25) g.grid[fy][fx].open_feature = 2; /* mud */
                    else if (r < 35) g.grid[fy][fx].open_feature = 3; /* bushes */
                    else             g.grid[fy][fx].open_feature = 0;
                }
            }
        }

        /* Validate forest count: at least 60 cells */
        forest_count = 0;
        for (y = 0; y < 30; y++)
            for (x = 0; x < 30; x++)
                if (g.grid[y][x].terrain == TERRAIN_FOREST) forest_count++;
        if (forest_count < 60) continue;

        /* Validate meadow count: at least 120 cells */
        meadow_count = 0;
        for (y = 0; y < 30; y++)
            for (x = 0; x < 30; x++)
                if (g.grid[y][x].terrain == TERRAIN_MEADOW) meadow_count++;
        if (meadow_count < 120) continue;

        /* Validate player start square (15, 28) is passable */
        start_terrain = g.grid[28][15].terrain;
        if (start_terrain == TERRAIN_RIVER    ||
            start_terrain == TERRAIN_FARMHOUSE ||
            start_terrain == TERRAIN_BARN) continue;

        /* Validate at least one bridge exists */
        bridge_count = 0;
        for (y = 0; y < 30; y++)
            for (x = 0; x < 30; x++)
                if (g.grid[y][x].terrain == TERRAIN_BRIDGE) bridge_count++;
        if (bridge_count == 0) continue;

        return; /* all validations passed */
    }

    fprintf(stderr, "Error: world generation failed after 50 retries.\n");
    exit(1);
}

/* =========================================================
 * Rendering stubs
 * ========================================================= */

int visibility_radius(void) {
    Terrain t = g.grid[g.player.y][g.player.x].terrain;
    if (t == TERRAIN_FOREST) return 5;
    if (t == TERRAIN_MEADOW || t == TERRAIN_MEADOW2 ||
        t == TERRAIN_FARM   || t == TERRAIN_FARMHOUSE ||
        t == TERRAIN_BARN) return 10;
    return 2;
}

void render_ascii(void) {
    int px = g.player.x;
    int py = g.player.y;
    int row, col;

    /* Key lines printed to the right of the map (one per map row) */
    static const char *key_lines[] = {
        "          " "--- MAP KEY ---",
        "          @ You",
        "          . Open    T Forest",
        "          , Meadow  ; Meadow2",
        "          o Pond    ~ River",
        "          = Bridge  _ Farm",
        "          H House   B Barn",
        "          G Gate    # Wall",
        "          / Sword   ! Flask",
        "          ] Shield  ? Scroll  K Key",
        "          ? Scroll  S Sign",
        "          b Bird    r Rabbit",
        "          ^ Tower   + Graveyard",
        ""
    };
    int num_key_lines = 14;

    /* 10x10 viewport centred on player */
    int view_r = 4;
    int row_min = py - view_r;
    int row_max = py + view_r + 1;
    int col_min = px - view_r;
    int col_max = px + view_r + 1;

    /* Print entire map block in yellow */
    printf(COL_YELLOW);
    int map_row_idx = 0;
    for (row = row_min; row <= row_max; row++) {
        /* Print one map row */
        for (col = col_min; col <= col_max; col++) {
            if (col < 0 || col >= 30 || row < 0 || row >= 30) {
                putchar(' ');
                continue;
            }

            /* Player */
            if (col == px && row == py) { putchar('@'); continue; }

            /* Animals */
            {
                int found = 0, i;
                for (i = 0; i < g.num_animals; i++) {
                    if (g.animals[i].alive && g.animals[i].x == col && g.animals[i].y == row) {
                        char glyph;
                        switch (g.animals[i].type) {
                            case ANIMAL_BIRD:   glyph = 'b'; break;
                            case ANIMAL_RABBIT: glyph = 'r'; break;
                            case ANIMAL_COW:    glyph = 'c'; break;
                            case ANIMAL_HORSE:  glyph = 'h'; break;
                            case ANIMAL_DEER:   glyph = 'd'; break;
                            case ANIMAL_WOLF:   glyph = 'w'; break;
                            default:            glyph = 'a'; break;
                        }
                        putchar(glyph); found = 1; break;
                    }
                }
                if (found) continue;
            }

            /* Items */
            {
                Cell *cell = &g.grid[row][col];
                if (!g.key_taken && col == g.key_x && row == g.key_y) { putchar('K'); continue; }
                if (cell->signpost > 0)      { putchar('S'); continue; }
                if (cell->has_scroll)        { putchar('?'); continue; }
                if (cell->has_shield)        { putchar(']'); continue; }
                if (cell->has_sword)         { putchar('/'); continue; }
                if (cell->has_flask)         { putchar('!'); continue; }
                if (cell->has_treasure >= 0) { putchar('$'); continue; }
            }

            /* Terrain */
            {
                char glyph;
                switch (g.grid[row][col].terrain) {
                    case TERRAIN_OPEN:      glyph = '.'; break;
                    case TERRAIN_FOREST:    glyph = 'T'; break;
                    case TERRAIN_MEADOW:    glyph = ','; break;
                    case TERRAIN_MEADOW2:   glyph = ';'; break;
                    case TERRAIN_POND:      glyph = 'o'; break;
                    case TERRAIN_RIVER:     glyph = '~'; break;
                    case TERRAIN_BRIDGE:    glyph = '='; break;
                    case TERRAIN_FARM:      glyph = '_'; break;
                    case TERRAIN_FARMHOUSE: glyph = 'H'; break;
                    case TERRAIN_BARN:      glyph = 'B'; break;
                    case TERRAIN_GATE:      glyph = 'G'; break;
                    case TERRAIN_WALL:      glyph = '#'; break;
                    case TERRAIN_TOWER:     glyph = '^'; break;
                    case TERRAIN_GRAVEYARD: glyph = '+'; break;
                    default:                glyph = '.'; break;
                }
                putchar(glyph);
            }
        }

        /* Append key line to the right */
        if (map_row_idx < num_key_lines)
            printf("%s", key_lines[map_row_idx]);
        map_row_idx++;
        putchar('\n');
    }
    printf(COL_WHITE);

    /* Status bar */
    {
        const char *hp_col = g.player.hp > 70 ? COL_GREEN :
                             g.player.hp > 40 ? COL_YELLOW : COL_RED;
        printf("HP:%s%d%s  Score:%d  Sword:%s%s%s  Shield:%s%s%s  Flasks:%s%d%s\n",
               hp_col, g.player.hp, COL_RESET,
               g.player.score,
               g.player.has_sword ? COL_GREEN : "", g.player.has_sword ? "yes" : "no",
               g.player.has_sword ? COL_RESET : "",
               g.player.has_shield ? COL_GREEN : "", g.player.has_shield ? "yes" : "no",
               g.player.has_shield ? COL_RESET : "",
               g.player.flask_count > 0 ? COL_GREEN : "", g.player.flask_count,
               g.player.flask_count > 0 ? COL_RESET : "");
    }
}

void render_prose(void) {
    int px = g.player.x;
    int py = g.player.y;
    int vr = visibility_radius();
    const char *terrain_name;

    switch (g.grid[py][px].terrain) {
        case TERRAIN_OPEN:      terrain_name = "open ground"; break;
        case TERRAIN_FOREST:    terrain_name = "a dense forest"; break;
        case TERRAIN_MEADOW:    terrain_name = "a meadow"; break;
        case TERRAIN_MEADOW2:   terrain_name = "a meadow by the pond"; break;
        case TERRAIN_RIVER:     terrain_name = "a river"; break;
        case TERRAIN_BRIDGE:    terrain_name = "a bridge"; break;
        case TERRAIN_FARM:      terrain_name = "a farm"; break;
        case TERRAIN_FARMHOUSE: terrain_name = "a farmhouse"; break;
        case TERRAIN_BARN:      terrain_name = "a barn"; break;
        case TERRAIN_GATE:      terrain_name = "the gate"; break;
        case TERRAIN_GRAVEYARD: terrain_name = "a graveyard"; break;
        default:                terrain_name = "open ground"; break;
    }
    printf("You are standing on %s at (%d,%d).\n", terrain_name, px, py);

    /* Describe open-ground feature underfoot */
    if (g.grid[py][px].terrain == TERRAIN_OPEN) {
        switch (g.grid[py][px].open_feature) {
            case 1: printf("Patches of wild flowers dot the ground around you.\n"); break;
            case 2: printf("The ground here is soft and muddy underfoot.\n"); break;
            case 3: printf("Scrubby bushes cluster nearby.\n"); break;
            default: break;
        }
    }

    /* Describe visible entities */
    {
        int i;
        for (i = 0; i < MAX_ORCS; i++) {
            if (!g.orcs[i].alive) continue;
            int dx = g.orcs[i].x - px;
            int dy = g.orcs[i].y - py;
            int dist = dx < 0 ? -dx : dx;
            int dy_abs = dy < 0 ? -dy : dy;
            if (dy_abs > dist) dist = dy_abs;
            if (dist <= 4)
                printf("An orc lurks nearby (HP:%d).\n", g.orcs[i].hp);
        }
        for (i = 0; i < MAX_ELVES; i++) {
            if (!g.elves[i].alive) continue;
            int dx = g.elves[i].x - px;
            int dy = g.elves[i].y - py;
            int dist = dx < 0 ? -dx : dx;
            int dy_abs = dy < 0 ? -dy : dy;
            if (dy_abs > dist) dist = dy_abs;
            if (dist <= vr)
                printf("A leprechaun watches you from nearby.\n");
        }
        {
            int counts[6]  = {0,0,0,0,0,0};
            int closest[6] = {9999,9999,9999,9999,9999,9999};
            int close_x[6] = {0,0,0,0,0,0};
            int close_y[6] = {0,0,0,0,0,0};
            static const char *singular[6] = {"a bird","a rabbit","a cow","a horse","a deer","a wolf"};
            static const char *plural[6]   = {"birds","rabbits","cows","horses","deer","wolves"};
            int t;
            for (i = 0; i < g.num_animals; i++) {
                if (!g.animals[i].alive) continue;
                int dx = g.animals[i].x - px;
                int dy = g.animals[i].y - py;
                int adx = dx < 0 ? -dx : dx;
                int ady = dy < 0 ? -dy : dy;
                int dist = adx > ady ? adx : ady;
                if (dist > vr) continue;
                t = (int)g.animals[i].type;
                counts[t]++;
                if (dist < closest[t]) {
                    closest[t] = dist;
                    close_x[t] = g.animals[i].x;
                    close_y[t] = g.animals[i].y;
                }
            }
            for (t = 0; t < 6; t++) {
                if (counts[t] == 0) continue;
                if (t == (int)ANIMAL_WOLF) {
                    /* Wolf gets its own menacing description */
                    printf("A wolf watches you from the %s, %s.\n",
                           compass_dir(px, py, close_x[t], close_y[t]),
                           dist_label(closest[t]));
                } else if (counts[t] == 1) {
                    printf("You see %s to the %s, %s.\n",
                           singular[t],
                           compass_dir(px, py, close_x[t], close_y[t]),
                           dist_label(closest[t]));
                } else {
                    printf("You see %d %s to the %s, %s.\n",
                           counts[t], plural[t],
                           compass_dir(px, py, close_x[t], close_y[t]),
                           dist_label(closest[t]));
                }
            }
        }
    }

    /* Describe items on current square */
    {
        Cell *cell = &g.grid[py][px];
        if (cell->has_sword)          printf("There is a sword here.\n");
        if (cell->has_shield)         printf("There is a shield here.\n");
        if (cell->has_flask)          printf("There is a flask here.\n");
        if (cell->has_treasure >= 0)  printf("There is treasure here!\n");
    }

    /* Signposts visible within visibility radius */
    {
        static const char *sign_dest_name[6] = {
            "", "the farm", "the gate", "the pond", "the forest", "the merchant"
        };
        int r, c;
        for (r = py - vr; r <= py + vr; r++) {
            for (c = px - vr; c <= px + vr; c++) {
                if (c < 0 || c >= 30 || r < 0 || r >= 30) continue;
                int sp = g.grid[r][c].signpost;
                if (sp <= 0) continue;
                if (c == px && r == py) continue;
                int sdx = c - px; if (sdx < 0) sdx = -sdx;
                int sdy = r - py; if (sdy < 0) sdy = -sdy;
                int sdist = sdx > sdy ? sdx : sdy;
                printf(COL_YELLOW "A signpost to the %s, %s, points toward %s.\n" COL_RESET,
                       compass_dir(px, py, c, r), dist_label(sdist), sign_dest_name[sp]);
            }
        }
    }

    /* Status bar */
    {
        const char *hp_col = g.player.hp > 70 ? COL_GREEN :
                             g.player.hp > 40 ? COL_YELLOW : COL_RED;
        printf("HP:%s%d%s  Score:%d  Sword:%s%s%s  Shield:%s%s%s  Flasks:%s%d%s\n",
               hp_col, g.player.hp, COL_RESET,
               g.player.score,
               g.player.has_sword ? COL_GREEN : "", g.player.has_sword ? "yes" : "no",
               g.player.has_sword ? COL_RESET : "",
               g.player.has_shield ? COL_GREEN : "", g.player.has_shield ? "yes" : "no",
               g.player.has_shield ? COL_RESET : "",
               g.player.flask_count > 0 ? COL_GREEN : "", g.player.flask_count,
               g.player.flask_count > 0 ? COL_RESET : "");
    }
}

void render(void) {
    if (g.skip_render) { g.skip_render = 0; return; }
    if (g.mode == MODE_ASCII) render_ascii();
    else                      render_prose();
}

/* =========================================================
 * Command parsing stub
 * ========================================================= */

Command read_command(void) {
    char buf[128];
    printf("> ");
    if (!fgets(buf, sizeof(buf), stdin)) return CMD_QUIT;
    /* strip newline */
    buf[strcspn(buf, "\n")] = '\0';
    /* lowercase */
    {
        int i;
        for (i = 0; buf[i]; i++) buf[i] = (char)tolower((unsigned char)buf[i]);
    }
    if (strcmp(buf, "n") == 0 || strcmp(buf, "north") == 0) return CMD_NORTH;
    if (strcmp(buf, "s") == 0 || strcmp(buf, "south") == 0) return CMD_SOUTH;
    if (strcmp(buf, "e") == 0 || strcmp(buf, "east")  == 0) return CMD_EAST;
    if (strcmp(buf, "w") == 0 || strcmp(buf, "west")  == 0) return CMD_WEST;
    if (strcmp(buf, "ne") == 0 || strcmp(buf, "northeast") == 0) return CMD_NORTHEAST;
    if (strcmp(buf, "se") == 0 || strcmp(buf, "southeast") == 0) return CMD_SOUTHEAST;
    if (strcmp(buf, "sw") == 0 || strcmp(buf, "southwest") == 0) return CMD_SOUTHWEST;
    if (strcmp(buf, "nw") == 0 || strcmp(buf, "northwest") == 0) return CMD_NORTHWEST;
    if (strcmp(buf, "attack") == 0) return CMD_ATTACK;
    if (strcmp(buf, "fight")  == 0) return CMD_FIGHT;
    if (strcmp(buf, "hello")  == 0) return CMD_HELLO;
    if (strcmp(buf, "pet")    == 0) return CMD_PET;
    if (strcmp(buf, "talk")   == 0) return CMD_TALK;
    if (strcmp(buf, "drink")  == 0) return CMD_DRINK;
    if (strcmp(buf, "pick up sword") == 0 ||
        strcmp(buf, "take sword") == 0 || strcmp(buf, "get sword") == 0) return CMD_PICKUP_SWORD;
    if (strcmp(buf, "pick up flask") == 0 ||
        strcmp(buf, "take flask") == 0 || strcmp(buf, "get flask") == 0) return CMD_PICKUP_FLASK;
    if (strcmp(buf, "pick up treasure") == 0 || strcmp(buf, "take treasure") == 0 ||
        strcmp(buf, "get treasure") == 0 || strcmp(buf, "grab treasure") == 0 ||
        strcmp(buf, "take") == 0 || strcmp(buf, "get") == 0) return CMD_PICKUP_TREASURE;
    if (strcmp(buf, "look")      == 0) return CMD_LOOK;
    if (strcmp(buf, "score")     == 0) return CMD_SCORE;
    if (strcmp(buf, "inventory") == 0) return CMD_INVENTORY;
    if (strcmp(buf, "help")      == 0) return CMD_HELP;
    if (strcmp(buf, "quit")      == 0) return CMD_QUIT;
    if (strcmp(buf, "left turn")  == 0 || strcmp(buf, "turn left")  == 0) return CMD_TURN_LEFT;
    if (strcmp(buf, "right turn") == 0 || strcmp(buf, "turn right") == 0) return CMD_TURN_RIGHT;
    if (strcmp(buf, "map") == 0) return CMD_MAP;
    if (strcmp(buf, "flee") == 0) return CMD_FLEE;
    if (strcmp(buf, "run") == 0) return CMD_RUN_PROMPT;
    if (strcmp(buf, "run north") == 0 || strcmp(buf, "run n") == 0) return CMD_RUN_NORTH;
    if (strcmp(buf, "run south") == 0 || strcmp(buf, "run s") == 0) return CMD_RUN_SOUTH;
    if (strcmp(buf, "run east")  == 0 || strcmp(buf, "run e") == 0) return CMD_RUN_EAST;
    if (strcmp(buf, "run west")  == 0 || strcmp(buf, "run w") == 0) return CMD_RUN_WEST;
    if (strcmp(buf, "run northeast") == 0 || strcmp(buf, "run ne") == 0) return CMD_RUN_NORTHEAST;
    if (strcmp(buf, "run southeast") == 0 || strcmp(buf, "run se") == 0) return CMD_RUN_SOUTHEAST;
    if (strcmp(buf, "run southwest") == 0 || strcmp(buf, "run sw") == 0) return CMD_RUN_SOUTHWEST;
    if (strcmp(buf, "run northwest") == 0 || strcmp(buf, "run nw") == 0) return CMD_RUN_NORTHWEST;
    if (strcmp(buf, "rest") == 0) return CMD_REST;
    if (strcmp(buf, "dodge") == 0 || strcmp(buf, "parry") == 0) return CMD_DODGE;
    if (strcmp(buf, "save") == 0) return CMD_SAVE;
    if (strcmp(buf, "load") == 0) return CMD_LOAD;
    if (strcmp(buf, "highscores") == 0 || strcmp(buf, "high scores") == 0 ||
        strcmp(buf, "scores") == 0 || strcmp(buf, "hiscore") == 0) return CMD_HIGHSCORES;
    if (strcmp(buf, "buy")  == 0 || strcmp(buf, "shop") == 0 || strcmp(buf, "trade") == 0) return CMD_BUY;
    return CMD_UNKNOWN;
}

/* =========================================================
 * Player action stubs
 * ========================================================= */

/* =========================================================
 * Directional view description (called after each move)
 * ========================================================= */

/* Return a short terrain label for prose descriptions */
static const char *terrain_label(Terrain t) {
    switch (t) {
        case TERRAIN_FOREST:    return "forest";
        case TERRAIN_MEADOW:    return "meadow";
        case TERRAIN_RIVER:     return "river";
        case TERRAIN_BRIDGE:    return "bridge";
        case TERRAIN_FARM:      return "farmland";
        case TERRAIN_FARMHOUSE: return "farmhouse";
        case TERRAIN_BARN:      return "barn";
        case TERRAIN_GATE:      return "the great gate";
        case TERRAIN_WALL:      return "the stone wall";
        default:                return "open ground";
    }
}

static const char *wall_style_desc(int style) {
    switch (style) {
        case 0: return "thick with moss";
        case 1: return "draped in ivy";
        case 2: return "bare ancient stone, showing great age";
        default: return "damaged in places, but still formidably strong";
    }
}

/*
 * Scan one ray from (px,py) in direction (ddx,ddy) up to max_dist steps.
 * Returns the distance at which the terrain first changes from the starting
 * terrain, or -1 if it never changes within range.
 * Also fills *end_terrain with the terrain at the last visible cell.
 */
static int scan_ray(int px, int py, int ddx, int ddy,
                    int max_dist, Terrain *end_terrain) {
    Terrain start = g.grid[py][px].terrain;
    Terrain last  = start;
    int change_dist = -1;
    int step;
    for (step = 1; step <= max_dist; step++) {
        int cx = px + ddx * step;
        int cy = py + ddy * step;
        if (cx < 0 || cx >= 30 || cy < 0 || cy >= 30) break;
        Terrain t = g.grid[cy][cx].terrain;
        last = t;
        if (t != start && change_dist < 0) {
            change_dist = step;
        }
    }
    *end_terrain = last;
    return change_dist;
}

/*
 * Given facing (0=N,1=E,2=S,3=W) and a lateral offset (-1=left, 0=ahead, +1=right),
 * compute the unit direction vector (ddx, ddy).
 *
 * Facing vectors:  N=(0,-1)  E=(1,0)  S=(0,1)  W=(-1,0)
 * Left of facing:  N->W  E->N  S->E  W->S
 * Right of facing: N->E  E->S  S->W  W->N
 */
static void ray_direction(int facing, int lateral, int *ddx, int *ddy) {
    /* forward unit vectors indexed by facing */
    static const int fwd_x[4] = { 0,  1, 0, -1 };
    static const int fwd_y[4] = {-1,  0, 1,  0 };
    /* right unit vectors (90° clockwise from forward) */
    static const int rgt_x[4] = { 1,  0, -1, 0 };
    static const int rgt_y[4] = { 0,  1,  0, -1 };

    *ddx = fwd_x[facing] + lateral * rgt_x[facing];
    *ddy = fwd_y[facing] + lateral * rgt_y[facing];

    /* Normalise diagonal to cardinal by clamping — we only want pure directions.
     * For lateral ±1 we want the pure sideways vector, not diagonal. */
    if (lateral != 0) {
        *ddx = lateral * rgt_x[facing];
        *ddy = lateral * rgt_y[facing];
    }
}

/*
 * Describe a single view ray in prose.
 * label  = "Ahead", "Left", "Left-center", "Right-center", "Right"
 * lateral = -1 (left), -1 (left-center uses diagonal), 0 (ahead), +1 (right-center), +1 (right)
 * For left-center and right-center we use a diagonal direction.
 */
static const char *dist_label(int steps) {
    if (steps <= 1) return "right beside you";
    if (steps <= 3) return "near you";
    if (steps <= 6) return "a little way out";
    return "off in the distance";
}

static void describe_ray(const char *label, int px, int py,
                         int ddx, int ddy, int max_dist) {
    Terrain end_t;
    int change = scan_ray(px, py, ddx, ddy, max_dist, &end_t);
    Terrain here = g.grid[py][px].terrain;

    if (change < 0) {
        /* No change — same terrain all the way */
        if (here == TERRAIN_WALL) {
            /* Shouldn't happen (player can't stand on wall) but guard anyway */
            printf("%-13s the stone wall surrounds you.\n", label);
        } else {
            printf("%-13s %s stretches out into the distance.\n",
                   label, terrain_label(here));
        }
    } else {
        if (end_t == TERRAIN_WALL) {
            /* Find the wall cell at change_dist to get its style */
            int wx = px + ddx * change;
            int wy = py + ddy * change;
            int style = (wx >= 0 && wx < 30 && wy >= 0 && wy < 30)
                        ? g.grid[wy][wx].wall_style : 0;
            printf("%-13s %s, then a high stone wall %s %s.\n",
                   label,
                   terrain_label(here),
                   wall_style_desc(style),
                   dist_label(change));
        } else {
            printf("%-13s %s, then %s %s.\n",
                   label,
                   terrain_label(here),
                   terrain_label(end_t),
                   dist_label(change));
        }
    }
}

void describe_view(void) {
    int px = g.player.x;
    int py = g.player.y;
    int vr = visibility_radius();
    int facing = g.player.facing;

    static const char *facing_names[4] = { "north", "east", "south", "west" };
    printf("\nFacing %s  (visibility: %d)\n", facing_names[facing], vr);

    /* Forward unit vectors */
    static const int fwd_x[4] = { 0,  1, 0, -1 };
    static const int fwd_y[4] = {-1,  0, 1,  0 };
    /* Right unit vectors */
    static const int rgt_x[4] = { 1,  0, -1, 0 };
    static const int rgt_y[4] = { 0,  1,  0, -1 };

    int fx = fwd_x[facing], fy = fwd_y[facing];
    int rx = rgt_x[facing], ry = rgt_y[facing];

    /* Five rays:
     *   Ahead        = forward
     *   Left-center  = forward + left  (diagonal)
     *   Left         = pure left
     *   Right-center = forward + right (diagonal)
     *   Right        = pure right
     */
    int ahead_dx   = fx,       ahead_dy   = fy;
    int lc_dx      = fx - rx,  lc_dy      = fy - ry;   /* fwd + left */
    int left_dx    = -rx,      left_dy    = -ry;
    int rc_dx      = fx + rx,  rc_dy      = fy + ry;   /* fwd + right */
    int right_dx   = rx,       right_dy   = ry;

    /* Normalise diagonals: keep both components but scan will step diagonally */
    describe_ray("Ahead:       ", px, py, ahead_dx, ahead_dy, vr);
    describe_ray("Left-center: ", px, py, lc_dx,    lc_dy,    vr);
    describe_ray("Left:        ", px, py, left_dx,  left_dy,  vr);
    describe_ray("Right-center:", px, py, rc_dx,    rc_dy,    vr);
    describe_ray("Right:       ", px, py, right_dx, right_dy, vr);

    /* Scan the full visibility square for wall cells.
     * Report at most one wall per direction (closest cell). */
    {
        static const char *dir_name[3] = {
            "ahead", "to your left", "to your right"
        };
        /* closest distance and style per direction (0=ahead,1=left,2=right) */
        int best_dist[3]  = {9999, 9999, 9999};
        int best_style[3] = {0, 0, 0};
        int r, c;

        for (r = py - vr; r <= py + vr; r++) {
            for (c = px - vr; c <= px + vr; c++) {
                if (c < 0 || c >= 30 || r < 0 || r >= 30) continue;
                if (g.grid[r][c].terrain != TERRAIN_WALL) continue;

                int wdx = c - px;
                int wdy = r - py;
                int fwd_dot = wdx * fx + wdy * fy;
                int rgt_dot = wdx * rx + wdy * ry;
                int dir;
                if (fwd_dot > 0 && (fwd_dot >= (rgt_dot < 0 ? -rgt_dot : rgt_dot)))
                    dir = 0; /* ahead */
                else if (fwd_dot < 0 && (-fwd_dot >= (rgt_dot < 0 ? -rgt_dot : rgt_dot)))
                    continue; /* behind — skip */
                else if (rgt_dot < 0)
                    dir = 1; /* left */
                else
                    dir = 2; /* right */

                int d = (wdx < 0 ? -wdx : wdx) > (wdy < 0 ? -wdy : wdy)
                        ? (wdx < 0 ? -wdx : wdx) : (wdy < 0 ? -wdy : wdy);
                if (d < best_dist[dir]) {
                    best_dist[dir]  = d;
                    best_style[dir] = g.grid[r][c].wall_style;
                }
            }
        }

        int printed_header = 0;
        int dir;
        for (dir = 0; dir < 3; dir++) {
            if (best_dist[dir] == 9999) continue;
            if (!printed_header) { printf("Walls in view:\n"); printed_header = 1; }
            printf("  A stone wall %s %s, %s.\n",
                   dir_name[dir],
                   dist_label(best_dist[dir]),
                   wall_style_desc(best_style[dir]));
        }
    }

    /* Sword visibility: bright shamrock glow up to 40 units,
     * blocked by farmhouse/barn — show only the closest visible sword */
    {
        int si;
        int best_sdist = 9999;
        int best_scx = -1, best_scy = -1;
        for (si = 0; si < g.num_swords; si++) {
            int sox = g.sword_ox[si], soy = g.sword_oy[si];
            int scx = sox + 2, scy = soy + 2;
            if (!g.grid[soy][sox].has_sword) continue;
            int ddx = scx - px; int adx2 = ddx<0?-ddx:ddx;
            int ddy = scy - py; int ady2 = ddy<0?-ddy:ddy;
            int sdist = adx2 > ady2 ? adx2 : ady2;
            if (sdist > SWORD_VISIBLE_DIST) continue;
            int blocked = 0;
            int steps = sdist > 0 ? sdist : 1;
            int st;
            for (st = 1; st <= steps && !blocked; st++) {
                int cx2 = px + ddx * st / steps;
                int cy2 = py + ddy * st / steps;
                if (cx2 < 0 || cx2 >= 30 || cy2 < 0 || cy2 >= 30) break;
                Terrain bt = g.grid[cy2][cx2].terrain;
                if (bt == TERRAIN_FARMHOUSE || bt == TERRAIN_BARN) blocked = 1;
            }
            if (!blocked && sdist < best_sdist) {
                best_sdist = sdist;
                best_scx = scx; best_scy = scy;
            }
        }
        if (best_scx >= 0)
            printf("A bright shamrock glow to the %s marks a sword, %s.\n",
                   compass_dir(px, py, best_scx, best_scy), dist_label(best_sdist));
    }

    /* Treasure visibility: rainbow glow up to 40 units,
     * blocked by farmhouse/barn — show only the closest visible treasure */
    {
        int ti;
        int best_tdist = 9999;
        int best_tcx = -1, best_tcy = -1;
        for (ti = 0; ti < 5; ti++) {
            if (g.player.treasure_mask & (1 << ti)) continue;
            int tox = g.treasure_ox[ti], toy = g.treasure_oy[ti];
            int tcx = tox + 2, tcy = toy + 2;
            if (g.grid[toy][tox].has_treasure < 0) continue;
            int ddx = tcx - px; int adx2 = ddx<0?-ddx:ddx;
            int ddy = tcy - py; int ady2 = ddy<0?-ddy:ddy;
            int tdist = adx2 > ady2 ? adx2 : ady2;
            if (tdist > TREASURE_VISIBLE_DIST) continue;
            int blocked = 0;
            int steps = tdist > 0 ? tdist : 1;
            int st;
            for (st = 1; st <= steps && !blocked; st++) {
                int cx2 = px + ddx * st / steps;
                int cy2 = py + ddy * st / steps;
                if (cx2 < 0 || cx2 >= 30 || cy2 < 0 || cy2 >= 30) break;
                Terrain bt = g.grid[cy2][cx2].terrain;
                if (bt == TERRAIN_FARMHOUSE || bt == TERRAIN_BARN) blocked = 1;
            }
            if (!blocked && tdist < best_tdist) {
                best_tdist = tdist;
                best_tcx = tcx; best_tcy = tcy;
            }
        }
        if (best_tcx >= 0)
            printf("A shimmering rainbow to the %s hints at treasure, %s.\n",
                   compass_dir(px, py, best_tcx, best_tcy), dist_label(best_tdist));
    }

    /* Signposts visible within visibility radius */
    {
        static const char *sign_dest_name[6] = {
            "", "the farm", "the gate", "the pond", "the forest", "the merchant"
        };
        int r, c;
        for (r = py - vr; r <= py + vr; r++) {
            for (c = px - vr; c <= px + vr; c++) {
                if (c < 0 || c >= 30 || r < 0 || r >= 30) continue;
                int sp = g.grid[r][c].signpost;
                if (sp <= 0) continue;
                if (c == px && r == py) continue; /* on current square — handled by move_player */
                int sdx = c - px; if (sdx < 0) sdx = -sdx;
                int sdy = r - py; if (sdy < 0) sdy = -sdy;
                int sdist = sdx > sdy ? sdx : sdy;
                printf(COL_YELLOW "A signpost to the %s, %s, points toward %s.\n" COL_RESET,
                       compass_dir(px, py, c, r), dist_label(sdist), sign_dest_name[sp]);
            }
        }
    }

    printf("\n");
}

/* Describe nearby barn (within 4) and farmhouse (within 6) if visible */
static void describe_nearby_structures(void) {
    int px = g.player.x;
    int py = g.player.y;
    int x, y;
    int barn_dist = 9999, house_dist = 9999, gate_dist = 9999;
    int barn_cx = -1, barn_cy = -1, house_cx = -1, house_cy = -1;
    int gate_cx = -1, gate_cy = -1;

    /* Facing: 0=N,1=E,2=S,3=W — used to exclude structures behind the player */
    int facing = g.player.facing;

    for (y = 0; y < 30; y++) {
        for (x = 0; x < 30; x++) {
            int dx = x - px; if (dx < 0) dx = -dx;
            int dy = y - py; if (dy < 0) dy = -dy;
            int d = dx > dy ? dx : dy;
            if (g.grid[y][x].terrain == TERRAIN_BARN && d <= 4 && d < barn_dist) {
                barn_dist = d; barn_cx = x; barn_cy = y;
            }
            if (g.grid[y][x].terrain == TERRAIN_FARMHOUSE && d <= 6 && d < house_dist) {
                house_dist = d; house_cx = x; house_cy = y;
            }
            if (g.grid[y][x].terrain == TERRAIN_GATE && d <= 20 && d < gate_dist) {
                /* Check it's not behind the player */
                int rdx = x - px;
                int rdy = y - py;
                int behind = 0;
                if (facing == 0 && rdy >  0) behind = 1; /* facing N, gate is south */
                if (facing == 1 && rdx <  0) behind = 1; /* facing E, gate is west  */
                if (facing == 2 && rdy <  0) behind = 1; /* facing S, gate is north */
                if (facing == 3 && rdx >  0) behind = 1; /* facing W, gate is east  */
                if (!behind) { gate_dist = d; gate_cx = x; gate_cy = y; }
            }
        }
    }

    if (barn_cx >= 0)
        printf("To the %s, %s, stands a weathered red barn, its paint peeling and boards warped by years of sun and rain.\n",
               compass_dir(px, py, barn_cx, barn_cy), dist_label(barn_dist));

    if (house_cx >= 0)
        printf("To the %s, %s, a white clapboard farmhouse sits weather-worn and tired. A porch runs across the front where an old man rocks slowly, a pipe trailing lazy smoke into the still air.\n",
               compass_dir(px, py, house_cx, house_cy), dist_label(house_dist));

    if (gate_cx >= 0)
        printf("To the %s, %s, a great iron gate stands set into the ancient stone wall, its bars dark with age and hung with trailing ivy.\n",
               compass_dir(px, py, gate_cx, gate_cy), dist_label(gate_dist));

    /* Describe merchant if within 3 units */
    if (g.merchant_alive) {
        int mdx = g.merchant_x - px; if (mdx < 0) mdx = -mdx;
        int mdy = g.merchant_y - py; if (mdy < 0) mdy = -mdy;
        int mdist = mdx > mdy ? mdx : mdy;
        if (mdist <= 15)
            printf("To the %s, %s, a weathered merchant stands beside a rickety cart piled with goods.\n",
                   compass_dir(px, py, g.merchant_x, g.merchant_y), dist_label(mdist));
    }

    /* Describe pond if within 10 units and not behind player */
    {
        int pond_dist = 9999, pond_cx = -1, pond_cy = -1;
        for (y = 0; y < 30; y++) {
            for (x = 0; x < 30; x++) {
                if (g.grid[y][x].terrain != TERRAIN_POND) continue;
                int dx2 = x - px; if (dx2 < 0) dx2 = -dx2;
                int dy2 = y - py; if (dy2 < 0) dy2 = -dy2;
                int d2 = dx2 > dy2 ? dx2 : dy2;
                if (d2 <= 10 && d2 < pond_dist) {
                    int rdx = x - px, rdy = y - py;
                    int behind = 0;
                    if (facing == 0 && rdy >  0) behind = 1;
                    if (facing == 1 && rdx <  0) behind = 1;
                    if (facing == 2 && rdy <  0) behind = 1;
                    if (facing == 3 && rdx >  0) behind = 1;
                    if (!behind) { pond_dist = d2; pond_cx = x; pond_cy = y; }
                }
            }
        }
        if (pond_cx >= 0)
            printf("To the %s, %s, a dark still pond shimmers in the meadow, its surface broken only by the occasional ripple of fish.\n",
                   compass_dir(px, py, pond_cx, pond_cy), dist_label(pond_dist));
    }

    /* Describe ruined tower within 8 units */
    {
        int tow_dist = 9999, tow_cx = -1, tow_cy = -1;
        for (y = 0; y < 30; y++) {
            for (x = 0; x < 30; x++) {
                if (g.grid[y][x].terrain != TERRAIN_TOWER) continue;
                int dx2 = x - px; if (dx2 < 0) dx2 = -dx2;
                int dy2 = y - py; if (dy2 < 0) dy2 = -dy2;
                int d2 = dx2 > dy2 ? dx2 : dy2;
                if (d2 <= 8 && d2 < tow_dist) { tow_dist = d2; tow_cx = x; tow_cy = y; }
            }
        }
        if (tow_cx >= 0)
            printf("To the %s, %s, a ruined stone tower rises against the sky, its upper floors long since collapsed.\n",
                   compass_dir(px, py, tow_cx, tow_cy), dist_label(tow_dist));
    }

    /* Describe graveyard within 6 units */
    {
        int grav_dist = 9999, grav_cx = -1, grav_cy = -1;
        for (y = 0; y < 30; y++) {
            for (x = 0; x < 30; x++) {
                if (g.grid[y][x].terrain != TERRAIN_GRAVEYARD) continue;
                int dx2 = x - px; if (dx2 < 0) dx2 = -dx2;
                int dy2 = y - py; if (dy2 < 0) dy2 = -dy2;
                int d2 = dx2 > dy2 ? dx2 : dy2;
                if (d2 <= 6 && d2 < grav_dist) { grav_dist = d2; grav_cx = x; grav_cy = y; }
            }
        }
        if (grav_cx >= 0 && g.grid[py][px].terrain != TERRAIN_GRAVEYARD)
            printf("To the %s, %s, a small graveyard huddles behind a low stone wall, its headstones dark with age.\n",
                   compass_dir(px, py, grav_cx, grav_cy), dist_label(grav_dist));
    }
}

void move_player(int dx, int dy) {
    int nx = g.player.x + dx;
    int ny = g.player.y + dy;

    /* Validate bounds */
    if (nx < 0 || nx >= 30 || ny < 0 || ny >= 30) {
        printf("You can't go that way.\n");
        return;
    }

    /* Validate terrain passability */
    {
        Terrain t = g.grid[ny][nx].terrain;
        if (t == TERRAIN_RIVER || t == TERRAIN_FARMHOUSE ||
            t == TERRAIN_BARN  || t == TERRAIN_WALL     ||
            t == TERRAIN_POND  || t == TERRAIN_TOWER) {
            if (t == TERRAIN_WALL)
                printf("A high stone wall blocks your way, %s.\n",
                       wall_style_desc(g.grid[ny][nx].wall_style));
            else if (t == TERRAIN_FARMHOUSE)
                printf("The farmhouse is in the way -- you can't go through it.\n");
            else if (t == TERRAIN_BARN)
                printf("The barn is in the way -- you can't go through it.\n");
            else if (t == TERRAIN_RIVER)
                printf("The river blocks your path -- find a bridge to cross.\n");
            else if (t == TERRAIN_POND)
                printf("The pond blocks your way. Fish dart beneath the dark surface.\n");
            else if (t == TERRAIN_TOWER) {
                static const char *tower_inscriptions[] = {
                    "Carved into the stone you read: \"The chest of Malachar lies to the %s of this tower.\"",
                    "Faded letters on the wall read: \"Seek the locked treasure -- it waits to the %s.\"",
                    "An inscription reads: \"He who holds the key shall find riches to the %s.\"",
                    "Scratched into the stone: \"The wizard's hoard lies to the %s -- find the key first.\"",
                    "Ancient runes translate roughly as: \"Look to the %s for what is locked and waiting.\""
                };
                printf("The crumbling tower walls block your way -- there is no entrance.\n");
                /* Point toward the locked treasure */
                if (g.locked_treasure >= 0 &&
                    !(g.player.treasure_mask & (1 << g.locked_treasure)) &&
                    g.grid[g.treasure_oy[g.locked_treasure]][g.treasure_ox[g.locked_treasure]].has_treasure >= 0) {
                    int tcx = g.treasure_ox[g.locked_treasure] + 2;
                    int tcy = g.treasure_oy[g.locked_treasure] + 2;
                    char ins_msg[256];
                    sprintf(ins_msg, tower_inscriptions[rand() % 5],
                            compass_dir(g.player.x, g.player.y, tcx, tcy));
                    printf(COL_YELLOW "%s\n" COL_RESET, ins_msg);
                }
            }
            else
                printf("You can't go that way.\n");
            return;
        }
    }

    /* Update position and facing */
    g.player.x = nx;
    g.player.y = ny;
    /* 0=North(dy<0), 1=East(dx>0), 2=South(dy>0), 3=West(dx<0)
     * For diagonals, forward component takes priority */
    if      (dy < 0 && dx == 0) g.player.facing = 0;
    else if (dx > 0 && dy == 0) g.player.facing = 1;
    else if (dy > 0 && dx == 0) g.player.facing = 2;
    else if (dx < 0 && dy == 0) g.player.facing = 3;
    else if (dx > 0 && dy < 0)  g.player.facing = 0; /* NE -> face north */
    else if (dx > 0 && dy > 0)  g.player.facing = 1; /* SE -> face east  */
    else if (dx < 0 && dy > 0)  g.player.facing = 2; /* SW -> face south */
    else if (dx < 0 && dy < 0)  g.player.facing = 3; /* NW -> face west  */

    /* Auto-collect items on landing square */
    {
        Cell *cell = &g.grid[ny][nx];

        /* Ancient key */
        if (!g.key_taken && nx == g.key_x && ny == g.key_y) {
            g.key_taken = 1;
            g.player.has_key = 1;
            printf(COL_YELLOW "You find an ancient iron key half-buried in the ground!\n" COL_RESET);
            printf("It looks like it might open something that is locked...\n");
        }

        /* Signpost */
        if (cell->signpost > 0) {
            static const char *sign_text[6] = {
                "",
                "A weathered wooden signpost reads: --> FARM",
                "A weathered wooden signpost reads: --> GATE (NORTH)",
                "A weathered wooden signpost reads: --> POND",
                "A weathered wooden signpost reads: --> FOREST",
                "A weathered wooden signpost reads: --> MERCHANT"
            };
            printf(COL_YELLOW "%s\n" COL_RESET, sign_text[cell->signpost]);
        }

        /* Graveyard entry */
        if (cell->terrain == TERRAIN_GRAVEYARD) {
            static const char *grave_msgs[] = {
                "You step into a small graveyard. Mossy headstones lean at odd angles, their inscriptions worn smooth by time.",
                "The air grows cold as you enter the graveyard. Something watches from between the stones.",
                "Crumbling headstones surround you. The names are long forgotten.",
                "You walk among the graves. The grass here is unnaturally still."
            };
            printf("%s\n", grave_msgs[rand() % 4]);

            /* 40% chance a ghost appears with a cryptic hint */
            if (rand() % 10 < 4) {
                /* Find nearest uncollected treasure */
                int best_ti = -1, best_td = 9999;
                int ti;
                for (ti = 0; ti < 5; ti++) {
                    if (g.player.treasure_mask & (1 << ti)) continue;
                    if (g.grid[g.treasure_oy[ti]][g.treasure_ox[ti]].has_treasure < 0) continue;
                    int tcx = g.treasure_ox[ti] + 2, tcy = g.treasure_oy[ti] + 2;
                    int ddx2 = tcx - nx; if (ddx2 < 0) ddx2 = -ddx2;
                    int ddy2 = tcy - ny; if (ddy2 < 0) ddy2 = -ddy2;
                    int d2 = ddx2 > ddy2 ? ddx2 : ddy2;
                    if (d2 < best_td) { best_td = d2; best_ti = ti; }
                }
                static const char *ghost_hints[] = {
                    "A pale shape drifts between the headstones and whispers: \"Seek what glitters... to the %s... where the living dare not linger...\"",
                    "A cold voice breathes from nowhere: \"The %s holds what you desire... but the dead know all paths...\"",
                    "A translucent figure points a bony finger to the %s and fades without a word.",
                    "Something brushes past you. A voice like dry leaves: \"To the %s... riches wait... for those bold enough...\"",
                    "The air shimmers and a ghost moans: \"I hid nothing... but others did... look to the %s...\""
                };
                if (best_ti >= 0) {
                    int tcx = g.treasure_ox[best_ti] + 2, tcy = g.treasure_oy[best_ti] + 2;
                    char ghost_msg[256];
                    sprintf(ghost_msg, ghost_hints[rand() % 5],
                            compass_dir(nx, ny, tcx, tcy));
                    printf(COL_WHITE "%s\n" COL_RESET, ghost_msg);
                } else {
                    printf(COL_WHITE "A pale shape drifts past and whispers: \"All treasures are claimed... your quest nears its end...\"\n" COL_RESET);
                }
            }
        }

        /* Malachar's scroll */
        if (cell->has_scroll) {
            cell->has_scroll = 0;
            printf(COL_YELLOW "\nYou find a rolled parchment sealed with wax.\n");
            printf("Breaking the seal, you read:\n\n");
            printf("  \"I, Malachar the Grey, write this in haste.\n");
            printf("   The orcs have come and I cannot hold them.\n");
            printf("   I have hidden my five treasures across this land --\n");
            printf("   gold, silver, diamonds, rubies, emeralds, sapphires --\n");
            printf("   scattered so no single orc may claim them all.\n");
            printf("   One chest I have locked with an ancient key.\n");
            printf("   The key lies hidden, as does the chest.\n");
            printf("   Whoever reads this: find the treasures.\n");
            printf("   Carry them through the great gate to the north.\n");
            printf("   Only then will this land know peace again.\n");
            printf("                              -- Malachar\"\n\n" COL_RESET);
        }

        /* Sword — clear entire 4x4 block */
        if (cell->has_sword && !g.player.has_sword) {
            /* Find which sword block this belongs to */
            int si;
            for (si = 0; si < g.num_swords; si++) {
                int sox = g.sword_ox[si], soy = g.sword_oy[si];
                if (nx >= sox && nx < sox+4 && ny >= soy && ny < soy+4) {
                    int r, c;
                    for (r = soy; r < soy+4; r++)
                        for (c = sox; c < sox+4; c++)
                            g.grid[r][c].has_sword = 0;
                    break;
                }
            }
            g.player.has_sword = 1;
            printf(COL_GREEN "You pick up a sword.\n" COL_RESET);
        }

        /* Flask */
        if (cell->has_flask) {
            cell->has_flask = 0;
            g.player.flask_count++;
            g.player.flasks_found++;
            printf(COL_YELLOW "You pick up a healing flask.\n" COL_RESET);
        }

        /* Shield */
        if (cell->has_shield && !g.player.has_shield) {
            cell->has_shield = 0;
            g.player.has_shield = 1;
            printf(COL_GREEN "You pick up a shield. Orc attacks will deal half damage!\n" COL_RESET);
        } else if (cell->has_shield && g.player.has_shield) {
            printf("There is a shield here, but you already carry one.\n");
        }

        /* Treasure — clear entire 5x5 block */
        if (cell->has_treasure >= 0) {
            int tidx = cell->has_treasure;
            int ttype = g.treasure_type[tidx];

            /* Locked chest check */
            if (tidx == g.locked_treasure && !g.player.has_key) {
                printf("You find a heavy iron-bound chest, but it is locked tight.\n");
                printf("You need an ancient key to open it.\n");
                /* Do NOT collect — leave treasure in place */
            } else {
                /* Consume key if this was the locked chest */
                if (tidx == g.locked_treasure && g.player.has_key) {
                    g.player.has_key = 0;
                    printf(COL_YELLOW "You use the ancient key -- the lock clicks open!\n" COL_RESET);
                }
                /* Clear the whole 5x5 block */
                {
                    int tox = g.treasure_ox[tidx], toy = g.treasure_oy[tidx];
                    int r, c;
                    for (r = toy; r < toy+5; r++)
                        for (c = tox; c < tox+5; c++)
                            g.grid[r][c].has_treasure = -1;
                }
                g.player.treasure_mask |= (1 << tidx);
                g.player.score += treasure_values[ttype];

                /* Excited discovery message */
                static const char *exclaim[6] = {
                    "Incredible! You've stumbled upon",
                    "By the stars! You've uncovered",
                    "Unbelievable! Your hands close around",
                    "Fortune smiles on you! You've found",
                    "Your heart leaps -- you've discovered",
                    "What a find! You've laid hands on"
                };
                printf(COL_YELLOW "\n*** %s a hoard of %s! ***\n",
                       exclaim[ttype % 6], treasure_names[ttype]);
                printf("The ground shimmers as you scoop it up. (+%d points)\n\n" COL_RESET,
                       treasure_values[ttype]);

                /* Show updated inventory */
                do_inventory();
                printf("\n");
            }
        }
    }

    /* Gate proximity message: check if player is adjacent (within 1 square)
     * to any TERRAIN_GATE cell at row 0 */
    {
        int gx;
        int near_gate = 0;
        for (gx = 0; gx < 30 && !near_gate; gx++) {
            if (g.grid[0][gx].terrain == TERRAIN_GATE) {
                int adx = nx - gx;
                int ady = ny - 0;
                if (adx < 0) adx = -adx;
                if (ady < 0) ady = -ady;
                if (adx <= 1 && ady <= 1) {
                    near_gate = 1;
                }
            }
        }
        /* Only print gate message when player is adjacent but not ON the gate
         * (landing on gate is handled by check_triggers) */
        if (near_gate && g.grid[ny][nx].terrain != TERRAIN_GATE) {
            if (popcount_mask(g.player.treasure_mask) >= 5) {
                printf("You have made an excellent journey and quest, well done, my friend!\n");
            } else {
                printf("Is that all you found? You need to continue your quest!\n");
            }
        }
        /* Reset wrong-answer streak when player moves away from gate area */
        if (!near_gate && g.grid[ny][nx].terrain != TERRAIN_GATE) {
            g.riddle_wrong_streak = 0;
        }
    }

    /* Describe what the player sees after moving */
    describe_nearby_structures();
    describe_view();
}
void do_attack(void) {
    int i;
    int px = g.player.x;
    int py = g.player.y;

    /* Attack nearest orc within 2 squares (Chebyshev).
     * Also always allow attacking any orc flagged in_melee or chasing,
     * since orc positions update AFTER the player acts. */
    {
        int best = -1;
        int best_dist = 3;
        for (i = 0; i < MAX_ORCS; i++) {
            if (!g.orcs[i].alive) continue;
            int odx = g.orcs[i].x - px;
            int ody = g.orcs[i].y - py;
            if (odx < 0) odx = -odx;
            if (ody < 0) ody = -ody;
            int dist = odx > ody ? odx : ody;
            /* In range, or actively engaged with player */
            if ((dist <= 2 || g.orcs[i].in_melee || g.orcs[i].chasing) && dist < best_dist) {
                best_dist = dist;
                best = i;
            }
        }
        /* Prefer the closest in-range orc; if none in range pick the engaged one */
        if (best < 0) {
            for (i = 0; i < MAX_ORCS; i++) {
                if (!g.orcs[i].alive) continue;
                if (g.orcs[i].in_melee || g.orcs[i].chasing) { best = i; break; }
            }
        }

        if (best >= 0) {
            if (!g.player.has_sword) {
                printf(COL_RED "You have no sword! You flail at the orc with your bare hands to no effect.\n" COL_RESET);
                return;
            }
            g.orcs[best].hp -= 10;
            if (g.orcs[best].hp <= 0) {
                g.orcs[best].alive = 0;
                g.orcs[best].in_melee = 0;
                g.orcs[best].chasing  = 0;
                g.player.orcs_killed++;
                if (g.orcs[best].is_warchief) {
                    g.player.score += 300;
                    printf(COL_RED "You slay the WARCHIEF! The orcs scatter in terror! (+300 points)\n" COL_RESET);
                } else {
                    g.player.score += 150;
                    printf(COL_RED "You slay the orc! (+150 points)\n" COL_RESET);
                }
            } else {
                if (g.orcs[best].is_warchief)
                    printf(COL_RED "You strike the Warchief! It roars in fury! (HP: %d)\n" COL_RESET, g.orcs[best].hp);
                else
                    printf(COL_RED "You strike the orc! (orc HP: %d)\n" COL_RESET, g.orcs[best].hp);
            }
            return;
        }
    }

    /* No adjacent orc — check if player is attacking an innocent animal */
    for (i = 0; i < g.num_animals; i++) {
        if (!g.animals[i].alive) continue;
        int adx = g.animals[i].x - px;
        int ady = g.animals[i].y - py;
        if (adx < 0) adx = -adx;
        if (ady < 0) ady = -ady;
        int dist = adx > ady ? adx : ady;
        if (dist <= 1) {
            g.player.hp = 0;
            show_death(
                "You raise your weapon against an innocent creature.\n"
                "The birds shriek, the rabbits charge, the cows stampede,\n"
                "and the horses rear up in fury. In a thunderous wave of\n"
                "hooves, beaks, and claws, the animals overwhelm you.\n"
                "You are trampled into the earth. Nature has spoken.");
            return;
        }
    }

    /* Nothing nearby */
    if (g.player.has_sword)
        printf("You slash mightily through thin air, but it is unharmed.\n");
    else
        printf("You meaninglessly shadowbox for everyone's amusement, but nothing is gained.\n");
}
void do_drink(void) {
    if (g.player.flask_count == 0) {
        printf("You have no flasks.\n");
        return;
    }
    g.player.hp = clamp_hp(g.player.hp + 30);
    g.player.flask_count--;
    printf("You drink a healing flask. HP restored.\n");
}

void do_pickup_sword(void) {
    Cell *cell = &g.grid[g.player.y][g.player.x];
    if (g.player.has_sword) {
        printf("You already carry a sword.\n");
        return;
    }
    if (!cell->has_sword) {
        printf("There is no sword here.\n");
        return;
    }
    cell->has_sword = 0;
    g.player.has_sword = 1;
    printf("You pick up the sword.\n");
}

void do_pickup_flask(void) {
    Cell *cell = &g.grid[g.player.y][g.player.x];
    if (!cell->has_flask) {
        printf("There is no flask here.\n");
        return;
    }
    cell->has_flask = 0;
    g.player.flask_count++;
    g.player.flasks_found++;
    printf(COL_YELLOW "You pick up a healing flask.\n" COL_RESET);
}

void do_pickup_treasure(void) {
    int px = g.player.x, py = g.player.y;
    Cell *cell = &g.grid[py][px];
    if (cell->has_treasure < 0) {
        printf("There is no treasure here.\n");
        return;
    }
    int tidx = cell->has_treasure;
    int ttype = g.treasure_type[tidx];

    if (tidx == g.locked_treasure && !g.player.has_key) {
        printf("The chest is locked tight. You need an ancient key to open it.\n");
        return;
    }
    if (tidx == g.locked_treasure && g.player.has_key) {
        g.player.has_key = 0;
        printf(COL_YELLOW "You use the ancient key -- the lock clicks open!\n" COL_RESET);
    }
    /* Clear the whole 5x5 block */
    {
        int tox = g.treasure_ox[tidx], toy = g.treasure_oy[tidx];
        int r, c;
        for (r = toy; r < toy+5; r++)
            for (c = tox; c < tox+5; c++)
                g.grid[r][c].has_treasure = -1;
    }
    g.player.treasure_mask |= (1 << tidx);
    g.player.score += treasure_values[ttype];
    static const char *exclaim[6] = {
        "Incredible! You've stumbled upon",
        "By the stars! You've uncovered",
        "Unbelievable! Your hands close around",
        "Fortune smiles on you! You've found",
        "Your heart leaps -- you've discovered",
        "What a find! You've laid hands on"
    };
    printf(COL_YELLOW "\n*** %s a hoard of %s! ***\n",
           exclaim[ttype % 6], treasure_names[ttype]);
    printf("The ground shimmers as you scoop it up. (+%d points)\n\n" COL_RESET,
           treasure_values[ttype]);
    do_inventory();
    printf("\n");
}
void do_interact(Command cmd) {
    int i, j, tmp;
    int px = g.player.x;
    int py = g.player.y;
    int vr = visibility_radius();
    int best = -1;
    int best_dist = vr + 1;

    /* 15 sayings for meadow animals (smart adult voice) — hello / pet / talk */
    static const char *meadow_hello[15] = {
        "\"Ah, a traveller. How refreshingly predictable.\"",
        "\"Good day. I trust the road treated you better than it looks.\"",
        "\"Welcome. I was just contemplating the nature of existence. You?\"",
        "\"Oh, a visitor. How delightful. I was getting tired of my own thoughts.\"",
        "\"Greetings. You have the look of someone on a quest. Am I wrong?\"",
        "\"Hello there. I've seen three adventurers pass today. You seem the most lost.\"",
        "\"Salutations. The weather is fine, the grass is finer, and you are here.\"",
        "\"Ah. You again. Well, not you specifically, but your type. Questing types.\"",
        "\"Good to meet you. I'd shake hands but, well, you know.\"",
        "\"Hello! I was hoping someone interesting would wander by.\"",
        "\"You look like you need directions. Everyone does, eventually.\"",
        "\"A fine afternoon for a quest, wouldn't you say? I would say that.\"",
        "\"Oh! A human. I don't get many of those. Well, I do, but I pretend I don't.\"",
        "\"Hello. I've been watching you from over there. Very entertaining.\"",
        "\"Greetings, adventurer. The treasure won't find itself, you know.\""
    };
    static const char *meadow_pet[15] = {
        "\"Affection noted. Personal space also noted.\"",
        "\"Mmm. That is... acceptable. You may continue.\"",
        "\"I am not a pet. I am a free creature of the wild. But yes, right there.\"",
        "\"Oh. Oh that is quite nice actually. Don't tell the others.\"",
        "\"I maintain that I am entirely indifferent to this. Entirely.\"",
        "\"You have surprisingly gentle hands for someone carrying a sword.\"",
        "\"I was going to say something witty but now I've forgotten it entirely.\"",
        "\"This is undignified. I am dignified. And yet here we are.\"",
        "\"Careful. I bite. I won't, but I could. Remember that.\"",
        "\"Ah. Yes. That spot. How did you know about that spot?\"",
        "\"I shall allow this precisely once. ...Perhaps twice.\"",
        "\"You know, most adventurers just walk past. I appreciate the gesture.\"",
        "\"Delightful. You have earned my temporary goodwill.\"",
        "\"I am a wild animal of great dignity. Also please don't stop.\"",
        "\"That is the nicest thing anyone has done for me all week.\""
    };
    static const char *meadow_talk[15] = {
        "\"The world is vast, the path is yours, and the grass is excellent today.\"",
        "\"I once watched a knight spend three days looking for treasure that was behind him.\"",
        "\"Wisdom: the river is cold, the forest is dark, and orcs are terrible conversationalists.\"",
        "\"If you find the treasure, what then? Have you considered that question?\"",
        "\"The elves are faster than they look. I'd keep that sword close if I were you.\"",
        "\"I've lived here my whole life. The farm is nice. The orcs are not.\"",
        "\"Every adventurer who passed here said they'd come back. None did. Curious.\"",
        "\"The bridges hold. Mostly. I'd cross quickly if I were you.\"",
        "\"You know what I find underrated? Standing still. Just standing. Try it sometime.\"",
        "\"The gate to the north is very grand. Very talkative too. Bit much, honestly.\"",
        "\"I heard the sapphires are hidden somewhere near the river. Or was it the rubies?\"",
        "\"An orc once tried to talk to me. I ran. I recommend the same strategy.\"",
        "\"The forest muffles sound. Useful if you're hiding. Less useful if something's hunting you.\"",
        "\"Safe travels. And if you see a flask, pick it up. You'll thank yourself later.\"",
        "\"I don't know what a quest is, but I hope yours goes well.\""
    };

    /* 15 sayings for farm animals (simple 5-year-old voice) — hello / pet / talk */
    static const char *farm_hello[15] = {
        "\"Hewwo! I like grass. Do you like grass?\"",
        "\"Hi hi hi! Are you my friend? I think you're my friend!\"",
        "\"Oh! A person! I saw a person yesterday too! Maybe it was you!\"",
        "\"Hello! I had breakfast today. It was grass. It was SO good.\"",
        "\"Hiya! Do you live here? I live here! It's the best place!\"",
        "\"Hi! I was just standing here. I'm very good at standing.\"",
        "\"Hello person! You smell funny. That's okay, I smell funny too!\"",
        "\"Oh! Oh! Oh! A visitor! I'm gonna tell everyone! Hi everyone!\"",
        "\"Hewwo! Are you lost? I get lost sometimes. It's scary but then I find grass.\"",
        "\"Hi! My name is... um... I forgot. But hi anyway!\"",
        "\"Hello! I like your face. It's a good face. I have a face too!\"",
        "\"Oh a person! Last person gave me a pat. Are you a pat person?\"",
        "\"Hi! Is it lunchtime? I think it might be lunchtime.\"",
        "\"Hewwo! I was just thinking about nothing. I'm very good at that.\"",
        "\"Hello! You're tall. I'm not tall. But I'm very wide!\""
    };
    static const char *farm_pet[15] = {
        "\"Yay! Pets! Pets are the best thing ever!\"",
        "\"Ooooh that's nice! Do it again! Do it again!\"",
        "\"Hehehe that tickles! More please more please!\"",
        "\"Oh WOW. That is the BEST feeling. You are the BEST.\"",
        "\"Yay yay yay! I'm gonna tell the others about this!\"",
        "\"Mmmmm. I feel like a cloud. A happy grass cloud.\"",
        "\"You're so nice! Can you stay forever? Please stay forever.\"",
        "\"Heehee! My tail is wagging! Wait, do I have a tail? Yes I do!\"",
        "\"Oh! Oh! Right there! That's the good spot! THE GOOD SPOT!\"",
        "\"I love you. Is that too much? I don't care. I love you.\"",
        "\"Pets are better than grass and I REALLY like grass.\"",
        "\"Hehe you have soft hands! My nose is soft too! Touch my nose!\"",
        "\"I'm gonna remember this forever. This is my best day.\"",
        "\"More! More! ...Okay I'm done. No wait, more.\"",
        "\"You are officially my favourite person. Don't tell the farmer.\""
    };
    static const char *farm_talk[15] = {
        "\"Um... I dunno. What is a quest? Is it like lunch?\"",
        "\"I know a secret! ...I forgot it. But I knew it!\"",
        "\"One time I walked to the other side of the farm. It was far.\"",
        "\"Do you think clouds are just big fluffy grass? I think that.\"",
        "\"I tried to count to ten once. I got to four. Four is a lot.\"",
        "\"The big shiny gate talks! It scared me. I ran. Then I came back.\"",
        "\"I saw an orc once. It was big and grumpy. Like farmer on Mondays.\"",
        "\"What's a sword? Is it for grass? Everything should be for grass.\"",
        "\"I had a dream about a big pile of hay. It was the best dream.\"",
        "\"Sometimes I just stand and think. Mostly I think about standing.\"",
        "\"The river is wet. I know because I fell in. It was a surprise.\"",
        "\"Are you going on an adventure? Can I come? ...Actually I'm tired.\"",
        "\"I heard there's treasure out there. I don't know what treasure is.\"",
        "\"My friend the bird told me something important. I wasn't listening.\"",
        "\"You seem nice. Most people just walk past. You stopped. That's nice.\""
    };

    /* Porch man: if CMD_HELLO and within 6 units of a TERRAIN_FARMHOUSE cell */
    if (cmd == CMD_HELLO) {
        static const char *irish_sayings[] = {
            "\"May the road rise up to meet you, and may the wind be always at your back.\"",
            "\"There's no use boiling your cabbage twice, friend.\"",
            "\"A good laugh and a long sleep are the two best cures.\"",
            "\"It is better to be a coward for a minute than dead for the rest of your life.\"",
            "\"The older the fiddle, the sweeter the tune.\"",
            "\"Firelight will not let you read fine stories, but it's warm and you won't see the dust.\"",
            "\"A man who holds his tongue saves his head.\"",
            "\"What's for you won't pass you by.\"",
            "\"You've got to do your own growing, no matter how tall your grandfather was.\"",
            "\"A kind word never broke anyone's mouth.\"",
            "\"The longest road out is the shortest road home.\"",
            "\"Praise the ripe field, not the green corn.\"",
            "\"Even a small thorn causes festering.\"",
            "\"It is easy to be pleasant when life flows by like a song.\"",
            "\"Drink is the curse of the land. It makes you fight with your neighbour.\""
        };
        int r2, c2;
        int near_farmhouse = 0;
        for (r2 = py - 6; r2 <= py + 6 && !near_farmhouse; r2++) {
            for (c2 = px - 6; c2 <= px + 6 && !near_farmhouse; c2++) {
                if (c2 < 0 || c2 >= 30 || r2 < 0 || r2 >= 30) continue;
                if (g.grid[r2][c2].terrain == TERRAIN_FARMHOUSE) {
                    int adx2 = c2 - px; if (adx2 < 0) adx2 = -adx2;
                    int ady2 = r2 - py; if (ady2 < 0) ady2 = -ady2;
                    int d2 = adx2 > ady2 ? adx2 : ady2;
                    if (d2 <= 6) near_farmhouse = 1;
                }
            }
        }
        if (near_farmhouse) {
            int idx = rand() % 15;
            printf(COL_YELLOW "The old man takes a long draw on his pipe and squints at you.\n");
            printf("%s\n", irish_sayings[idx]);
            printf("He waves his pipe toward the horizon. \"Now be gone about your quest, and don't be botherin' me further.\"\n" COL_RESET);
            return;
        }
    }

    /* Find nearest alive animal within visibility_radius */
    for (i = 0; i < g.num_animals; i++) {
        if (!g.animals[i].alive) continue;
        int adx = g.animals[i].x - px;
        int ady = g.animals[i].y - py;
        if (adx < 0) adx = -adx;
        if (ady < 0) ady = -ady;
        int dist = adx > ady ? adx : ady;
        if (dist <= vr && dist < best_dist) {
            best_dist = dist;
            best = i;
        }
    }

    if (best < 0) {
        printf("There is no animal nearby to interact with.\n");
        return;
    }

    g.player.animals_interacted++;

    /* Count how many of the same type are visible, and find this animal's rank
     * (sorted by distance) so we can number it if there are multiples */
    int same_type_count = 0;
    int this_animal_rank = 1; /* 1-based */
    {
        AnimalType btype = g.animals[best].type;
        /* Collect distances of all visible same-type animals */
        int ranks[MAX_ANIMALS];
        int rdist[MAX_ANIMALS];
        int rn = 0;
        for (i = 0; i < g.num_animals; i++) {
            if (!g.animals[i].alive) continue;
            if (g.animals[i].type != btype) continue;
            int adx = g.animals[i].x - px; if (adx < 0) adx = -adx;
            int ady = g.animals[i].y - py; if (ady < 0) ady = -ady;
            int dist = adx > ady ? adx : ady;
            if (dist > vr) continue;
            ranks[rn] = i;
            rdist[rn] = dist;
            rn++;
        }
        same_type_count = rn;
        /* Sort by distance (bubble sort — small N) */
        int si2, sj2, stmp;
        for (si2 = 0; si2 < rn - 1; si2++)
            for (sj2 = si2 + 1; sj2 < rn; sj2++)
                if (rdist[sj2] < rdist[si2]) {
                    stmp = rdist[si2]; rdist[si2] = rdist[sj2]; rdist[sj2] = stmp;
                    stmp = ranks[si2]; ranks[si2] = ranks[sj2]; ranks[sj2] = stmp;
                }
        for (si2 = 0; si2 < rn; si2++)
            if (ranks[si2] == best) { this_animal_rank = si2 + 1; break; }
    }

    /* Build the animal's display name, numbered if multiples of same type visible */
    char animal_label[32];
    {
        static const char *base_names[] = {"bird","rabbit","cow","horse","deer","wolf"};
        const char *base = base_names[(int)g.animals[best].type];
        if (same_type_count > 1)
            sprintf(animal_label, "%s #%d", base, this_animal_rank);
        else
            sprintf(animal_label, "%s", base);
    }

    switch (g.animals[best].type) {
        case ANIMAL_BIRD:
        case ANIMAL_RABBIT: {
            /* Reshuffle deck when all 15 sayings have been used */
            if (g.meadow_deck_pos >= 15) {
                for (i = 0; i < 15; i++) g.meadow_deck[i] = i;
                for (i = 14; i > 0; i--) {
                    j = rand() % (i + 1);
                    tmp = g.meadow_deck[i];
                    g.meadow_deck[i] = g.meadow_deck[j];
                    g.meadow_deck[j] = tmp;
                }
                g.meadow_deck_pos = 0;
            }
            int idx = g.meadow_deck[g.meadow_deck_pos++];
            const char *name = animal_label;
            if (cmd == CMD_HELLO) {
                printf("The %s tilts its head and regards you with bright eyes.\n%s\n",
                       name, meadow_hello[idx]);
                /* Hint nearest sword within 60 units of the PLAYER */
                {
                    int si, best_si = -1, best_sd = 9999;
                    for (si = 0; si < g.num_swords; si++) {
                        if (!g.grid[g.sword_oy[si]][g.sword_ox[si]].has_sword) continue;
                        int scx = g.sword_ox[si] + 2, scy = g.sword_oy[si] + 2;
                        int ddx = scx - px; if (ddx < 0) ddx = -ddx;
                        int ddy = scy - py; if (ddy < 0) ddy = -ddy;
                        int d = ddx > ddy ? ddx : ddy;
                        if (d <= 60 && d < best_sd) { best_sd = d; best_si = si; }
                    }
                    if (best_si >= 0) {
                        int scx = g.sword_ox[best_si] + 2, scy = g.sword_oy[best_si] + 2;
                        static const char *sword_desc[4] = {
                            "a blade that gleams like captured moonlight",
                            "a sword with an edge sharp enough to split shadow",
                            "a weapon that hums faintly, hungry for adventure",
                            "a bright steel sword, its hilt wrapped in worn leather"
                        };
                        printf("The %s chirps urgently: \"Something shines to the %s, %s! %s!\"\n",
                               name, compass_dir(px, py, scx, scy),
                               dist_label(best_sd),
                               sword_desc[best_sd % 4]);
                    }
                }
                /* Hint nearest uncollected treasure within 60 units of the PLAYER */
                {
                    int ti, best_ti = -1, best_td = 9999;
                    for (ti = 0; ti < 5; ti++) {
                        if (g.player.treasure_mask & (1 << ti)) continue;
                        if (g.grid[g.treasure_oy[ti]][g.treasure_ox[ti]].has_treasure < 0) continue;
                        int tcx = g.treasure_ox[ti] + 2, tcy = g.treasure_oy[ti] + 2;
                        int ddx = tcx - px; if (ddx < 0) ddx = -ddx;
                        int ddy = tcy - py; if (ddy < 0) ddy = -ddy;
                        int d = ddx > ddy ? ddx : ddy;
                        if (d <= 60 && d < best_td) { best_td = d; best_ti = ti; }
                    }
                    if (best_ti >= 0) {
                        int tcx = g.treasure_ox[best_ti] + 2, tcy = g.treasure_oy[best_ti] + 2;
                        static const char *treasure_desc[4] = {
                            "a chest that spills colours like a broken rainbow",
                            "something that glows with the warmth of a hundred sunsets",
                            "a hoard that shimmers as if the stars fell into it",
                            "riches that pulse with every colour you have ever dreamed"
                        };
                        printf("The %s trills excitedly: \"And to the %s, %s... %s!\"\n",
                               name, compass_dir(px, py, tcx, tcy),
                               dist_label(best_td),
                               treasure_desc[best_td % 4]);
                    }
                }
            } else if (cmd == CMD_PET) {
                printf("The %s allows your hand to approach, then sidesteps neatly.\n%s\n",
                       name, meadow_pet[idx]);
            } else {
                printf("The %s fixes you with a philosophical gaze.\n%s\n",
                       name, meadow_talk[idx]);
            }
            break;
        }
        case ANIMAL_COW:
        case ANIMAL_HORSE: {
            /* Reshuffle deck when all 15 sayings have been used */
            if (g.farm_deck_pos >= 15) {
                for (i = 0; i < 15; i++) g.farm_deck[i] = i;
                for (i = 14; i > 0; i--) {
                    j = rand() % (i + 1);
                    tmp = g.farm_deck[i];
                    g.farm_deck[i] = g.farm_deck[j];
                    g.farm_deck[j] = tmp;
                }
                g.farm_deck_pos = 0;
            }
            int idx = g.farm_deck[g.farm_deck_pos++];
            const char *name = animal_label;
            if (cmd == CMD_HELLO) {
                printf("The %s blinks at you slowly.\n%s\n",
                       name, farm_hello[idx]);
                /* Hint nearest sword within 60 units of the PLAYER */
                {
                    int si, best_si = -1, best_sd = 9999;
                    for (si = 0; si < g.num_swords; si++) {
                        if (!g.grid[g.sword_oy[si]][g.sword_ox[si]].has_sword) continue;
                        int scx = g.sword_ox[si] + 2, scy = g.sword_oy[si] + 2;
                        int ddx = scx - px; if (ddx < 0) ddx = -ddx;
                        int ddy = scy - py; if (ddy < 0) ddy = -ddy;
                        int d = ddx > ddy ? ddx : ddy;
                        if (d <= 60 && d < best_sd) { best_sd = d; best_si = si; }
                    }
                    if (best_si >= 0) {
                        int scx = g.sword_ox[best_si] + 2, scy = g.sword_oy[best_si] + 2;
                        static const char *sword_desc[4] = {
                            "a blade that gleams like captured moonlight",
                            "a sword with an edge sharp enough to split shadow",
                            "a weapon that hums faintly, hungry for adventure",
                            "a bright steel sword, its hilt wrapped in worn leather"
                        };
                        printf("The %s snorts and nods to the %s, %s: \"%s.\"\n",
                               name, compass_dir(px, py, scx, scy),
                               dist_label(best_sd),
                               sword_desc[best_sd % 4]);
                    }
                }
                /* Hint nearest uncollected treasure within 60 units of the PLAYER */
                {
                    int ti, best_ti = -1, best_td = 9999;
                    for (ti = 0; ti < 5; ti++) {
                        if (g.player.treasure_mask & (1 << ti)) continue;
                        if (g.grid[g.treasure_oy[ti]][g.treasure_ox[ti]].has_treasure < 0) continue;
                        int tcx = g.treasure_ox[ti] + 2, tcy = g.treasure_oy[ti] + 2;
                        int ddx = tcx - px; if (ddx < 0) ddx = -ddx;
                        int ddy = tcy - py; if (ddy < 0) ddy = -ddy;
                        int d = ddx > ddy ? ddx : ddy;
                        if (d <= 60 && d < best_td) { best_td = d; best_ti = ti; }
                    }
                    if (best_ti >= 0) {
                        int tcx = g.treasure_ox[best_ti] + 2, tcy = g.treasure_oy[best_ti] + 2;
                        static const char *treasure_desc[4] = {
                            "a chest that spills colours like a broken rainbow",
                            "something that glows with the warmth of a hundred sunsets",
                            "a hoard that shimmers as if the stars fell into it",
                            "riches that pulse with every colour you have ever dreamed"
                        };
                        printf("The %s stamps a hoof toward the %s, %s: \"%s.\"\n",
                               name, compass_dir(px, py, tcx, tcy),
                               dist_label(best_td),
                               treasure_desc[best_td % 4]);
                    }
                }
            } else if (cmd == CMD_PET) {
                printf("The %s leans into your hand happily.\n%s\n",
                       name, farm_pet[idx]);
            } else {
                printf("The %s stares at you with wide, earnest eyes.\n%s\n",
                       name, farm_talk[idx]);
            }
            break;
        }
        default:
            printf("The %s looks at you curiously.\n", animal_label);
            break;
    }
}

/* Return compass direction string from (px,py) toward (tx,ty) */
static const char *compass_dir(int px, int py, int tx, int ty) {
    int dx = tx - px;
    int dy = ty - py;  /* dy < 0 = north */
    /* Use 8-way classification based on which component dominates */
    if (dx == 0 && dy < 0) return "north";
    if (dx == 0 && dy > 0) return "south";
    if (dx > 0  && dy == 0) return "east";
    if (dx < 0  && dy == 0) return "west";
    /* Diagonals: classify by ratio — within 2:1 ratio = diagonal */
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    if (adx >= ady * 2) return dx > 0 ? "east"  : "west";
    if (ady >= adx * 2) return dy < 0 ? "north" : "south";
    if (dx > 0 && dy < 0) return "northeast";
    if (dx > 0 && dy > 0) return "southeast";
    if (dx < 0 && dy > 0) return "southwest";
    return "northwest";
}

void do_look(void) {
    int i;
    int px = g.player.x;
    int py = g.player.y;
    int vr = visibility_radius();
    const char *terrain_name;

    switch (g.grid[py][px].terrain) {
        case TERRAIN_OPEN:      terrain_name = "open ground";  break;
        case TERRAIN_FOREST:    terrain_name = "a dense forest"; break;
        case TERRAIN_MEADOW:    terrain_name = "a meadow";     break;
        case TERRAIN_MEADOW2:   terrain_name = "a meadow by the pond"; break;
        case TERRAIN_RIVER:     terrain_name = "a river";      break;
        case TERRAIN_BRIDGE:    terrain_name = "a bridge";     break;
        case TERRAIN_FARM:      terrain_name = "a farm";       break;
        case TERRAIN_FARMHOUSE: terrain_name = "a farmhouse";  break;
        case TERRAIN_BARN:      terrain_name = "a barn";       break;
        case TERRAIN_GATE:      terrain_name = "the gate";     break;
        case TERRAIN_WALL:      terrain_name = "the stone wall"; break;
        case TERRAIN_GRAVEYARD: terrain_name = "a graveyard";  break;
        default:                terrain_name = "open ground";   break;
    }

    printf("You are standing on %s. Visibility: %d square(s).\n",
           terrain_name, vr);

    /* Describe open-ground feature underfoot */
    if (g.grid[py][px].terrain == TERRAIN_OPEN) {
        switch (g.grid[py][px].open_feature) {
            case 1: printf("Wild flowers grow in patches around you.\n"); break;
            case 2: printf("The ground is soft and muddy here.\n"); break;
            case 3: printf("Scrubby bushes grow nearby.\n"); break;
            default: break;
        }
    }

    /* Describe nearby barn / farmhouse */
    describe_nearby_structures();

    /* Describe wall sections — one per compass direction, closest only */
    {
        int r, c;
        /* 8 directions: N NE E SE S SW W NW */
        int dir_dist[8];
        int dir_style[8];
        int dir_dx[8], dir_dy[8];
        int d2;
        for (d2 = 0; d2 < 8; d2++) { dir_dist[d2] = 9999; dir_style[d2] = -1; }

        for (r = py - vr; r <= py + vr; r++) {
            for (c = px - vr; c <= px + vr; c++) {
                if (c < 0 || c >= 30 || r < 0 || r >= 30) continue;
                if (g.grid[r][c].terrain != TERRAIN_WALL) continue;
                int wdx = c - px, wdy = r - py;
                int adx = wdx < 0 ? -wdx : wdx;
                int ady = wdy < 0 ? -wdy : wdy;
                int dist = adx > ady ? adx : ady;

                /* Classify into one of 8 directions */
                int dir;
                if      (wdx == 0 && wdy < 0) dir = 0; /* N  */
                else if (wdx > 0  && wdy < 0) dir = 1; /* NE */
                else if (wdx > 0  && wdy == 0) dir = 2; /* E  */
                else if (wdx > 0  && wdy > 0) dir = 3; /* SE */
                else if (wdx == 0 && wdy > 0) dir = 4; /* S  */
                else if (wdx < 0  && wdy > 0) dir = 5; /* SW */
                else if (wdx < 0  && wdy == 0) dir = 6; /* W  */
                else                           dir = 7; /* NW */

                if (dist < dir_dist[dir]) {
                    dir_dist[dir]  = dist;
                    dir_style[dir] = g.grid[r][c].wall_style;
                    dir_dx[dir]    = wdx;
                    dir_dy[dir]    = wdy;
                }
            }
        }

        for (d2 = 0; d2 < 8; d2++) {
            if (dir_style[d2] < 0) continue;
            /* Skip walls behind the player */
            int behind = 0;
            int facing = g.player.facing;
            if (facing == 0 && dir_dy[d2] > 0) behind = 1;
            if (facing == 1 && dir_dx[d2] < 0) behind = 1;
            if (facing == 2 && dir_dy[d2] < 0) behind = 1;
            if (facing == 3 && dir_dx[d2] > 0) behind = 1;
            if (behind) continue;
            printf("A stone wall to the %s, %s, %s.\n",
                   compass_dir(px, py, px + dir_dx[d2], py + dir_dy[d2]),
                   dist_label(dir_dist[d2]),
                   wall_style_desc(dir_style[d2]));
        }
    }

    /* Visible orcs — within 2 squares */
    for (i = 0; i < MAX_ORCS; i++) {
        if (!g.orcs[i].alive) continue;
        int dx = g.orcs[i].x - px;
        int dy = g.orcs[i].y - py;
        int adx = dx < 0 ? -dx : dx;
        int ady = dy < 0 ? -dy : dy;
        int dist = adx > ady ? adx : ady;
        if (dist <= 2)
            printf("An orc to the %s, %s (HP:%d).\n",
                   compass_dir(px, py, g.orcs[i].x, g.orcs[i].y),
                   dist_label(dist), g.orcs[i].hp);
    }

    /* Visible leprechauns */
    for (i = 0; i < MAX_ELVES; i++) {
        if (!g.elves[i].alive) continue;
        int dx = g.elves[i].x - px;
        int dy = g.elves[i].y - py;
        int adx = dx < 0 ? -dx : dx;
        int ady = dy < 0 ? -dy : dy;
        int dist = adx > ady ? adx : ady;
        if (dist <= vr)
            printf("A leprechaun to the %s, %s.\n",
                   compass_dir(px, py, g.elves[i].x, g.elves[i].y),
                   dist_label(dist));
    }

    /* Visible animals — group by type, print one line per type */
    {
        int counts[6]   = {0,0,0,0,0,0};
        int closest[6]  = {9999,9999,9999,9999,9999,9999};
        int close_x[6]  = {0,0,0,0,0,0};
        int close_y[6]  = {0,0,0,0,0,0};

        for (i = 0; i < g.num_animals; i++) {
            if (!g.animals[i].alive) continue;
            int dx = g.animals[i].x - px;
            int dy = g.animals[i].y - py;
            int adx = dx < 0 ? -dx : dx;
            int ady = dy < 0 ? -dy : dy;
            int dist = adx > ady ? adx : ady;
            if (dist > vr) continue;
            int t = (int)g.animals[i].type;
            counts[t]++;
            if (dist < closest[t]) {
                closest[t] = dist;
                close_x[t] = g.animals[i].x;
                close_y[t] = g.animals[i].y;
            }
        }

        static const char *singular[6] = {"a bird","a rabbit","a cow","a horse","a deer","a wolf"};
        static const char *plural[6]   = {"birds","rabbits","cows","horses","deer","wolves"};
        int t;
        for (t = 0; t < 6; t++) {
            if (counts[t] == 0) continue;
            if (t == (int)ANIMAL_WOLF) {
                printf("A wolf watches you from the %s, %s.\n",
                       compass_dir(px, py, close_x[t], close_y[t]),
                       dist_label(closest[t]));
            } else if (counts[t] == 1) {
                printf("You see %s to the %s, %s.\n",
                       singular[t],
                       compass_dir(px, py, close_x[t], close_y[t]),
                       dist_label(closest[t]));
            } else {
                printf("You see %d %s to the %s, %s.\n",
                       counts[t], plural[t],
                       compass_dir(px, py, close_x[t], close_y[t]),
                       dist_label(closest[t]));
            }
        }
    }

    /* Items on current square */
    {
        Cell *cell = &g.grid[py][px];
        if (cell->has_sword)         printf("There is a sword here.\n");
        if (cell->has_shield)        printf("There is a shield here.\n");
        if (cell->has_flask)         printf("There is a flask here.\n");
        if (cell->has_treasure >= 0) printf("There is treasure here!\n");
    }

    /* Suppress the automatic render_prose that follows every command */
    g.skip_render = 1;
}
void do_score(void) {
    int tcount = 0;
    unsigned m = (unsigned)g.player.treasure_mask;
    while (m) { tcount += (int)(m & 1u); m >>= 1; }
    printf("Current score: %d points (%d treasure%s collected)\n",
           g.player.score, tcount, tcount == 1 ? "" : "s");
    printf("Title: %s\n", score_title(g.player.score));
}
void do_inventory(void) {
    int i;
    printf("Inventory:\n");
    printf("  Sword : %s\n", g.player.has_sword  ? "yes (carrying)" : "no");
    printf("  Shield: %s\n", g.player.has_shield ? "yes (carrying)" : "no");
    printf("  Flasks: %d\n", g.player.flask_count);
    if (g.player.has_key)
        printf("  Key   : ancient iron key (opens a locked chest)\n");
    printf("  Treasures:\n");
    {
        int found = 0;
        for (i = 0; i < 5; i++) {
            if (g.player.treasure_mask & (1 << i)) {
                int ttype = g.treasure_type[i];
                printf("    - %s\n", treasure_names[ttype]);
                found++;
            }
        }
        if (!found) printf("    (none)\n");
    }
}
void do_help(void) {
    printf("Commands:\n");
    printf("  n / north        - Move north\n");
    printf("  s / south        - Move south\n");
    printf("  e / east         - Move east\n");
    printf("  w / west         - Move west\n");
    printf("  ne / northeast   - Move northeast\n");
    printf("  se / southeast   - Move southeast\n");
    printf("  sw / southwest   - Move southwest\n");
    printf("  nw / northwest   - Move northwest\n");
    printf("  left turn        - Turn 90 degrees left (stay in place)\n");
    printf("  right turn       - Turn 90 degrees right (stay in place)\n");
    printf("  map              - Show the full map\n");
    printf("  attack / fight   - Attack an adjacent orc (or hurt yourself if unarmed)\n");
    printf("  hello            - Greet a nearby animal\n");
    printf("  pet              - Pet a nearby animal\n");
    printf("  talk             - Talk to a nearby animal\n");
    printf("  flee / run       - Flee from a nearby orc (-10 HP)\n");
    printf("  run n/s/e/w      - Run 3 squares in a direction (stops if blocked)\n");
    printf("  rest             - Rest and recover 5 HP (not near orcs)\n");
    printf("  dodge / parry    - Halve orc damage for this turn\n");
    printf("  buy / shop       - Trade with the merchant (if nearby)\n");
    printf("  drink            - Drink a healing flask (+30 HP)\n");
    printf("  pick up sword    - Pick up a sword from the ground\n");
    printf("  pick up flask    - Pick up a healing flask from the ground\n");
    printf("  pick up treasure - Pick up treasure (also: take, get)\n");
    printf("  save             - Save the game to savegame.dat\n");
    printf("  load             - Load a saved game\n");
    printf("  look             - Describe your surroundings\n");
    printf("  score            - Show your current score\n");
    printf("  inventory        - Show your inventory\n");
    printf("  help             - Show this command list\n");
    printf("  quit             - Quit the game\n");
}

void do_turn(int direction) {
    /* direction: -1 = left (counter-clockwise), +1 = right (clockwise) */
    static const char *facing_names[4] = { "north", "east", "south", "west" };
    g.player.facing = (g.player.facing + direction + 4) % 4;
    printf("You turn to face %s.\n", facing_names[g.player.facing]);
}

void do_map(void) {
    int row, col;
    int px = g.player.x;
    int py = g.player.y;

    /* Legend */
    printf("\n  MAP\n");
    printf("  @ You   T Forest   , Meadow   _ Farm   ~ River\n");
    printf("  = Bridge   H House   B Barn   G Gate   # Wall\n");
    printf("  L Leprechaun   O Orc   . Open ground\n\n");

    /* Column ruler */
    printf("   ");
    for (col = 0; col < 30; col++) printf("%d", col % 10);
    printf("\n");

    for (row = 0; row < 30; row++) {
        printf("%2d ", row);
        for (col = 0; col < 30; col++) {
            if (col == px && row == py) {
                putchar('@');
                continue;
            }
            switch (g.grid[row][col].terrain) {
                case TERRAIN_OPEN:      putchar('.'); break;
                case TERRAIN_FOREST:    putchar('T'); break;
                case TERRAIN_MEADOW:    putchar(','); break;
                case TERRAIN_RIVER:     putchar('~'); break;
                case TERRAIN_BRIDGE:    putchar('='); break;
                case TERRAIN_FARM:      putchar('_'); break;
                case TERRAIN_FARMHOUSE: putchar('H'); break;
                case TERRAIN_BARN:      putchar('B'); break;
                case TERRAIN_GATE:      putchar('G'); break;
                case TERRAIN_MEADOW2:   putchar(';'); break;
                case TERRAIN_POND:      putchar('o'); break;
                case TERRAIN_TOWER:     putchar('^'); break;
                case TERRAIN_GRAVEYARD: putchar('+'); break;
                case TERRAIN_WALL:      putchar('#'); break;
                default:                putchar('.'); break;
            }
        }
        /* Row annotations for key features */
        if (row == 0)  printf("  <- EXIT (north)");
        if (row == py) printf("  <- you are here");
        printf("\n");
    }
    printf("\n");
}

void do_flee(void) {
    int i;
    int px = g.player.x, py = g.player.y;
    int orc_found = 0;
    int orc_dx = 0, orc_dy = 0;
    int best_dist = 9999;

    for (i = 0; i < MAX_ORCS; i++) {
        if (!g.orcs[i].alive) continue;
        int dx = g.orcs[i].x - px;
        int dy = g.orcs[i].y - py;
        int adx = dx < 0 ? -dx : dx;
        int ady = dy < 0 ? -dy : dy;
        int dist = adx > ady ? adx : ady;
        if (dist <= 2 && dist < best_dist) {
            best_dist = dist; orc_dx = dx; orc_dy = dy; orc_found = 1;
        }
    }

    if (!orc_found) { printf("There is nothing to flee from.\n"); return; }

    int fdx = 0, fdy = 0;
    if (orc_dx > 0) fdx = -1; else if (orc_dx < 0) fdx = 1;
    if (orc_dy > 0) fdy = -1; else if (orc_dy < 0) fdy = 1;

    int moved = 0, step;
    for (step = 0; step < 2; step++) {
        int nx = g.player.x + fdx;
        int ny = g.player.y + fdy;
        if (nx < 1 || nx > 28 || ny < 1 || ny > 28) break;
        Terrain t = g.grid[ny][nx].terrain;
        if (t == TERRAIN_WALL || t == TERRAIN_RIVER ||
            t == TERRAIN_FARMHOUSE || t == TERRAIN_BARN || t == TERRAIN_POND) break;
        g.player.x = nx; g.player.y = ny; moved++;
    }

    g.player.hp = clamp_hp(g.player.hp - 10);
    printf(COL_RED "You flee desperately! (-10 HP)\n" COL_RESET);
    if (moved == 0) printf("You stumble but can't get away!\n");
    else printf("You scramble %d square%s away from the orc.\n", moved, moved == 1 ? "" : "s");
    if (g.player.hp <= 0) show_death("You collapsed while fleeing.");
}

void do_buy(void) {    int px = g.player.x, py = g.player.y;
    if (!g.merchant_alive) { printf("There is no merchant here.\n"); return; }
    int dx = g.merchant_x - px; if (dx < 0) dx = -dx;
    int dy = g.merchant_y - py; if (dy < 0) dy = -dy;
    int dist = dx > dy ? dx : dy;
    if (dist > 2) {
        printf("There is no merchant nearby. (The merchant is somewhere on the farm.)\n");
        return;
    }
    printf("A weathered merchant eyes you from behind a rickety cart.\n");
    printf("\"What'll it be? I don't take coin -- only glory.\"\n\n");
    printf("  (1) Healing flask    -- 75 points\n");
    printf("  (2) Sword            -- 100 points\n");
    printf("  (3) Shield           -- 80 points\n");
    printf("  (4) Nothing, thanks\n");
    printf("> ");
    {
        char buf2[16];
        if (!fgets(buf2, sizeof(buf2), stdin)) return;
        buf2[strcspn(buf2, "\n")] = '\0';
        if (buf2[0] == '1') {
            if (g.player.score < 75) {
                printf("\"Not enough glory. Come back when you've done something impressive.\"\n");
            } else {
                g.player.score -= 75;
                g.player.flask_count++;
                printf("\"There ye go.\" He tosses you a flask. (-75 points)\n");
            }
        } else if (buf2[0] == '2') {
            if (g.player.has_sword) {
                printf("\"You already have a sword. Don't be greedy.\"\n");
            } else if (g.player.score < 100) {
                printf("\"Not enough glory for that.\"\n");
            } else {
                g.player.score -= 100;
                g.player.has_sword = 1;
                printf(COL_GREEN "\"A fine blade.\" He hands you a sword. (-100 points)\n" COL_RESET);
            }
        } else if (buf2[0] == '3') {
            if (g.player.has_shield) {
                printf("\"You already carry a shield. One is enough.\"\n");
            } else if (g.player.score < 80) {
                printf("\"Not enough glory for that.\"\n");
            } else {
                g.player.score -= 80;
                g.player.has_shield = 1;
                printf(COL_GREEN "\"Good protection.\" He hands you a shield. (-80 points)\n" COL_RESET);
            }
        } else {
            printf("\"Suit yourself. The orcs won't be so polite.\"\n");
        }
    }
}

void do_rest(void) {
    int i, orc_near = 0;
    int px = g.player.x, py = g.player.y;
    for (i = 0; i < MAX_ORCS; i++) {
        if (!g.orcs[i].alive) continue;
        int dx = g.orcs[i].x - px; if (dx < 0) dx = -dx;
        int dy = g.orcs[i].y - py; if (dy < 0) dy = -dy;
        if ((dx > dy ? dx : dy) <= 5) { orc_near = 1; break; }
    }
    if (orc_near) {
        printf("You cannot rest -- an orc is too close!\n");
        return;
    }
    g.player.hp = clamp_hp(g.player.hp + 5);
    printf("You rest for a while. (+5 HP)\n");
    /* Advance 3 turns */
    update_npcs(); g.turn++;
    update_npcs(); g.turn++;
    update_npcs(); g.turn++;
}

void do_dodge(void) {
    g.player.dodging = 1;
    printf("You ready yourself to dodge -- orc attacks will deal half damage this turn.\n");
}

void do_save(void) {
    FILE *f = fopen("savegame.dat", "wb");
    if (!f) { printf("Could not save game.\n"); return; }
    fwrite(&g, sizeof(GameState), 1, f);
    fclose(f);
    printf("Game saved.\n");
}

void do_load(void) {
    FILE *f = fopen("savegame.dat", "rb");
    if (!f) { printf("No saved game found.\n"); return; }
    fread(&g, sizeof(GameState), 1, f);
    fclose(f);
    printf("Game loaded.\n");
}

static void do_run(int dx, int dy) {
    int step;
    for (step = 0; step < 3; step++) {
        int nx = g.player.x + dx;
        int ny = g.player.y + dy;
        if (nx < 0 || nx >= 30 || ny < 0 || ny >= 30) break;
        Terrain t = g.grid[ny][nx].terrain;
        if (t == TERRAIN_RIVER || t == TERRAIN_FARMHOUSE ||
            t == TERRAIN_BARN  || t == TERRAIN_WALL     ||
            t == TERRAIN_POND  || t == TERRAIN_TOWER) break;
        move_player(dx, dy);
        if (g.game_over) return;
    }
}

void process_command(Command cmd) {
    switch (cmd) {
        case CMD_NORTH:     move_player( 0, -1); break;
        case CMD_SOUTH:     move_player( 0,  1); break;
        case CMD_EAST:      move_player( 1,  0); break;
        case CMD_WEST:      move_player(-1,  0); break;
        case CMD_NORTHEAST: move_player( 1, -1); break;
        case CMD_SOUTHEAST: move_player( 1,  1); break;
        case CMD_SOUTHWEST: move_player(-1,  1); break;
        case CMD_NORTHWEST: move_player(-1, -1); break;
        case CMD_RUN_NORTH:     do_run( 0, -1); break;
        case CMD_RUN_SOUTH:     do_run( 0,  1); break;
        case CMD_RUN_EAST:      do_run( 1,  0); break;
        case CMD_RUN_WEST:      do_run(-1,  0); break;
        case CMD_RUN_NORTHEAST: do_run( 1, -1); break;
        case CMD_RUN_SOUTHEAST: do_run( 1,  1); break;
        case CMD_RUN_SOUTHWEST: do_run(-1,  1); break;
        case CMD_RUN_NORTHWEST: do_run(-1, -1); break;
        case CMD_REST:   do_rest();  break;
        case CMD_DODGE:  do_dodge(); break;
        case CMD_SAVE:   do_save();         break;
        case CMD_LOAD:   do_load();         break;
        case CMD_HIGHSCORES: show_high_scores(); break;
        case CMD_RUN_PROMPT:
            printf("Run which way? (n/s/e/w/ne/se/sw/nw): ");
            {
                char rbuf[32];
                if (fgets(rbuf, sizeof(rbuf), stdin)) {
                    rbuf[strcspn(rbuf, "\n")] = '\0';
                    int ri; for (ri = 0; rbuf[ri]; ri++) rbuf[ri] = (char)tolower((unsigned char)rbuf[ri]);
                    if (strcmp(rbuf,"n")==0||strcmp(rbuf,"north")==0)     do_run(0,-1);
                    else if (strcmp(rbuf,"s")==0||strcmp(rbuf,"south")==0) do_run(0,1);
                    else if (strcmp(rbuf,"e")==0||strcmp(rbuf,"east")==0)  do_run(1,0);
                    else if (strcmp(rbuf,"w")==0||strcmp(rbuf,"west")==0)  do_run(-1,0);
                    else if (strcmp(rbuf,"ne")==0||strcmp(rbuf,"northeast")==0) do_run(1,-1);
                    else if (strcmp(rbuf,"se")==0||strcmp(rbuf,"southeast")==0) do_run(1,1);
                    else if (strcmp(rbuf,"sw")==0||strcmp(rbuf,"southwest")==0) do_run(-1,1);
                    else if (strcmp(rbuf,"nw")==0||strcmp(rbuf,"northwest")==0) do_run(-1,-1);
                    else printf("Unknown direction.\n");
                }
            }
            break;
        case CMD_ATTACK:
        case CMD_FIGHT:  do_attack();       break;
        case CMD_HELLO:
        case CMD_PET:
        case CMD_TALK:   do_interact(cmd);  break;
        case CMD_DRINK:  do_drink();        break;
        case CMD_PICKUP_SWORD: do_pickup_sword(); break;
        case CMD_PICKUP_FLASK: do_pickup_flask(); break;
        case CMD_PICKUP_TREASURE: do_pickup_treasure(); break;
        case CMD_LOOK:      do_look();      break;
        case CMD_SCORE:     do_score();     break;
        case CMD_INVENTORY: do_inventory(); break;
        case CMD_HELP:      do_help();      break;
        case CMD_TURN_LEFT:  do_turn(-1);   break;
        case CMD_TURN_RIGHT: do_turn(1);    break;
        case CMD_MAP:        do_map();      break;
        case CMD_FLEE:       do_flee();     break;
        case CMD_BUY:        do_buy();      break;
        case CMD_QUIT:
            printf("Are you sure you want to quit? (y/n): ");
            {
                char qbuf[8];
                if (fgets(qbuf, sizeof(qbuf), stdin)) {
                    if (qbuf[0] == 'y' || qbuf[0] == 'Y') {
                        printf("Save before quitting? (y/n): ");
                        char sbuf[8];
                        if (fgets(sbuf, sizeof(sbuf), stdin)) {
                            if (sbuf[0] == 'y' || sbuf[0] == 'Y') do_save();
                        }
                        printf("You are a quitter.\n");
                        printf(COL_GREEN "Final score: %d -- Title: %s\n\n\n\n" COL_RESET,
                               g.player.score, score_title(g.player.score));
                        g.game_over = 2;
                    } else {
                        printf("Wise choice. The quest continues.\n");
                    }
                }
            }
            break;
        default:
            printf("Unknown command. Type 'help' for a list of commands.\n");
            break;
    }
}

/* Spawn a replacement 4x4 sword block at a random valid location */
static void spawn_replacement_sword(void) {
    int attempts, ox, oy, r, c, ok;
    for (attempts = 0; attempts < 100000; attempts++) {
        ox = 1 + rand() % 24;
        oy = 1 + rand() % 24;
        /* Skip player start area */
        if (ox <= 15 && 15 <= ox+3 && oy <= 28 && 28 <= oy+3) continue;
        ok = 1;
        for (r = oy; r < oy+4 && ok; r++) {
            for (c = ox; c < ox+4 && ok; c++) {
                Terrain t = g.grid[r][c].terrain;
                if (t == TERRAIN_RIVER || t == TERRAIN_FARMHOUSE ||
                    t == TERRAIN_BARN  || t == TERRAIN_WALL ||
                    t == TERRAIN_GATE) ok = 0;
                if (g.grid[r][c].has_sword || g.grid[r][c].has_treasure >= 0) ok = 0;
            }
        }
        if (!ok) continue;
        /* Place the block */
        for (r = oy; r < oy+4; r++)
            for (c = ox; c < ox+4; c++)
                g.grid[r][c].has_sword = 1;
        if (g.num_swords < 15) {
            g.sword_ox[g.num_swords] = ox;
            g.sword_oy[g.num_swords] = oy;
            g.num_swords++;
        }
        return;
    }
    /* If placement failed after many tries, silently skip */
}

/* =========================================================
 * NPC update stubs
 * ========================================================= */

void update_orcs(void) {
    int i, j;
    int px = g.player.x;
    int py = g.player.y;
    int attacked_this_turn = 0;

    for (i = 0; i < MAX_ORCS; i++) {
        if (!g.orcs[i].alive) continue;

        int dx = g.orcs[i].x - px;
        int dy = g.orcs[i].y - py;
        int adx = dx < 0 ? -dx : dx;
        int ady = dy < 0 ? -dy : dy;
        int dist = adx > ady ? adx : ady;

        /* Orc detection radius is 4 squares */
        if (dist <= 4) {
            if (!g.orcs[i].spotted) {
                /* First time in range — shout a warning */
                g.orcs[i].spotted = 1;
                if (g.orcs[i].is_warchief) {
                    static const char *warchief_taunts[] = {
                        "A massive orc in black armour bellows: \"I AM THE WARCHIEF! YOUR SKULL WILL DECORATE MY THRONE!\"",
                        "A towering orc roars: \"WARCHIEF GRUKK SEES YOU, LITTLE WORM! RUN IF YOU DARE!\"",
                        "The Warchief snarls: \"I have crushed a hundred warriors! You are nothing!\"",
                        "A huge orc with a scarred face shouts: \"THE WARCHIEF HUNTS TODAY! AND YOU ARE THE PREY!\"",
                        "The Warchief beats its chest and screams: \"BLOOD AND GLORY! COME, LITTLE ADVENTURER!\""
                    };
                    printf(COL_RED "%s\n" COL_RESET, warchief_taunts[rand() % 5]);
                } else {
                    printf(COL_RED "Look out! An orc appears to the %s, %s!\n" COL_RESET,
                           compass_dir(px, py, g.orcs[i].x, g.orcs[i].y),
                           dist_label(dist));
                }
            }
            if (!g.orcs[i].chasing && !g.orcs[i].in_melee) {
                g.orcs[i].chasing    = 1;
                g.orcs[i].chase_dist = 0;
                g.orcs[i].chase_ox   = g.orcs[i].x;
                g.orcs[i].chase_oy   = g.orcs[i].y;
            }
        }

        /* Reset spotted when orc moves out of range */
        if (dist > 4 && !g.orcs[i].chasing && !g.orcs[i].in_melee) {
            g.orcs[i].spotted = 0;
        }

        /* --- MELEE: orc is adjacent, stays put and attacks --- */
        if (g.orcs[i].in_melee) {
            if (dist > 1) {
                /* Player moved away — switch to chasing, but only if within 3 units of chase origin */
                int cdx = g.orcs[i].x - g.orcs[i].chase_ox; if (cdx < 0) cdx = -cdx;
                int cdy = g.orcs[i].y - g.orcs[i].chase_oy; if (cdy < 0) cdy = -cdy;
                int chase_so_far = cdx > cdy ? cdx : cdy;
                if (chase_so_far < 5) {
                    g.orcs[i].in_melee = 0;
                    g.orcs[i].chasing  = 1;
                } else {
                    g.orcs[i].in_melee   = 0;
                    g.orcs[i].chasing    = 0;
                    g.orcs[i].chase_dist = 0;
                    g.orcs[i].spotted    = 0;
                    printf(COL_RED "The orc gives up the chase.\n" COL_RESET);
                }
            } else {
                /* Still adjacent — attack every turn regardless of sword */
                attacked_this_turn = 1;
                int odx = g.orcs[i].x - px;
                int ody = g.orcs[i].y - py;
                const char *from;
                if      (odx == 0 && ody < 0) from = "the north";
                else if (odx == 0 && ody > 0) from = "the south";
                else if (odx > 0  && ody == 0) from = "the east";
                else if (odx < 0  && ody == 0) from = "the west";
                else if (odx > 0  && ody < 0) from = "the northeast";
                else if (odx < 0  && ody < 0) from = "the northwest";
                else if (odx > 0  && ody > 0) from = "the southeast";
                else                           from = "the southwest";
                {
                    int dmg = g.orcs[i].is_warchief ? 15 : 10;
                    if (g.player.has_shield) { dmg /= 2; if (dmg < 1) dmg = 1; }
                    if (g.player.dodging)    { dmg /= 2; if (dmg < 1) dmg = 1; }
                    if (g.player.dodging && g.player.has_shield)
                        printf(COL_RED "The orc strikes from %s! Shield + dodge! (-%d HP)\n" COL_RESET, from, dmg);
                    else if (g.player.has_shield)
                        printf(COL_RED "The orc strikes from %s! Your shield absorbs half! (-%d HP)\n" COL_RESET, from, dmg);
                    else if (g.player.dodging)
                        printf(COL_RED "The orc strikes from %s! You partially dodge! (-%d HP)\n" COL_RESET, from, dmg);
                    else
                        printf(COL_RED "The orc strikes from %s! (-%d HP) [use 'attack' to fight back]\n" COL_RESET, from, dmg);
                    g.player.hp = clamp_hp(g.player.hp - dmg);
                }
                if (g.player.hp <= 0) { show_death("The orc's blows prove fatal."); return; }
                if (g.orcs[i].hp <= 0) g.orcs[i].alive = 0;
                continue;
            }
        }

        /* --- CHASING --- */
        if (g.orcs[i].chasing) {
            /* Check if orc has pursued 3 units from its chase origin */
            {
                int cdx = g.orcs[i].x - g.orcs[i].chase_ox; if (cdx < 0) cdx = -cdx;
                int cdy = g.orcs[i].y - g.orcs[i].chase_oy; if (cdy < 0) cdy = -cdy;
                int chase_so_far = cdx > cdy ? cdx : cdy;
                if (chase_so_far >= 5) {
                    g.orcs[i].chasing    = 0;
                    g.orcs[i].chase_dist = 0;
                    g.orcs[i].spotted    = 0;
                    printf(COL_RED "The orc gives up the chase.\n" COL_RESET);
                    continue;
                }
            }

            /* Move toward player */
            if (dx != 0 || dy != 0) {
                int mx = 0, my = 0;
                if (adx >= ady) mx = dx > 0 ? -1 : 1;
                else            my = dy > 0 ? -1 : 1;
                int nx2 = g.orcs[i].x + mx;
                int ny2 = g.orcs[i].y + my;

                int too_close = 0;
                if (dist > 1) {
                    for (j = 0; j < MAX_ORCS; j++) {
                        if (j == i || !g.orcs[j].alive) continue;
                        int odx2 = nx2 - g.orcs[j].x;
                        int ody2 = ny2 - g.orcs[j].y;
                        int od = (odx2<0?-odx2:odx2) > (ody2<0?-ody2:ody2)
                                 ? (odx2<0?-odx2:odx2) : (ody2<0?-ody2:ody2);
                        if (od < ORC_MIN_DIST) { too_close = 1; break; }
                    }
                }
                if (!too_close) {
                    g.orcs[i].x = nx2;
                    g.orcs[i].y = ny2;
                }
            }

            /* Recalculate distance after move */
            dx = g.orcs[i].x - px;
            dy = g.orcs[i].y - py;
            adx = dx < 0 ? -dx : dx;
            ady = dy < 0 ? -dy : dy;
            dist = adx > ady ? adx : ady;

            if (dist <= 1) {
                /* Reached player — enter melee */
                g.orcs[i].chasing  = 0;
                g.orcs[i].in_melee = 1;

                if (!attacked_this_turn) {
                    attacked_this_turn = 1;
                    int odx = g.orcs[i].x - px;
                    int ody = g.orcs[i].y - py;
                    const char *from;
                    if      (odx == 0 && ody < 0) from = "the north";
                    else if (odx == 0 && ody > 0) from = "the south";
                    else if (odx > 0  && ody == 0) from = "the east";
                    else if (odx < 0  && ody == 0) from = "the west";
                    else if (odx > 0  && ody < 0) from = "the northeast";
                    else if (odx < 0  && ody < 0) from = "the northwest";
                    else if (odx > 0  && ody > 0) from = "the southeast";
                    else                           from = "the southwest";
                    printf(COL_RED "An orc lunges at you from %s! (-10 HP) [use 'attack' to fight back]\n" COL_RESET, from);
                    g.player.hp = clamp_hp(g.player.hp - 10);
                    if (g.player.hp <= 0) { show_death("The orc's blows prove fatal."); return; }
                }
            } else {
                /* Still chasing but not adjacent — chip damage */
                if (!attacked_this_turn) {
                    attacked_this_turn = 1;
                    int odx = g.orcs[i].x - px;
                    int ody = g.orcs[i].y - py;
                    const char *from;
                    if      (odx == 0 && ody < 0) from = "the north";
                    else if (odx == 0 && ody > 0) from = "the south";
                    else if (odx > 0  && ody == 0) from = "the east";
                    else if (odx < 0  && ody == 0) from = "the west";
                    else if (odx > 0  && ody < 0) from = "the northeast";
                    else if (odx < 0  && ody < 0) from = "the northwest";
                    else if (odx > 0  && ody > 0) from = "the southeast";
                    else                           from = "the southwest";
                    printf(COL_RED "An orc charges from %s! (-5 HP)\n" COL_RESET, from);
                    g.player.hp = clamp_hp(g.player.hp - 5);
                    if (g.player.hp <= 0) { show_death("The orc runs you down."); return; }
                }
            }
        }

        if (g.orcs[i].hp <= 0) g.orcs[i].alive = 0;
    }
    /* Reset dodge flag after all orcs have acted */
    g.player.dodging = 0;
}

void update_elves(void) {
    int i, k, tmp;
    int px = g.player.x;
    int py = g.player.y;
    int vr = visibility_radius();
    int fired_this_turn = 0; /* only one leprechaun event per turn */

    static const char *lep_taunts[15] = {
        "\"Ha! And what would ye be doin' with a sword anyway, ya eejit?\"",
        "\"May your sword arm wither and your feet find only puddles!\"",
        "\"Top o' the mornin' - and bottom o' your luck, so it is!\"",
        "\"Sure, you'll not be needin' that where you're goin'!\"",
        "\"I've stolen finer swords from fiercer fools, so I have!\"",
        "\"Ah, don't be givin' me that look - you'd have only dropped it!\"",
        "\"May the road rise up to meet ye - and trip ye on the way!\"",
        "\"That sword was wasted on ye entirely, so it was!\"",
        "\"Swords are for warriors, not wanderers! Good day to ye!\"",
        "\"I'll be keepin' this safe - safer than it was with you, anyway!\"",
        "\"Ah, you're as useful as a chocolate teapot, so ye are!\"",
        "\"May your flasks be empty and your orcs be plenty!\"",
        "\"Did ye think that sword was yours? Bless your heart, ye did!\"",
        "\"Off I go now - don't be cryin', it doesn't suit ye!\"",
        "\"Luck o' the Irish - mine, not yours! Cheerio!\""
    };

    for (i = 0; i < MAX_ELVES; i++) {
        if (!g.elves[i].alive) continue;

        g.elves[i].turn_counter++;

        /* Move at most once every 2 turns */
        if (g.elves[i].turn_counter % 2 == 0) {
            int dirs[4][2] = {{0,-1},{0,1},{1,0},{-1,0}};
            int start = rand() % 4;
            int d;
            for (d = 0; d < 4; d++) {
                int nd = (start + d) % 4;
                int nx = g.elves[i].x + dirs[nd][0];
                int ny = g.elves[i].y + dirs[nd][1];
                if (nx < 1 || nx > 28 || ny < 1 || ny > 28) continue;
                int cdx = nx - g.elves[i].patrol_cx;
                int cdy = ny - g.elves[i].patrol_cy;
                if (cdx < 0) cdx = -cdx;
                if (cdy < 0) cdy = -cdy;
                if (cdx > 3 || cdy > 3) continue;
                Terrain nt = g.grid[ny][nx].terrain;
                if (nt == TERRAIN_RIVER || nt == TERRAIN_FARMHOUSE ||
                    nt == TERRAIN_BARN  || nt == TERRAIN_WALL) continue;
                g.elves[i].x = nx;
                g.elves[i].y = ny;
                break;
            }
        }

        /* Check if leprechaun is within player's visibility radius */
        {
            int edx = g.elves[i].x - px;
            int edy = g.elves[i].y - py;
            int adx = edx < 0 ? -edx : edx;
            int ady = edy < 0 ? -edy : edy;
            int dist = adx > ady ? adx : ady;

            if (dist <= vr) {
                /* River barrier check */
                int elf_col = g.elves[i].x;
                int elf_row = g.elves[i].y;
                int river_at_elf = -1, river_at_player = -1;
                int r;
                for (r = 0; r < 30; r++) {
                    if (g.grid[r][elf_col].terrain == TERRAIN_RIVER && river_at_elf < 0)
                        river_at_elf = r;
                    if (g.grid[r][px].terrain == TERRAIN_RIVER && river_at_player < 0)
                        river_at_player = r;
                }
                int blocked_by_river = 0;
                if (river_at_elf >= 0 && river_at_player >= 0) {
                    if ((elf_row < river_at_elf && py > river_at_player) ||
                        (elf_row > river_at_elf && py < river_at_player))
                        blocked_by_river = 1;
                }

                if (fired_this_turn) continue; /* only one leprechaun event per turn */

                g.player.elves_encountered++;

                if (!blocked_by_river && (g.player.has_sword || g.player.has_shield)) {
                    fired_this_turn = 1;
                    g.elves[i].alive = 0;

                    /* Pick taunt from shuffle deck */
                    if (g.lep_deck_pos >= 15) {
                        for (k = 0; k < 15; k++) g.lep_deck[k] = k;
                        for (k = 14; k > 0; k--) {
                            int jj = rand() % (k + 1);
                            tmp = g.lep_deck[k]; g.lep_deck[k] = g.lep_deck[jj]; g.lep_deck[jj] = tmp;
                        }
                        g.lep_deck_pos = 0;
                    }
                    int tidx = g.lep_deck[g.lep_deck_pos++];

                    /* Steal sword or shield — prefer sword, but steal shield if no sword */
                    if (g.player.has_sword && g.player.has_shield) {
                        /* Has both — steal one at random */
                        if (rand() % 2 == 0) {
                            g.player.has_sword = 0;
                            printf(COL_GREEN "A leprechaun darts out and steals your sword! It vanishes!\n");
                            spawn_replacement_sword();
                        } else {
                            g.player.has_shield = 0;
                            printf(COL_GREEN "A leprechaun darts out and steals your shield! It vanishes!\n");
                        }
                    } else if (g.player.has_sword) {
                        g.player.has_sword = 0;
                        printf(COL_GREEN "A leprechaun darts out and steals your sword! It vanishes!\n");
                        spawn_replacement_sword();
                    } else {
                        g.player.has_shield = 0;
                        printf(COL_GREEN "A leprechaun darts out and steals your shield! It vanishes!\n");
                    }
                    printf("%s\n" COL_RESET, lep_taunts[tidx]);
                } else if (!blocked_by_river && !g.player.has_sword && !g.player.has_shield) {
                    fired_this_turn = 1;
                    g.elves[i].alive = 0;
                    printf("A leprechaun eyes you up and down, then grins.\n");
                    printf("\"I'll be keepin' an eye on ye for sure!\" It darts away into the distance.\n");
                } else if (!blocked_by_river) {
                    fired_this_turn = 1;
                    g.elves[i].alive = 0;
                    printf("A leprechaun appears briefly, winks, and vanishes.\n");
                }
            }
        }
    }
}

void update_animals(void) {
    int i;
    int dirs[4][2] = {{0,-1},{0,1},{1,0},{-1,0}};
    int px = g.player.x;
    int py = g.player.y;

    for (i = 0; i < g.num_animals; i++) {
        if (!g.animals[i].alive) continue;

        g.animals[i].turn_counter++;

        /* --- WOLF special logic --- */
        if (g.animals[i].type == ANIMAL_WOLF) {
            int wdx = g.animals[i].x - px;
            int wdy = g.animals[i].y - py;
            int adx = wdx < 0 ? -wdx : wdx;
            int ady = wdy < 0 ? -wdy : wdy;
            int dist = adx > ady ? adx : ady;

            if (dist <= 15) {
                /* Wolf is watching — set threshold on first spot, then increment */
                if (g.animals[i].wolf_menace_turns == 0) {
                    g.animals[i].wolf_menace_threshold = 2 + rand() % 3; /* 2, 3, or 4 */
                    printf("A wolf eyes you menacingly from the %s.\n",
                           compass_dir(px, py, g.animals[i].x, g.animals[i].y));
                }
                g.animals[i].wolf_menace_turns++;

                if (g.animals[i].wolf_menace_turns >= g.animals[i].wolf_menace_threshold) {
                    /* 10% chance: wolf bites and flees; 45% charges but veers; 45% just leaves */
                    int roll = rand() % 10;
                    if (roll == 0) {
                        /* Bite! */
                        int wdmg = g.player.has_shield ? 5 : 10;
                        printf(COL_RED "The wolf lunges and bites you before bolting into the distance! (-%d HP)\n" COL_RESET, wdmg);
                        if (g.player.has_shield)
                            printf("Your shield absorbs half the damage!\n");
                        g.player.hp = clamp_hp(g.player.hp - wdmg);
                        if (g.player.hp <= 0) { show_death("The wolf's bite proved fatal."); return; }
                    } else if (roll <= 4) {
                        printf("The wolf snarls and charges at you -- then veers away at the last moment, vanishing into the distance!\n");
                    } else {
                        printf("The wolf holds your gaze a moment longer, then turns and lopes away into the distance.\n");
                    }
                    /* Move wolf far away (reset to a random open square far from player) */
                    {
                        int attempts2, fx2, fy2;
                        for (attempts2 = 0; attempts2 < 500; attempts2++) {
                            fx2 = rand() % 30;
                            fy2 = rand() % 30;
                            if (g.grid[fy2][fx2].terrain != TERRAIN_OPEN) continue;
                            int ddx2 = fx2 - px; if (ddx2 < 0) ddx2 = -ddx2;
                            int ddy2 = fy2 - py; if (ddy2 < 0) ddy2 = -ddy2;
                            if (ddx2 > 15 || ddy2 > 15) {
                                g.animals[i].x = fx2;
                                g.animals[i].y = fy2;
                                break;
                            }
                        }
                    }
                    g.animals[i].wolf_menace_turns     = 0;
                    g.animals[i].wolf_menace_threshold = 0;
                }
            } else {
                /* Out of range — reset menace and wander slowly */
                g.animals[i].wolf_menace_turns     = 0;
                g.animals[i].wolf_menace_threshold = 0;
                if (g.animals[i].turn_counter % 5 == 0) {
                    int start = rand() % 4;
                    int d;
                    for (d = 0; d < 4; d++) {
                        int nd = (start + d) % 4;
                        int nx = g.animals[i].x + dirs[nd][0];
                        int ny = g.animals[i].y + dirs[nd][1];
                        if (nx < 1 || nx > 28 || ny < 1 || ny > 28) continue;
                        if (g.grid[ny][nx].terrain == TERRAIN_OPEN) {
                            g.animals[i].x = nx;
                            g.animals[i].y = ny;
                            break;
                        }
                    }
                }
            }
            continue;
        }

        /* Move at most once every 3 turns for other animals */
        if (g.animals[i].turn_counter % 3 != 0) continue;

        Terrain home;
        if (g.animals[i].type == ANIMAL_BIRD || g.animals[i].type == ANIMAL_RABBIT) {
            home = TERRAIN_MEADOW;
        } else if (g.animals[i].type == ANIMAL_DEER) {
            home = TERRAIN_OPEN;
        } else {
            home = TERRAIN_FARM;
        }

        /* Try a random adjacent cell of the correct terrain type */
        int start = rand() % 4;
        int d;
        for (d = 0; d < 4; d++) {
            int nd = (start + d) % 4;
            int nx = g.animals[i].x + dirs[nd][0];
            int ny = g.animals[i].y + dirs[nd][1];
            if (nx < 0 || nx >= 30 || ny < 0 || ny >= 30) continue;
            if (g.grid[ny][nx].terrain == home) {
                g.animals[i].x = nx;
                g.animals[i].y = ny;
                break;
            }
        }
    }
}

void update_dragon(void) {
    static const char *dragon_greetings[] = {
        "\"Top o' the mornin' to ye, adventurer! Grand day for a quest, is it not?\"",
        "\"Dia dhuit! Don't mind me -- just havin' a stretch. Lovely pond, isn't it?\"",
        "\"Failte! Welcome, welcome! I'd shake your hand but, well, claws.\"",
        "\"Ah, a visitor! Slan abhaile when you're done, and mind the orcs!\"",
        "\"Conas ata tu? How are ye? I'm grand, thanks for not asking!\"",
        "\"Beannachtai! Blessings upon ye, traveller. The pond is fine today.\"",
        "\"Maith thu! Well done for making it this far. Most turn back at the river.\"",
        "\"Erin go bragh! Ireland forever! ...Well, this meadow forever, anyway.\"",
        "\"Slainte! Good health to ye! I'd offer a drink but the pond is mine.\"",
        "\"Go n-eirí an bothar leat! May the road rise with you, friend!\""
    };

    if (!g.dragon.alive) return;
    if (g.turn < g.dragon.surface_turn) return;

    /* Dragon surfaces -- only if player is within 2 units of any pond cell */
    int px = g.player.x, py = g.player.y;
    int near_pond = 0;
    int pond_cx = g.dragon.x, pond_cy = g.dragon.y; /* fallback direction */
    {
        int r, c;
        int best = 9999;
        for (r = 0; r < 30; r++) {
            for (c = 0; c < 30; c++) {
                if (g.grid[r][c].terrain != TERRAIN_POND) continue;
                int ddx = c - px; if (ddx < 0) ddx = -ddx;
                int ddy = r - py; if (ddy < 0) ddy = -ddy;
                int d = ddx > ddy ? ddx : ddy;
                if (d <= 2 && d < best) {
                    best = d; near_pond = 1;
                    pond_cx = c; pond_cy = r;
                }
            }
        }
    }

    if (near_pond) {
        int idx = rand() % 10;
        printf("\nA great emerald head rises from the pond to the %s with a tremendous splash!\n",
               compass_dir(px, py, pond_cx, pond_cy));
        printf("The dragon blinks its golden eyes at you and booms:\n");
        printf("%s\n", dragon_greetings[idx]);

        /* 30% chance: hint about locked chest location */
        if (rand() % 10 < 3 && g.locked_treasure >= 0 &&
            !(g.player.treasure_mask & (1 << g.locked_treasure)) &&
            g.grid[g.treasure_oy[g.locked_treasure]][g.treasure_ox[g.locked_treasure]].has_treasure >= 0) {
            int tcx = g.treasure_ox[g.locked_treasure] + 2;
            int tcy = g.treasure_oy[g.locked_treasure] + 2;
            static const char *dragon_hints[] = {
                "The dragon lowers its voice: \"Between you and me... the locked chest lies to the %s. Don't tell the orcs.\"",
                "The dragon winks a golden eye: \"A word of advice -- the locked treasure waits to the %s. You'll need a key.\"",
                "The dragon rumbles thoughtfully: \"I've seen that locked chest to the %s. Malachar hid it well, but not from me.\"",
                "The dragon whispers (which still shakes the ground): \"The %s holds a locked chest. Find the key first!\""
            };
            char hint_msg[256];
            sprintf(hint_msg, dragon_hints[rand() % 4],
                    compass_dir(px, py, tcx, tcy));
            printf("%s\n", hint_msg);
        }

        printf("Then, with a cheerful wink, it slips back beneath the dark water.\n\n");
    }

    /* Schedule next surfacing: 5-15 turns from now */
    g.dragon.surface_turn = g.turn + 5 + rand() % 11;
}

void update_npcs(void) {
    update_orcs();
    update_elves();
    update_animals();
    update_dragon();
}

/* =========================================================
 * End-game stubs
 * ========================================================= */

static const char *score_title(int s) {
    if (s >= 2000) return "The One Ring-bearer (Frodo himself would be humbled)";
    if (s >= 1500) return "Ranger of the North (worthy of Aragorn's company)";
    if (s >= 1200) return "Knight of Gondor (Faramir would be proud)";
    if (s >= 1000) return "Companion of the Fellowship (Gandalf nods approvingly)";
    if (s >= 800)  return "Rider of Rohan (Eowyn raises her sword in salute)";
    if (s >= 600)  return "Warden of the Shire (the hobbits buy you a round)";
    if (s >= 400)  return "Wanderer of Middle-earth (Bilbo would understand)";
    if (s >= 200)  return "Apprentice Adventurer (Samwise has seen better)";
    if (s >= 100)  return "Lost Traveller (even the Sackville-Bagginses did more)";
    return "Worm of Morgoth (you didn't last long)";
}

/* =========================================================
 * High score functions
 * ========================================================= */

void load_high_scores(void) {
    FILE *f = fopen(HIGHSCORE_FILE, "rb");
    hi_count = 0;
    if (!f) return;
    fread(&hi_count, sizeof(int), 1, f);
    if (hi_count < 0 || hi_count > MAX_HIGH_SCORES) hi_count = 0;
    fread(hi_scores, sizeof(HighScore), hi_count, f);
    fclose(f);
}

void save_high_scores(void) {
    FILE *f = fopen(HIGHSCORE_FILE, "wb");
    if (!f) return;
    fwrite(&hi_count, sizeof(int), 1, f);
    fwrite(hi_scores, sizeof(HighScore), hi_count, f);
    fclose(f);
}

void add_high_score(const char *name, int score, const char *title) {
    int i, j;
    /* Find insertion point (sorted descending by score) */
    int pos = hi_count;
    for (i = 0; i < hi_count; i++) {
        if (score > hi_scores[i].score) { pos = i; break; }
    }
    if (pos >= MAX_HIGH_SCORES) return; /* not in top 10 */
    /* Shift entries down */
    int new_count = hi_count < MAX_HIGH_SCORES ? hi_count + 1 : MAX_HIGH_SCORES;
    for (j = new_count - 1; j > pos; j--)
        hi_scores[j] = hi_scores[j-1];
    /* Insert */
    strncpy(hi_scores[pos].name,  name,  PLAYER_NAME_LEN - 1);
    hi_scores[pos].name[PLAYER_NAME_LEN - 1] = '\0';
    hi_scores[pos].score = score;
    strncpy(hi_scores[pos].title, title, 63);
    hi_scores[pos].title[63] = '\0';
    hi_count = new_count;
    save_high_scores();
}

void show_high_scores(void) {
    int i;
    printf(COL_YELLOW "\n=== HIGH SCORES ===\n" COL_RESET);
    if (hi_count == 0) {
        printf("  No scores yet. Be the first!\n\n");
        return;
    }
    printf("  %-4s %-24s %-8s %s\n", "Rank", "Name", "Score", "Title");
    printf("  %-4s %-24s %-8s %s\n", "----", "----", "-----", "-----");
    for (i = 0; i < hi_count; i++) {
        printf(COL_YELLOW "  #%-3d %-24s %-8d %s\n" COL_RESET,
               i + 1, hi_scores[i].name, hi_scores[i].score, hi_scores[i].title);
    }
    printf("\n");
}

void show_death(const char *msg) {
    g.player.score = 0;
    printf("\n*** YOU HAVE DIED ***\n%s\n", msg);
    printf(COL_GREEN "Your score: 0\nTitle: %s\n\n\n\n" COL_RESET, score_title(0));
    add_high_score(player_name, 0, score_title(0));
    show_high_scores();
    g.game_over = 2;
}

int popcount_mask(int mask) {
    int count = 0;
    unsigned m = (unsigned)mask;
    while (m) { count += (int)(m & 1u); m >>= 1; }
    return count;
}

void show_summary(void) {
    int all_treasures = (popcount_mask(g.player.treasure_mask) >= 5);
    if (all_treasures &&
        g.grid[g.player.y][g.player.x].terrain == TERRAIN_GATE) {
        g.player.score += 1000;
        printf(COL_YELLOW "\n*** You carry all the treasures through the great gate! (+1000 points) ***\n" COL_RESET);
    }

    int s = g.player.score;
    printf("\n=== GAME SUMMARY ===\n");
    printf("Player              : %s\n", player_name);
    printf("Treasures collected : %d\n", popcount_mask(g.player.treasure_mask));
    printf("Orcs killed         : %d\n", g.player.orcs_killed);
    printf("Elves encountered   : %d\n", g.player.elves_encountered);
    printf("Flasks found        : %d\n", g.player.flasks_found);
    printf("Animals interacted  : %d\n", g.player.animals_interacted);
    printf(COL_GREEN "Final score         : %d\n", s);
    printf("Title               : %s\n\n\n\n" COL_RESET, score_title(s));

    add_high_score(player_name, s, score_title(s));
    show_high_scores();
}

void check_triggers(void) {
    if (g.player.hp <= 0) {
        show_death("Your wounds were too great.");
    }
    if (g.game_over == 0 &&
        g.grid[g.player.y][g.player.x].terrain == TERRAIN_GATE) {

        if (!g.riddle_answered) {
            /* Kill any orcs within 10 squares with random magic */
            static const char *magic_kills[] = {
                "A bolt of white lightning arcs from the gate and strikes the orc dead!",
                "The gatekeeper raises one hand -- the orc freezes, then crumbles to ash.",
                "A wave of blue fire rolls outward from the gate, consuming the orc instantly.",
                "The orc clutches its chest and collapses as an invisible force crushes it.",
                "Roots burst from the earth and drag the orc screaming into the ground.",
                "The gatekeeper whispers a single word -- the orc simply ceases to exist."
            };
            int i;
            int orc_killed = 0;
            int px = g.player.x, py = g.player.y;
            for (i = 0; i < MAX_ORCS; i++) {
                if (!g.orcs[i].alive) continue;
                int dx = g.orcs[i].x - px; if (dx < 0) dx = -dx;
                int dy = g.orcs[i].y - py; if (dy < 0) dy = -dy;
                int dist = dx > dy ? dx : dy;
                if (dist <= 10) {
                    g.orcs[i].alive = 0;
                    if (!orc_killed) {
                        printf(COL_YELLOW "\n%s\n" COL_RESET,
                               magic_kills[rand() % 6]);
                        orc_killed = 1;
                    }
                }
            }
            /* Three blank lines before the gatekeeper speaks */
            printf("\n\n\n");

            /* Gatekeeper riddle — must answer correctly to pass */
            static const char *riddles[][2] = {
                { "I have cities, but no houses live there. I have mountains, but no trees grow. I have water, but no fish swim. I have roads, but no cars travel. What am I?", "map" },
                { "The more you take, the more you leave behind. What am I?", "footsteps" },
                { "I speak without a mouth and hear without ears. I have no body, but I come alive with wind. What am I?", "echo" },
                { "I have hands but cannot clap. What am I?", "clock" },
                { "What has keys but no locks, space but no room, and you can enter but can't go inside?", "keyboard" },
                { "I am always in front of you but cannot be seen. What am I?", "future" },
                { "The more you have of it, the less you see. What am I?", "darkness" },
                { "I have a head and a tail, but no body. What am I?", "coin" },
                { "I am light as a feather, yet the strongest man cannot hold me for more than a few minutes. What am I?", "breath" },
                { "What has teeth but cannot bite?", "comb" },
                { "I run but have no legs. I have a mouth but never talk. What am I?", "river" },
                { "The more you feed me, the more I grow. Give me water and I die. What am I?", "fire" },
                { "I have branches but no fruit, trunk, or leaves. What am I?", "bank" },
                { "What can travel around the world while staying in a corner?", "stamp" },
                { "I have an eye but cannot see. What am I?", "needle" },
                { "What gets wetter the more it dries?", "towel" },
                { "I am not alive, but I grow. I have no lungs, but I need air. What am I?", "fire" },
                { "What has one eye but cannot see?", "needle" },
                { "I shrink every time you use me. What am I?", "candle" },
                { "What can you catch but not throw?", "cold" }
            };
            int num_riddles = 20;
            int ridx = rand() % num_riddles;

            printf(COL_YELLOW "\nA hooded figure steps from the shadows beside the gate.\n");
            printf("\"None may pass without answering my riddle.\"\n\n");
            printf("RIDDLE: %s\n\n", riddles[ridx][0]);
            printf("Your answer (one word): " COL_RESET);

            char ans[64];
            if (fgets(ans, sizeof(ans), stdin)) {
                /* Strip newline and trailing spaces, lowercase */
                int len = (int)strlen(ans);
                while (len > 0 && (ans[len-1] == '\n' || ans[len-1] == '\r' || ans[len-1] == ' '))
                    ans[--len] = '\0';
                /* Lowercase */
                int ci;
                for (ci = 0; ci < len; ci++)
                    ans[ci] = (char)tolower((unsigned char)ans[ci]);

                if (strcmp(ans, riddles[ridx][1]) == 0) {
                    g.riddle_answered = 1;
                    g.riddle_wrong_streak = 0;
                    printf(COL_YELLOW "\nThe gatekeeper nods slowly. \"Correct. You may pass.\"\n");
                    printf("The great iron gate groans and swings wide.\n");
                    printf("You step through into the world beyond, your quest complete.\n" COL_RESET);
                    g.game_over = 1;
                    show_summary();
                } else {
                    g.riddle_wrong_streak++;
                    /* Push player back one square south */
                    if (g.player.y < 29) {
                        Terrain t = g.grid[g.player.y + 1][g.player.x].terrain;
                        if (t != TERRAIN_WALL && t != TERRAIN_RIVER &&
                            t != TERRAIN_FARMHOUSE && t != TERRAIN_BARN && t != TERRAIN_POND)
                            g.player.y++;
                    }

                    if (g.riddle_wrong_streak >= 2) {
                        /* Every 2nd consecutive wrong answer — gatekeeper takes a treasure */
                        static const char *gk_angry[] = {
                            "\"FOOL! You dare waste my time with such ignorance?\"",
                            "\"Blithering idiot! A child could answer that!\"",
                            "\"By the ancient stones, you are the dimmest soul I have ever seen!\"",
                            "\"ENOUGH! Your stupidity offends the very gate itself!\"",
                            "\"You absolute dullard! I've seen smarter orcs!\""
                        };
                        printf(COL_RED "\n%s\n", gk_angry[rand() % 5]);

                        /* Find a treasure the player has and take it */
                        int took = 0, ti;
                        for (ti = 4; ti >= 0 && !took; ti--) {
                            if (g.player.treasure_mask & (1 << ti)) {
                                int ttype = g.treasure_type[ti];
                                g.player.treasure_mask &= ~(1 << ti);
                                g.player.score -= treasure_values[ttype];
                                if (g.player.score < 0) g.player.score = 0;
                                printf("The gatekeeper snatches your %s and hurls it back into the world!\n",
                                       treasure_names[ttype]);
                                took = 1;
                            }
                        }
                        if (!took)
                            printf("The gatekeeper glares at you. \"Go find something worth bringing back!\"\n");
                        printf(COL_RESET);
                        g.riddle_wrong_streak = 0; /* reset streak after penalty */
                    } else {
                        printf(COL_RED "\nThe gatekeeper shakes his head. \"Wrong. You may not pass.\"\n" COL_RESET);
                    }
                }
            }
        } else {
            g.game_over = 1;
            printf(COL_YELLOW "\nThe great iron gate groans and swings wide as you approach.\n");
            printf("You step through into the world beyond, your quest complete.\n" COL_RESET);
            show_summary();
        }
    }
}

/* =========================================================
 * Initialization
 * ========================================================= */

void init_game(void) {
    int i, j, tmp;
    srand((unsigned)time(NULL));
    memset(&g, 0, sizeof(g));
    gen_world(); /* place_items() sets has_treasure correctly */
    g.player.x      = 15;
    g.player.y      = 28;
    g.player.hp     = 100;
    g.player.facing = 0; /* start facing north */

    /* Initialise meadow animal saying deck (shuffle 0-14) */
    for (i = 0; i < 15; i++) g.meadow_deck[i] = i;
    for (i = 14; i > 0; i--) {
        j = rand() % (i + 1);
        tmp = g.meadow_deck[i]; g.meadow_deck[i] = g.meadow_deck[j]; g.meadow_deck[j] = tmp;
    }
    g.meadow_deck_pos = 0;

    /* Initialise farm animal saying deck (shuffle 0-14) */
    for (i = 0; i < 15; i++) g.farm_deck[i] = i;
    for (i = 14; i > 0; i--) {
        j = rand() % (i + 1);
        tmp = g.farm_deck[i]; g.farm_deck[i] = g.farm_deck[j]; g.farm_deck[j] = tmp;
    }
    g.farm_deck_pos = 0;

    /* Initialise leprechaun taunt deck (shuffle 0-14) */
    for (i = 0; i < 15; i++) g.lep_deck[i] = i;
    for (i = 14; i > 0; i--) {
        j = rand() % (i + 1);
        tmp = g.lep_deck[i]; g.lep_deck[i] = g.lep_deck[j]; g.lep_deck[j] = tmp;
    }
    g.lep_deck_pos = 0;
}

void show_intro(void) {
    /* Clear screen */
    printf("\x1b[2J\x1b[H");
    printf(COL_YELLOW);
    printf("  ____        _                       _\n");
    printf(" |  _ \\ _   _| | __ _ _ __   ___  _ __( )___\n");
    printf(" | |_) | | | | |/ _` | '_ \\ / _ \\| '__|// __|\n");
    printf(" |  _ <| |_| | | (_| | | | | (_) | |    \\__ \\\n");
    printf(" |_| \\_\\\\__, |_|\\__,_|_| |_|\\___/|_|    |___/\n");
    printf("         |___/\n");
    printf("\n");
    printf("   ____       _     _    ___                  _\n");
    printf("  / ___| ___ | | __| |  / _ \\ _   _  ___  ___| |_\n");
    printf(" | |  _ / _ \\| |/ _` | | | | | | | |/ _ \\/ __| __|\n");
    printf(" | |_| | (_) | | (_| | | |_| | |_| |  __/\\__ \\ |_\n");
    printf("  \\____|\\___/|_|\\__,_|  \\__\\_\\\\__,_|\\___||___/\\__|\n");
    printf(COL_WHITE);
    printf("\n");
    printf("          Created by Rich Buckley  March 2026\n");
    printf(COL_RESET);
    printf("THE STORY:\n");
    printf("  The wizard Malachar hid five ancient treasures across this land\n");
    printf("  before the orcs came. Now the orcs are searching for them.\n");
    printf("  You must find all five treasures before they do -- then escape\n");
    printf("  through the great gate to the north. One chest is locked; an\n");
    printf("  ancient key lies hidden somewhere in the world.\n");
    printf("  A gatekeeper guards the exit. Answer his riddle to pass.\n\n");
    printf("THE WORLD:\n");
    printf("  A dense forest (visibility: 5 squares)\n");
    printf("  A peaceful meadow with talking animals (visibility: 10 squares)\n");
    printf("  A river blocks your path -- find one of 6 bridges to cross\n");
    printf("  A farm with a farmhouse, barn, and a merchant who trades in glory\n");
    printf("  A second meadow with a dark pond -- and something lurking within\n\n");
    printf("Press ENTER to continue...\n");
    { char buf[8]; fgets(buf, sizeof(buf), stdin); }
    printf("DANGERS:\n");
    printf("  10 Orcs roam the land -- they attack on sight\n");
    printf("  6 Leprechauns patrol the world -- they will steal your sword!\n\n");
    printf("ITEMS:\n");
    printf("  Swords (15) -- required to fight orcs\n");
    printf("  Healing flasks (20) -- restore 30 HP each\n");
    printf("  Treasures (5) -- each worth points; one is locked\n");
    printf("  Ancient key (1) -- hidden somewhere; opens the locked chest\n\n");
    printf("GOAL:\n");
    printf("  Collect all 5 treasures and exit through the north gate.\n");
    printf("  You start with 100 HP. Maximum HP is 120.\n\n");
    printf("COMMANDS:\n");
    printf("  n/s/e/w  ne/se/sw/nw              -- move\n");
    printf("  run n/s/e/w                       -- run 3 squares\n");
    printf("  left turn / right turn            -- turn in place\n");
    printf("  attack / fight                    -- attack adjacent orc\n");
    printf("  dodge / parry                     -- halve orc damage this turn\n");
    printf("  flee                              -- flee from nearby orc (-10 HP)\n");
    printf("  rest                              -- rest and recover 5 HP\n");
    printf("  buy / shop                        -- trade with the merchant\n");
    printf("  hello / pet / talk                -- interact with animals\n");
    printf("  take / get                        -- pick up items\n");
    printf("  drink                             -- use a healing flask\n");
    printf("  look                              -- describe surroundings\n");
    printf("  map                               -- show full map\n");
    printf("  save / load                       -- save or load game\n");
    printf("  highscores                        -- show high score table\n");
    printf("  score / inventory / help / quit\n\n");
    printf("Press ENTER to begin your quest...\n");
    { char buf[8]; fgets(buf, sizeof(buf), stdin); }
}

void choose_display_mode(void) {
    char buf[16];
    printf("Choose display mode:\n");
    printf("  (1) ASCII map    -- visual grid showing the world\n");
    printf("  (2) Text only    -- prose descriptions of your surroundings\n");
    printf("> ");
    if (fgets(buf, sizeof(buf), stdin)) {
        if (buf[0] == '2') g.mode = MODE_TEXT;
        else               g.mode = MODE_ASCII;
    }
}

void game_loop(void) {
    Command cmd;
    while (g.game_over == 0) {
        render();
        cmd = read_command();
        process_command(cmd);
        if (g.game_over != 0) break;
        update_npcs();
        check_triggers();
        g.turn++;
    }
}

/* =========================================================
 * Entry point (DOS build only)
 * ========================================================= */

#ifndef TEST_BUILD
int main(void) {
    load_high_scores();
    show_intro();

    /* Ask for player name */
    printf("Enter your name (for the high score table): ");
    if (fgets(player_name, PLAYER_NAME_LEN, stdin)) {
        player_name[strcspn(player_name, "\n\r")] = '\0';
        if (player_name[0] == '\0') strcpy(player_name, "Unknown");
    }

    /* Show existing high scores */
    show_high_scores();
    printf("Press ENTER to continue...\n");
    { char buf[8]; fgets(buf, sizeof(buf), stdin); }

    /* Check for existing save file and offer to load */
    {
        FILE *f = fopen("savegame.dat", "rb");
        if (f) {
            fclose(f);
            char lbuf[8];
            printf("A saved game was found. Load it? (y/n): ");
            if (fgets(lbuf, sizeof(lbuf), stdin) && (lbuf[0] == 'y' || lbuf[0] == 'Y')) {
                memset(&g, 0, sizeof(g));
                f = fopen("savegame.dat", "rb");
                if (f) { fread(&g, sizeof(GameState), 1, f); fclose(f); }
                printf("Game loaded. Welcome back, %s!\n", player_name);
                printf("Press ENTER to continue...\n");
                { char buf[8]; fgets(buf, sizeof(buf), stdin); }
                game_loop();
                return 0;
            }
        }
    }

    init_game();
    choose_display_mode();
    game_loop();
    return 0;
}
#endif /* TEST_BUILD */
