/* anim.c - algorithm visualizer breakout state (ST_ANIM)
   intro card -> animated chart + live kernel-trace panel -> result, in horror/hacker style */
#include "app.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

#define INTRO_MS 1300.0

static const char *TAUNTS[5]={
    "i see your page table",
    "nice frame. mine now.",
    "you are not in control",
    "your cache belongs to me",
    "every fault feeds me",
};

static int clampi(int v,int lo,int hi){ return v<lo?lo:(v>hi?hi:v); }
static int imin(int a,int b){ return a<b?a:b; }

/* draw a label but keep it inside [xlo,xhi] so it never clips off-panel */
static void draw_label_clamped(Framebuffer *fb,int x,int y,const char *s,uint32_t c,int xlo,int xhi){
    int w=(int)strlen(s)*fb->ch_w;
    if(x+w>xhi) x=xhi-w;
    if(x<xlo) x=xlo;
    fb_text(fb,x,y,s,c);
}

/* raw font px needed so `count` cells of `chars` glyphs each fit in `avail` px
   (may be < FONT_MIN_PX, which signals "switch to a scrolling viewport"). */
static int need_px_w(Framebuffer *fb,int avail,int count,int chars){
    if(count<1||chars<1) return fb->base_fh;
    long denom=(long)count*chars*(fb->bch_w>0?fb->bch_w:1);
    if(denom<1) return fb->base_fh;
    return (int)((long)avail*fb->base_fh/denom);
}
/* largest font px so `rows` text rows fit in `avail` px, clamped to [MIN, base] */
static int fit_px_rows(Framebuffer *fb,int avail,int rows,int base_px){
    if(rows<1) return base_px;
    int px=avail/rows-2;
    if(px>base_px)px=base_px;
    if(px<FONT_MIN_PX)px=FONT_MIN_PX;
    return px;
}

static uint32_t pcol(int id){
    static const uint32_t P[8]={0x0000FF66,0x0000FFEE,0x00FFB000,0x00FF6699,
                                0x0066AAFF,0x00AAFF66,0x00FF8844,0x00CC88FF};
    if(id<1) return COL_GRAY;
    return P[(id-1)&7];
}

/* ------------------------------------------------------------------ */
/* trace + summary builders                                           */
/* ------------------------------------------------------------------ */

void anim_trace(App *a, double at_clock, uint32_t col, const char *fmt, ...){
    Anim *an=&a->anim;
    if(an->ntrace>=ANIM_TRACE_MAX) return;
    va_list ap; va_start(ap,fmt);
    vsnprintf(an->trace[an->ntrace], ANIM_TRACE_W, fmt, ap);
    va_end(ap);
    an->trace_col[an->ntrace]=col;
    an->trace_at[an->ntrace]=at_clock;
    an->ntrace++;
}

void anim_summary(App *a, uint32_t col, const char *fmt, ...){
    Anim *an=&a->anim;
    if(an->nsummary>=8) return;
    va_list ap; va_start(ap,fmt);
    vsnprintf(an->summary[an->nsummary], TERM_MAXLINE, fmt, ap);
    va_end(ap);
    an->summary_col[an->nsummary]=col;
    an->nsummary++;
}

static void build_sched(App *a){
    Anim *an=&a->anim;
    int swt=0,stat=0,sbt=0;
    for(int k=0;k<an->sched.nseg;k++){
        int p=an->sched.seg_pid[k], a0=an->sched.seg_a[k];
        if(p<0){ anim_trace(a,a0,COL_GRAY,"[!] t=%02d CPU idle -- hlt",a0); continue; }
        anim_trace(a,a0,COL_AMBER,"[*] t=%02d SWITCH -> P%d",a0,p);
        anim_trace(a,a0,COL_DGREEN,"[+]   ctx_save eax,ebx,esp");
        anim_trace(a,a0,COL_DGREEN,"      P%d: mov ecx,[burst]",p);
        anim_trace(a,a0,COL_DGREEN,"          dec ecx ; jnz .run");
    }
    for(int i=0;i<an->sched.n;i++){ swt+=an->sched.wait[i]; stat+=an->sched.tat[i]; sbt+=an->sched.bt[i]; }
    double aw=(double)swt/an->sched.n, at=(double)stat/an->sched.n;
    int mk=an->sched.makespan;
    anim_summary(a,COL_CYAN, "[%s] avg wait=%.2f  avg TAT=%.2f  util=%.0f%%  makespan=%d",
                 an->title, aw, at, mk?100.0*sbt/mk:0, mk);
}

static void build_vmem(App *a){
    Anim *an=&a->anim;
    for(int i=0;i<an->vmem.n;i++){
        if(an->vmem.hit[i]){ anim_trace(a,i,COL_DGREEN,"[+] %02d ref %d  HIT (resident)",i,an->vmem.ref[i]); }
        else {
            anim_trace(a,i,COL_RED,"[!] %02d #PF page %d (invalid)",i,an->vmem.ref[i]);
            if(an->vmem.victim[i]>=0) anim_trace(a,i,COL_AMBER,"[>>]    evict pg %d -> swap out",an->vmem.victim[i]);
            anim_trace(a,i,COL_DGREEN,"[+]    load pg %d <- disk; valid=1",an->vmem.ref[i]);
        }
    }
    anim_summary(a,COL_CYAN,"[%s] page faults = %d / %d  (rate %.1f%%)",
                 an->title, an->vmem.faults, an->vmem.n, 100.0*an->vmem.faults/an->vmem.n);
}

