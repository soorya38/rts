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

void draw_top_bar(GameState *gs, UIState *ui){
    (void)ui;
    int lp = net_get_local_player();
    PlayerRes *pr=&gs->res[lp];
    DrawRectangle(0,0,SCREEN_W,HUD_TOP_H,C_HUD_BG);
    DrawRectangle(0,HUD_TOP_H-1,SCREEN_W,2,C_HUD_LINE);
    char buf[32];
    int cx=10;
    int f0=cx; draw_food_icon(cx,11); cx+=20;
    snprintf(buf,sizeof(buf),"%d",pr->amount[RES_FOOD]);
    DrawText(buf,cx,14,14,C_FOOD); int f1=cx+MeasureText(buf,14); cx=f1+18;
    int w0=cx; draw_wood_icon(cx,11); cx+=20;
    snprintf(buf,sizeof(buf),"%d",pr->amount[RES_WOOD]);
    DrawText(buf,cx,14,14,C_WOOD); int w1=cx+MeasureText(buf,14); cx=w1+18;
    int g0=cx; draw_gold_icon(cx,11); cx+=20;
    snprintf(buf,sizeof(buf),"%d",pr->amount[RES_GOLD]);
    DrawText(buf,cx,14,14,C_GOLD); int g1=cx+MeasureText(buf,14); cx=g1+18;
    int s0=cx; draw_stone_icon(cx,11); cx+=20;
    snprintf(buf,sizeof(buf),"%d",pr->amount[RES_STONE]);
    DrawText(buf,cx,14,14,C_STONE); int s1=cx+MeasureText(buf,14); cx=s1+18;
    int p0=cx;
    Color pc=(pr->population>=pr->pop_cap)?C_POP_WARN:C_POP_OK;
    snprintf(buf,sizeof(buf),"Pop: %d/%d",pr->population,pr->pop_cap);
    DrawText(buf,cx,14,13,pc); int p1=cx+MeasureText(buf,13);
    Vector2 mp = GetMousePosition();
    if(mp.y < HUD_TOP_H){
        if(mp.x >= f0 && mp.x <= f1) draw_tooltip("Food: Used to train villagers and most units", (int)mp.x+5, (int)mp.y+15);
        if(mp.x >= w0 && mp.x <= w1) draw_tooltip("Wood: Used for buildings and archers", (int)mp.x+5, (int)mp.y+15);
        if(mp.x >= g0 && mp.x <= g1) draw_tooltip("Gold: Used for advanced units and aging up", (int)mp.x+5, (int)mp.y+15);
        if(mp.x >= s0 && mp.x <= s1) draw_tooltip("Stone: Used for castles and some defensive buildings", (int)mp.x+5, (int)mp.y+15);
        if(mp.x >= p0 && mp.x <= p1) draw_tooltip("Population: Total units vs capacity", (int)mp.x+5, (int)mp.y+15);
    }
    const char *an=age_names[pr->age];
    if(pr->advancing){
        snprintf(buf,sizeof(buf),"-> %s (%.0fs)",age_names[pr->age+1],pr->advance_timer);
        DrawText(buf,SCREEN_W/2-MeasureText(buf,12)/2,14,12,C_AGE);
    } else {
        DrawText(an,SCREEN_W/2-MeasureText(an,13)/2,14,13,C_AGE);
    }
    if(pr->age<3 && !pr->advancing){
        Cost c=age_advance_cost(pr->age);
        bool can=res_can_afford(pr,c);
        char label[40];
        if(c.gold>0) snprintf(label,sizeof(label),"Advance Age: %dF %dG",c.food,c.gold);
        else         snprintf(label,sizeof(label),"Advance Age: %dF",c.food);
        if(draw_button(label,SCREEN_W-208,6,200,30,can)) {
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
            DrawText(need,SCREEN_W-206,38,10,CLITERAL(Color){220,160,80,220});
        }
    } else if(pr->advancing){
        char buf2[48];
        snprintf(buf2,sizeof(buf2),"Advancing... %.0fs left",pr->advance_timer);
        DrawText(buf2,SCREEN_W-210,12,11,C_AGE);
    }
    int minutes=(int)(gs->game_time/60), seconds=(int)(gs->game_time)%60;
    snprintf(buf,sizeof(buf),"%02d:%02d",minutes,seconds);
    DrawText(buf,SCREEN_W-48,14,12,CLITERAL(Color){140,130,100,255});
}

