#include "wasm4.h"

/* Gamepad debounce buffer size */
#define GAMEPAD_LAST_SIZE 5
#define GROUND_Y 145

#define PLANE_Y 10
#define PLANE_DX 30

#define DIVER_Y_START 18
#define DIVER_DY_START 5
#define DIVER_DY_ACCEL 10
#define DIVER_DY_MAX 30
#define DIVER_DY_CHUTE_FACTOR 2
#define DIVER_DY_CHUTE_MIN 15
#define DIVER_DY_CHUTE_DECEL 2
#define DIVER_DY_WIND_CHUTE 10

#define WIND_DX_MAX 10

#define WIN_JUMPS 10

#define FPS(x) ((x) / 60.0)

enum game_state {
  TITLE_SCREEN,
  STARTING,
  READY_JUMP,
  NO_JUMP,
  FALLING,
  CRASHED,
  TOO_FAST,
  YOU_WIN,
  GAME_OVER
};

void reset();
void transition(enum game_state new);
void title_screen();
void starting();
void ready_jump();
void no_jump();
void falling();
void crashed();
void you_win();
void game_over();
void draw_background();
void draw_overlay();
void draw_plane();
void draw_diver();
void draw_wind();
void update_plane();

void itoa(int num, char *buffer, size_t len);
uint32_t rand(uint32_t m);

const uint16_t plane_sprite[] = {
  0b0000011111100000,
  0b1100000110000000,
  0b1111111111111100,
  0b0011111111111111,
  0b0000000110000000,
  0b0000011111100000
};

const uint8_t diver_chute[] = {
  0b00111100,
  0b01111110,
  0b10000001,
  0b01011010,
  0b00011000,
  0b00100100,
  0b00100100,
  0b00000000
};

const uint8_t diver_splat[] = {
  0b00111100,
  0b01111110,
  0b01011010,
  0b01011010,
};

const uint8_t diver_falling[] = {
  0b00000100,
  0b00001000,
  0b10101101,
  0b01011110,
  0b00111100
};

const uint8_t flag_3[] = {
  0b00000000,
  0b10000000,
  0b11100000,
  0b11111111,
  0b11111000,
  0b10000000,
  0b10000000,
  0b10000000
};

const uint8_t flag_2[] = {
  0b00000000,
  0b10000000,
  0b11100000,
  0b11111100,
  0b11110000,
  0b10000000,
  0b10000000,
  0b10000000
};

const uint8_t flag_1[] = {
  0b00000000,
  0b10000000,
  0b11100000,
  0b11110000,
  0b11110000,
  0b10010000,
  0b10000000,
  0b10000000
};

const uint8_t flag_0[] = {
  0b00000000,
  0b10000000,
  0b11000000,
  0b11000000,
  0b11000000,
  0b10000000,
  0b10000000,
  0b10000000
};


/* number of jumps left */
static int8_t tries;
static uint32_t score;

/* ring buffer of input, used for debouncing */
static uint8_t gamepad1;
static uint8_t gamepad1_last[GAMEPAD_LAST_SIZE];
static int gamepad1_next;

/* mouse state, for rand seed */
static uint16_t last_mouse_x;
static uint16_t last_mouse_y;

static uint32_t seed_z;
static uint32_t seed_w;


/* which part of the game are we in? */
static enum game_state state;

static uint64_t ticks;
static uint64_t state_enter_at; /* when did we enter this state? */

/* how much wind are we talking about? */
static double wind_dx;

/* plane position, target, step */
static double plane_x;
static double plane_dx;
static double plane_end;

/* diver position, target, step */
static double diver_x;
static double diver_y;
static double diver_dx;
static double diver_dy;
static int diver_target_left;
static int diver_target_right;
static int diver_open;
static int diver_open_at;

void
start()
{
  ticks = 0;
  seed_w = (uint32_t)(*MOUSE_X == 0 ? 1: *MOUSE_X);
  seed_z = (uint32_t)(*MOUSE_Y == 0 ? 1: *MOUSE_Y);

  reset();
}

void
reset()
{
  gamepad1 = 0;
  for (int i = 0; i < GAMEPAD_LAST_SIZE; i++) {
    gamepad1_last[i] = 0;
  }
  gamepad1_next = 0;

  /* reset game state */
  tries = 3;
  score = 0;
  wind_dx = 0;
  plane_x = 0;
  plane_end = 0;
  diver_x = 0;
  diver_y = 0;
  diver_dx = 0;
  diver_dy = 0;
  diver_target_left = 0;
  diver_target_right = 0;
  diver_open = 0;
  diver_open_at = 0;

  transition(TITLE_SCREEN);
}