static void build_disk(App *a){
    Anim *an=&a->anim;
    for(int i=1;i<an->disk.np;i++){
        int mv=an->disk.path[i]-an->disk.path[i-1]; if(mv<0)mv=-mv;
        if(an->disk.isreq[i]) anim_trace(a,i,COL_DGREEN,"[>>] seek %d -> %d (+%d) READ",
                              an->disk.path[i-1],an->disk.path[i],mv);
        else anim_trace(a,i,COL_GRAY,"[>>] seek %d -> %d (+%d)",
                              an->disk.path[i-1],an->disk.path[i],mv);
    }
    anim_summary(a,COL_CYAN,"[%s] total head movement = %d tracks", an->title, an->disk.total);
}

static void build_mem(App *a){
    Anim *an=&a->anim;
    if(an->mem.paging){
        for(int i=0;i<an->mem.nconv;i++)
            anim_trace(a,i,COL_DGREEN,"[+] xlate LA=%d: p=%d d=%d -> f=%d PA=%d",
                       an->mem.la[i],an->mem.pg[i],an->mem.doff[i],an->mem.fr[i],an->mem.pa[i]);
        anim_summary(a,COL_CYAN,"[paging] translated %d addresses (PA=f*P+d)",an->mem.nconv);
        return;
    }
    for(int i=0;i<an->mem.nstep;i++){
        if(an->mem.step_region[i]<0)
            anim_trace(a,i,COL_RED,"[!] alloc(%c,%dK) NO FIT -> requeue",an->mem.step_jid[i],an->mem.step_job[i]);
        else
            anim_trace(a,i,COL_DGREEN,"[+] alloc(%c,%dK) -> region[%d] @0x%05X frag %dK",
                       an->mem.step_jid[i],an->mem.step_job[i],an->mem.step_region[i],
                       0x40000 + an->mem.step_region[i]*0x1000, an->mem.step_frag[i]);
    }
    anim_summary(a,COL_CYAN,"[%s] regions used=%d/%d  total internal frag=%dK",
                 an->title, an->mem.used, an->mem.nreg, an->mem.totfrag);
}

/* ------------------------------------------------------------------ */
/* lifecycle                                                          */
/* ------------------------------------------------------------------ */

static double clock_max_of(Anim *an){
    switch(an->kind){
    case AV_SCHED: return an->sched.makespan>0?an->sched.makespan:1;
    case AV_VMEM:  return an->vmem.n>0?an->vmem.n:1;
    case AV_DISK:  return an->disk.np>1?an->disk.np-1:1;
    case AV_MEM:   return an->mem.paging? (an->mem.nconv>0?an->mem.nconv:1)
                                        : (an->mem.nstep>0?an->mem.nstep:1);
    case AV_CALC:  return an->calc.nline>0?an->calc.nline:1;
    }
    return 1;
}

static double speed_of(Anim *an){
    switch(an->kind){
    case AV_SCHED: return 4.0;
    case AV_VMEM:  return 2.2;
    case AV_DISK:  return 1.6;
    case AV_MEM:   return an->mem.paging?1.0:1.4;
    case AV_CALC:  return 1.2;
    }
    return 2.0;
}

void anim_begin(App *a, AnimKind kind, const char *title, const char *subtitle){
    Anim *an=&a->anim;
    an->kind=kind;
    snprintf(an->title,64,"%s",title);
    snprintf(an->subtitle,96,"%s",subtitle?subtitle:"");
    an->phase=0; an->phase_time=0; an->clock=0; an->paused=0; an->step_req=0;
    an->flash=0; an->glitch=0; an->ntrace=0; an->nsummary=0;
    an->clock_max=clock_max_of(an);
    switch(kind){
    case AV_SCHED: build_sched(a); break;
    case AV_VMEM:  build_vmem(a);  break;
    case AV_DISK:  build_disk(a);  break;
    case AV_MEM:   build_mem(a);   break;
    case AV_CALC:  anim_summary(a,COL_CYAN,"%s = %s",an->title,an->calc.result); break;
    }
    audio_sfx(SFX_DECRYPT,0);   /* "decrypting payload" launch blip */
    a->state=ST_ANIM; a->state_time=0;
}

void anim_exit(App *a){
    Anim *an=&a->anim;
    term_print(&a->term, COL_DGREEN, "  [visualizer closed]");
    for(int i=0;i<an->nsummary;i++)
        term_print(&a->term, an->summary_col[i], "%s", an->summary[i]);
    a->state=ST_TERMINAL; a->state_time=0;
}

static void fire_event(App *a, int step){
    Anim *an=&a->anim;
    if(an->kind==AV_VMEM){
        int i=clampi(step,0,an->vmem.n-1);
        if(!an->vmem.hit[i]){ an->flash=260; an->glitch=140; audio_sfx(SFX_PAGEFAULT,0); }
        else audio_sfx(SFX_HIT,0);
    } else if(an->kind==AV_DISK){
        an->glitch=90;
        int i=clampi(step,0,an->disk.np-1);
        int lo=an->disk.dmin, hi=an->disk.dmax; if(hi<=lo)hi=lo+1;
        float r=(float)(an->disk.path[i]-lo)/(float)(hi-lo);
        audio_sfx(SFX_SEEK,r);
    } else if(an->kind==AV_MEM && !an->mem.paging){
        int i=clampi(step,0,an->mem.nstep-1);
        if(an->mem.step_region[i]<0){ an->flash=220; an->glitch=80; audio_sfx(SFX_NOFIT,0); }
        else audio_sfx(SFX_ALLOC,0);
    } else if(an->kind==AV_SCHED){
        an->glitch=80;   /* context switch jolt */
        audio_sfx(SFX_SWITCH,0);
    }
}

