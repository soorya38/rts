/*=============================================================
 * hud_overlays.c  –  Shared helpers, icons, alert, end/menu screens
 *=============================================================*/
#include "game.h"
#include "ui_state.h"
#include "hud_common.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "net.h"

/* ── Scaled icon helpers ─────────────────────────────────── */
void draw_food_icon(UIState *ui, int x,int y){
    if (ui->tex_ui_food.id != 0) {
        float sc = (int)(24 * hud_scale()) / (float)ui->tex_ui_food.width;
        DrawTextureEx(ui->tex_ui_food, (Vector2){(float)x, (float)y - 4}, 0.0f, sc, WHITE);
    } else {
        int s = (int)(8 * hud_scale());
        DrawCircle(x+s,y+s,s,CLITERAL(Color){50,170,40,255});
        DrawCircle(x+s,y+s-1,(int)(s*0.55f),CLITERAL(Color){100,210,70,255});
    }
}
void draw_wood_icon(UIState *ui, int x,int y){
    if (ui->tex_ui_wood.id != 0) {
        float sc = (int)(24 * hud_scale()) / (float)ui->tex_ui_wood.width;
        DrawTextureEx(ui->tex_ui_wood, (Vector2){(float)x, (float)y - 4}, 0.0f, sc, WHITE);
    } else {
        int s = (int)(8 * hud_scale());
        DrawRectangle(x+s/2,y+s/3,s/2,(int)(s*1.3f),CLITERAL(Color){120,75,25,255});
        DrawRectangle(x+s/4,y+s/2,s,s/3,CLITERAL(Color){150,100,40,255});
    }
}
void draw_gold_icon(UIState *ui, int x,int y){
    if (ui->tex_ui_gold.id != 0) {
        float sc = (int)(24 * hud_scale()) / (float)ui->tex_ui_gold.width;
        DrawTextureEx(ui->tex_ui_gold, (Vector2){(float)x, (float)y - 4}, 0.0f, sc, WHITE);
    } else {
        int s = (int)(8 * hud_scale());
        DrawCircle(x+s,y+s,s,CLITERAL(Color){210,170,20,255});
        DrawCircle(x+s,y+s-1,(int)(s*0.55f),CLITERAL(Color){240,210,60,255});
    }
}
void draw_stone_icon(UIState *ui, int x,int y){
    if (ui->tex_ui_stone.id != 0) {
        float sc = (int)(24 * hud_scale()) / (float)ui->tex_ui_stone.width;
        DrawTextureEx(ui->tex_ui_stone, (Vector2){(float)x, (float)y - 4}, 0.0f, sc, WHITE);
    } else {
        int s = (int)(8 * hud_scale());
        DrawCircle(x+s,y+s+1,s,CLITERAL(Color){155,148,138,255});
        DrawCircle(x+s-1,y+s-1,(int)(s*0.55f),CLITERAL(Color){195,188,178,255});
    }
}

bool draw_button(const char *label, int x, int y, int w, int h, bool enabled){
    float sc = hud_scale();
    Vector2 mp = GetMousePosition();
    /* Also check touch position for hit-testing */
    bool hover = enabled && mp.x>=x && mp.x<=x+w && mp.y>=y && mp.y<=y+h;
    if (!hover && GetTouchPointCount() > 0) {
        Vector2 tp = GetTouchPosition(0);
        hover = enabled && tp.x>=x && tp.x<=x+w && tp.y>=y && tp.y<=y+h;
    }
    Color bg = hover ? C_BTN_HOV : C_BTN_NORM;
    if(!enabled) bg=CLITERAL(Color){30,25,15,200};
    DrawRectangle(x,y,w,h,bg);
    DrawRectangleLinesEx((Rectangle){(float)x,(float)y,(float)w,(float)h},1.5f,C_BTN_BORD);
    Color tc = enabled ? C_HUD_TXT : CLITERAL(Color){100,90,60,255};
    int fs=(int)(12*sc);
    const char *nl = strchr(label, '\n');
    if (nl) {
        int len1 = nl - label;
        char line1[64];
        if ((size_t)len1 >= sizeof(line1)) len1 = (int)sizeof(line1) - 1;
        strncpy(line1, label, len1);
        line1[len1] = '\0';
        const char *line2 = nl + 1;
        
        int tw1 = MeasureText(line1, fs);
        int tw2 = MeasureText(line2, fs);
        int total_h = fs * 2 + 2; 
        int start_y = y + (h - total_h) / 2;
        
        DrawText(line1, x + (w - tw1)/2, start_y, fs, tc);
        DrawText(line2, x + (w - tw2)/2, start_y + fs + 2, fs, tc);
    } else {
        int tw=MeasureText(label,fs);
        DrawText(label,x+(w-tw)/2,y+(h-fs)/2,fs,tc);
    }
    return hover && (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) || IsGestureDetected(GESTURE_TAP));
}

