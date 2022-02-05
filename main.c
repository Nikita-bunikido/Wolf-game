#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include <windows.h>
#include <conio.h>
#include <stdarg.h>

#define get_texture_char(obj,x,y,c,T)\
*(T*)((obj)->tex[c].buffer+((x)+(y)*(obj)->tex[c].sx))
#define swap(a,b,T)\
do{\
    T tmp = *a;\
    *a = *b;\
    *b = tmp;\
} while(0);
#define SCORE_STR   "SCORE:"
#define GAME_OVER_STR   "GAME OVER!"

#define SMASHED_EGG "(\\."

#define RES_H   15
#define RES_W   20

#define EGGS_SPEED    10
#define BASKET_CHAR   '@'

typedef struct {
    uint8_t* buffer;
    size_t buffer_size;
    uint16_t sx, sy;
} texture_t;

typedef struct {
    texture_t* tex;
    size_t tex_num;
    uint16_t posx, posy;
} object_t;

typedef enum {
    ET_EGG,
    ET_AIR,
    ET_TRAY,
    ET_BASKET
} egg_type;

typedef struct {
    egg_type type;
    uint8_t tex[2];
    uint8_t ct;
    uint8_t life_time;
    bool updated;
} egg_t;

#define LIT_AIR (egg_t){.type = ET_AIR, .tex = {' ', ' '}, .ct = 0, .life_time = 0, .updated = true };


typedef char Tscreen[RES_H][RES_W+1];
egg_t egg_screen[RES_H*RES_W];
static uint64_t game_score;
static uint64_t game_smashed;

void error (const char* msg, FILE* stream){
    fprintf(stream, "error: %s\n", msg);
}

void object_move (uint16_t px, uint16_t py, object_t* obj){
    obj->posx = px;
    obj->posy = py;
}

void object_draw (object_t* obj, size_t ctex, Tscreen* screen){
    assert(screen);
    assert(obj);
    assert(obj->tex_num > ctex);

    for (uint16_t py = obj->posy; py < obj->posy + obj->tex[ctex].sy; py++)
    for (uint16_t px = obj->posx; px < obj->posx + obj->tex[ctex].sx; px++)
        (*screen)[py][px] = get_texture_char(obj, px-obj->posx, py-obj->posy, ctex, char);
}

void screen_draw (Tscreen* screen){
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), (COORD){0,0});

    for (uint16_t py = 0; py < RES_H; py++){
        puts((*screen)[py]);
    }
}

static void delete_chars (uint8_t ch, const uint8_t* buf, size_t* bufsz){

    uint8_t* buffer = malloc(*bufsz);
    const uint8_t* buf_begin = buf;
    size_t buffer_idx = 0;

    while (*buf){
        buffer[buffer_idx] = *buf;
        if (*buf++ != ch) buffer_idx++;
    }
    *bufsz = buffer_idx;
    memcpy((void*)buf_begin, (void*)buffer, buffer_idx);
    free (buffer);
    return;
}

texture_t load_texture (FILE* stream){
    texture_t t = {0};
    fscanf(stream, "%hd %hd", &(t.sx), &(t.sy));
    (void)fgetc(stream);

    t.buffer_size = t.sx * t.sy + t.sy + 1;
    t.buffer = malloc(t.buffer_size);
    fread(t.buffer, t.buffer_size-1, 1, stream);
    t.buffer[t.buffer_size-1] = '\0';

    delete_chars ('\n', t.buffer, &(t.buffer_size));
    return t;
}

void texture_flip (texture_t* src){
    #define REPLACEMENTS    5
    const char* replace[REPLACEMENTS] = {
        "[]", "()", "<>", "/\\", "{}"
    };

    uint8_t* buf = malloc(src->buffer_size);
    for (uint16_t y = 0; y < src->sy; y++)
    for (int16_t xf = src->sx-1, x = 0; xf >= 0; xf--, x++){
        char *c = (char*)&(buf[xf+y*src->sx]);
        *c = src->buffer[x+y*src->sx];

        for (uint8_t k = 0; k < REPLACEMENTS; k++){
            if (strchr(replace[k], *c)!=NULL){
                static uint8_t pos;
                pos = strchr(replace[k], *c)-replace[k];
                *c = replace[k][!pos];
            }
        }
    }
    memcpy((void*)src->buffer, (void*)buf, src->buffer_size);
    free (buf);
}

