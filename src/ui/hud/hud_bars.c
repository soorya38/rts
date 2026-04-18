/*=============================================================
 * hud_bars.c  –  Top resource bar, bottom panel, minimap
 *=============================================================*/
#include "game.h"
#include "ui_state.h"
#include "hud_common.h"
#include <stdio.h>
#include <string.h>
#include "net.h"
#include "renderer.h"

static const char *age_names[4]={"Dark Age","Feudal Age","Castle Age","Imperial Age"};

static void draw_training_queue(Building *b, int x, int y, int fs10, int fs9){
    if(b->queue_len <= 0) return;

    DrawText("Queue:", x, y, fs10, CLITERAL(Color){180,165,130,220});
    int slot_w = 54;
    int slot_h = 20;
    int slot_gap = 5;
    int sx = x + 42;
    for(int i=0; i<BQUEUE_CAP; i++){
        int bx = sx + i * (slot_w + slot_gap);
        Color bg = (i < b->queue_len)
            ? CLITERAL(Color){56,46,26,255}
            : CLITERAL(Color){28,23,14,200};
        DrawRectangle(bx, y - 2, slot_w, slot_h, bg);
        DrawRectangleLinesEx((Rectangle){(float)bx,(float)(y - 2),(float)slot_w,(float)slot_h},
                             1.0f, C_HUD_LINE);
        if(i < b->queue_len){
            const char *name = unit_name(b->queue[i]);
            char mini[16];
            snprintf(mini, sizeof(mini), "%-.6s", name);
            DrawText(mini, bx + 4, y + 3, fs9, i == 0 ? C_GOLD : C_HUD_TXT);
        } else {
            DrawText("-", bx + slot_w/2 - 2, y + 3, fs9, CLITERAL(Color){90,80,55,180});
        }
    }
}

static bool can_queue_unit_now(GameState *gs, Building *b, int player, UnitType ut){
    if(!b || !b->complete || b->active_tech != TECH_NONE) return false;
    if(b->queue_len >= BQUEUE_CAP) return false;
    if(!building_can_train_unit(b->type, ut)) return false;
    if(gs->res[player].age < unit_age_required(ut)) return false;
    if(!res_can_afford(&gs->res[player], unit_cost(ut))) return false;
    if(gs->res[player].population + building_queued_population(b) >= gs->res[player].pop_cap) return false;
    return true;
}

static bool can_set_rally_now(Building *b, int player){
    return b && b->active && b->complete && b->player == player && building_supports_rally(b->type);
}

static void draw_sandbox_tools(GameState *gs, UIState *ui, int panel_w, int by_start){
    if(gs->mode != GAME_MODE_SANDBOX) return;

    float sc = hud_scale();
    int lp = net_get_local_player();
    int enemy = (gs->num_players > 1 && lp == 0) ? 1 : 0;
    int pad = (int)(12 * sc);
    int bw = (int)(92 * sc);
    int bh = (int)(26 * sc);
    int gap = (int)(6 * sc);
    int fs11 = (int)(11 * sc);
    int fs10 = (int)(10 * sc);

    int row1_w = bw * 3 + gap * 2;
    int row2_w = bw * 2 + gap;
    int row1_x = panel_w - pad - row1_w;
    int row2_x = panel_w - pad - row2_w;
    int top_y = by_start + (int)(10 * sc);
    int row1_y = top_y + (int)(16 * sc);
    int row2_y = row1_y + bh + gap;
    int hint_y = row2_y + bh + (int)(6 * sc);

    DrawText("SANDBOX QUICK ACTIONS", row1_x, top_y, fs11, CLITERAL(Color){210,190,130,255});

    if(draw_button("+1000 All", row1_x, row1_y, bw, bh, true))
        game_sandbox_add_resources(gs, lp, 1000);
    if(draw_button("Age +1", row1_x + bw + gap, row1_y, bw, bh, true))
        game_sandbox_next_age(gs, lp);
    if(draw_button("Ally Wave", row1_x + 2 * (bw + gap), row1_y, bw, bh, true))
        game_sandbox_spawn_wave(gs, lp);

    if(draw_button("Enemy Wave", row2_x, row2_y, bw, bh, true))
        game_sandbox_spawn_wave(gs, enemy);
    if(draw_button("Restore", row2_x + bw + gap, row2_y, bw, bh, true))
        game_sandbox_heal_selection(gs, lp, ui->sel_building, ui->sel_units, ui->sel_count);

    DrawText("Hotkeys: F1 resources  F2 age  F3 ally  F4 enemy  F5 restore",
             row1_x, hint_y, fs10, CLITERAL(Color){130,120,90,210});
}