void anim_update(App *a, double dt){
    Anim *an=&a->anim;
    an->phase_time+=dt;
    if(an->flash>0) an->flash-=dt;
    if(an->glitch>0) an->glitch-=dt;

    if(an->phase==0){
        if(an->phase_time>INTRO_MS){ an->phase=1; an->phase_time=0; an->clock=0; }
        return;
    }
    if(an->phase==2) return;

    double old=an->clock;
    if(an->step_req!=0){
        an->paused=1;
        an->clock=(double)((int)(old+0.0001)+an->step_req);
        an->step_req=0;
        if(an->clock<0)an->clock=0;
        if(an->clock>an->clock_max)an->clock=an->clock_max;
    } else if(!an->paused){
        an->clock += speed_of(an)*dt/1000.0;
        if(an->clock>an->clock_max)an->clock=an->clock_max;
    }
    int ps=(int)old, ns=(int)an->clock;
    for(int s=ps+1;s<=ns;s++) fire_event(a,s);

    if(an->clock>=an->clock_max && !an->paused){ an->phase=2; an->phase_time=0; audio_sfx(SFX_DECRYPT,0); }
}

void anim_key(App *a, int vk){
    Anim *an=&a->anim;
    switch(vk){
    case VK_SPACE:  an->paused=!an->paused; break;
    case VK_RIGHT:  an->step_req=+1; break;
    case VK_LEFT:   an->step_req=-1; break;
    case VK_RETURN:
        if(an->phase==0){ an->phase=1; an->phase_time=0; an->clock=0; }
        else { an->clock=an->clock_max; an->phase=2; an->phase_time=0; }
        break;
    }
}

/* ------------------------------------------------------------------ */
/* rendering                                                          */
/* ------------------------------------------------------------------ */

static void hud_gauge(Framebuffer *fb,int x,int y,const char *lbl,double frac,uint32_t col){
    int cw=fb->bch_w, ch=fb->bch_h;
    fb_text(fb,x,y,lbl,COL_DGREEN);
    int bx=x+(int)strlen(lbl)*cw+cw, bw=cw*9;
    fb_frame(fb,bx,y+1,bw,ch-3,COL_DGREEN);
    if(frac<0)frac=0;
    if(frac>1)frac=1;
    int fw=(int)((bw-2)*frac);
    fb_fill_rect(fb,bx+1,y+2,fw,ch-5,col);
}

static void draw_trace(App *a, int x, int y, int w){
    Anim *an=&a->anim; Framebuffer *fb=&a->fb; int ch=fb->bch_h;
    fb_text(fb,x,y,"// KERNEL TRACE  [* live]",COL_RED); y+=ch+4;
    int maxlines=(fb->h-ch-20-y)/ch; if(maxlines<1)maxlines=1;
    int vis[ANIM_TRACE_MAX],nv=0;
    for(int i=0;i<an->ntrace;i++) if(an->trace_at[i]<=an->clock+0.0001) vis[nv++]=i;
    int start = nv>maxlines? nv-maxlines:0;
    int yy=y;
    for(int j=start;j<nv;j++){ int i=vis[j]; fb_text(fb,x,yy,an->trace[i],an->trace_col[i]); yy+=ch; }
    /* fill the dead space below the trace with a decorative memory dump */
    int hbottom=fb->h-ch*2-10, hdy=yy+ch;
    if(hdy < hbottom-ch*2) gfx_hexdump(fb,&a->rng,x,hdy,w,hbottom-hdy);
}

static void chrome(App *a, int *mx,int *my,int *mw,int *mh){
    Framebuffer *fb=&a->fb; Anim *an=&a->anim;
    int W=fb->w,H=fb->h,ch=fb->bch_h,cw=fb->bch_w;
    fb_clear(fb,COL_BG);
    fb_fill_rect(fb,0,0,W,ch*2+8,0x00101810);
    char tb[200]; snprintf(tb,200,"  >>> %s  ::  %s",an->title,an->subtitle);
    fb_text(fb,16,8,tb,COL_RED);

    /* live-HUD: blinking intrusion light, fake exfil counter, activity gauges */
    int blink=((int)(a->state_time/350))&1;
    if(blink){ fb_fill_rect(fb,W-cw*24,11,cw,cw,COL_RED);
               fb_text(fb,W-cw*22,8,"LIVE INTRUSION",COL_RED); }
    char hb[64]; snprintf(hb,64,"EXFIL %lu KB",(unsigned long)(a->now_ms*0.131));
    fb_text(fb,W-cw*24,8+ch,hb,COL_DGREEN);
    double prog=an->clock_max>0?an->clock/an->clock_max:0;
    hud_gauge(fb,16,8+ch,"CPU",prog,COL_GREEN);
    hud_gauge(fb,16+cw*17,8+ch,"MEM",0.25+0.6*prog,COL_CYAN);

    fb_hline(fb,0,ch*2+8,W,COL_DGREEN);

    int px=(int)(W*0.66);
    fb_vline(fb,px,ch*2+9,H-(ch*2+9),COL_DGREEN);
    /* skull watermark: the parasite watches, and slowly tracks side to side */
    int sx=px-130+(int)(40.0*sin(a->state_time/900.0));
    skull_render(fb, sx, H-150, (int)(a->state_time/170), 0x00351515);
    if((rng_next(&a->rng)&127)==0)
        fb_text(fb,px+20,H-150,TAUNTS[rng_next(&a->rng)%5],0x00401515);

    int fy=H-ch-6;
    fb_fill_rect(fb,0,H-ch-10,W,ch+10,0x00101810);
    fb_text(fb,16,fy,"[SPACE] pause   [<- ->] step   [ENTER] skip   [ESC] return to shell",COL_DGREEN);
    if(an->paused) fb_text(fb,W-11*cw,fy,"|| PAUSED",COL_AMBER);

    *mx=24; *my=ch*2+18; *mw=px-48; *mh=(H-ch-12)-(*my)-8;
    /* TUI frame hugging the data panel edges (never crosses the data itself) */
    fb_box(fb,*mx-8,*my-6,*mw+14,*mh+12,0x00153015);
    draw_trace(a, px+14, ch*2+18, W-px-22);
}