void draw_tooltip(const char *text, int x, int y) {
    int fs = (int)(11 * hud_scale()), tw = MeasureText(text, fs), th = fs + 6;
    DrawRectangle(x, y, tw + 10, th, CLITERAL(Color){20, 18, 12, 230});
    DrawRectangleLines(x, y, tw + 10, th, C_HUD_LINE);
    DrawText(text, x + 5, y + 3, fs, C_HUD_TXT);
}

void draw_alert(GameState *gs, UIState *ui){
    (void)ui;
    if(gs->alert_timer<=0) return;
    float sc = hud_scale();
    float alpha=clampf(gs->alert_timer/1.5f,0,1)*255;
    int afs = (int)(22*sc);
    int tw=MeasureText(gs->alert,afs);
    int bw=tw+40, bh=(int)(36*sc);
    int bx=GetScreenWidth()/2-bw/2, by=HUD_TOP_H+8;
    DrawRectangleRounded((Rectangle){(float)bx,(float)by,(float)bw,(float)bh},0.3f,8,
                         CLITERAL(Color){25,20,10,(unsigned char)(alpha*0.9f)});
    DrawText(gs->alert,bx+(bw-tw)/2,by+(bh-afs)/2,afs,
             CLITERAL(Color){230,200,100,(unsigned char)alpha});
}

void draw_end_screen(GameState *gs, UIState *ui){
    (void)ui;
    if(gs->phase!=PHASE_VICTORY&&gs->phase!=PHASE_DEFEAT) return;
    float sc = hud_scale();
    DrawRectangle(0,0,GetScreenWidth(),GetScreenHeight(),CLITERAL(Color){0,0,0,180});
    bool win=(gs->phase==PHASE_VICTORY);
    const char *msg=win?"VICTORY!":"DEFEATED";
    Color mc=win?CLITERAL(Color){220,200,50,255}:CLITERAL(Color){210,50,40,255};
    int fs=(int)(52*sc), tw=MeasureText(msg,fs);
    DrawText(msg,GetScreenWidth()/2-tw/2,GetScreenHeight()/2-60,fs,mc);
    const char *sub=win?"The enemy town center has fallen!":"Your town center has been destroyed!";
    int sfs=(int)(18*sc), stw=MeasureText(sub,sfs);
    DrawText(sub,GetScreenWidth()/2-stw/2,GetScreenHeight()/2+10,sfs,CLITERAL(Color){200,185,150,255});
    const char *hint="Press [R] to restart or [Q] to quit";
    int hfs=(int)(14*sc);
    DrawText(hint,GetScreenWidth()/2-MeasureText(hint,hfs)/2,
             GetScreenHeight()/2+50,hfs,CLITERAL(Color){150,135,100,255});
}

void draw_placement_bar(GameState *gs, UIState *ui){
    (void)ui;
    if(!gs->build_mode.active) return;
    float sc = hud_scale();
    char buf[80];
    snprintf(buf,sizeof(buf),
             "  Placing: %s   ·   Tap map to place   ·   [ESC] to cancel  ",
             building_name(gs->build_mode.type));
    int fs=(int)(12*sc);
    int tw=MeasureText(buf,fs);
    int bh=(int)(20*sc);
    Color bg = gs->build_mode.valid ?
        CLITERAL(Color){20,60,20,230} : CLITERAL(Color){60,20,20,230};
    DrawRectangle(GetScreenWidth()/2-tw/2-8,HUD_TOP_H+2,tw+16,bh,bg);
    DrawRectangleLinesEx((Rectangle){(float)(GetScreenWidth()/2-tw/2-8),(float)(HUD_TOP_H+2),(float)(tw+16),(float)bh},
                         1,gs->build_mode.valid?CLITERAL(Color){60,180,60,200}:CLITERAL(Color){180,60,60,200});
    DrawText(buf,GetScreenWidth()/2-tw/2,HUD_TOP_H+5,fs,
             gs->build_mode.valid?CLITERAL(Color){180,240,180,255}:CLITERAL(Color){240,160,160,255});
}