void draw_top_bar(GameState *gs, UIState *ui){
    (void)ui;
    float sc = hud_scale();
    int lp = net_get_local_player();
    PlayerRes *pr=&gs->res[lp];
    int th = HUD_TOP_H;
    DrawRectangle(0,0,GetScreenWidth(),th,C_HUD_BG);
    DrawRectangle(0,th-1,GetScreenWidth(),2,C_HUD_LINE);
    char buf[32];
    int icon = (int)(20*sc);   /* icon column width */
    int fs14 = (int)(14*sc);
    int fs13 = (int)(13*sc);
    int fs12 = (int)(12*sc);
    int fs10 = (int)(10*sc);
    int fs11 = (int)(11*sc);
    int cx=10, cy=(th-fs14)/2;
    int f0=cx; draw_food_icon(ui, cx,(th-icon)/2); cx+=icon+2;
    snprintf(buf,sizeof(buf),"%d",pr->amount[RES_FOOD]);
    DrawText(buf,cx,cy,fs14,C_FOOD); int f1=cx+MeasureText(buf,fs14); cx=f1+(int)(18*sc);
    int w0=cx; draw_wood_icon(ui, cx,(th-icon)/2); cx+=icon+2;
    snprintf(buf,sizeof(buf),"%d",pr->amount[RES_WOOD]);
    DrawText(buf,cx,cy,fs14,C_WOOD); int w1=cx+MeasureText(buf,fs14); cx=w1+(int)(18*sc);
    int g0=cx; draw_gold_icon(ui, cx,(th-icon)/2); cx+=icon+2;
    snprintf(buf,sizeof(buf),"%d",pr->amount[RES_GOLD]);
    DrawText(buf,cx,cy,fs14,C_GOLD); int g1=cx+MeasureText(buf,fs14); cx=g1+(int)(18*sc);
    int s0=cx; draw_stone_icon(ui, cx,(th-icon)/2); cx+=icon+2;
    snprintf(buf,sizeof(buf),"%d",pr->amount[RES_STONE]);
    DrawText(buf,cx,cy,fs14,C_STONE); int s1=cx+MeasureText(buf,fs14); cx=s1+(int)(18*sc);
    int p0=cx;
    Color pc=(pr->population>=pr->pop_cap)?C_POP_WARN:C_POP_OK;
    snprintf(buf,sizeof(buf),"Pop: %d/%d",pr->population,pr->pop_cap);
    DrawText(buf,cx,cy,fs13,pc); int p1=cx+MeasureText(buf,fs13);
    Vector2 mp = GetMousePosition();
    if(mp.y < th){
        if(mp.x >= f0 && mp.x <= f1) draw_tooltip("Food: Used to train villagers and most units", (int)mp.x+5, (int)mp.y+15);
        if(mp.x >= w0 && mp.x <= w1) draw_tooltip("Wood: Used for buildings and archers", (int)mp.x+5, (int)mp.y+15);
        if(mp.x >= g0 && mp.x <= g1) draw_tooltip("Gold: Used for advanced units and aging up", (int)mp.x+5, (int)mp.y+15);
        if(mp.x >= s0 && mp.x <= s1) draw_tooltip("Stone: Used for castles and some defensive buildings", (int)mp.x+5, (int)mp.y+15);
        if(mp.x >= p0 && mp.x <= p1) draw_tooltip("Population: Total units vs capacity", (int)mp.x+5, (int)mp.y+15);
    }
    const char *an=age_names[pr->age];
    if(pr->advancing){
        snprintf(buf,sizeof(buf),"-> %s (%.0fs)",age_names[pr->age+1],pr->advance_timer);
        DrawText(buf,GetScreenWidth()/2-MeasureText(buf,fs12)/2,cy,fs12,C_AGE);
    } else {
        DrawText(an,GetScreenWidth()/2-MeasureText(an,fs13)/2,cy,fs13,C_AGE);
    }
    int btn_w=(int)(208*sc);
    if(pr->age<3 && !pr->advancing){
        Cost c=age_advance_cost(pr->age);
        bool can=res_can_afford(pr,c);
        char label[40];
        if(c.gold>0) snprintf(label,sizeof(label),"Advance Age: %dF %dG",c.food,c.gold);
        else         snprintf(label,sizeof(label),"Advance Age: %dF",c.food);
        if(draw_button(label,GetScreenWidth()-btn_w-8,6,btn_w,(int)(30*sc),can)) {
            if (g_net_active) {
                NetPacket pkt = {0};
                pkt.type = PKT_AGE_ADVANCE;
                pkt.player = lp;
                net_dispatch_packet(gs, &pkt);
            } else {
                res_try_advance_age(gs, lp);
            }
        }
        if(!can){
            char need[48]="Need:";
            if(pr->amount[RES_FOOD]<c.food){ char tmp[20]; snprintf(tmp,sizeof(tmp)," %dF",c.food-pr->amount[RES_FOOD]); strcat(need,tmp); }
            if(c.gold>0&&pr->amount[RES_GOLD]<c.gold){ char tmp[20]; snprintf(tmp,sizeof(tmp)," %dG",c.gold-pr->amount[RES_GOLD]); strcat(need,tmp); }
            DrawText(need,GetScreenWidth()-btn_w-6,(int)(38*sc),fs10,CLITERAL(Color){220,160,80,220});
        }
    } else if(pr->advancing){
        char buf2[48];
        snprintf(buf2,sizeof(buf2),"Advancing... %.0fs left",pr->advance_timer);
        DrawText(buf2,GetScreenWidth()-btn_w-6,(int)(12*sc),fs11,C_AGE);
    }
    int minutes=(int)(gs->game_time/60), seconds=(int)(gs->game_time)%60;
    snprintf(buf,sizeof(buf),"%02d:%02d",minutes,seconds);
    DrawText(buf,GetScreenWidth()-(int)(48*sc),cy,fs12,CLITERAL(Color){140,130,100,255});
}