static void postfx(App *a){
    Framebuffer *fb=&a->fb; Anim *an=&a->anim;
    if(an->glitch>0){ gfx_rgb_split(fb, 3+(int)(rng_next(&a->rng)%5));
                      gfx_jitter(fb,&a->rng,3);
                      if(rng_next(&a->rng)&1) gfx_slice_tear(fb,&a->rng,20,2); }
    if(an->flash>0){
        int W=fb->w,ch=fb->bch_h;
        fb_fill_rect(fb,0,fb->h/2-ch,W,ch*2+8,0x00400008);
        fb_text_center(fb,W/3,fb->h/2-ch+4,">>> #PF  PAGE FAULT / FAULT EVENT <<<",COL_RED);
        gfx_datamosh(fb,&a->rng,0,fb->h/2-ch,W,ch*2+8);   /* corruption confined to the flash band */
        gfx_slice_tear(fb,&a->rng,26,3);
    }
    /* rare subliminal skull flash (one frame) - the parasite blinks into view */
    if(an->flash<=0 && (rng_next(&a->rng)&2047)==0)
        skull_render(fb,fb->w/2,fb->h/2,(int)(a->state_time/100),COL_RED);
    /* occasional power flicker / darkness pulse */
    if((rng_next(&a->rng)&511)<3) gfx_brightness(fb,52+(int)(rng_next(&a->rng)%28));
    gfx_scanlines(fb,86);
    gfx_vignette(fb);
}

/* ---- intro card ---- */
static void render_intro(App *a){
    Framebuffer *fb=&a->fb; Anim *an=&a->anim;
    int W=fb->w,H=fb->h,ch=fb->ch_h,cw=fb->ch_w;
    fb_clear(fb,COL_BG);
    /* decrypt the title from hex to text */
    char dec[64]; int n=(int)strlen(an->title);
    double per=INTRO_MS/(n+1);
    for(int i=0;i<n;i++){
        if(an->phase_time > i*per) dec[i]=an->title[i];
        else { const char*hx="0123456789ABCDEF#@%&"; dec[i]=hx[rng_next(&a->rng)%20]; }
    }
    dec[n]=0;
    fb_text_center(fb,W/2,H/2-ch*2,"LOADING MODULE VISUALIZER",COL_DGREEN);
    fb_text_center(fb,W/2,H/2,dec,COL_GREEN);
    fb_text_center(fb,W/2,H/2+ch*2,an->subtitle,COL_AMBER);
    fb_text_center(fb,W/2,H/2+ch*4,"// decrypting payload...",COL_RED);
    (void)cw;
    gfx_slice_tear(fb,&a->rng,40,3);
    gfx_rgb_split(fb,4+(int)(rng_next(&a->rng)%6));
    gfx_scanlines(fb,80);
    gfx_vignette(fb);
}