/* ─── Local-IP helper (non-Windows) ──────────────────────── */
#if !defined(_WIN32) && !defined(WIN32)
#  include <ifaddrs.h>
#  include <arpa/inet.h>
static void get_local_ip(char *out, int size) {
    out[0] = '\0';
#  if defined(__ANDROID__) && __ANDROID_API__ < 24
    strncpy(out, "Android < 7.0 (N/A)", size-1);
#  else
    struct ifaddrs *ifap = NULL;
    if (getifaddrs(&ifap) != 0) return;
    for (struct ifaddrs *ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        const char *nm = ifa->ifa_name ? ifa->ifa_name : "";
        if (nm[0]=='l' && nm[1]=='o') continue; /* skip loopback */
        struct sockaddr_in *s = (struct sockaddr_in *)ifa->ifa_addr;
        inet_ntop(AF_INET, &s->sin_addr, out, size);
        break;
    }
    freeifaddrs(ifap);
#  endif
    if (out[0] == '\0') strncpy(out, "N/A", size-1);
}
#else
static void get_local_ip(char *out, int size) {
    strncpy(out, "run ipconfig", size-1); out[size-1]='\0';
}
#endif

/* ─── IP text-box (draw + handle input) ─────────────────── */
static void draw_ip_box(UIState *ui, int x, int y, int w, int h) {
    float sc = hud_scale();
    Vector2 mp = GetMousePosition();
    bool hover = mp.x>=x && mp.x<=x+w && mp.y>=y && mp.y<=y+h;
    if (!hover && GetTouchPointCount() > 0) {
        Vector2 tp = GetTouchPosition(0);
        hover = tp.x>=x && tp.x<=x+w && tp.y>=y && tp.y<=y+h;
    }

    /* Click/tap to focus */
    bool click = hover && (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) ||
                           IsGestureDetected(GESTURE_TAP));
    if (click) ui->net_ip_active = true;
    /* Click outside → unfocus */
    if ((IsMouseButtonPressed(MOUSE_LEFT_BUTTON) ||
         IsGestureDetected(GESTURE_TAP)) && !hover)
        ui->net_ip_active = false;

    /* Handle keyboard input when focused */
    if (ui->net_ip_active) {
        int ch;
        while ((ch = GetCharPressed()) > 0) {
            int len = (int)strlen(ui->net_ip);
            /* Allow digits and dots only (IPv4) */
            if ((ch >= '0' && ch <= '9') || ch == '.') {
                if (len < 63) {
                    ui->net_ip[len]   = (char)ch;
                    ui->net_ip[len+1] = '\0';
                }
            }
        }
        if (IsKeyPressed(KEY_BACKSPACE)) {
            int len = (int)strlen(ui->net_ip);
            if (len > 0) ui->net_ip[len-1] = '\0';
        }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER))
            ui->net_ip_active = false;
    }

    /* Draw box */
    Color bg  = ui->net_ip_active ? CLITERAL(Color){40,32,16,255}
                                   : CLITERAL(Color){22,17,10,220};
    Color brd = ui->net_ip_active ? CLITERAL(Color){220,170,60,255} : C_BTN_BORD;
    DrawRectangle(x, y, w, h, bg);
    DrawRectangleLinesEx((Rectangle){(float)x,(float)y,(float)w,(float)h}, 1.5f, brd);
    int fs = (int)(15*sc);
    const char *display = ui->net_ip[0] ? ui->net_ip : "tap to type IP";
    Color tc = ui->net_ip[0] ? C_HUD_TXT : CLITERAL(Color){100,90,60,220};
    int tw = MeasureText(display, fs);
    DrawText(display, x+8, y+(h-fs)/2, fs, tc);
    /* Blinking cursor */
    if (ui->net_ip_active && ((int)(GetTime()*2) % 2 == 0))
        DrawRectangle(x+8+tw+2, y+(h-fs)/2, 2, fs, C_HUD_TXT);
}