void draw_bottom_panel(GameState *gs, UIState *ui){
    DrawRectangle(0,HUD_BOT_Y,SCREEN_W-MINI_SIZE-16,HUD_BOT_H,C_HUD_BG);
    DrawRectangle(0,HUD_BOT_Y,SCREEN_W-MINI_SIZE-16,2,C_HUD_LINE);
    int panel_w = SCREEN_W-MINI_SIZE-16;
    char buf[64];

    if(ui->sel_building>=0){
        Building *b=&gs->buildings[ui->sel_building];
        if(!b->active){ui->sel_building=-1;return;}
        static const char *BLD_NAMES[BLD_COUNT]={
            "Town Center","House","Barracks","Archery Range","Stable",
            "Blacksmith","Market",
            "Mill","Lumber Camp","Mining Camp","Farm"
        };
        DrawText(BLD_NAMES[b->type],12,HUD_BOT_Y+8,16,C_HUD_TXT);
        snprintf(buf,sizeof(buf),"HP: %d / %d",b->hp,b->max_hp);
        DrawText(buf,12,HUD_BOT_Y+28,12,CLITERAL(Color){180,165,130,255});
        if(!b->complete){
            int builders=0;
            for(int i=0;i<MAX_UNITS;i++){ Unit *u=&gs->units[i]; if(u->active&&u->build_id==b->id) builders++; }
            DrawRectangle(12,HUD_BOT_Y+44,200,7,CLITERAL(Color){35,28,16,255});
            DrawRectangle(12,HUD_BOT_Y+44,(int)(200*b->construction),7,CLITERAL(Color){200,160,40,255});
            snprintf(buf,sizeof(buf),"Construction: %.0f%%",b->construction*100);
            DrawText(buf,12,HUD_BOT_Y+56,12,CLITERAL(Color){200,180,100,255});
            if(builders==0) DrawText("No builders!  Select a villager and click this building",12,HUD_BOT_Y+72,11,CLITERAL(Color){220,100,60,230});
            else { snprintf(buf,sizeof(buf),"%d builder%s  (%dx speed)",builders,builders>1?"s":"",builders); DrawText(buf,12,HUD_BOT_Y+72,11,CLITERAL(Color){120,200,100,220}); }
            return;
        }
        if(b->active_tech != TECH_NONE){
            float full=tech_time(b->active_tech);
            float prog=1.0f-(b->tech_timer/full);
            snprintf(buf,sizeof(buf),"Researching: %s (%.0fs)",tech_name(b->active_tech),b->tech_timer);
            DrawText(buf,12,HUD_BOT_Y+44,12,CLITERAL(Color){100,180,255,255});
            DrawRectangle(12,HUD_BOT_Y+60,160,6,CLITERAL(Color){20,30,50,255});
            DrawRectangle(12,HUD_BOT_Y+60,(int)(160*prog),6,CLITERAL(Color){60,140,255,255});
        } else if(b->queue_len>0){
            static const char *UN[UNIT_COUNT]={"Villager","Scout","Militia","Man-at-Arms","Archer","Knight"};
            snprintf(buf,sizeof(buf),"Training: %s (%.0fs)",UN[b->queue[0]],b->train_timer);
            DrawText(buf,12,HUD_BOT_Y+44,12,C_GOLD);
            float prog=1.0f-(b->train_timer/building_train_time(b->queue[0]));
            DrawRectangle(12,HUD_BOT_Y+60,160,6,CLITERAL(Color){40,35,20,255});
            DrawRectangle(12,HUD_BOT_Y+60,(int)(160*prog),6,CLITERAL(Color){50,200,60,255});
        }
        int bx=220, by=HUD_BOT_Y+10;
        int lp = net_get_local_player();
        switch(b->type){
            case BLD_TOWN_CENTER:
                if(draw_button("Villager\n50F",bx,by,80,50,gs->res[lp].amount[RES_FOOD]>=50)) {
                    if (g_net_active) {
                        NetPacket pkt = {0}; pkt.type = PKT_TRAIN_UNIT; pkt.player = lp;
                        pkt.target_id = ui->sel_building; pkt.extra = UNIT_VILLAGER;
                        net_dispatch_packet(gs, &pkt);
                    } else building_enqueue_unit(gs,b,UNIT_VILLAGER);
                }
                if(draw_button("Scout\n80F",bx+88,by,80,50,gs->res[lp].amount[RES_FOOD]>=80)) {
                    if (g_net_active) {
                        NetPacket pkt = {0}; pkt.type = PKT_TRAIN_UNIT; pkt.player = lp;
                        pkt.target_id = ui->sel_building; pkt.extra = UNIT_SCOUT;
                        net_dispatch_packet(gs, &pkt);
                    } else building_enqueue_unit(gs,b,UNIT_SCOUT);
                }
                break;
            case BLD_BARRACKS:
                if(draw_button("Militia\n60F 20G",bx,by,90,50,gs->res[lp].amount[RES_FOOD]>=60&&gs->res[lp].amount[RES_GOLD]>=20)) {
                    if (g_net_active) {
                        NetPacket pkt = {0}; pkt.type = PKT_TRAIN_UNIT; pkt.player = lp;
                        pkt.target_id = ui->sel_building; pkt.extra = UNIT_MILITIA;
                        net_dispatch_packet(gs, &pkt);
                    } else building_enqueue_unit(gs,b,UNIT_MILITIA);
                }
                if(gs->res[lp].age>=1&&draw_button("Man@Arms\n60F 20G",bx+98,by,90,50,gs->res[lp].amount[RES_FOOD]>=60&&gs->res[lp].amount[RES_GOLD]>=20)) {
                     if (g_net_active) {
                        NetPacket pkt = {0}; pkt.type = PKT_TRAIN_UNIT; pkt.player = lp;
                        pkt.target_id = ui->sel_building; pkt.extra = UNIT_MAN_AT_ARMS;
                        net_dispatch_packet(gs, &pkt);
                    } else building_enqueue_unit(gs,b,UNIT_MAN_AT_ARMS);
                }
                break;
            case BLD_ARCHERY_RANGE:
                if(draw_button("Archer\n25W 45G",bx,by,90,50,gs->res[lp].amount[RES_WOOD]>=25&&gs->res[lp].amount[RES_GOLD]>=45)) {
                    if (g_net_active) {
                        NetPacket pkt = {0}; pkt.type = PKT_TRAIN_UNIT; pkt.player = lp;
                        pkt.target_id = ui->sel_building; pkt.extra = UNIT_ARCHER;
                        net_dispatch_packet(gs, &pkt);
                    } else building_enqueue_unit(gs,b,UNIT_ARCHER);
                }
                break;
            case BLD_STABLE:
                if(draw_button("Knight\n60F 75G",bx,by,90,50,gs->res[lp].amount[RES_FOOD]>=60&&gs->res[lp].amount[RES_GOLD]>=75)) {
                    if (g_net_active) {
                        NetPacket pkt = {0}; pkt.type = PKT_TRAIN_UNIT; pkt.player = lp;
                        pkt.target_id = ui->sel_building; pkt.extra = UNIT_KNIGHT;
                        net_dispatch_packet(gs, &pkt);
                    } else building_enqueue_unit(gs,b,UNIT_KNIGHT);
                }
                break;
            case BLD_BLACKSMITH:
                DrawText("No units  —  research upgrades below",bx,by+18,11,CLITERAL(Color){160,145,110,200});
                break;
            case BLD_MARKET: {
                /* Trade buttons: sell resources for gold, buy food with gold */
                bool can_w = (gs->res[lp].amount[RES_WOOD]  >= 100);
                bool can_f = (gs->res[lp].amount[RES_FOOD]  >= 100);
                bool can_g = (gs->res[lp].amount[RES_GOLD]  >= 100);
                if(draw_button("100W -> 75G", bx,     by, 100, 42, can_w && !g_net_active)){
                    res_deduct(&gs->res[lp], (Cost){0,100,0,0});
                    res_add   (&gs->res[lp], RES_GOLD, 75);
                }
                if(draw_button("100F -> 50G", bx+108, by, 100, 42, can_f && !g_net_active)){
                    res_deduct(&gs->res[lp], (Cost){100,0,0,0});
                    res_add   (&gs->res[lp], RES_GOLD, 50);
                }
                if(draw_button("100G -> 150F",bx+216, by, 106, 42, can_g && !g_net_active)){
                    res_deduct(&gs->res[lp], (Cost){0,0,100,0});
                    res_add   (&gs->res[lp], RES_FOOD, 150);
                }
                if(g_net_active)
                    DrawText("(Solo only)", bx, by+46, 9, CLITERAL(Color){180,120,60,200});
                break;
            }
            default: break;
        }
        /* Research tech buttons by building type */
        bool busy = (b->active_tech != TECH_NONE || b->queue_len > 0);
        if(b->player == lp){
            TechType techs[2] = {TECH_NONE, TECH_NONE};
            int tc = 0;
            if(b->type == BLD_MILL){ techs[0]=TECH_CROP_ROTATION; techs[1]=TECH_FERTILIZER; tc=2; }
            else if(b->type == BLD_BARRACKS){ techs[0]=TECH_IRON_WEAPONRY; tc=1; }
            else if(b->type == BLD_ARCHERY_RANGE){ techs[0]=TECH_COMPOSITE_BOWS; tc=1; }
            else if(b->type == BLD_STABLE){ techs[0]=TECH_MOUNTED_ARMOR; tc=1; }
            else if(b->type == BLD_BLACKSMITH){ techs[0]=TECH_SCALE_ARMOR; techs[1]=TECH_FORGED_ARROWS; tc=2; }
            for(int ti=0; ti<tc; ti++){
                TechType tt = techs[ti];
                int tbx = bx + ti*112;
                if(!gs->res[lp].tech_unlocked[tt]){
                    Cost tc2 = tech_cost(tt);
                    bool can = res_can_afford(&gs->res[lp], tc2) && !busy;
                    char tbuf[80];
                    snprintf(tbuf,sizeof(tbuf),"%s",tech_name(tt));
                    if(draw_button(tbuf, tbx, by+58, 107, 40, can)){
                        if(g_net_active){
                            NetPacket pkt={0}; pkt.type=PKT_RESEARCH;
                            pkt.player=lp; pkt.target_id=ui->sel_building; pkt.extra=(int32_t)tt;
                            net_dispatch_packet(gs,&pkt);
                        } else building_start_tech(gs,b,tt);
                    }
                    DrawText(tech_desc(tt), tbx, by+100, 10, CLITERAL(Color){160,200,255,200});
                } else {
                    DrawText(tech_name(tt), tbx, by+62, 10, CLITERAL(Color){80,220,100,220});
                    DrawText("[Researched]", tbx, by+74, 9, CLITERAL(Color){60,180,80,200});
                }
            }
        }

        /* Sell button – shown for own complete buildings (not Town Center) */
        if(b->player == lp && b->complete && b->type != BLD_TOWN_CENTER){
            Cost refund = building_cost(b->type);
            snprintf(buf, sizeof(buf), "Demolish\n+%dW +%dF", (int)(refund.wood*0.95f), (int)(refund.food*0.95f));
            if(draw_button(buf, 12, HUD_BOT_Y+80, 115, 36, true)){
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
                DrawText(TILE_LABEL[t->type],12,HUD_BOT_Y+8,18,col);
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
                DrawText(rbuf,12,HUD_BOT_Y+30,12,CLITERAL(Color){200,185,140,220});
                static const int MAX_AMT[]={0,0,250,900,800,500,400};
                int maxv=MAX_AMT[t->type]; if(maxv<=0) maxv=500;
                int bar_w=(int)(200.0f*((float)t->resource_amt/(float)maxv));
                if(bar_w<0)bar_w=0; if(bar_w>200)bar_w=200;
                DrawRectangle(12,HUD_BOT_Y+46,200,8,CLITERAL(Color){20,18,12,220});
                DrawRectangle(12,HUD_BOT_Y+46,bar_w,8,col);
                DrawRectangleLinesEx((Rectangle){12,HUD_BOT_Y+46,200,8},1,CLITERAL(Color){80,70,50,200});
                DrawText("Click to inspect  |  Select villager + click to gather",12,HUD_BOT_Y+62,10,CLITERAL(Color){90,80,55,180});
                return;
            }
        }
        DrawText("No units selected",12,HUD_BOT_Y+8,13,CLITERAL(Color){100,90,65,200});
        DrawText("Click unit/building to select  |  Drag to box-select",12,HUD_BOT_Y+28,11,CLITERAL(Color){90,80,55,180});
        DrawText("B: build menu  |  WASD: scroll  |  Mouse wheel: zoom",12,HUD_BOT_Y+44,11,CLITERAL(Color){90,80,55,180});
        return;
    }

    if(ui->sel_count==1){
        Unit *u=&gs->units[ui->sel_units[0]];
        static const char *UN[UNIT_COUNT]={"Villager","Scout","Militia","Man-at-Arms","Archer","Knight"};
        static const char *ST[]={"Idle","Moving","Gathering","Returning","Building","Attacking","Dying","Dead"};
        DrawText(UN[u->type],12,HUD_BOT_Y+8,16,CLITERAL(Color){220,200,155,255});
        snprintf(buf,sizeof(buf),"HP: %d/%d  Atk: %d  Armor: %d",u->hp,u->max_hp,u->attack_dmg,u->armor);
        DrawText(buf,12,HUD_BOT_Y+28,12,CLITERAL(Color){180,165,130,255});
        if(u->player == net_get_local_player() && u->type != UNIT_VILLAGER && u->type != UNIT_SCOUT){
            snprintf(buf,sizeof(buf),"State: %s   Stance: %s",ST[u->state], u->stance_manual ? "Manual" : "Auto");
        } else {
            snprintf(buf,sizeof(buf),"State: %s",ST[u->state]);
        }
        DrawText(buf,12,HUD_BOT_Y+44,12,CLITERAL(Color){160,145,110,255});
        if(u->type==UNIT_VILLAGER && u->carry_amt>0){
            static const char *RT[]={"Food","Wood","Gold","Stone"};
            snprintf(buf,sizeof(buf),"Carrying: %d %s",u->carry_amt,RT[u->carry_type]);
            DrawText(buf,12,HUD_BOT_Y+60,12,C_GOLD);
        }
        DrawRectangle(panel_w-60,HUD_BOT_Y+8,48,48,CLITERAL(Color){35,28,16,255});
        DrawRectangleLinesEx((Rectangle){(float)(panel_w-60),(float)(HUD_BOT_Y+8),48,48},1.5f,C_HUD_LINE);
        DrawCircle(panel_w-36,HUD_BOT_Y+24,8,CLITERAL(Color){220,185,145,255});
        DrawRectangle(panel_w-42,HUD_BOT_Y+35,12,14,player_color(u->player));
    } else {
        snprintf(buf,sizeof(buf),"%d units selected",ui->sel_count);
        DrawText(buf,12,HUD_BOT_Y+8,14,C_HUD_TXT);
        for(int i=0;i<ui->sel_count&&i<12;i++){
            Unit *u=&gs->units[ui->sel_units[i]];
            Color mc=player_color(u->player);
            DrawRectangle(12+i*22,HUD_BOT_Y+30,18,18,mc);
            DrawRectangleLinesEx((Rectangle){12.0f+i*22,HUD_BOT_Y+30.0f,18,18},1,C_HUD_LINE);
            float frac=(float)u->hp/u->max_hp;
            DrawRectangle(12+i*22,HUD_BOT_Y+50,18,3,CLITERAL(Color){30,30,30,200});
            DrawRectangle(12+i*22,HUD_BOT_Y+50,(int)(18*frac),3,frac>0.5f?CLITERAL(Color){50,200,60,255}:CLITERAL(Color){210,50,40,255});
        }
    }
    if(ui->sel_count>=1){
        bool vil=false;
        for(int i=0;i<ui->sel_count;i++) if(gs->units[ui->sel_units[i]].type==UNIT_VILLAGER){vil=true;break;}
        if(vil){
            bool menu_active = ui->build_panel_open || gs->build_mode.active;
            if(draw_button(menu_active?"[B] Cancel":"[B] Build",12,HUD_BOT_Y+80,90,36,true)){
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
            int bx = vil ? 110 : 12;
            const char *lbl = all_manual ? "Stance: Manual" : "Stance: Auto";
            if(draw_button(lbl, bx, HUD_BOT_Y+80, 120, 36, true)){
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

    /* Helper macro to map world tile coordinates to minimap screen coords */
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
    float cam_w = SCREEN_W / ui->camera.zoom;
    float cam_h = SCREEN_H / ui->camera.zoom;
    
    /* Calculate corners of the camera view in world coordinates */
    /* Target in ui->camera.target is already in ISO space!
       Let's convert it back to world space to map to the minimap. */
    Vec2 world_center = iso_to_world(ui->camera.target.x, ui->camera.target.y);
    float t_cx = world_center.x / TILE_SIZE;
    float t_cy = world_center.y / TILE_SIZE;

    /* To draw a proper camera quad on the minimap, we need the 4 corners of the screen in world coords */
    Vector2 cam_tl = GetScreenToWorld2D((Vector2){0, 0}, ui->camera);
    Vector2 cam_tr = GetScreenToWorld2D((Vector2){SCREEN_W, 0}, ui->camera);
    Vector2 cam_bl = GetScreenToWorld2D((Vector2){0, SCREEN_H}, ui->camera);
    Vector2 cam_br = GetScreenToWorld2D((Vector2){SCREEN_W, SCREEN_H}, ui->camera);

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

    /* Minimap clicking to move camera */
    bool minimap_pressed = IsMouseButtonDown(MOUSE_LEFT_BUTTON) || GetTouchPointCount() > 0;
    if(minimap_pressed){
        Vector2 mp = GetTouchPointCount() > 0 ? GetTouchPosition(0) : GetMousePosition();
        /* Check if inside the diamond bounding box */
        if(mp.x >= MINI_X && mp.x <= MINI_X+MINI_SIZE && mp.y >= MINI_Y && mp.y <= MINI_Y+MINI_SIZE){
            /* Inverse map the point */
            float dx = mp.x - cx;
            float dy = mp.y - cy;
            /* iso_x = dx / half_w; iso_y = dy / MINI_SIZE; 
               iso_x = (x - y)/W; iso_y = (x + y)/(W+H); */
               
            float iso_x = dx / half_w;
            float iso_y = dy / MINI_SIZE;
            
            float target_tx = (iso_y * (MAP_W + MAP_H) + iso_x * MAP_W) / 2.0f;
            float target_ty = (iso_y * (MAP_W + MAP_H) - iso_x * MAP_W) / 2.0f;

            /* clamp */
            target_tx = clampf(target_tx, 0, MAP_W-1);
            target_ty = clampf(target_ty, 0, MAP_H-1);

            /* set target */
            Vec2 new_iso = world_to_iso(target_tx * TILE_SIZE, target_ty * TILE_SIZE);
            ui->camera.target = to_rvec2(new_iso);
        }
    }
}