/* ---- CPU scheduling ---- */
static void render_sched(App *a){
    Framebuffer *fb=&a->fb; Anim *an=&a->anim; int ch=fb->bch_h,cw=fb->bch_w;
    int mx,my,mw,mh; chrome(a,&mx,&my,&mw,&mh);
    int ms=an->sched.makespan; if(ms<1)ms=1;
    double tc=an->clock; if(tc>ms)tc=ms;
    double sc=(double)mw/ms;
    int y=my;

    int run=-1;
    for(int k=0;k<an->sched.nseg;k++)
        if(an->sched.seg_pid[k]>0 && an->sched.seg_a[k]<=tc && tc<an->sched.seg_b[k]) run=an->sched.seg_pid[k];
    char big[48];
    if(run>0) snprintf(big,48,"CPU EXEC: P%d",run); else snprintf(big,48,"CPU: --idle--");
    fb_text(fb,mx,y,big, run>0?pcol(run):COL_GRAY);
    char clk[40]; snprintf(clk,40,"clock t = %0.0f / %d",tc,ms); fb_text(fb,mx,y+ch,clk,COL_DGREEN);
    y+=ch*2+6;

    /* ---- Gantt ---- */
    fb_text(fb,mx,y,"GANTT TIMELINE",COL_CYAN); y+=ch+2;
    int gy=y, gh=ch*2+10;
    for(int k=0;k<an->sched.nseg;k++){
        int a0=an->sched.seg_a[k],b0=an->sched.seg_b[k],pid=an->sched.seg_pid[k];
        if(a0>tc) continue;
        double bb=b0<tc?b0:tc;
        int x0=mx+(int)(a0*sc), x1=mx+(int)(bb*sc); if(x1<=x0)x1=x0+1;
        uint32_t c=pid>0?pcol(pid):0x00202820;
        fb_fill_rect(fb,x0,gy,x1-x0,gh,c);
        fb_frame(fb,x0,gy,x1-x0,gh,COL_BG);
        if(pid>0 && x1-x0>cw*2){ char l[16]; snprintf(l,16,"P%d",pid); fb_text(fb,x0+3,gy+gh/2-ch/2,l,COL_BG); }
    }
    int hx=mx+(int)(tc*sc); fb_vline(fb,hx,gy-6,gh+12,COL_WHITE);
    /* tick labels: small font, skip if too close to the previous one */
    {
        int tpx=need_px_w(fb,mw,ms+1,3); if(tpx>fb->base_fh*72/100) tpx=fb->base_fh*72/100;
        fb_font_for(fb,tpx); int scw=fb->ch_w;
        int lastx=-100000;
        for(int k=0;k<an->sched.nseg;k++){
            int a0=an->sched.seg_a[k];
            if(a0>tc) continue;
            int lx=mx+(int)(a0*sc);
            char tn[8]; snprintf(tn,8,"%d",a0);
            int lw=(int)strlen(tn)*scw;
            if(lx-lastx<lw+scw) continue;
            draw_label_clamped(fb,lx-lw/2,gy+gh+2,tn,COL_DGREEN,mx,mx+mw);
            lastx=lx;
        }
        { char tn[8]; snprintf(tn,8,"%d",(int)tc); int lw=(int)strlen(tn)*scw;
          draw_label_clamped(fb,hx-lw/2,gy+gh+2,tn,COL_WHITE,mx,mx+mw); }
        fb_font_base(fb);
    }
    y=gy+gh+ch+8;

    /* ---- ready queue (wraps instead of clipping) ---- */
    fb_text(fb,mx,y,"READY QUEUE @ t  (P# : remaining)",COL_CYAN); y+=ch+2;
    {
        int bx=mx, by=y;
        for(int i=0;i<an->sched.n;i++){
            int id=an->sched.id[i];
            if(an->sched.at[i]>tc) continue;
            int ex=0;
            for(int k=0;k<an->sched.nseg;k++) if(an->sched.seg_pid[k]==id){
                int a0=an->sched.seg_a[k],b0=an->sched.seg_b[k]; double bb=b0<tc?b0:tc; if(a0<tc) ex+=(int)(bb-a0);
            }
            int rem=an->sched.bt[i]-ex;
            if(rem<=0||id==run) continue;
            char b[16]; snprintf(b,16,"P%d:%d",id,rem);
            int bw=(int)strlen(b)*cw+10;
            if(bx+bw>mx+mw){ bx=mx; by+=ch+12; }
            fb_frame(fb,bx,by,bw,ch+8,pcol(id)); fb_text(fb,bx+5,by+4,b,pcol(id));
            bx+=bw+10;
        }
        y=by+ch+14;
    }

    /* ---- analysis (phase 2): font-fit so all rows stay above the footer ---- */
    if(an->phase==2){
        int rows=an->sched.n+3;
        int px=fit_px_rows(fb,(my+mh)-y,rows,fb->bch_h);
        fb_font_for(fb,px); int rch=fb->ch_h;
        fb_text(fb,mx,y,"== ANALYSIS ==   ID  Comp  TAT  Wait  Resp",COL_CYAN); y+=rch;
        for(int i=0;i<an->sched.n;i++){
            char r[80]; snprintf(r,80,"                 P%-2d  %4d  %3d  %4d  %4d",
                an->sched.id[i],an->sched.comp[i],an->sched.tat[i],an->sched.wait[i],an->sched.resp[i]);
            fb_text(fb,mx,y,r,pcol(an->sched.id[i])); y+=rch;
        }
        if(an->nsummary>0){ fb_text(fb,mx,y+2,an->summary[0],COL_AMBER); y+=rch; }
        fb_text(fb,mx,y+2,"-- press [ESC] to return to shell --",COL_RED);
        fb_font_base(fb);
    }
    postfx(a);
}

