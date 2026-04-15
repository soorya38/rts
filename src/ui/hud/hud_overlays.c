/*=============================================================
 * hud_overlays.c  –  Shared helpers, icons, alert, end/menu screens
 *=============================================================*/
#include "game.h"
#include "ui_state.h"
#include "hud_common.h"
#include <stdio.h>
#include <string.h>
#include "net.h"

static const char *age_names[4]={"Dark Age","Feudal Age","Castle Age","Imperial Age"};

void draw_food_icon(int x,int y){
    DrawCircle(x+8,y+8,7,CLITERAL(Color){50,170,40,255});
    DrawCircle(x+8,y+7,4,CLITERAL(Color){100,210,70,255});
}
void draw_wood_icon(int x,int y){
    DrawRectangle(x+5,y+3,6,13,CLITERAL(Color){120,75,25,255});
    DrawRectangle(x+3,y+5,10,3,CLITERAL(Color){150,100,40,255});
}
void draw_gold_icon(int x,int y){
    DrawCircle(x+8,y+8,7,CLITERAL(Color){210,170,20,255});
    DrawCircle(x+8,y+7,4,CLITERAL(Color){240,210,60,255});
}
void draw_stone_icon(int x,int y){
    DrawCircle(x+8,y+9,7,CLITERAL(Color){155,148,138,255});
    DrawCircle(x+7,y+7,4,CLITERAL(Color){195,188,178,255});
}