void draw_menu(GameState *gs, UIState *ui){
    float sc = hud_scale();
    int sw = GetScreenWidth(), sh = GetScreenHeight();

    /* ── Background ── */
    DrawRectangleGradientV(0,0,sw,sh,
        CLITERAL(Color){8,12,22,255},CLITERAL(Color){18,28,48,255});
    for(int i=0;i<60;i++){
        int sx=(i*137)%sw, sy=(i*197)%sh;
        int bs=(i%3==0)?2:1;
        DrawRectangle(sx,sy,bs,bs,CLITERAL(Color){255,255,255,(unsigned char)(100+i*3)});
    }

    /* ── Title ── */
    const char *t1="AGE OF EMPIRES II"; const char *t2="Raylib Edition";
    int f1=(int)(48*sc), f2=(int)(22*sc);
    DrawText(t1,sw/2-MeasureText(t1,f1)/2,sh/2-(int)(155*sc),f1,CLITERAL(Color){220,185,40,255});
    DrawText(t2,sw/2-MeasureText(t2,f2)/2,sh/2-(int)(100*sc),f2,CLITERAL(Color){170,155,110,255});
    DrawRectangle(sw/2-120,sh/2-(int)(72*sc),240,2,CLITERAL(Color){130,110,60,200});

    int lfs=(int)(12*sc);
    const char *lines[]={
        "Gather resources  \xc2\xb7  Build structures  \xc2\xb7  Train armies",
        "Destroy the enemy Town Center to win!",
        "Sandbox mode starts with every major system ready to test",
        "Controls:  WASD / edge scroll  |  Pinch: zoom  |  Drag: pan",
        "Tap to select  |  Tap (selected) = command  |  Tap [B] Build"
    };
    for(int i=0;i<5;i++)
        DrawText(lines[i],sw/2-MeasureText(lines[i],lfs)/2,
                 sh/2-(int)(52*sc)+i*(int)(16*sc),lfs,CLITERAL(Color){150,140,110,220});

    /* ── Buttons ── */
    int bw=(int)(260*sc), bh=(int)(46*sc), bx=sw/2-bw/2, by=sh/2+(int)(2*sc);

    /* Solo */
    if(draw_button("Start Solo Campaign", bx, by, bw, bh, true)){
        game_init_started_game(gs, (uint32_t)time(NULL), 2);
    }
    by += bh + (int)(8*sc);

    if(draw_button("Open Sandbox Test Grounds", bx, by, bw, bh, true)){
        game_init_sandbox(gs, (uint32_t)time(NULL));
    }
    by += bh + (int)(8*sc);

    if (!g_net_active) {
        /* ─── Host ─── */
        if(draw_button("Host LAN Game  (port 12345)", bx, by, bw, bh, true)){
            if(net_init() && net_host_create(12345)){
                game_set_alert(gs, "Hosting! Share your IP with the other player.");
            }
        }
        by += bh + (int)(8*sc);

        /* ─── Join section ─── */
        DrawText("Host IP address:", bx, by, lfs, C_HUD_TXT);
        by += (int)(18*sc);
        draw_ip_box(ui, bx, by, bw, bh);
        by += bh + (int)(6*sc);

#if defined(PLATFORM_ANDROID) || defined(ANDROID)
        DrawText("Tap the field above, then use your keyboard to type.",
                 bx, by, lfs, CLITERAL(Color){160,150,120,200});
        by += (int)(16*sc);
#endif

        if(draw_button("Join Game at above IP", bx, by, bw, bh,
                       ui->net_ip[0] != '\0')){
            if(net_init()){
                if(net_join(ui->net_ip, 12345)){
                    game_set_alert(gs, "Connecting...");
                } else {
                    game_set_alert(gs, "Connection failed — check the IP and port.");
                }
            }
        }
        by += bh + (int)(8*sc);

    } else {
        /* ─── Lobby ─── */
        char my_ip[64];
        get_local_ip(my_ip, sizeof(my_ip));
        char ip_line[96];
        snprintf(ip_line, sizeof(ip_line), "Your LAN IP: %s  (port 12345)", my_ip);
        DrawRectangle(bx, by, bw, (int)(30*sc), CLITERAL(Color){25,40,18,210});
        DrawRectangleLinesEx((Rectangle){(float)bx,(float)by,(float)bw,(float)(30*sc)},
                             1, CLITERAL(Color){80,160,60,200});
        DrawText(ip_line, bx+(bw-MeasureText(ip_line,lfs))/2, by+(int)(9*sc),
                 lfs, CLITERAL(Color){140,220,100,255});
        by += (int)(36*sc);

        int total = net_get_peer_count() + 1;
        char slots[64];
        snprintf(slots, sizeof(slots), "Lobby: %d / %d players connected",
                 total, net_get_max_players());
        int sfs=(int)(14*sc);
        DrawText(slots, bx+(bw-MeasureText(slots,sfs))/2, by,
                 sfs, (total > 1) ? GREEN : YELLOW);
        by += (int)(24*sc);

        if(g_local_player_id == 0){  /* I am host */
            if(draw_button("START GAME", bx, by, bw, bh, total > 1)){
                uint32_t seed = (uint32_t)time(NULL);
                NetPacket sp = {0}; sp.type = PKT_SYNC_SEED; sp.extra = seed;
                net_dispatch_packet(gs, &sp);
                NetPacket start = {0}; start.type = PKT_START_GAME;
                start.extra = total;
                net_dispatch_packet(gs, &start);
            }
        } else {
            DrawText("Waiting for host to start...",
                     bx+(bw-MeasureText("Waiting for host to start...",lfs))/2,
                     by+(int)(16*sc), lfs, GRAY);
        }
        by += bh + (int)(8*sc);

        if(draw_button("Cancel Lobby", bx, by, bw-(int)(110*sc), (int)(36*sc), true))
            net_deinit();
    }

    DrawText("Built with Raylib 5.0 + ENet",
             8, sh-20, (int)(10*sc), CLITERAL(Color){60,55,40,200});
}