/* ---- page replacement : full replacement grid (cols = refs, rows = frames) ---- */
static void render_vmem(App *a){
    Framebuffer *fb=&a->fb; Anim *an=&a->anim; int ch=fb->bch_h, cw=fb->bch_w;
    int mx,my,mw,mh; chrome(a,&mx,&my,&mw,&mh);
    int n=an->vmem.n, F=an->vmem.frames;
    if(n<1)n=1;
    if(F<1)F=1;
    int cur=clampi((int)an->clock,0,n-1);

    int y=my;
    char hdr[48]; snprintf(hdr,48,"PAGE REPLACEMENT  (frames=%d)",F);
    fb_text(fb,mx,y,hdr,COL_CYAN);

    /* left gutter for frame labels (base-width estimate), grid to its right */
    int gutter=cw*4;
    int gx0=mx+gutter, gridw=mw-gutter;
    if(gridw<cw*8){ gutter=0; gx0=mx; gridw=mw; }

    int gy=y+ch*2;
    int avail_h=(my+mh)-gy-ch*4;            /* reserve for counters + ESC line */
    if(avail_h<ch*4) avail_h=ch*4;

    /* choose column window + font: zoom to fit, else scroll a window centred on cur */
    int start=0, shown=n;
    int raw=need_px_w(fb,gridw,n,3);
    if(raw<FONT_MIN_PX){
        fb_font_for(fb,FONT_MIN_PX);
        int cwm=fb->ch_w*3+6;
        shown=gridw/(cwm>0?cwm:1); if(shown<1)shown=1; if(shown>n)shown=n;
        start=cur-shown/2;
        if(start<0)start=0;
        if(start+shown>n)start=n-shown;
        if(start<0)start=0;
    } else {
        fb_font_for(fb,raw);   /* fb_font_for clamps up to the base size */
    }
    int gch=fb->ch_h;
    int cellw=imin(fb->ch_w*3+6, gridw/(shown>0?shown:1));
    int rowh=imin(gch+4, avail_h/(F+2)); if(rowh<gch) rowh=gch;

    /* scroll note */
    if(shown<n){
        char note[64]; snprintf(note,64,">> %d refs : window %d-%d <<",n,start,start+shown-1);
        fb_font_base(fb); fb_text(fb,mx+(int)strlen(hdr)*cw+cw*2,y,note,COL_AMBER);
        fb_font_for(fb,raw<FONT_MIN_PX?FONT_MIN_PX:raw);
    }

    /* reference row */
    for(int c=0;c<shown;c++){
        int i=start+c; int x=gx0+c*cellw;
        uint32_t col=(i==cur)?COL_WHITE:(i<cur?COL_DGREEN:COL_GRAY);
        if(i==cur) fb_frame(fb,x-1,gy-1,cellw,rowh+2,COL_RED);
        char s[8]; snprintf(s,8,"%d",an->vmem.ref[i]);
        fb_text(fb,x+3,gy+(rowh-gch)/2,s,col);
    }

    /* frame rows */
    int fy0=gy+rowh+4;
    for(int k=0;k<F;k++){
        int ry=fy0+k*rowh;
        char lbl[16]; snprintf(lbl,16,"f%d",k);
        fb_text(fb,mx,ry+(rowh-gch)/2,lbl,COL_DGREEN);
        for(int c=0;c<shown;c++){
            int i=start+c; int x=gx0+c*cellw;
            if(i>cur){ fb_frame(fb,x,ry,cellw-2,rowh-2,0x00141c14); continue; }
            int val=an->vmem.snap[i][k];
            int loaded=(val>=0)&&((i==0)||(val!=an->vmem.snap[i-1][k]));
            uint32_t col=COL_DGREEN;
            if(an->vmem.hit[i] && val==an->vmem.ref[i]) col=COL_GREEN;
            else if(loaded) col=COL_AMBER;
            fb_frame(fb,x,ry,cellw-2,rowh-2,(i==cur)?COL_RED:0x00203020);
            if(val>=0){ char s[16]; snprintf(s,16,"%d",val); fb_text(fb,x+3,ry+(rowh-gch)/2,s,col); }
        }
    }

    /* PF / H footer row */
    int py=fy0+F*rowh+2;
    for(int c=0;c<shown;c++){
        int i=start+c; if(i>cur) break;
        int x=gx0+c*cellw;
        if(an->vmem.hit[i]) fb_text(fb,x+3,py,"H",COL_GREEN);
        else fb_text(fb,x+2,py,"PF",COL_RED);
    }
    fb_font_base(fb);

    /* counters below the grid (base font) */
    int fcnt=0; for(int i=0;i<=cur;i++) if(!an->vmem.hit[i])fcnt++;
    int by=imin(py+rowh+ch, my+mh-ch*3);
    if(an->vmem.hit[cur]) fb_text(fb,mx,by,"STATUS: HIT",COL_GREEN);
    else {
        char st[64];
        if(an->vmem.victim[cur]>=0) snprintf(st,64,"STATUS: #PF  evict victim pg %d",an->vmem.victim[cur]);
        else snprintf(st,64,"STATUS: #PF PAGE FAULT");
        fb_text(fb,mx,by,st,COL_RED);
    }
    char cc[64]; snprintf(cc,64,"faults %d / %d   rate %.0f%%",fcnt,cur+1,100.0*fcnt/(cur+1));
    fb_text(fb,mx,by+ch,cc,COL_AMBER);

    if(an->phase==2){
        char r[80]; snprintf(r,80,"== TOTAL FAULTS = %d / %d  (%.1f%%) ==  -- [ESC] shell --",
            an->vmem.faults,n,100.0*an->vmem.faults/n);
        fb_text(fb,mx,by+ch*2,r,COL_RED);
    }
    postfx(a);
}