void object_destroy(object_t** obj){
    if (obj == NULL)
        return;
    assert((*obj));
    if ((*obj)->tex_num > 0){
        for (size_t i = 0; i < (*obj)->tex_num; i++){
            if ((*obj)->tex[i].buffer) free((*obj)->tex[i].buffer);
        }
        free((*obj)->tex);
    }
}

object_t* object_create (bool ispath, uint16_t px, uint16_t py, uint32_t parametrs, ...){
    assert (parametrs > 0);

    object_t* obj = (object_t*)malloc(sizeof(object_t));
    if (obj == NULL){
        error("can\'t malloc object.", stderr);
        goto error;
    }
    obj->posx = px;
    obj->posy = py;
    obj->tex_num = parametrs;

    obj->tex = malloc(sizeof(texture_t) * parametrs);
    if (obj->tex == NULL){
        error("can\'t malloc object textures.", stderr);
        goto error;
    }
    va_list va_ptr;
    va_start(va_ptr, parametrs);

    if (ispath){
        for (size_t i = 0; i < parametrs; i++){
            const char* current_path = va_arg(va_ptr, const char*);
            FILE* cs = fopen(current_path, "r");
            if (cs == NULL){
                error("can\'t open texture file.", stderr);
                goto error;
            }
            obj->tex[i] = load_texture(cs);
        }
    } else {
        for (size_t i = 0; i < parametrs; i++){
            const char* current_buffer = va_arg(va_ptr, const char*);
            obj->tex[i].buffer_size = strlen(current_buffer);
            obj->tex[i].buffer = malloc(strlen(current_buffer));
            memcpy(obj->tex[i].buffer, current_buffer, obj->tex[i].buffer_size);
            obj->tex[i].sx = obj->tex[i].sy = 1;
        }
    }
    va_end(va_ptr);

    return obj;
    error:
        object_destroy(&obj);
        return NULL;
}

void log_buffer (const char* buf, size_t buf_size, FILE* sink){
    assert(buf_size > 0);

    static int cb = 0;
    fprintf(sink, "buffer[%d] - \'", cb);
    fwrite(buf, buf_size, 1, sink);
    fprintf(sink, "\'\n");
    cb++;
}

void draw_object_to_eggs (object_t* object, size_t ct){
    for (uint16_t py = object->posy; py < object->posy + object->tex[ct].sy; py++)
    for (uint16_t px = object->posx; px < object->posx + object->tex[ct].sx; px++){
        uint8_t current_byte = get_texture_char(object, px-object->posx, py-object->posy, 0, uint8_t);
        egg_screen[px+py*RES_W] = (current_byte != ' ') ? (egg_t){
            .type = ET_TRAY,
            .tex = {219, 219},
            .ct = 0,
            .life_time = 0
        } : LIT_AIR;
    }
}

void init_egg_screen(){
    for (uint32_t i = 0; i < RES_W*RES_H; i++)
        egg_screen[i] = LIT_AIR;
}

egg_t* get_egg (uint16_t px, uint16_t py){
    return &egg_screen[px+py*RES_W];
}

void update_eggs (uint32_t *eggs_spawn_speed){
    for (uint16_t py = RES_H-1; py > 0; py--)
        for (uint16_t px = 0; px < RES_W-1; px++){
            egg_t* cur = get_egg(px, py); //current cell
            if (cur->updated) continue;   //if it was already updated
            cur->updated = true; //updated
            cur->ct = !(cur->ct); //changing texture

            uint16_t side = px < RES_W/2 ? 1 : -1; //movement side

            if (cur->type == ET_EGG){
                cur->life_time++;
                if (py == RES_H-1 || cur->life_time > 7){
                    game_smashed++;
                    egg_screen[px+py*RES_W] = LIT_AIR;
                    for (uint32_t i = 0; i < RES_W*RES_H; i++)
                        if (egg_screen[i].type == ET_EGG)
                            egg_screen[i] = LIT_AIR;
                    Sleep(1000);
                    *eggs_spawn_speed = 50;
                    return;
                }
                /* check near basket */
                for (int8_t x = 0; x < 3; x++)
                for (int8_t y = 0; y < 3; y++){
                    //real coordinates
                    uint16_t rx = px - 1 + x;
                    uint16_t ry = py - 1 + y;
                    uint16_t ridx = rx+ry*RES_W;
                    //out of range
                    if (ridx < 0 || ridx >= RES_W*RES_H)
                        continue;
                    //near basket - clearing egg, increment score
                    if (egg_screen[ridx].type == ET_BASKET){
                        game_score++;
                        egg_screen[px+py*RES_W] = LIT_AIR;
                        continue;
                    }  
                }
                egg_t* move_down = get_egg(px, py+1), *move_side = get_egg(px+side, py);
                if (move_down->type == ET_AIR){
                    swap (cur, move_down, egg_t);
                    continue;
                } else if (move_side->type == ET_AIR){
                    swap (cur, move_side, egg_t);
                    continue;
                }
            }
        }
}