void
update_input(uint8_t new_gamepad)
{

  gamepad1_last[gamepad1_next++] = new_gamepad;
  gamepad1_next %= GAMEPAD_LAST_SIZE;
  gamepad1 = gamepad1_last[0];
  for (int i = 1; i < GAMEPAD_LAST_SIZE; i++) {
    gamepad1 &= gamepad1_last[i];
  }

  if (new_gamepad) {
    uint32_t tmp = seed_z ^ ((uint32_t)new_gamepad << (seed_z % 8));
    if (tmp != 0) {
      seed_z = tmp;
    }
  }

  uint16_t mouse_x = (uint16_t) *MOUSE_X;
  uint16_t mouse_y = (uint16_t) *MOUSE_Y;

  if (last_mouse_x != mouse_x) {
    uint32_t rot = (mouse_x ^ ticks) % 12;
    uint32_t tmp = ((seed_w << rot) | mouse_x);
    if (tmp != 0) {
      seed_w = (uint32_t) tmp;
    }
  }

  if (last_mouse_y != mouse_y) {
    uint32_t rot = (mouse_y ^ ticks) % 12;
    uint32_t tmp = ((seed_z << rot) | mouse_y);
    if (tmp != 0) {
      seed_z = (uint32_t) tmp;
    }
  }

  last_mouse_x = mouse_x;
  last_mouse_y = mouse_y;
}

int
pressed(uint8_t buttons)
{
  return gamepad1 & buttons;
}

/* X key is BUTTON1
 * Z key is BUTTON2
 */

void
update_plane()
{
  plane_x += plane_dx;
}

void
update()
{
  ticks++;
  update_input(*GAMEPAD1);
  draw_background();

  if (score >= WIN_JUMPS) {
    transition(YOU_WIN);
  }

  switch (state) {
  case TITLE_SCREEN:
    title_screen();
    break;
  case STARTING:
    starting();
    break;
  case READY_JUMP:
    ready_jump();
    break;
  case NO_JUMP:
    no_jump();
    break;
  case FALLING:
    falling();
    break;
  case CRASHED:
  case TOO_FAST:
    crashed();
    break;
  case YOU_WIN:
    you_win();
    break;
  case GAME_OVER:
    game_over();
    break;
  }
  draw_overlay();
}

void
draw_background()
{
  *DRAW_COLORS = 0x10;
  rect(0, 0, 160, 160);

  *DRAW_COLORS = 0x02;
  rect(0, 140, 160, 20);

  draw_wind();
}

void
draw_wind() {
  double dx = wind_dx;
  int x = 0;
  uint8_t flags = BLIT_1BPP;
  if (wind_dx < 0) {
    flags |= BLIT_FLIP_X;
    dx *= -1.0;
    x -= 4;
  }

  *DRAW_COLORS = 0x40;
  if (dx > (FPS(WIND_DX_MAX)*.75)) {
    blit((uint8_t*)flag_3, 76+x, 137, 8, 8, flags);
  }
  else if (dx > (FPS(WIND_DX_MAX)*.5)) {
    blit((uint8_t*)flag_2, 76+x, 137, 8, 8, flags);
  }
  else if (dx > (FPS(WIND_DX_MAX)*.25)) {
    blit((uint8_t*)flag_1, 76+x, 137, 8, 8, flags);
  }
  else {
    blit((uint8_t*)flag_0, 76+x, 137, 8, 8, flags);
  }
}

void
draw_overlay()
{
  *DRAW_COLORS = 0x4;
  for (int i = 0; i < tries; i++) {
    text("X", 2+(i*10), 2);
  }

  for (int i = 0; i < (int)score; i++) {
    text("+", 148-(i*10), 2);
  }
}

void
draw_plane()
{
  *DRAW_COLORS = 0x44;
  rect((int)plane_x, 10, 2, 4);
  rect((int)plane_x+2, 12, 2, 2);
  rect((int)plane_x, 14, 20, 4);
}

void
draw_diver()
{
  *DRAW_COLORS = 0x40;
  if (state == CRASHED) {
    blit(diver_splat, (int)diver_x - 4, (int)diver_y, 8, 4, BLIT_1BPP);
  }
  else if (diver_open) {
    blit(diver_chute, (int)diver_x - 4, (int)diver_y - 4, 8, 8, BLIT_1BPP);
  }
  else {
    blit(diver_falling, (int)diver_x - 4, (int)diver_y, 8, 5, BLIT_1BPP);
  }
}

void
draw_target()
{
  *DRAW_COLORS = 0x30;
  oval(diver_target_left,
       141,
       (uint32_t)(diver_target_right - diver_target_left),
       5);
}

void
title_screen()
{
  *DRAW_COLORS = 0x4;
  text("INSERT COIN", 45, 70);
  if (pressed(BUTTON_1 | BUTTON_2)) {
    transition(STARTING);
  }
}

void
you_win()
{
  *DRAW_COLORS = 0x3;
  text("YOU DID IT!", 8, 30);
  text("YOU MADE 10 JUMPS!", 8, 40);
  text("GREAT JOB!!", 8, 50);

  if (pressed(BUTTON_1 | BUTTON_2)) {
    transition(STARTING);
  }
}

void
transition(enum game_state new)
{
  state = new;
  state_enter_at = ticks;
}