/* ---- disk scheduling ---- */
static void render_disk(App *a){
    Framebuffer *fb=&a->fb; Anim *an=&a->anim; int ch=fb->bch_h,cw=fb->bch_w;
    int mx,my,mw,mh; chrome(a,&mx,&my,&mw,&mh);
    int lo=an->disk.dmin, hi=an->disk.dmax; if(hi<=lo)hi=lo+1;
    int cur=clampi((int)an->clock,0,an->disk.np-1);
    #define TRK2X(t) (mx + ((t)-lo)*mw/(hi-lo))

    int y=my;
    char th[64]; snprintf(th,64,"DISK TRACKS (%d..%d)   minimize seek time",lo,hi);
    fb_text(fb,mx,y,th,COL_CYAN);
    int ry=y+ch*2;
    fb_hline(fb,mx,ry,mw,COL_DGREEN);
    for(int i=0;i<an->disk.n;i++){
        int t=an->disk.req[i]; int x=TRK2X(t);
        int served=0; for(int k=1;k<=cur;k++) if(an->disk.path[k]==t)served=1;
        fb_vline(fb,x,ry-6,12,served?COL_GREEN:COL_GRAY);
    }
    /* track-number labels: small font, sorted, skip-if-too-close */
    {
        int tpx=need_px_w(fb,mw,an->disk.n,4); if(tpx>fb->base_fh*72/100) tpx=fb->base_fh*72/100;
        fb_font_for(fb,tpx); int scw=fb->ch_w, sch=fb->ch_h;
        int idx[32], nn=an->disk.n>32?32:an->disk.n;
        for(int i=0;i<nn;i++) idx[i]=an->disk.req[i];
        for(int i=1;i<nn;i++){ int v=idx[i],j=i-1; while(j>=0&&idx[j]>v){idx[j+1]=idx[j];j--;} idx[j+1]=v; }
        int lastx=-100000;
        for(int i=0;i<nn;i++){
            int x=TRK2X(idx[i]);
            char s[8]; snprintf(s,8,"%d",idx[i]);
            int lw=(int)strlen(s)*scw;
            if(x-lastx < lw+scw) continue;      /* keep a full label-width + gap apart */
            draw_label_clamped(fb,x-lw/2,ry-sch-6,s,COL_DGREEN,mx,mx+mw);  /* centre, keep on-panel */
            lastx=x;
        }
        fb_font_base(fb);
    }
    int hpos=an->disk.path[cur]; int hx=TRK2X(hpos);
    fb_fill_rect(fb,hx-3,ry-10,7,20,COL_RED);
    char hl[40]; snprintf(hl,40,"head @ %d",hpos);
    draw_label_clamped(fb,hx-cw*2,ry+10,hl,COL_WHITE,mx,mx+mw);
    y=ry+ch*3;

    /* zigzag position-vs-step graph (geometric: scales freely) */
    fb_text(fb,mx,y,"HEAD PATH (track vs. step)",COL_CYAN); y+=ch+2;
    int gy=y, reserve=ch*3;
    int gh=(my+mh)-gy-reserve; if(gh<ch*4)gh=ch*4;
    fb_frame(fb,mx,gy,mw,gh,0x00203020);
    int np=an->disk.np; if(np<2)np=2;
    for(int i=0;i<=cur;i++){
        int x=mx+i*mw/(np-1);
        int yy=gy+ (an->disk.path[i]-lo)*gh/(hi-lo);
        fb_fill_rect(fb,x-2,yy-2,5,5,COL_AMBER);
        if(i>0){
            int x0=mx+(i-1)*mw/(np-1);
            int y0=gy+(an->disk.path[i-1]-lo)*gh/(hi-lo);
            int steps=x-x0; if(steps<1)steps=1;
            for(int s=0;s<=steps;s++){ int xx=x0+s; int yyy=y0+(yy-y0)*s/steps; fb_fill_rect(fb,xx,yyy,1,1,COL_GREEN); }
        }
    }
    y=gy+gh+4;
    int tot=0; for(int i=1;i<=cur;i++){ int d=an->disk.path[i]-an->disk.path[i-1]; tot+=d<0?-d:d; }
    char tcs[48]; snprintf(tcs,48,"head movement so far: %d tracks",tot);
    fb_text(fb,mx,y,tcs,COL_AMBER); y+=ch;
    if(an->phase==2){
        char r[80]; snprintf(r,80,"== TOTAL HEAD MOVEMENT = %d tracks ==   -- [ESC] shell --",an->disk.total);
        fb_text(fb,mx,y,r,COL_RED);
    }
    postfx(a);
    #undef TRK2X
}

