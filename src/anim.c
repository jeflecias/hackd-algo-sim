/* anim.c - algorithm visualizer breakout state (ST_ANIM)
   intro card -> animated chart + live kernel-trace panel -> result, in horror/hacker style */
#include "app.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#define INTRO_MS 1300.0

static int clampi(int v,int lo,int hi){ return v<lo?lo:(v>hi?hi:v); }

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
        if(p<0){ anim_trace(a,a0,COL_GRAY,"[t=%02d] CPU idle -- hlt",a0); continue; }
        anim_trace(a,a0,COL_AMBER,"[t=%02d] SWITCH -> P%d",a0,p);
        anim_trace(a,a0,COL_DGREEN,"   ctx_save eax,ebx,esp");
        anim_trace(a,a0,COL_DGREEN,"   P%d: mov ecx,[burst]",p);
        anim_trace(a,a0,COL_DGREEN,"       dec ecx ; jnz .run");
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
        if(an->vmem.hit[i]){ anim_trace(a,i,COL_DGREEN,"[%02d] ref %d  resident (hit)",i,an->vmem.ref[i]); }
        else {
            anim_trace(a,i,COL_RED,"[%02d] #PF page %d (invalid)",i,an->vmem.ref[i]);
            if(an->vmem.victim[i]>=0) anim_trace(a,i,COL_AMBER,"     evict pg %d -> swap out",an->vmem.victim[i]);
            anim_trace(a,i,COL_DGREEN,"     load pg %d <- disk; valid=1",an->vmem.ref[i]);
        }
    }
    anim_summary(a,COL_CYAN,"[%s] page faults = %d / %d  (rate %.1f%%)",
                 an->title, an->vmem.faults, an->vmem.n, 100.0*an->vmem.faults/an->vmem.n);
}

static void build_disk(App *a){
    Anim *an=&a->anim;
    for(int i=1;i<an->disk.np;i++){
        int mv=an->disk.path[i]-an->disk.path[i-1]; if(mv<0)mv=-mv;
        if(an->disk.isreq[i]) anim_trace(a,i,COL_DGREEN,"[seek] %d -> %d (+%d) READ",
                              an->disk.path[i-1],an->disk.path[i],mv);
        else anim_trace(a,i,COL_GRAY,"[seek] %d -> %d (+%d)",
                              an->disk.path[i-1],an->disk.path[i],mv);
    }
    anim_summary(a,COL_CYAN,"[%s] total head movement = %d tracks", an->title, an->disk.total);
}