void put_egg (uint16_t px, uint16_t py, const char* states){
    egg_screen[px+py*RES_W] = (egg_t){
        .type = ET_EGG,
        .tex  = {(uint8_t)states[0], (uint8_t)states[1]},
        .life_time = 0,
        .ct   = 0
    };
}

void draw_eggs (Tscreen* screen){
    for (uint16_t py = 0; py < RES_H; py++)
    for (uint16_t px = 0; px < RES_W-1; px++)
    if (egg_screen[px+py*RES_W].type == ET_EGG)
        (*screen)[py][px] = (char)(egg_screen[px+py*RES_W].tex[egg_screen[px+py*RES_W].ct]);
}

void screen_clear (Tscreen* screen){
    memset(*screen, ' ', sizeof(*screen));
    for (uint16_t py = 0; py < RES_H; py++)
        (*screen)[py][RES_W] = '\0';
}

void find_basket (object_t* wolf, int wolf_side, int wolf_state, uint16_t* rx, uint16_t* ry){
    for (uint16_t py = 0; py < wolf->tex[wolf_state].sy; py++)
    for (uint16_t px = 0; px < wolf->tex[wolf_state].sx; px++){
        char cc = get_texture_char(wolf, px, py, wolf_state, char);
        if (cc == BASKET_CHAR){
            *rx = px + wolf->posx;
            *ry = py + wolf->posy;
            return;
        }
    }
    *rx = (uint16_t)0;
    *ry = (uint16_t)0;
    return;
}

void draw_text (Tscreen* screen, uint16_t px, uint16_t py, const char* text){
    uint16_t offset = 0;
    while (*text){
        (*screen)[py][(px+offset++)] = *text++;
    }
}