void
starting()
{
  *DRAW_COLORS = 0x4;
  if (state_enter_at + 240 < ticks) {
    plane_dx = FPS(PLANE_DX);
    plane_x = 0;
    plane_end = 180;

    diver_target_left = (int)rand(100);
    diver_target_right = diver_target_left + 32;

    wind_dx = FPS((int)rand(WIND_DX_MAX) - (WIND_DX_MAX/2));
    transition(READY_JUMP);
  }
  else if (state_enter_at + 180 < ticks) {
    text("GO!", 68, 70);
  }
  else if (state_enter_at + 120 < ticks) {
    text("1", 78, 70);
  }
  else if (state_enter_at + 60 < ticks) {
    text("2", 78, 70);
  }
  else if (state_enter_at < ticks) {
    text("3", 78, 70);
  }
}

void
ready_jump()
{
  if (plane_x >= plane_end) {
    transition(NO_JUMP);
    return;
  }

  draw_plane();
  draw_target();

  if (pressed(BUTTON_1 | BUTTON_2)) {
    /* jumped! */
    diver_x = plane_x + 10;
    diver_y = DIVER_Y_START;
    diver_dx = plane_dx;
    diver_dy = FPS(DIVER_DY_START);
    diver_open = 0;
    diver_open_at = 0;
    transition(FALLING);
  }

  update_plane();
}

void
no_jump()
{
  if (state_enter_at + 120 < ticks) {
    transition(STARTING);
  }

  *DRAW_COLORS = 0x4;
  text("NO JUMP?", 45, 70);
}

void
crashed()
{
  if (state_enter_at + 120 < ticks) {
    if (tries > 0) {
      transition(STARTING);
      return;
    }
    transition(GAME_OVER);
    return;
  }

  draw_diver();

  *DRAW_COLORS = 0x4;
  if (state == TOO_FAST) {
    text("TOO FAST!!!", 45, 60);
  }
  text("OUCH!!!", 45, 70);
}

void
falling()
{
  draw_plane();
  draw_target();

  if (diver_y > 140) { /* within the ground */

    if (diver_dy > (FPS(DIVER_DY_MAX) * .5)) {
      tries--;
      transition(TOO_FAST);
      return;
    }

    if (diver_open &&
        (diver_x >= diver_target_left) &&
        (diver_x <= diver_target_right)) {
      score++;
      transition(STARTING);
      return;
    }
    else if (!diver_open) {
      tries--;
      transition(CRASHED);
      return;
    }
    else if (tries > 0) {
      tries--;
      transition(STARTING);
      return;
    }
    else {
      transition(GAME_OVER);
    }
  }

  if (pressed(BUTTON_LEFT)) {
    diver_x -= diver_dx;
  }
  else if (pressed(BUTTON_RIGHT)) {
    diver_x += diver_dx;
  }
  else if (pressed(BUTTON_UP)) {
    /* used to calculate points for dive */
    if (!diver_open){
      diver_open_at = (int)diver_y;
      diver_open = 1;
      /* Immediate decel for chute */
      diver_dy -= FPS(DIVER_DY_CHUTE_FACTOR);
    }
  }

  /* apply the wind: refactor if chute is open */
  diver_x += (wind_dx * (diver_open ? DIVER_DY_WIND_CHUTE: 1));

  /* diver accelerates until terminal velocity */
  if (diver_open) {
    diver_dy -= FPS(DIVER_DY_CHUTE_DECEL);
    if (diver_dy < FPS(DIVER_DY_CHUTE_MIN)) {
      diver_dy = FPS(DIVER_DY_CHUTE_MIN);
    }
  }
  else {
    diver_dy += FPS(DIVER_DY_ACCEL);
    if (diver_dy > FPS(DIVER_DY_MAX)) {
      diver_dy = FPS(DIVER_DY_MAX);
    }
  }

  diver_y += diver_dy;

  draw_diver();
  update_plane();
}

void
game_over()
{
  *DRAW_COLORS = 0x40;
  text("GAME OVER", 45, 70);
  if (pressed(BUTTON_1 | BUTTON_2)) {
    transition(TITLE_SCREEN);
  }
}


void
itoa(int num, char *buffer, size_t len)
{
  if (num == 0 && len >= 2) {
    buffer[0] = '0';
    buffer[1] = '\0';
    return;
  }
  int sign = num < 0;
  int idx = (sign) ? 1: 0;
  if (sign) {
    num *= -1;
    buffer[0] = '-';
  }


  while ((size_t)idx < (len-1) && num > 0) {
    buffer[idx++] = '0' + (num % 10);
    num /= 10;
  }

  buffer[idx--] = '\0';

  int i = sign;
  while (i < idx) {
    char tmp = buffer[idx];
    buffer[idx] = buffer[i];
    buffer[i] = tmp;
    i++;
    idx--;
  }
}


/* George Marsaglia's Uniform Random number generator
 * Adapted from Cook's implementation:
 * https://www.codeproject.com/Articles/25172/Simple-Random-Number-Generation
 */
static uint32_t
_rand()
{
  seed_z = 36969 * (seed_z & 65535) + (seed_z >> 16);
  seed_w = 18000 * (seed_w & 65535) + (seed_w >> 16);
  return (seed_z << 16) + seed_w;
}

uint32_t
rand(uint32_t n)
{
  uint64_t x = _rand();
  double r = (double)x / (double)4294967296;
  return (uint32_t) (r * (double)n);
}