bool draw_button(const char *label, int x, int y, int w, int h, bool enabled){
    Vector2 mp=GetMousePosition();
    bool hover=enabled && mp.x>=x && mp.x<=x+w && mp.y>=y && mp.y<=y+h;
    Color bg = hover ? C_BTN_HOV : C_BTN_NORM;
    if(!enabled) bg=CLITERAL(Color){30,25,15,200};
    DrawRectangle(x,y,w,h,bg);
    DrawRectangleLinesEx((Rectangle){(float)x,(float)y,(float)w,(float)h},1.5f,C_BTN_BORD);
    Color tc = enabled ? C_HUD_TXT : CLITERAL(Color){100,90,60,255};
    int fs=12;
    const char *nl = strchr(label, '\n');
    if (nl) {
        int len1 = nl - label;
        char line1[64];
        if (len1 >= sizeof(line1)) len1 = sizeof(line1) - 1;
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
    int fs = 11, tw = MeasureText(text, fs), th = fs + 6;
    DrawRectangle(x, y, tw + 10, th, CLITERAL(Color){20, 18, 12, 230});
    DrawRectangleLines(x, y, tw + 10, th, C_HUD_LINE);
    DrawText(text, x + 5, y + 3, fs, C_HUD_TXT);
}

void draw_alert(GameState *gs, UIState *ui){
    (void)ui;
    if(gs->alert_timer<=0) return;
    float alpha=clampf(gs->alert_timer/1.5f,0,1)*255;
    int tw=MeasureText(gs->alert,22);
    int bw=tw+40, bh=36;
    int bx=GetScreenWidth()/2-bw/2, by=HUD_TOP_H+8;
    DrawRectangleRounded((Rectangle){(float)bx,(float)by,(float)bw,(float)bh},0.3f,8,
                         CLITERAL(Color){25,20,10,(unsigned char)(alpha*0.9f)});
    DrawText(gs->alert,bx+(bw-tw)/2,by+(bh-22)/2,22,
             CLITERAL(Color){230,200,100,(unsigned char)alpha});
}

void draw_end_screen(GameState *gs, UIState *ui){
    (void)ui;
    if(gs->phase!=PHASE_VICTORY&&gs->phase!=PHASE_DEFEAT) return;
    DrawRectangle(0,0,GetScreenWidth(),GetScreenHeight(),CLITERAL(Color){0,0,0,180});
    bool win=(gs->phase==PHASE_VICTORY);
    const char *msg=win?"VICTORY!":"DEFEATED";
    Color mc=win?CLITERAL(Color){220,200,50,255}:CLITERAL(Color){210,50,40,255};
    int fs=52, tw=MeasureText(msg,fs);
    DrawText(msg,GetScreenWidth()/2-tw/2,GetScreenHeight()/2-60,fs,mc);
    const char *sub=win?"The enemy town center has fallen!":"Your town center has been destroyed!";
    int sfs=18, stw=MeasureText(sub,sfs);
    DrawText(sub,GetScreenWidth()/2-stw/2,GetScreenHeight()/2+10,sfs,CLITERAL(Color){200,185,150,255});
    DrawText("Press [R] to restart or [Q] to quit",
             GetScreenWidth()/2-MeasureText("Press [R] to restart or [Q] to quit",14)/2,
             GetScreenHeight()/2+50,14,CLITERAL(Color){150,135,100,255});
}

void draw_placement_bar(GameState *gs, UIState *ui){
    (void)ui;
    if(!gs->build_mode.active) return;
    static const char *BLD_NAMES[BLD_COUNT]={
        "Town Center","House","Barracks","Archery Range","Stable",
        "Blacksmith","Market",
        "Mill","Lumber Camp","Mining Camp","Farm"
    };
    char buf[80];
    snprintf(buf,sizeof(buf),
             "  Placing: %s   ·   Click on map to place   ·   [ESC] to cancel  ",
             BLD_NAMES[gs->build_mode.type]);
    int tw=MeasureText(buf,12);
    Color bg = gs->build_mode.valid ?
        CLITERAL(Color){20,60,20,230} : CLITERAL(Color){60,20,20,230};
    DrawRectangle(GetScreenWidth()/2-tw/2-8,HUD_TOP_H+2,tw+16,20,bg);
    DrawRectangleLinesEx((Rectangle){(float)(GetScreenWidth()/2-tw/2-8),(float)(HUD_TOP_H+2),(float)(tw+16),20},
                         1,gs->build_mode.valid?CLITERAL(Color){60,180,60,200}:CLITERAL(Color){180,60,60,200});
    DrawText(buf,GetScreenWidth()/2-tw/2,HUD_TOP_H+5,12,
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
    Vector2 mp = GetMousePosition();
    bool hover = mp.x>=x && mp.x<=x+w && mp.y>=y && mp.y<=y+h;

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
    int fs = 15;
    const char *display = ui->net_ip[0] ? ui->net_ip : "tap to type IP";
    Color tc = ui->net_ip[0] ? C_HUD_TXT : CLITERAL(Color){100,90,60,220};
    int tw = MeasureText(display, fs);
    DrawText(display, x+8, y+(h-fs)/2, fs, tc);
    /* Blinking cursor */
    if (ui->net_ip_active && ((int)(GetTime()*2) % 2 == 0))
        DrawRectangle(x+8+tw+2, y+(h-fs)/2, 2, fs, C_HUD_TXT);
}

void draw_menu(GameState *gs, UIState *ui){
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
    int f1=48,f2=22;
    DrawText(t1,sw/2-MeasureText(t1,f1)/2,sh/2-155,f1,CLITERAL(Color){220,185,40,255});
    DrawText(t2,sw/2-MeasureText(t2,f2)/2,sh/2-100,f2,CLITERAL(Color){170,155,110,255});
    DrawRectangle(sw/2-120,sh/2-72,240,2,CLITERAL(Color){130,110,60,200});

    const char *lines[]={
        "Gather resources  \xc2\xb7  Build structures  \xc2\xb7  Train armies",
        "Destroy the enemy Town Center to win!",
        "Controls:  WASD / edge scroll  |  Mouse wheel: zoom",
        "Click to select | Drag to box-select | Click (selected) = command"
    };
    for(int i=0;i<4;i++)
        DrawText(lines[i],sw/2-MeasureText(lines[i],12)/2,
                 sh/2-52+i*16,12,CLITERAL(Color){150,140,110,220});

    /* ── Buttons ── */
    int bw=260, bh=46, bx=sw/2-bw/2, by=sh/2+2;

    /* Solo */
    if(draw_button("Start Solo Campaign", bx, by, bw, bh, true)){
        game_init_started_game(gs, (uint32_t)time(NULL), 2);
    }
    by += bh + 8;

    if (!g_net_active) {
        /* ─── Host ─── */
        if(draw_button("Host LAN Game  (port 12345)", bx, by, bw, bh, true)){
            if(net_init() && net_host_create(12345)){
                game_set_alert(gs, "Hosting! Share your IP with the other player.");
            }
        }
        by += bh + 8;

        /* ─── Join section ─── */
        DrawText("Host IP address:", bx, by, 13, C_HUD_TXT);
        by += 18;
        draw_ip_box(ui, bx, by, bw, bh);
        by += bh + 6;

#if defined(PLATFORM_ANDROID) || defined(ANDROID)
        DrawText("Tap the field above, then use your keyboard to type.",
                 bx, by, 11, CLITERAL(Color){160,150,120,200});
        by += 16;
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
        by += bh + 8;

    } else {
        /* ─── Lobby ─── */
        /* Show our own LAN IP so the other player knows where to connect */
        char my_ip[64];
        get_local_ip(my_ip, sizeof(my_ip));
        char ip_line[96];
        snprintf(ip_line, sizeof(ip_line), "Your LAN IP: %s  (port 12345)", my_ip);
        DrawRectangle(bx, by, bw, 30, CLITERAL(Color){25,40,18,210});
        DrawRectangleLinesEx((Rectangle){(float)bx,(float)by,(float)bw,30},
                             1, CLITERAL(Color){80,160,60,200});
        DrawText(ip_line, bx+(bw-MeasureText(ip_line,12))/2, by+9,
                 12, CLITERAL(Color){140,220,100,255});
        by += 36;

        int total = net_get_peer_count() + 1;
        char slots[64];
        snprintf(slots, sizeof(slots), "Lobby: %d / %d players connected",
                 total, net_get_max_players());
        DrawText(slots, bx+(bw-MeasureText(slots,14))/2, by,
                 14, (total > 1) ? GREEN : YELLOW);
        by += 24;

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
                     bx+(bw-MeasureText("Waiting for host to start...",12))/2,
                     by+16, 12, GRAY);
        }
        by += bh + 8;

        if(draw_button("Cancel Lobby", bx, by, bw-110, 36, true))
            net_deinit();
    }

    DrawText("Built with Raylib 5.0 + ENet",
             8, sh-20, 10, CLITERAL(Color){60,55,40,200});
}

static const char *_age_names_unused(void){ return age_names[0]; } /* suppress warning */