int main(int argc, char** argv){
    system("cls");
    uint64_t game_time = 0x0ULL;
    Tscreen screen;
    bool game = true;
    bool game_over = false;
    uint16_t basketx = 0, baskety = 0;

    memset(screen, ' ', sizeof(screen));
    init_egg_screen();

    for (uint16_t py = 0; py < RES_H; py++)
        screen[py][RES_W] = '\0';

    /* eggs spawn speed */
    uint32_t EGGS_SPAWN_SPEED = 50;

    /* game score as string */
    char str_score[255];

    /* two egg states */
    const char* egg_states = {
        "Oo"
    };
    /* wolf paths */
    const char* wolf_load_paths[] = {
        "source\\wolfd.txt",
        "source\\wolfu.txt"
    };
    /* trays paths */
    const char* tray_load_paths[] = {
        "source\\trayd.txt",
        "source\\trayu.txt"
    };
    /* trays drawing positions */
    const uint16_t tray_positions[4][2] = {
        {0, 2},
        {0, 7},
        {14, 2},
        {14, 7}
    };
    /* eggs spawners positions */
    const uint16_t eggs_emmiters[4][2] = {
        {tray_positions[0][0]+2,  tray_positions[0][1]+2},
        {tray_positions[1][0]+2,  tray_positions[1][1]+2},
        {tray_positions[2][0]+3,tray_positions[2][1]+2},
        {tray_positions[3][0]+3,tray_positions[3][1]+2}
    };

    object_t* wolf = object_create(true, 0, 0, 2, wolf_load_paths[0], wolf_load_paths[1]);
    object_t* ground = object_create(true, 0, 12, 1, "source\\ground.txt");
    object_t* trays[4];

    for (uint16_t i = 0; i < 4; i++){
        trays[i] = object_create(true, tray_positions[i][0], tray_positions[i][1], 1, tray_load_paths[!(i%2)]);
        if (i >= 2) texture_flip(&(trays[i]->tex[0]));
    }
    object_move (RES_W/2 - wolf->tex[0].sx/2, 5, wolf);

    enum {
        WOLF_DOWN,
        WOLF_UP
    } wolf_state = WOLF_DOWN;
    enum {
        WOLF_RIGHT,
        WOLF_LEFT
    } wolf_side = WOLF_LEFT;

    for (uint16_t i = 0; i < 4; i++)
        draw_object_to_eggs(trays[i], 0);

    
    restart:
    do {
        screen_clear (&screen);
        /* basket to eggs draw */

        egg_screen[basketx+baskety*RES_W] = (egg_t){ //setting previous basket position to air
            .type = ET_AIR,
            .tex  = {' ', ' '},
            .life_time = 0,
            .ct   = 0,
            .updated = false
        };
        //finding new basket position
        find_basket (wolf, wolf_side, wolf_state, &basketx, &baskety);
        //setting basket to egg screen
        egg_screen[basketx+baskety*RES_W] = (egg_t){
            .type = ET_BASKET,
            .tex  = {' ', ' '},
            .life_time = 0,
            .ct   = 0,
            .updated = false
        };

        /* trays drawing */
        for (uint16_t i = 0; i < 4; i++){
            object_draw(trays[i], 0, &screen);
        }
        /* wolf and ground */
        object_draw(ground, 0, &screen);
        object_draw(wolf, wolf_state, &screen);
        /* eggs drawing */
        draw_eggs(&screen);

        /* drawing score */
        ltoa(game_score, str_score, 10);
        draw_text(&screen, RES_W-1-strlen(str_score)-strlen(SCORE_STR), 0, SCORE_STR);
        draw_text(&screen, RES_W-1-strlen(str_score), 0, str_score);

        /* drawing smashed */
        for (uint8_t i = 0; i < game_smashed; i++)
            draw_text(&screen, (strlen(SMASHED_EGG)+1)*i+1,0, SMASHED_EGG);
        
        /* game over string draw */
        if (game_over) draw_text(&screen, RES_W/2 - strlen(GAME_OVER_STR)/2, RES_H/2, GAME_OVER_STR);
        
        screen_draw(&screen);

        if (game_over){
            printf("Do you want to play again? [Y/N]\n> ");
            char c;
            scanf("\n%c", &c);
            if (c == 'N') break;
            
            system("cls");
            screen_clear(&screen);
            EGGS_SPAWN_SPEED = 50;
            for (uint16_t i = 0; i < RES_W*RES_H; i++)
                egg_screen[i] = LIT_AIR;
            if (wolf_side == WOLF_RIGHT){
                texture_flip(&(wolf->tex[0]));
                texture_flip(&(wolf->tex[1]));
            }

            wolf_side = WOLF_LEFT;
            wolf_state = WOLF_DOWN;
            for (uint16_t i = 0; i < 4; i++)
                draw_object_to_eggs(trays[i], 0);
            game_over = false;
            game_smashed = 0;
            game_score = 0;
            goto restart;
        }

        int old_wolf_side = wolf_side;
        if (GetKeyState(VK_DOWN)<0) wolf_state = WOLF_DOWN;
        if (GetKeyState(VK_UP)<0) wolf_state = WOLF_UP;
        if (GetKeyState(VK_LEFT)<0) wolf_side = WOLF_LEFT;
        if (GetKeyState(VK_RIGHT)<0) wolf_side = WOLF_RIGHT;
        if (old_wolf_side != wolf_side){
            texture_flip(&(wolf->tex[0]));
            texture_flip(&(wolf->tex[1]));
        }

        /* eggs generate */
        if (game_time % EGGS_SPAWN_SPEED == 0){
            uint8_t tray = rand()%4;
            put_egg(eggs_emmiters[tray][0], eggs_emmiters[tray][1], egg_states);
        }
        
        /* eggs update */
        if (game_time % EGGS_SPEED == 0)
        for (uint64_t k = 0; k < RES_W*RES_H; k++) egg_screen[k].updated = false;
        update_eggs(&EGGS_SPAWN_SPEED);

        /* eggs spawn boost */
        if (game_time % 100 == 0 && EGGS_SPAWN_SPEED != 0)
            EGGS_SPAWN_SPEED--;

        /* game over check */
        if (game_smashed >= 3){
            game_over = true;
        }
        
        if (GetKeyState('Q')<0) break;
        game_time++;
        Sleep(20);
    } while (game);

    /* objects destroying */
    object_destroy(&wolf);
    object_destroy(&ground);

    /* trays destroying */
    for (uint16_t i = 0; i < 4; i++)
        object_destroy (trays+i);
    return 0;
}