void draw_bottom_panel(GameState *gs, UIState *ui){
    float sc = hud_scale();
    int by_start = HUD_BOT_Y;
    int panel_w = GetScreenWidth()-MINI_SIZE-16;
    DrawRectangle(0,by_start,panel_w,HUD_BOT_H,C_HUD_BG);
    DrawRectangle(0,by_start,panel_w,2,C_HUD_LINE);
    char buf[64];

    int fs16=(int)(16*sc);
    int fs14=(int)(14*sc);
    int fs13=(int)(13*sc);
    int fs12=(int)(12*sc);
    int fs11=(int)(11*sc);
    int fs10=(int)(10*sc);
    int fs9 =(int)(9*sc);
    int pad =(int)(12*sc);

    if(ui->sel_building>=0){
        Building *b=&gs->buildings[ui->sel_building];
        if(!b->active){ui->sel_building=-1;return;}
        DrawText(building_name(b->type),pad,by_start+(int)(8*sc),fs16,C_HUD_TXT);
        snprintf(buf,sizeof(buf),"HP: %d / %d",b->hp,b->max_hp);
        DrawText(buf,pad,by_start+(int)(28*sc),fs12,CLITERAL(Color){180,165,130,255});
        if(!b->complete){
            int builders=0;
            for(int i=0;i<MAX_UNITS;i++){ Unit *u=&gs->units[i]; if(u->active&&u->build_id==b->id) builders++; }
            int bar_w=(int)(200*sc), bar_h=(int)(7*sc);
            DrawRectangle(pad,by_start+(int)(44*sc),bar_w,bar_h,CLITERAL(Color){35,28,16,255});
            DrawRectangle(pad,by_start+(int)(44*sc),(int)(bar_w*b->construction),bar_h,CLITERAL(Color){200,160,40,255});
            snprintf(buf,sizeof(buf),"Construction: %.0f%%",b->construction*100);
            DrawText(buf,pad,by_start+(int)(56*sc),fs12,CLITERAL(Color){200,180,100,255});
            if(builders==0) DrawText("No builders!  Select a villager and click this building",pad,by_start+(int)(72*sc),fs11,CLITERAL(Color){220,100,60,230});
            else { snprintf(buf,sizeof(buf),"%d builder%s  (%dx speed)",builders,builders>1?"s":"",builders); DrawText(buf,pad,by_start+(int)(72*sc),fs11,CLITERAL(Color){120,200,100,220}); }
            return;
        }
        if(b->active_tech != TECH_NONE){
            float full=tech_time(b->active_tech);
            float prog=1.0f-(b->tech_timer/full);
            snprintf(buf,sizeof(buf),"Researching: %s (%.0fs)",tech_name(b->active_tech),b->tech_timer);
            DrawText(buf,pad,by_start+(int)(44*sc),fs12,CLITERAL(Color){100,180,255,255});
            int bar_w=(int)(160*sc), bar_h=(int)(6*sc);
            DrawRectangle(pad,by_start+(int)(60*sc),bar_w,bar_h,CLITERAL(Color){20,30,50,255});
            DrawRectangle(pad,by_start+(int)(60*sc),(int)(bar_w*prog),bar_h,CLITERAL(Color){60,140,255,255});
        } else if(b->queue_len>0){
            snprintf(buf,sizeof(buf),"Training: %s (%.0fs)",unit_name(b->queue[0]),b->train_timer);
            DrawText(buf,pad,by_start+(int)(44*sc),fs12,C_GOLD);
            float prog=1.0f-(b->train_timer/building_train_time(b->queue[0]));
            int bar_w=(int)(160*sc), bar_h=(int)(6*sc);
            DrawRectangle(pad,by_start+(int)(60*sc),bar_w,bar_h,CLITERAL(Color){40,35,20,255});
            DrawRectangle(pad,by_start+(int)(60*sc),(int)(bar_w*prog),bar_h,CLITERAL(Color){50,200,60,255});
            draw_training_queue(b, pad, by_start+(int)(72*sc), fs10, fs9);
        }
        int btn_w=(int)(80*sc), btn_h=(int)(50*sc), btn_gap=(int)(8*sc);
        int bx=(int)(220*sc), bby=by_start+(int)(10*sc);
        int lp = net_get_local_player();
        int rally_btn_w = (int)(112*sc);
        int rally_btn_h = (int)(32*sc);
        int rally_btn_x = panel_w - pad - rally_btn_w;
        int rally_btn_y = by_start + (int)(10*sc);
        bool can_set_rally = can_set_rally_now(b, lp);

        if(can_set_rally){
            const char *rally_label = ui->rally_mode ? "[G] Cancel" : "[G] Rally";
            if(draw_button(rally_label, rally_btn_x, rally_btn_y, rally_btn_w, rally_btn_h, true)){
                ui->rally_mode = !ui->rally_mode;
                if(ui->rally_mode) game_set_alert(gs, "Click the map to place the rally point.");
                else game_set_alert(gs, "Rally point canceled.");
            }
            snprintf(buf, sizeof(buf), "Point: %d,%d", b->rally_tx, b->rally_ty);
            DrawText(buf, rally_btn_x, rally_btn_y + rally_btn_h + (int)(4*sc), fs10,
                     CLITERAL(Color){180,165,130,220});
            if(ui->rally_mode){
                DrawText("New units spawn here, then move there", rally_btn_x - (int)(44*sc),
                         rally_btn_y + rally_btn_h + (int)(18*sc), fs9,
                         CLITERAL(Color){120,200,255,220});
            }
        }
        switch(b->type){
            case BLD_TOWN_CENTER:
                if(draw_button("Villager\n50F",bx,bby,btn_w,btn_h,can_queue_unit_now(gs,b,lp,UNIT_VILLAGER))) {
                    if (g_net_active) {
                        NetPacket pkt = {0}; pkt.type = PKT_TRAIN_UNIT; pkt.player = lp;
                        pkt.target_id = ui->sel_building; pkt.extra = UNIT_VILLAGER;
                        net_dispatch_packet(gs, &pkt);
                    } else building_enqueue_unit(gs,b,UNIT_VILLAGER);
                }
                if(draw_button("Scout\n80F",bx+btn_w+btn_gap,bby,btn_w,btn_h,can_queue_unit_now(gs,b,lp,UNIT_SCOUT))) {
                    if (g_net_active) {
                        NetPacket pkt = {0}; pkt.type = PKT_TRAIN_UNIT; pkt.player = lp;
                        pkt.target_id = ui->sel_building; pkt.extra = UNIT_SCOUT;
                        net_dispatch_packet(gs, &pkt);
                    } else building_enqueue_unit(gs,b,UNIT_SCOUT);
                }
                break;
            case BLD_BARRACKS: {
                int bw2=(int)(90*sc);
                if(draw_button("Militia\n60F 20G",bx,bby,bw2,btn_h,can_queue_unit_now(gs,b,lp,UNIT_MILITIA))) {
                    if (g_net_active) {
                        NetPacket pkt = {0}; pkt.type = PKT_TRAIN_UNIT; pkt.player = lp;
                        pkt.target_id = ui->sel_building; pkt.extra = UNIT_MILITIA;
                        net_dispatch_packet(gs, &pkt);
                    } else building_enqueue_unit(gs,b,UNIT_MILITIA);
                }
                if(gs->res[lp].age>=1&&draw_button("Man@Arms\n60F 20G",bx+bw2+btn_gap,bby,bw2,btn_h,can_queue_unit_now(gs,b,lp,UNIT_MAN_AT_ARMS))) {
                     if (g_net_active) {
                        NetPacket pkt = {0}; pkt.type = PKT_TRAIN_UNIT; pkt.player = lp;
                        pkt.target_id = ui->sel_building; pkt.extra = UNIT_MAN_AT_ARMS;
                        net_dispatch_packet(gs, &pkt);
                    } else building_enqueue_unit(gs,b,UNIT_MAN_AT_ARMS);
                }
                if(gs->res[lp].age>=1&&draw_button("Spearman\n35F 25W",bx+2*(bw2+btn_gap),bby,bw2,btn_h,
                    can_queue_unit_now(gs,b,lp,UNIT_SPEARMAN))) {
                    if (g_net_active) {
                        NetPacket pkt = {0}; pkt.type = PKT_TRAIN_UNIT; pkt.player = lp;
                        pkt.target_id = ui->sel_building; pkt.extra = UNIT_SPEARMAN;
                        net_dispatch_packet(gs, &pkt);
                    } else building_enqueue_unit(gs,b,UNIT_SPEARMAN);
                }
                break;
            }
            case BLD_ARCHERY_RANGE: {
                int bw2=(int)(90*sc);
                if(draw_button("Archer\n25W 45G",bx,bby,bw2,btn_h,can_queue_unit_now(gs,b,lp,UNIT_ARCHER))) {
                    if (g_net_active) {
                        NetPacket pkt = {0}; pkt.type = PKT_TRAIN_UNIT; pkt.player = lp;
                        pkt.target_id = ui->sel_building; pkt.extra = UNIT_ARCHER;
                        net_dispatch_packet(gs, &pkt);
                    } else building_enqueue_unit(gs,b,UNIT_ARCHER);
                }
                if(gs->res[lp].age>=1&&draw_button("Skirm\n25F 35W",bx+bw2+btn_gap,bby,bw2,btn_h,
                    can_queue_unit_now(gs,b,lp,UNIT_SKIRMISHER))) {
                    if (g_net_active) {
                        NetPacket pkt = {0}; pkt.type = PKT_TRAIN_UNIT; pkt.player = lp;
                        pkt.target_id = ui->sel_building; pkt.extra = UNIT_SKIRMISHER;
                        net_dispatch_packet(gs, &pkt);
                    } else building_enqueue_unit(gs,b,UNIT_SKIRMISHER);
                }
                if(gs->res[lp].age>=2&&draw_button("Cav Archer\n40F 70G",bx+2*(bw2+btn_gap),bby,bw2,btn_h,
                    can_queue_unit_now(gs,b,lp,UNIT_CAVALRY_ARCHER))) {
                    if (g_net_active) {
                        NetPacket pkt = {0}; pkt.type = PKT_TRAIN_UNIT; pkt.player = lp;
                        pkt.target_id = ui->sel_building; pkt.extra = UNIT_CAVALRY_ARCHER;
                        net_dispatch_packet(gs, &pkt);
                    } else building_enqueue_unit(gs,b,UNIT_CAVALRY_ARCHER);
                }
                break;
            }
            case BLD_STABLE: {
                int bw2=(int)(90*sc);
                if(draw_button("Knight\n60F 75G",bx,bby,bw2,btn_h,can_queue_unit_now(gs,b,lp,UNIT_KNIGHT))) {
                    if (g_net_active) {
                        NetPacket pkt = {0}; pkt.type = PKT_TRAIN_UNIT; pkt.player = lp;
                        pkt.target_id = ui->sel_building; pkt.extra = UNIT_KNIGHT;
                        net_dispatch_packet(gs, &pkt);
                    } else building_enqueue_unit(gs,b,UNIT_KNIGHT);
                }
                break;
            }
            case BLD_MONASTERY: {
                int bw2=(int)(96*sc);
                if(draw_button("Monk\n100G",bx,bby,bw2,btn_h,can_queue_unit_now(gs,b,lp,UNIT_MONK))) {
                    if (g_net_active) {
                        NetPacket pkt = {0}; pkt.type = PKT_TRAIN_UNIT; pkt.player = lp;
                        pkt.target_id = ui->sel_building; pkt.extra = UNIT_MONK;
                        net_dispatch_packet(gs, &pkt);
                    } else building_enqueue_unit(gs,b,UNIT_MONK);
                }
                DrawText("Monks heal nearby allies and convert enemy units",bx,bby+(int)(58*sc),fs10,CLITERAL(Color){180,165,130,220});
                break;
            }
            case BLD_SIEGE_WORKSHOP: {
                int bw2=(int)(92*sc);
                if(draw_button("Ram\n160W 75G",bx,bby,bw2,btn_h,can_queue_unit_now(gs,b,lp,UNIT_BATTERING_RAM))) {
                    if (g_net_active) {
                        NetPacket pkt = {0}; pkt.type = PKT_TRAIN_UNIT; pkt.player = lp;
                        pkt.target_id = ui->sel_building; pkt.extra = UNIT_BATTERING_RAM;
                        net_dispatch_packet(gs, &pkt);
                    } else building_enqueue_unit(gs,b,UNIT_BATTERING_RAM);
                }
                if(draw_button("Mangonel\n160W 135G",bx+bw2+btn_gap,bby,bw2,btn_h,can_queue_unit_now(gs,b,lp,UNIT_MANGONEL))) {
                    if (g_net_active) {
                        NetPacket pkt = {0}; pkt.type = PKT_TRAIN_UNIT; pkt.player = lp;
                        pkt.target_id = ui->sel_building; pkt.extra = UNIT_MANGONEL;
                        net_dispatch_packet(gs, &pkt);
                    } else building_enqueue_unit(gs,b,UNIT_MANGONEL);
                }
                if(draw_button("Scorpion\n75W 75G",bx+2*(bw2+btn_gap),bby,bw2,btn_h,can_queue_unit_now(gs,b,lp,UNIT_SCORPION))) {
                    if (g_net_active) {
                        NetPacket pkt = {0}; pkt.type = PKT_TRAIN_UNIT; pkt.player = lp;
                        pkt.target_id = ui->sel_building; pkt.extra = UNIT_SCORPION;
                        net_dispatch_packet(gs, &pkt);
                    } else building_enqueue_unit(gs,b,UNIT_SCORPION);
                }
                DrawText("Ram crushes buildings. Mangonel deals splash damage. Scorpion is ranged siege support.",
                         bx,bby+(int)(58*sc),fs10,CLITERAL(Color){180,165,130,220});
                break;
            }
            case BLD_MILL:
                DrawText("Economic upgrades for farms and food collection",bx,bby+(int)(18*sc),fs11,CLITERAL(Color){160,145,110,200});
                break;
            case BLD_LUMBER_CAMP:
                DrawText("Economic upgrades for woodcutting and hauling",bx,bby+(int)(18*sc),fs11,CLITERAL(Color){160,145,110,200});
                break;
            case BLD_WALL:
                DrawText("Cheap defensive segment that blocks movement",bx,bby+(int)(18*sc),fs11,CLITERAL(Color){160,145,110,200});
                break;
            case BLD_GATE:
                DrawText("Passable fortified opening for your wall line",bx,bby+(int)(18*sc),fs11,CLITERAL(Color){160,145,110,200});
                break;
            case BLD_BLACKSMITH:
                DrawText("No units  —  research upgrades below",bx,bby+(int)(18*sc),fs11,CLITERAL(Color){160,145,110,200});
                break;
            case BLD_MARKET: {
                int bw2=(int)(100*sc), bgap2=(int)(8*sc);
                bool can_w = (gs->res[lp].amount[RES_WOOD]  >= 100);
                bool can_f = (gs->res[lp].amount[RES_FOOD]  >= 100);
                bool can_g = (gs->res[lp].amount[RES_GOLD]  >= 100);
                int bbt_h=(int)(42*sc);
                if(draw_button("100W -> 75G", bx,         bby, bw2, bbt_h, can_w && !g_net_active)){
                    res_deduct(&gs->res[lp], (Cost){0,100,0,0});
                    res_add   (&gs->res[lp], RES_GOLD, 75);
                }
                if(draw_button("100F -> 50G", bx+bw2+bgap2, bby, bw2, bbt_h, can_f && !g_net_active)){
                    res_deduct(&gs->res[lp], (Cost){100,0,0,0});
                    res_add   (&gs->res[lp], RES_GOLD, 50);
                }
                if(draw_button("100G -> 150F",bx+2*(bw2+bgap2), bby, (int)(106*sc), bbt_h, can_g && !g_net_active)){
                    res_deduct(&gs->res[lp], (Cost){0,0,100,0});
                    res_add   (&gs->res[lp], RES_FOOD, 150);
                }
                if(g_net_active)
                    DrawText("(Solo only)", bx, bby+(int)(46*sc), fs9, CLITERAL(Color){180,120,60,200});
                break;
            }
            default: break;
        }
        /* Research tech buttons */
        bool busy = (b->active_tech != TECH_NONE || b->queue_len > 0);
        if(b->player == lp){
            if(b->active_tech == TECH_NONE && b->queue_len >= BQUEUE_CAP)
                DrawText("Queue full", pad, by_start+(int)(96*sc), fs10, CLITERAL(Color){220,160,80,220});
            else if(b->active_tech == TECH_NONE &&
                    gs->res[lp].population + building_queued_population(b) >= gs->res[lp].pop_cap)
                DrawText("Need more housing to queue more units", pad, by_start+(int)(96*sc), fs10, CLITERAL(Color){220,160,80,220});
            TechType techs[6] = {TECH_NONE, TECH_NONE, TECH_NONE, TECH_NONE, TECH_NONE, TECH_NONE};
            int tc = 0;
            if(b->type == BLD_TOWN_CENTER){ techs[0]=TECH_LOOM; techs[1]=TECH_WHEELBARROW; techs[2]=TECH_HAND_CART; tc=3; }
            else if(b->type == BLD_MILL){
                techs[0]=TECH_HAND_MILL; techs[1]=TECH_CROP_ROTATION; techs[2]=TECH_GRANARY_BASKETS;
                techs[3]=TECH_FERTILIZER; techs[4]=TECH_IRRIGATION; techs[5]=TECH_REAPING; tc=6;
            }
            else if(b->type == BLD_LUMBER_CAMP){
                techs[0]=TECH_DOUBLE_BIT_AXE; techs[1]=TECH_LOG_STRAPS; techs[2]=TECH_BOW_SAW;
                techs[3]=TECH_TIMBER_ROUTE; techs[4]=TECH_TWO_MAN_SAW; techs[5]=TECH_HARDWOOD_CARTS; tc=6;
            }
            else if(b->type == BLD_BARRACKS){
                techs[0]=TECH_IRON_WEAPONRY; techs[1]=TECH_SQUIRES; techs[2]=TECH_CHAIN_MAIL;
                techs[3]=TECH_HARDENED_BLADES; techs[4]=TECH_IMPERIAL_INFANTRY; techs[5]=TECH_VETERAN_LEGION; tc=6;
            }
            else if(b->type == BLD_ARCHERY_RANGE){
                techs[0]=TECH_COMPOSITE_BOWS; techs[1]=TECH_THUMB_RING; techs[2]=TECH_REINFORCED_STRINGS;
                techs[3]=TECH_EAGLE_EYE; techs[4]=TECH_IMPERIAL_ARCHERY; techs[5]=TECH_FIELD_CRAFT; tc=6;
            }
            else if(b->type == BLD_STABLE){
                techs[0]=TECH_MOUNTED_ARMOR; techs[1]=TECH_HUSBANDRY; techs[2]=TECH_CAVALRY_DRILL;
                techs[3]=TECH_BLOODLINES; techs[4]=TECH_IMPERIAL_CAVALRY; techs[5]=TECH_STEEL_SPURS; tc=6;
            }
            else if(b->type == BLD_MONASTERY){
                techs[0]=TECH_SANCTITY; techs[1]=TECH_DEVOTION; techs[2]=TECH_FERVOR;
                techs[3]=TECH_ILLUMINATION; techs[4]=TECH_BLOCK_PRINTING; techs[5]=TECH_HOLY_VISION; tc=6;
            }
            else if(b->type == BLD_SIEGE_WORKSHOP){
                techs[0]=TECH_REINFORCED_RAM; techs[1]=TECH_SIEGE_ENGINEERS; techs[2]=TECH_ONAGER;
                techs[3]=TECH_DRILL_CREW; techs[4]=TECH_HEAVY_SCORPION; techs[5]=TECH_TORSION_ENGINES; tc=6;
            }
            else if(b->type == BLD_BLACKSMITH){
                techs[0]=TECH_SCALE_ARMOR; techs[1]=TECH_BLAST_FURNACE; techs[2]=TECH_FORGED_ARROWS;
                techs[3]=TECH_PLATE_ARMOR; techs[4]=TECH_BODKIN_ARROW; techs[5]=TECH_BRACER; tc=6;
            }
            int tech_btn_w=(int)(107*sc), tech_btn_h=(int)((tc > 3) ? 34 : 40), tech_gap=(int)(5*sc);
            int tech_cols = (tc > 3) ? 3 : tc;
            int tech_row_gap = (int)((tc > 3) ? 28 * sc : 0);
            if(tech_cols < 1) tech_cols = 1;
            for(int ti=0; ti<tc; ti++){
                TechType tt2 = techs[ti];
                int tcol = ti % tech_cols;
                int trow = ti / tech_cols;
                int tbx = bx + tcol*(tech_btn_w+tech_gap);
                int tby = bby + (int)(58*sc) + trow*(tech_btn_h + tech_row_gap);
                if(!gs->res[lp].tech_unlocked[tt2]){
                    Cost tc2 = tech_cost(tt2);
                    bool age_ok = gs->res[lp].age >= tech_age_required(tt2);
                    bool can = res_can_afford(&gs->res[lp], tc2) && !busy && age_ok;
                    char tbuf[80];
                    snprintf(tbuf,sizeof(tbuf),"%s",tech_name(tt2));
                    if(draw_button(tbuf, tbx, tby, tech_btn_w, tech_btn_h, can)){
                        if(g_net_active){
                            NetPacket pkt={0}; pkt.type=PKT_RESEARCH;
                            pkt.player=lp; pkt.target_id=ui->sel_building; pkt.extra=(int32_t)tt2;
                            net_dispatch_packet(gs,&pkt);
                        } else building_start_tech(gs,b,tt2);
                    }
                    DrawText(tech_desc(tt2), tbx, tby + tech_btn_h + (int)(2*sc), fs10, CLITERAL(Color){160,200,255,200});
                    if(!age_ok){
                        static const char *AGE_NAMES[] = {"Dark Age","Feudal Age","Castle Age","Imperial Age"};
                        DrawText(AGE_NAMES[tech_age_required(tt2)], tbx, tby + tech_btn_h + (int)(14*sc), fs9, CLITERAL(Color){220,140,60,220});
                    }
                } else {
                    DrawText(tech_name(tt2), tbx, tby + (int)(4*sc), fs10, CLITERAL(Color){80,220,100,220});
                    DrawText("[Researched]", tbx, tby + (int)(16*sc), fs9, CLITERAL(Color){60,180,80,200});
                }
            }
        }
        /* Demolish button */
        if(b->player == lp && b->complete && b->type != BLD_TOWN_CENTER){
            Cost refund = building_cost(b->type);
            snprintf(buf, sizeof(buf), "Demolish\n+%dW +%dF", (int)(refund.wood*0.95f), (int)(refund.food*0.95f));
            if(draw_button(buf, pad, by_start+(int)(80*sc), (int)(115*sc), (int)(36*sc), true)){
                int sell_id = ui->sel_building;
                ui->sel_building = -1;
                if(g_net_active){
                    NetPacket pkt={0}; pkt.type=PKT_DELETE_BLD;
                    pkt.player=lp; pkt.target_id=sell_id;
                    net_dispatch_packet(gs,&pkt);
                } else {
                    building_sell(gs, sell_id);
                }
            }
        }
        return;
    }

    if(ui->sel_count==0){
        if(ui->sel_building<0 && ui->sel_tile_x>=0 && ui->sel_tile_y>=0 && map_in_bounds(ui->sel_tile_x,ui->sel_tile_y)){
            Tile *t=&gs->map[ui->sel_tile_y][ui->sel_tile_x];
            if(t->type==TILE_FOREST||t->type==TILE_GOLD||t->type==TILE_STONE||t->type==TILE_BERRIES||t->type==TILE_FARM){
                static const char *TILE_LABEL[]={"Grass","Water","Forest","Gold Deposit","Stone Deposit","Berry Bush","Farmland"};
                static Color TILE_COLOR[]={{80,120,60,255},{60,100,170,255},{60,130,50,255},{210,175,30,255},{160,155,140,255},{180,60,80,255},{160,140,80,255}};
                Color col=TILE_COLOR[t->type];
                DrawText(TILE_LABEL[t->type],pad,by_start+(int)(8*sc),fs16+4,col);
                char rbuf[48];
                static const char *RNAME[]={"Food","Wood","Gold","Stone"};
                ResType rtype;
                switch(t->type){
                    case TILE_FOREST: rtype=RES_WOOD; break;
                    case TILE_GOLD:   rtype=RES_GOLD; break;
                    case TILE_STONE:  rtype=RES_STONE; break;
                    default:          rtype=RES_FOOD; break;
                }
                snprintf(rbuf,sizeof(rbuf),"%s remaining: %d",RNAME[rtype],t->resource_amt);
                DrawText(rbuf,pad,by_start+(int)(30*sc),fs12,CLITERAL(Color){200,185,140,220});
                static const int MAX_AMT[]={0,0,250,900,800,500,400};
                int maxv=MAX_AMT[t->type]; if(maxv<=0) maxv=500;
                int bar_w=(int)(200*sc), bar_h=(int)(8*sc);
                int bar_val=(int)((float)bar_w*((float)t->resource_amt/(float)maxv));
                if(bar_val<0)bar_val=0; if(bar_val>bar_w)bar_val=bar_w;
                DrawRectangle(pad,by_start+(int)(46*sc),bar_w,bar_h,CLITERAL(Color){20,18,12,220});
                DrawRectangle(pad,by_start+(int)(46*sc),bar_val,bar_h,col);
                DrawRectangleLinesEx((Rectangle){(float)pad,(float)(by_start+(int)(46*sc)),(float)bar_w,(float)bar_h},1,CLITERAL(Color){80,70,50,200});
                DrawText("Tap to inspect  |  Select villager + tap to gather",pad,by_start+(int)(62*sc),fs10,CLITERAL(Color){90,80,55,180});
                draw_sandbox_tools(gs, ui, panel_w, by_start);
                return;
            }
        }
        DrawText("No units selected",pad,by_start+(int)(8*sc),fs13,CLITERAL(Color){100,90,65,200});
        DrawText("Tap unit/building to select  |  Drag to box-select",pad,by_start+(int)(28*sc),fs11,CLITERAL(Color){90,80,55,180});
        DrawText("B: build menu  |  WASD: scroll  |  Pinch: zoom",pad,by_start+(int)(44*sc),fs11,CLITERAL(Color){90,80,55,180});
        draw_sandbox_tools(gs, ui, panel_w, by_start);
        return;
    }

    if(ui->sel_count==1){
        Unit *u=&gs->units[ui->sel_units[0]];
        static const char *ST[]={"Idle","Moving","Gathering","Returning","Building","Attacking","Dying","Dead"};
        DrawText(unit_name(u->type),pad,by_start+(int)(8*sc),fs16,CLITERAL(Color){220,200,155,255});
        snprintf(buf,sizeof(buf),"HP: %d/%d  Atk: %d  Armor: %d",u->hp,u->max_hp,u->attack_dmg,u->armor);
        DrawText(buf,pad,by_start+(int)(28*sc),fs12,CLITERAL(Color){180,165,130,255});
        if(u->player == net_get_local_player() && u->type != UNIT_VILLAGER && u->type != UNIT_SCOUT){
            snprintf(buf,sizeof(buf),"State: %s   Stance: %s",ST[u->state], u->stance_manual ? "Manual" : "Auto");
        } else {
            snprintf(buf,sizeof(buf),"State: %s",ST[u->state]);
        }
        DrawText(buf,pad,by_start+(int)(44*sc),fs12,CLITERAL(Color){160,145,110,255});
        if(u->type==UNIT_VILLAGER && u->carry_amt>0){
            static const char *RT[]={"Food","Wood","Gold","Stone"};
            snprintf(buf,sizeof(buf),"Carrying: %d %s",u->carry_amt,RT[u->carry_type]);
            DrawText(buf,pad,by_start+(int)(60*sc),fs12,C_GOLD);
        }
        int portrait=(int)(48*sc);
        DrawRectangle(panel_w-portrait-pad,by_start+(int)(8*sc),portrait,portrait,CLITERAL(Color){35,28,16,255});
        DrawRectangleLinesEx((Rectangle){(float)(panel_w-portrait-pad),(float)(by_start+(int)(8*sc)),(float)portrait,(float)portrait},1.5f,C_HUD_LINE);
        DrawCircle(panel_w-portrait/2-pad,by_start+(int)(8*sc)+(int)(16*sc),(int)(8*sc),CLITERAL(Color){220,185,145,255});
        DrawRectangle(panel_w-portrait/2-pad-(int)(6*sc),by_start+(int)(8*sc)+(int)(27*sc),(int)(12*sc),(int)(14*sc),player_color(u->player));
    } else {
        snprintf(buf,sizeof(buf),"%d units selected",ui->sel_count);
        DrawText(buf,pad,by_start+(int)(8*sc),fs14,C_HUD_TXT);
        int uw=(int)(22*sc);
        for(int i=0;i<ui->sel_count&&i<12;i++){
            Unit *u=&gs->units[ui->sel_units[i]];
            Color mc=player_color(u->player);
            DrawRectangle(pad+i*uw,by_start+(int)(30*sc),(int)(18*sc),(int)(18*sc),mc);
            DrawRectangleLinesEx((Rectangle){(float)(pad+i*uw),(float)(by_start+(int)(30*sc)),(float)(int)(18*sc),(float)(int)(18*sc)},1,C_HUD_LINE);
            float frac=(float)u->hp/u->max_hp;
            DrawRectangle(pad+i*uw,by_start+(int)(50*sc),(int)(18*sc),(int)(3*sc),CLITERAL(Color){30,30,30,200});
            DrawRectangle(pad+i*uw,by_start+(int)(50*sc),(int)((18*sc)*frac),(int)(3*sc),frac>0.5f?CLITERAL(Color){50,200,60,255}:CLITERAL(Color){210,50,40,255});
        }
    }
    if(ui->sel_count>=1){
        bool vil=false;
        for(int i=0;i<ui->sel_count;i++) if(gs->units[ui->sel_units[i]].type==UNIT_VILLAGER){vil=true;break;}
        int bb_h=(int)(36*sc), bb_w=(int)(90*sc);
        if(vil){
            bool menu_active = ui->build_panel_open || gs->build_mode.active;
            if(draw_button(menu_active?"[B] Cancel":"[B] Build",pad,by_start+(int)(80*sc),bb_w,bb_h,true)){
                if(menu_active){ ui->build_panel_open=false; gs->build_mode.active=false; }
                else { ui->build_panel_open=true; gs->build_mode.active=false; }
            }
        }
        
        bool military=false;
        bool all_manual=true;
        int lp = net_get_local_player();
        for(int i=0;i<ui->sel_count;i++) {
            Unit *u = &gs->units[ui->sel_units[i]];
            if(u->player == lp && u->type!=UNIT_VILLAGER && u->type!=UNIT_SCOUT) {
                military=true;
                if(!u->stance_manual) all_manual=false;
            }
        }
        if(military){
            int bx2 = vil ? pad+bb_w+(int)(8*sc) : pad;
            const char *lbl = all_manual ? "Stance: Manual" : "Stance: Auto";
            if(draw_button(lbl, bx2, by_start+(int)(80*sc), (int)(120*sc), bb_h, true)){
                bool new_stance = !all_manual;
                if (g_net_active) {
                    NetPacket pkt = {0}; pkt.type = PKT_STANCE; pkt.player = lp;
                    pkt.extra = new_stance ? 1 : 0;
                    for(int i=0; i<ui->sel_count && pkt.unit_count<64; i++){
                        Unit *u = &gs->units[ui->sel_units[i]];
                        if(u->player == lp && u->type != UNIT_VILLAGER && u->type != UNIT_SCOUT){
                            pkt.units[pkt.unit_count++] = ui->sel_units[i];
                        }
                    }
                    net_dispatch_packet(gs, &pkt);
                } else {
                    for(int i=0; i<ui->sel_count; i++){
                        Unit *u = &gs->units[ui->sel_units[i]];
                        if(u->player == lp && u->type != UNIT_VILLAGER && u->type != UNIT_SCOUT){
                            u->stance_manual = new_stance;
                        }
                    }
                }
            }
        }
    }
}