static void build_mem(App *a){
    Anim *an=&a->anim;
    if(an->mem.paging){
        for(int i=0;i<an->mem.nconv;i++)
            anim_trace(a,i,COL_DGREEN,"xlate LA=%d: p=%d d=%d -> f=%d PA=%d",
                       an->mem.la[i],an->mem.pg[i],an->mem.doff[i],an->mem.fr[i],an->mem.pa[i]);
        anim_summary(a,COL_CYAN,"[paging] translated %d addresses (PA=f*P+d)",an->mem.nconv);
        return;
    }
    for(int i=0;i<an->mem.nstep;i++){
        if(an->mem.step_region[i]<0)
            anim_trace(a,i,COL_RED,"alloc(%c,%dK): no fit -> requeue",an->mem.step_jid[i],an->mem.step_job[i]);
        else
            anim_trace(a,i,COL_DGREEN,"alloc(%c,%dK) -> region[%d] @0x%05X frag %dK",
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
        if(!an->vmem.hit[i]){ an->flash=260; an->glitch=140; }
    } else if(an->kind==AV_DISK){
        an->glitch=90;
    } else if(an->kind==AV_MEM && !an->mem.paging){
        int i=clampi(step,0,an->mem.nstep-1);
        if(an->mem.step_region[i]<0){ an->flash=220; an->glitch=80; }
    } else if(an->kind==AV_SCHED){
        an->glitch=80;   /* context switch jolt */
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

    if(an->clock>=an->clock_max && !an->paused){ an->phase=2; an->phase_time=0; }
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

static void draw_trace(App *a, int x, int y, int w){
    (void)w;
    Anim *an=&a->anim; Framebuffer *fb=&a->fb; int ch=fb->ch_h;
    fb_text(fb,x,y,"// KERNEL TRACE",COL_RED); y+=ch+4;
    int maxlines=(fb->h-ch-20-y)/ch; if(maxlines<1)maxlines=1;
    int vis[ANIM_TRACE_MAX],nv=0;
    for(int i=0;i<an->ntrace;i++) if(an->trace_at[i]<=an->clock+0.0001) vis[nv++]=i;
    int start = nv>maxlines? nv-maxlines:0;
    for(int j=start;j<nv;j++){ int i=vis[j]; fb_text(fb,x,y,an->trace[i],an->trace_col[i]); y+=ch; }
}

static void chrome(App *a, int *mx,int *my,int *mw,int *mh){
    Framebuffer *fb=&a->fb; Anim *an=&a->anim;
    int W=fb->w,H=fb->h,ch=fb->ch_h,cw=fb->ch_w;
    fb_clear(fb,COL_BG);
    fb_fill_rect(fb,0,0,W,ch*2+8,0x00101810);
    char tb[200]; snprintf(tb,200,"  >>> %s  ::  %s",an->title,an->subtitle);
    fb_text(fb,16,8,tb,COL_RED);
    fb_hline(fb,0,ch*2+8,W,COL_DGREEN);

    int px=(int)(W*0.66);
    fb_vline(fb,px,ch*2+9,H-(ch*2+9),COL_DGREEN);
    /* faint skull watermark - the parasite watches */
    skull_render(fb, px-130, H-150, (int)(a->state_time/170), 0x00351515);

    int fy=H-ch-6;
    fb_fill_rect(fb,0,H-ch-10,W,ch+10,0x00101810);
    fb_text(fb,16,fy,"[SPACE] pause   [<- ->] step   [ENTER] skip   [ESC] return to shell",COL_DGREEN);
    if(an->paused) fb_text(fb,W-11*cw,fy,"|| PAUSED",COL_AMBER);

    *mx=24; *my=ch*2+18; *mw=px-48; *mh=(H-ch-12)-(*my)-8;
    draw_trace(a, px+14, ch*2+18, W-px-22);
}

static void postfx(App *a){
    Framebuffer *fb=&a->fb; Anim *an=&a->anim;
    if(an->glitch>0){ gfx_rgb_split(fb, 3+(int)(rng_next(&a->rng)%5));
                      if(rng_next(&a->rng)&1) gfx_slice_tear(fb,&a->rng,20,2); }
    if(an->flash>0){
        int W=fb->w,ch=fb->ch_h;
        fb_fill_rect(fb,0,fb->h/2-ch,W,ch*2+8,0x00400008);
        fb_text_center(fb,W/3,fb->h/2-ch+4,">>> #PF  PAGE FAULT / FAULT EVENT <<<",COL_RED);
        gfx_slice_tear(fb,&a->rng,26,3);
    }
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
    Framebuffer *fb=&a->fb; Anim *an=&a->anim; int ch=fb->ch_h,cw=fb->ch_w;
    int mx,my,mw,mh; chrome(a,&mx,&my,&mw,&mh); (void)mh;
    int ms=an->sched.makespan; if(ms<1)ms=1;
    double tc=an->clock; if(tc>ms)tc=ms;
    double sc=(double)mw/ms;
    int gy=my+ch*3, gh=ch*2+10;

    int run=-1;
    for(int k=0;k<an->sched.nseg;k++)
        if(an->sched.seg_pid[k]>0 && an->sched.seg_a[k]<=tc && tc<an->sched.seg_b[k]) run=an->sched.seg_pid[k];
    char big[48];
    if(run>0) snprintf(big,48,"CPU EXEC: P%d",run); else snprintf(big,48,"CPU: --idle--");
    fb_text(fb,mx,my,big, run>0?pcol(run):COL_GRAY);
    char clk[40]; snprintf(clk,40,"clock t = %0.0f / %d",tc,ms); fb_text(fb,mx,my+ch,clk,COL_DGREEN);

    fb_text(fb,mx,gy-ch-2,"GANTT TIMELINE",COL_CYAN);
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
    for(int k=0;k<an->sched.nseg;k++){
        int a0=an->sched.seg_a[k];
        if(a0<=tc){ char tn[8]; snprintf(tn,8,"%d",a0); fb_text(fb,mx+(int)(a0*sc)-cw/2,gy+gh+2,tn,COL_DGREEN); }
    }
    { char tn[8]; snprintf(tn,8,"%d",(int)tc); fb_text(fb,mx+(int)(tc*sc)-cw/2,gy+gh+2,tn,COL_WHITE); }
    int hx=mx+(int)(tc*sc); fb_vline(fb,hx,gy-6,gh+12,COL_WHITE);

    int ry=gy+gh+ch*3;
    fb_text(fb,mx,ry-ch,"READY QUEUE @ t  (P# : remaining)",COL_CYAN);
    int bx=mx;
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
        if(bx+bw>mx+mw) break;
        fb_frame(fb,bx,ry,bw,ch+8,pcol(id)); fb_text(fb,bx+5,ry+4,b,pcol(id));
        bx+=bw+10;
    }

    if(an->phase==2){
        int yy=ry+ch*3;
        fb_text(fb,mx,yy,"== ANALYSIS ==  ID  Comp  TAT  Wait  Resp",COL_CYAN); yy+=ch;
        for(int i=0;i<an->sched.n;i++){
            fb_text(fb,mx,yy,"",COL_GREEN);
            char r[80]; snprintf(r,80,"               P%-2d  %4d  %3d  %4d  %4d",
                an->sched.id[i],an->sched.comp[i],an->sched.tat[i],an->sched.wait[i],an->sched.resp[i]);
            fb_text(fb,mx,yy,r,pcol(an->sched.id[i])); yy+=ch;
        }
        if(an->nsummary>0){ fb_text(fb,mx,yy+4,an->summary[0],COL_AMBER); }
        fb_text(fb,mx,yy+ch+8,"-- press [ESC] to return to shell --",COL_RED);
    }
    postfx(a);
}

/* ---- page replacement ---- */
static void render_vmem(App *a){
    Framebuffer *fb=&a->fb; Anim *an=&a->anim; int ch=fb->ch_h,cw=fb->ch_w;
    int mx,my,mw,mh; chrome(a,&mx,&my,&mw,&mh); (void)mh;
    int n=an->vmem.n, F=an->vmem.frames;
    int cur=clampi((int)an->clock,0,n-1);

    fb_text(fb,mx,my,"REFERENCE STRING",COL_CYAN);
    int tx=mx, ty=my+ch+4;
    int cellw=cw*3;
    for(int i=0;i<n;i++){
        int x=tx+i*cellw; if(x+cellw>mx+mw){ break; }
        uint32_t c = (i==cur)?COL_WHITE : (i<cur?COL_DGREEN:COL_GRAY);
        char s[8]; snprintf(s,8,"%d",an->vmem.ref[i]);
        if(i==cur) fb_frame(fb,x-2,ty-2,cellw,ch+4,COL_RED);
        fb_text(fb,x,ty,s,c);
    }
    fb_text(fb,tx+cur*cellw,ty+ch+2,"^",COL_RED);

    /* frame slots */
    int fy=ty+ch*4;
    fb_text(fb,mx,fy-ch,"PHYSICAL FRAMES",COL_CYAN);
    int boxw=cw*8, boxh=ch+10;
    for(int s=0;s<F;s++){
        int y=fy+s*(boxh+6);
        int val=an->vmem.snap[cur][s];
        uint32_t c = (val==an->vmem.ref[cur] && an->vmem.hit[cur])?COL_GREEN :
                     (val==an->vmem.ref[cur] && !an->vmem.hit[cur])?COL_AMBER : COL_DGREEN;
        fb_frame(fb,mx,y,boxw,boxh,c);
        char s2[24]; if(val<0)snprintf(s2,24,"frame%d: --",s); else snprintf(s2,24,"frame%d: %d",s,val);
        fb_text(fb,mx+6,y+5,s2, val<0?COL_GRAY:c);
    }

    /* banner + counters */
    int by=fy;
    int fcnt=0; for(int i=0;i<=cur;i++) if(!an->vmem.hit[i])fcnt++;
    int bx=mx+boxw+cw*4;
    if(an->vmem.hit[cur]) fb_text(fb,bx,by,"STATUS: HIT",COL_GREEN);
    else { fb_text(fb,bx,by,"STATUS: #PF PAGE FAULT",COL_RED);
           if(an->vmem.victim[cur]>=0){ char v[40]; snprintf(v,40,"evict victim pg %d",an->vmem.victim[cur]);
               fb_text(fb,bx,by+ch,v,COL_AMBER); } }
    char cc[64]; snprintf(cc,64,"faults: %d / %d",fcnt,cur+1); fb_text(fb,bx,by+ch*3,cc,COL_AMBER);
    snprintf(cc,64,"rate  : %.0f%%",100.0*fcnt/(cur+1)); fb_text(fb,bx,by+ch*4,cc,COL_AMBER);

    if(an->phase==2){
        int yy=fy+F*(boxh+6)+ch;
        char r[80]; snprintf(r,80,"== TOTAL FAULTS = %d / %d  (%.1f%%) ==",
            an->vmem.faults,n,100.0*an->vmem.faults/n);
        fb_text(fb,mx,yy,r,COL_RED);
        fb_text(fb,mx,yy+ch,"-- press [ESC] to return to shell --",COL_RED);
    }
    postfx(a);
}

/* ---- disk scheduling ---- */
static void render_disk(App *a){
    Framebuffer *fb=&a->fb; Anim *an=&a->anim; int ch=fb->ch_h,cw=fb->ch_w;
    int mx,my,mw,mh; chrome(a,&mx,&my,&mw,&mh); (void)mh;
    int lo=an->disk.dmin, hi=an->disk.dmax; if(hi<=lo)hi=lo+1;
    int cur=clampi((int)an->clock,0,an->disk.np-1);
    #define TRK2X(t) (mx + ((t)-lo)*mw/(hi-lo))

    fb_text(fb,mx,my,"DISK TRACKS (0..199)",COL_CYAN);
    int ry=my+ch*2;
    fb_hline(fb,mx,ry,mw,COL_DGREEN);
    for(int i=0;i<an->disk.n;i++){
        int t=an->disk.req[i]; int x=TRK2X(t);
        int served=0; for(int k=1;k<=cur;k++) if(an->disk.path[k]==t)served=1;
        uint32_t c=served?COL_GREEN:COL_GRAY;
        fb_vline(fb,x,ry-6,12,c);
        char s[8]; snprintf(s,8,"%d",t); fb_text(fb,x-cw,ry-ch-6,s,c);
    }
    int hpos=an->disk.path[cur]; int hx=TRK2X(hpos);
    fb_fill_rect(fb,hx-3,ry-10,7,20,COL_RED);
    char hl[40]; snprintf(hl,40,"head @ %d",hpos); fb_text(fb,hx-cw*2,ry+10,hl,COL_WHITE);

    /* zigzag position-vs-step graph */
    int gy=ry+ch*4, gh=mh - (gy-my) - ch*4; if(gh<ch*4)gh=ch*4;
    fb_text(fb,mx,gy-ch,"HEAD PATH (track vs. step)",COL_CYAN);
    fb_frame(fb,mx,gy,mw,gh,0x00203020);
    int np=an->disk.np; if(np<2)np=2;
    for(int i=0;i<=cur;i++){
        int x=mx+i*mw/(np-1);
        int y=gy+ (an->disk.path[i]-lo)*gh/(hi-lo);
        fb_fill_rect(fb,x-2,y-2,5,5,COL_AMBER);
        if(i>0){
            int x0=mx+(i-1)*mw/(np-1);
            int y0=gy+(an->disk.path[i-1]-lo)*gh/(hi-lo);
            /* simple line: step along x */
            int steps=x-x0; if(steps<1)steps=1;
            for(int s=0;s<=steps;s++){ int xx=x0+s; int yy=y0+(y-y0)*s/steps; fb_fill_rect(fb,xx,yy,1,1,COL_GREEN); }
        }
    }
    int tot=0; for(int i=1;i<=cur;i++){ int d=an->disk.path[i]-an->disk.path[i-1]; tot+=d<0?-d:d; }
    char tc[48]; snprintf(tc,48,"head movement so far: %d tracks",tot);
    fb_text(fb,mx,gy+gh+4,tc,COL_AMBER);

    if(an->phase==2){
        char r[64]; snprintf(r,64,"== TOTAL HEAD MOVEMENT = %d tracks ==",an->disk.total);
        fb_text(fb,mx,gy+gh+ch+4,r,COL_RED);
        fb_text(fb,mx,gy+gh+ch*2+4,"-- press [ESC] to return to shell --",COL_RED);
    }
    postfx(a);
}

/* ---- memory: fits + paging ---- */
static void render_mem(App *a){
    Framebuffer *fb=&a->fb; Anim *an=&a->anim; int ch=fb->ch_h,cw=fb->ch_w;
    int mx,my,mw,mh; chrome(a,&mx,&my,&mw,&mh);

    if(an->mem.paging){
        int cur=clampi((int)an->clock,0,an->mem.nconv-1);
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
        fb_text(fb,mx,yy,"-- translations done --",COL_DGREEN); yy+=ch;
        for(int i=0;i<=cur;i++){ snprintf(s,80,"  LA %2d -> PA %d",an->mem.la[i],an->mem.pa[i]);
            fb_text(fb,mx,yy,s,i==cur?COL_WHITE:COL_DGREEN); yy+=ch; }
        if(an->phase==2) fb_text(fb,mx,yy+ch,"-- press [ESC] to return to shell --",COL_RED);
        postfx(a); return;
    }

    /* fits: vertical memory column */
    int cur=clampi((int)an->clock,0,an->mem.nstep-1);
    fb_text(fb,mx,my,"MAIN MEMORY (regions)",COL_CYAN);
    int totalK=0; for(int i=0;i<an->mem.nreg;i++) totalK+=an->mem.reg[i];
    if(totalK<1)totalK=1;
    int colx=mx, coly=my+ch*2, colw=cw*16, colh=mh-ch*4; if(colh<ch*6)colh=ch*6;
    int yy=coly;
    for(int r=0;r<an->mem.nreg;r++){
        int rh=an->mem.reg[r]*colh/totalK; if(rh<ch+6)rh=ch+6;
        /* occupant up to cur */
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
        fb_text(fb,colx+4,yy+rh/2-ch/2,s, filled?COL_BG:COL_GRAY);
        yy+=rh+4;
    }

    /* incoming job queue (steps after cur) */
    int qx=colx+colw+cw*4;
    fb_text(fb,qx,coly-ch,"INPUT QUEUE",COL_CYAN);
    int qy=coly;
    for(int s=cur+1;s<an->mem.nstep;s++){
        char b[24]; snprintf(b,24,"%c = %dK",an->mem.step_jid[s],an->mem.step_job[s]);
        fb_frame(fb,qx,qy,cw*10,ch+6,COL_AMBER); fb_text(fb,qx+4,qy+3,b,COL_AMBER); qy+=ch+10;
        if(qy>coly+colh)break;
    }
    /* current action */
    char act[64];
    if(an->mem.step_region[cur]<0) snprintf(act,64,"job %c (%dK): NO FIT -> requeue",an->mem.step_jid[cur],an->mem.step_job[cur]);
    else snprintf(act,64,"job %c (%dK) -> region %d",an->mem.step_jid[cur],an->mem.step_job[cur],an->mem.step_region[cur]);
    fb_text(fb,qx,coly+colh+ch,act, an->mem.step_region[cur]<0?COL_RED:COL_GREEN);

    if(an->phase==2){
        char r[64]; snprintf(r,64,"== used %d/%d regions, internal frag %dK ==",an->mem.used,an->mem.nreg,an->mem.totfrag);
        fb_text(fb,qx,coly+colh+ch*3,r,COL_RED);
        fb_text(fb,qx,coly+colh+ch*4,"-- press [ESC] to return to shell --",COL_RED);
    }
    postfx(a);
}

/* ---- calc board (swap/eat) ---- */
static void render_calc(App *a){
    Framebuffer *fb=&a->fb; Anim *an=&a->anim; int ch=fb->ch_h;
    int mx,my,mw,mh; chrome(a,&mx,&my,&mw,&mh); (void)mw;(void)mh;
    int cur=clampi((int)an->clock,0,an->calc.nline-1);
    fb_text(fb,mx,my,"COMPUTATION",COL_CYAN);
    int yy=my+ch*2;
    for(int i=0;i<=cur;i++){ fb_text(fb,mx,yy,an->calc.line[i], i==cur?COL_WHITE:COL_GREEN); yy+=ch; }
    if(an->phase==2){
        yy+=ch; char r[110]; snprintf(r,110,">>> RESULT: %s",an->calc.result);
        fb_frame(fb,mx-6,yy-4,(int)strlen(r)*fb->ch_w+12,ch+8,COL_RED);
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