/* ---- memory: fits + paging ---- */
static void render_mem(App *a){
    Framebuffer *fb=&a->fb; Anim *an=&a->anim; int ch=fb->bch_h,cw=fb->bch_w;
    int mx,my,mw,mh; chrome(a,&mx,&my,&mw,&mh);

    if(an->mem.paging){
        int nc=an->mem.nconv>0?an->mem.nconv:1;
        int cur=clampi((int)an->clock,0,nc-1);
        fb_text(fb,mx,my,"PAGING : LOGICAL -> PHYSICAL",COL_CYAN);
        int la=an->mem.la[cur],pg=an->mem.pg[cur],d=an->mem.doff[cur],f=an->mem.fr[cur],pa=an->mem.pa[cur];
        int yy=my+ch*2; char s[80];
        snprintf(s,80,"LA = %d", la); fb_text(fb,mx,yy,s,COL_WHITE); yy+=ch;
        snprintf(s,80,"p = LA / P = %d / %d = %d", la, an->mem.page_size, pg); fb_text(fb,mx,yy,s,COL_GREEN); yy+=ch;
        snprintf(s,80,"d = LA %% P = %d", d); fb_text(fb,mx,yy,s,COL_GREEN); yy+=ch;
        snprintf(s,80,"page table: p%d -> frame %d", pg, f); fb_text(fb,mx,yy,s,COL_AMBER); yy+=ch;
        snprintf(s,80,"PA = f*P + d = %d*%d + %d = %d", f, an->mem.page_size, d, pa);
        fb_text(fb,mx,yy,s,COL_RED); yy+=ch*2;
        snprintf(s,80,"hex PA = 0x%05X", pa); fb_text(fb,mx,yy,s,COL_DGREEN); yy+=ch*2;
        /* translations list: font-fit so it never crosses the footer */
        fb_text(fb,mx,yy,"-- translations --",COL_DGREEN); yy+=ch;
        int px=fit_px_rows(fb,(my+mh)-yy-ch,cur+2,fb->bch_h);
        fb_font_for(fb,px); int rch=fb->ch_h;
        for(int i=0;i<=cur;i++){ snprintf(s,80,"  LA %2d -> PA %d",an->mem.la[i],an->mem.pa[i]);
            fb_text(fb,mx,yy,s,i==cur?COL_WHITE:COL_DGREEN); yy+=rch; }
        fb_font_base(fb);
        if(an->phase==2) fb_text(fb,mx,yy+2,"-- press [ESC] to return to shell --",COL_RED);
        postfx(a); return;
    }

    /* fits: vertical memory column (geometric: scales to mh) */
    int cur=clampi((int)an->clock,0,an->mem.nstep-1);
    fb_text(fb,mx,my,"MAIN MEMORY (MFT regions)  internal frag = region - job",COL_CYAN);
    int totalK=0; for(int i=0;i<an->mem.nreg;i++) totalK+=an->mem.reg[i];
    if(totalK<1)totalK=1;
    int nreg=an->mem.nreg>0?an->mem.nreg:1;
    int colx=mx, coly=my+ch*2;
    int colw=mw*9/20;                         /* ~45% so region labels fit */
    if(colw<cw*18) colw=cw*18;
    if(colw>mw-cw*16) colw=mw-cw*16;
    if(colw<cw*18) colw=cw*18;
    int colh=(my+mh)-coly-ch; if(colh<ch*6)colh=ch*6;
    int rhfloor=imin(ch+6, colh/nreg);
    int yy=coly;
    for(int r=0;r<an->mem.nreg;r++){
        int rh=an->mem.reg[r]*colh/totalK; if(rh<rhfloor)rh=rhfloor;
        int occ_job=-1; char occ_jid=' '; int occ_frag=0;
        for(int s=0;s<=cur;s++) if(an->mem.step_region[s]==r){ occ_job=an->mem.step_job[s]; occ_jid=an->mem.step_jid[s]; occ_frag=an->mem.step_frag[s]; }
        int filled = occ_job>=0;
        int isnow = (cur>=0 && an->mem.step_region[cur]==r);
        uint32_t c = filled? (isnow?COL_WHITE:COL_GREEN) : COL_DGREEN;
        fb_frame(fb,colx,yy,colw,rh,c);
        char s[48];
        if(filled){ int usedh=(an->mem.reg[r]-occ_frag)*rh/an->mem.reg[r];
            fb_fill_rect(fb,colx+1,yy+1,colw-2,usedh-2>0?usedh-2:1, pcol(occ_jid-'A'+1));
            snprintf(s,48,"R%d %dK: job %c (frag %dK)",r,an->mem.reg[r],occ_jid,occ_frag);
        } else snprintf(s,48,"R%d %dK: FREE",r,an->mem.reg[r]);
        /* shrink the label font if it would overrun the box width */
        int needpx=need_px_w(fb,colw-8,1,(int)strlen(s));
        uint32_t lc = filled?COL_BG:COL_GRAY;
        if(needpx<fb->bch_h){ fb_font_for(fb,needpx); fb_text(fb,colx+4,yy+rh/2-fb->ch_h/2,s,lc); fb_font_base(fb); }
        else fb_text(fb,colx+4,yy+rh/2-ch/2,s,lc);
        yy+=rh+4;
    }

    /* right column: input queue, then action + summary anchored to the bottom */
    int qx=colx+colw+cw*3;
    int qw=(mx+mw)-qx; if(qw<cw*10)qw=cw*10;
    int qbottom=coly+colh;
    fb_text(fb,qx,coly-ch,"INPUT QUEUE",COL_CYAN);
    int qy=coly;
    for(int s=cur+1;s<an->mem.nstep;s++){
        if(qy>qbottom-ch*5) break;            /* leave room for action lines */
        char b[24]; snprintf(b,24,"%c = %dK",an->mem.step_jid[s],an->mem.step_job[s]);
        int bw=imin(cw*12,qw); fb_frame(fb,qx,qy,bw,ch+6,COL_AMBER); fb_text(fb,qx+4,qy+3,b,COL_AMBER); qy+=ch+10;
    }
    int ay=qbottom-ch*3;
    char act[80];
    if(an->mem.step_region[cur]<0) snprintf(act,80,"job %c (%dK): NO FIT -> SKIP, requeue",an->mem.step_jid[cur],an->mem.step_job[cur]);
    else snprintf(act,80,"job %c (%dK) -> region %d",an->mem.step_jid[cur],an->mem.step_job[cur],an->mem.step_region[cur]);
    draw_label_clamped(fb,qx,ay,act, an->mem.step_region[cur]<0?COL_RED:COL_GREEN, qx, mx+mw);
    if(an->phase==2){
        char r[80]; snprintf(r,80,"== used %d/%d regions, internal frag %dK ==",an->mem.used,an->mem.nreg,an->mem.totfrag);
        draw_label_clamped(fb,qx,ay+ch,r,COL_RED,qx,mx+mw);
        draw_label_clamped(fb,qx,ay+ch*2,"-- press [ESC] to return to shell --",COL_RED,qx,mx+mw);
    }
    postfx(a);
}

/* ---- calc board (swap/eat) ---- */
static void render_calc(App *a){
    Framebuffer *fb=&a->fb; Anim *an=&a->anim; int ch=fb->bch_h;
    int mx,my,mw,mh; chrome(a,&mx,&my,&mw,&mh); (void)mh;
    int nl=an->calc.nline>0?an->calc.nline:1;
    int cur=clampi((int)an->clock,0,nl-1);
    fb_text(fb,mx,my,"COMPUTATION",COL_CYAN);
    int yy=my+ch*2;
    for(int i=0;i<=cur;i++){ fb_text(fb,mx,yy,an->calc.line[i], i==cur?COL_WHITE:COL_GREEN); yy+=ch; }
    if(an->phase==2){
        yy+=ch; char r[110]; snprintf(r,110,">>> RESULT: %s",an->calc.result);
        int rw=(int)strlen(r)*fb->bch_w+12; if(rw>mw)rw=mw;
        fb_frame(fb,mx-6,yy-4,rw,ch+8,COL_RED);
        fb_text(fb,mx,yy,r,COL_RED);
        fb_text(fb,mx,yy+ch*2,"-- press [ESC] to return to shell --",COL_RED);
    }
    postfx(a);
}

void anim_render(App *a){
    if(a->anim.phase==0){ render_intro(a); return; }
    switch(a->anim.kind){
    case AV_SCHED: render_sched(a); break;
    case AV_VMEM:  render_vmem(a);  break;
    case AV_DISK:  render_disk(a);  break;
    case AV_MEM:   render_mem(a);   break;
    case AV_CALC:  render_calc(a);  break;
    }
}