void draw_minimap(GameState *gs, UIState *ui){
    float cx = MINI_X + MINI_SIZE / 2.0f;
    float cy = MINI_Y;
    float half_w = MINI_SIZE / 2.0f;

    Vector2 pt_top = { cx, cy - 2 };
    Vector2 pt_right = { cx + half_w + 2, cy + MINI_SIZE / 2.0f };
    Vector2 pt_bot = { cx, cy + MINI_SIZE + 2 };
    Vector2 pt_left = { cx - half_w - 2, cy + MINI_SIZE / 2.0f };

    DrawTriangle(pt_top, pt_left, pt_right, BLACK);
    DrawTriangle(pt_bot, pt_right, pt_left, BLACK);
    DrawLineEx(pt_top, pt_right, 1.5f, C_HUD_LINE);
    DrawLineEx(pt_right, pt_bot, 1.5f, C_HUD_LINE);
    DrawLineEx(pt_bot, pt_left, 1.5f, C_HUD_LINE);
    DrawLineEx(pt_left, pt_top, 1.5f, C_HUD_LINE);

    #define MAP_TO_MINI(tx, ty, out_x, out_y) do { \
        float iso_x = ((tx) - (ty)) / (float)MAP_W; \
        float iso_y = ((tx) + (ty)) / (float)(MAP_W + MAP_H); \
        (out_x) = cx + iso_x * half_w; \
        (out_y) = cy + iso_y * MINI_SIZE; \
    } while(0)

    int lp = net_get_local_player();
    for(int y=0;y<MAP_H;y++) for(int x=0;x<MAP_W;x++){
        FogState fs=gs->map[y][x].fog[lp];
        if(fs==FOG_HIDDEN) continue;
        Color c;
        switch(gs->map[y][x].type){
            case TILE_GRASS:   c=CLITERAL(Color){55,100,38,255}; break;
            case TILE_WATER:   c=CLITERAL(Color){30,80,160,255}; break;
            case TILE_FOREST:  c=CLITERAL(Color){22,60,22,255};  break;
            case TILE_GOLD:    c=CLITERAL(Color){200,170,20,255};break;
            case TILE_STONE:   c=CLITERAL(Color){140,130,120,255};break;
            case TILE_BERRIES: c=CLITERAL(Color){160,30,30,255}; break;
            default:           c=CLITERAL(Color){130,100,50,255};break;
        }
        if(fs==FOG_EXPLORED){ c.r/=2;c.g/=2;c.b/=2; }
        float dx, dy;
        MAP_TO_MINI(x, y, dx, dy);
        DrawRectangle((int)dx, (int)dy, 2, 2, c);
    }
    for(int i=0;i<MAX_BUILDINGS;i++){
        Building *b=&gs->buildings[i];
        if(!b->active) continue;
        FogState fs=gs->map[clampi(b->ty,0,MAP_H-1)][clampi(b->tx,0,MAP_W-1)].fog[lp];
        if(fs==FOG_HIDDEN&&b->player!=lp) continue;
        Color c=player_color(b->player);
        float dx, dy;
        MAP_TO_MINI(b->tx + b->tw*0.5f, b->ty + b->th*0.5f, dx, dy);
        int bw = (int)(b->tw * (half_w / MAP_W));
        if(bw < 2) bw = 2;
        DrawRectangle((int)dx - bw/2, (int)dy - bw/2, bw, bw, c);
    }
    for(int i=0;i<MAX_UNITS;i++){
        Unit *u=&gs->units[i];
        if(!u->active||u->state==US_DEAD) continue;
        int utx=(int)(u->wx/TILE_SIZE),uty=(int)(u->wy/TILE_SIZE);
        FogState fs=gs->map[clampi(uty,0,MAP_H-1)][clampi(utx,0,MAP_W-1)].fog[lp];
        if(fs==FOG_HIDDEN&&u->player!=lp) continue;
        Color c=player_color(u->player);
        float dx, dy;
        MAP_TO_MINI(utx, uty, dx, dy);
        DrawRectangle((int)dx, (int)dy, 2, 2, c);
    }

    /* Camera view box */
    Vector2 cam_tl = GetScreenToWorld2D((Vector2){0, 0}, ui->camera);
    Vector2 cam_tr = GetScreenToWorld2D((Vector2){GetScreenWidth(), 0}, ui->camera);
    Vector2 cam_bl = GetScreenToWorld2D((Vector2){0, GetScreenHeight()}, ui->camera);
    Vector2 cam_br = GetScreenToWorld2D((Vector2){GetScreenWidth(), GetScreenHeight()}, ui->camera);

    Vec2 w_tl = iso_to_world(cam_tl.x, cam_tl.y);
    Vec2 w_tr = iso_to_world(cam_tr.x, cam_tr.y);
    Vec2 w_bl = iso_to_world(cam_bl.x, cam_bl.y);
    Vec2 w_br = iso_to_world(cam_br.x, cam_br.y);

    float mini_tl_x, mini_tl_y; MAP_TO_MINI(w_tl.x / TILE_SIZE, w_tl.y / TILE_SIZE, mini_tl_x, mini_tl_y);
    float mini_tr_x, mini_tr_y; MAP_TO_MINI(w_tr.x / TILE_SIZE, w_tr.y / TILE_SIZE, mini_tr_x, mini_tr_y);
    float mini_bl_x, mini_bl_y; MAP_TO_MINI(w_bl.x / TILE_SIZE, w_bl.y / TILE_SIZE, mini_bl_x, mini_bl_y);
    float mini_br_x, mini_br_y; MAP_TO_MINI(w_br.x / TILE_SIZE, w_br.y / TILE_SIZE, mini_br_x, mini_br_y);

    DrawLineEx((Vector2){mini_tl_x, mini_tl_y}, (Vector2){mini_tr_x, mini_tr_y}, 1.5f, CLITERAL(Color){220,200,150,200});
    DrawLineEx((Vector2){mini_tr_x, mini_tr_y}, (Vector2){mini_br_x, mini_br_y}, 1.5f, CLITERAL(Color){220,200,150,200});
    DrawLineEx((Vector2){mini_br_x, mini_br_y}, (Vector2){mini_bl_x, mini_bl_y}, 1.5f, CLITERAL(Color){220,200,150,200});
    DrawLineEx((Vector2){mini_bl_x, mini_bl_y}, (Vector2){mini_tl_x, mini_tl_y}, 1.5f, CLITERAL(Color){220,200,150,200});

    /* Minimap clicking/tapping to move camera */
    bool minimap_pressed = IsMouseButtonDown(MOUSE_LEFT_BUTTON) || GetTouchPointCount() > 0;
    if(minimap_pressed){
        Vector2 mp = GetTouchPointCount() > 0 ? GetTouchPosition(0) : GetMousePosition();
        if(mp.x >= MINI_X && mp.x <= MINI_X+MINI_SIZE && mp.y >= MINI_Y && mp.y <= MINI_Y+MINI_SIZE){
            float dx = mp.x - cx;
            float dy = mp.y - cy;
            float iso_x = dx / half_w;
            float iso_y = dy / MINI_SIZE;
            
            float target_tx = (iso_y * (MAP_W + MAP_H) + iso_x * MAP_W) / 2.0f;
            float target_ty = (iso_y * (MAP_W + MAP_H) - iso_x * MAP_W) / 2.0f;
            target_tx = clampf(target_tx, 0, MAP_W-1);
            target_ty = clampf(target_ty, 0, MAP_H-1);
            Vec2 new_iso = world_to_iso(target_tx * TILE_SIZE, target_ty * TILE_SIZE);
            ui->camera.target = to_rvec2(new_iso);
        }
    }
}
