/* dataedit.c - fullscreen interactive data editor (ST_DATAEDIT)
   Edit the live g_data datasets with arrow keys / typing, horror-hacker styled.
   ENTER launches that module's default visualizer so you edit-then-see instantly. */
#include "app.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { char label[40]; int *ip; int lo, hi; int isdir; } EField;

static int clampi2(int v,int lo,int hi){ return v<lo?lo:(v>hi?hi:v); }

/* build the editable field list for the current tab (rebuilt each access so it
   tracks live edits to counts like ref-string length / queue length). */
static int build_fields(App *a, EField *f, int max){
    memset(f, 0, sizeof(EField)*max);
    int c=0, tab=a->edit.tab;
    if(tab==0){
        SchedData *s=&g_data.sched;
        for(int i=0;i<s->n;i++){
            if(c<max){ snprintf(f[c].label,40,"P%d arrival",s->id[i]); f[c].ip=&s->arrival[i]; f[c].lo=0; f[c].hi=99; c++; }
            if(c<max){ snprintf(f[c].label,40,"P%d burst",s->id[i]);   f[c].ip=&s->burst[i];   f[c].lo=1; f[c].hi=99; c++; }
            if(c<max){ snprintf(f[c].label,40,"P%d prio",s->id[i]);    f[c].ip=&s->prio[i];    f[c].lo=0; f[c].hi=9;  c++; }
        }
        if(c<max){ snprintf(f[c].label,40,"quantum"); f[c].ip=&s->quantum; f[c].lo=1; f[c].hi=16; c++; }
    } else if(tab==1){
        MemData *m=&g_data.mem;
        if(c<max){ snprintf(f[c].label,40,"total (K)"); f[c].ip=&m->total; f[c].lo=1; f[c].hi=512; c++; }
        for(int i=0;i<m->nreg;i++) if(c<max){ snprintf(f[c].label,40,"region %d (K)",i); f[c].ip=&m->reg[i]; f[c].lo=1; f[c].hi=256; c++; }
        for(int i=0;i<m->njob;i++) if(c<max){ snprintf(f[c].label,40,"job %c (K)",m->jid[i]); f[c].ip=&m->job[i]; f[c].lo=1; f[c].hi=256; c++; }
        if(c<max){ snprintf(f[c].label,40,"page size (K)"); f[c].ip=&m->page_size; f[c].lo=1; f[c].hi=64; c++; }
    } else if(tab==2){
        VmemData *v=&g_data.vmem;
        if(c<max){ snprintf(f[c].label,40,"frames");    f[c].ip=&v->frames; f[c].lo=1; f[c].hi=10; c++; }
        if(c<max){ snprintf(f[c].label,40,"ref count"); f[c].ip=&v->n;      f[c].lo=1; f[c].hi=64; c++; }
        for(int i=0;i<v->n;i++) if(c<max){ snprintf(f[c].label,40,"ref[%d]",i); f[c].ip=&v->ref[i]; f[c].lo=0; f[c].hi=99; c++; }
    } else {
        DiskData *d=&g_data.disk;
        if(c<max){ snprintf(f[c].label,40,"head start"); f[c].ip=&d->start;   f[c].lo=d->dmin; f[c].hi=d->dmax; c++; }
        if(c<max){ snprintf(f[c].label,40,"direction");  f[c].ip=&d->dirdown; f[c].lo=0; f[c].hi=1; f[c].isdir=1; c++; }
        if(c<max){ snprintf(f[c].label,40,"track min");  f[c].ip=&d->dmin;    f[c].lo=0; f[c].hi=511; c++; }
        if(c<max){ snprintf(f[c].label,40,"track max");  f[c].ip=&d->dmax;    f[c].lo=1; f[c].hi=512; c++; }
        if(c<max){ snprintf(f[c].label,40,"queue count");f[c].ip=&d->n;       f[c].lo=1; f[c].hi=32; c++; }
        for(int i=0;i<d->n;i++) if(c<max){ snprintf(f[c].label,40,"queue[%d]",i); f[c].ip=&d->req[i]; f[c].lo=d->dmin; f[c].hi=d->dmax; c++; }
    }
    return c;
}

void dataedit_open(App *a){
    a->state=ST_DATAEDIT; a->state_time=0;
    a->edit.tab=0; a->edit.field=0; a->edit.typing=0; a->edit.inbuf[0]=0;
    a->edit.glitch=0; a->edit.t=0;
}

void dataedit_update(App *a, double dt){
    a->edit.t += dt;
    if(a->edit.glitch>0) a->edit.glitch-=dt;
    a->scare_at += dt;   /* freeze the jumpscare countdown while editing */
}

static void edit_move(App *a, int dv){
    EField f[200]; int n=build_fields(a,f,200);
    a->edit.typing=0; a->edit.inbuf[0]=0;
    a->edit.field += dv;
    if(a->edit.field<0) a->edit.field=0;
    if(a->edit.field>=n) a->edit.field=n-1;
    audio_sfx(SFX_KEY,0);
}

static void edit_nudge(App *a, int dv){
    EField f[200]; int n=build_fields(a,f,200);
    if(a->edit.field<0||a->edit.field>=n) return;
    EField *cf=&f[a->edit.field];
    a->edit.typing=0; a->edit.inbuf[0]=0;
    if(cf->isdir) *cf->ip = (*cf->ip)?0:1;
    else *cf->ip = clampi2(*cf->ip+dv, cf->lo, cf->hi);
    a->edit.glitch=80;
    audio_sfx(SFX_GLITCH,0.25f);
}

static void edit_tab(App *a, int dv){
    a->edit.tab=(a->edit.tab+dv+4)%4;
    a->edit.field=0; a->edit.typing=0; a->edit.inbuf[0]=0;
    audio_sfx(SFX_SWITCH,0);
}

static void edit_digit(App *a, char ch){
    EField f[200]; int n=build_fields(a,f,200);
    if(a->edit.field<0||a->edit.field>=n) return;
    EField *cf=&f[a->edit.field];
    if(cf->isdir) return;
    if(!a->edit.typing){ a->edit.typing=1; a->edit.inbuf[0]=0; }
    int l=(int)strlen(a->edit.inbuf);
    if(l<6){ a->edit.inbuf[l]=ch; a->edit.inbuf[l+1]=0; }
    *cf->ip = clampi2(atoi(a->edit.inbuf), cf->lo, cf->hi);
    a->edit.glitch=70;
    audio_sfx(SFX_GLITCH,0.2f);
}

static void edit_backspace(App *a){
    if(!a->edit.typing) return;
    int l=(int)strlen(a->edit.inbuf);
    if(l>0) a->edit.inbuf[l-1]=0;
    EField f[200]; int n=build_fields(a,f,200);
    if(a->edit.field<0||a->edit.field>=n) return;
    EField *cf=&f[a->edit.field];
    if(a->edit.inbuf[0]) *cf->ip = clampi2(atoi(a->edit.inbuf), cf->lo, cf->hi);
}

static void edit_run(App *a){
    audio_sfx(SFX_DECRYPT,0);
    switch(a->edit.tab){
    case 0: sched_run(a,"fcfs");     break;
    case 1: mem_run(a,"firstfit");   break;
    case 2: vmem_run(a,"fifo");      break;
    case 3: disk_run(a,"fcfs");      break;
    }
}

void dataedit_key_char(App *a, char c){
    if(c>='0' && c<='9') edit_digit(a,c);
}

void dataedit_key_special(App *a, int vk){
    switch(vk){
    case VK_UP:    edit_move(a,-1);  break;
    case VK_DOWN:  edit_move(a,+1);  break;
    case VK_LEFT:  edit_nudge(a,-1); break;
    case VK_RIGHT: edit_nudge(a,+1); break;
    case VK_TAB:   edit_tab(a,+1);   break;
    case VK_RETURN:edit_run(a);      break;
    case VK_BACK:  edit_backspace(a);break;
    }
}

void dataedit_render(App *a){
    Framebuffer *fb=&a->fb; int W=fb->w,H=fb->h,ch=fb->bch_h,cw=fb->bch_w;
    fb_clear(fb,COL_BG);

    /* top bar */
    fb_fill_rect(fb,0,0,W,ch*2+8,0x00101810);
    fb_text(fb,16,8,    ">>> EDITING LIVE KERNEL STRUCTURES <<<",COL_RED);
    fb_text(fb,16,8+ch, "careless writes corrupt the host",COL_DGREEN);
    fb_hline(fb,0,ch*2+8,W,COL_DGREEN);

    /* module tabs */
    const char *T[4]={"SCHED","MEM","VMEM","DISK"};
    int tx=24, ty=ch*2+16;
    for(int i=0;i<4;i++){
        char b[16]; int sel=(i==a->edit.tab);
        snprintf(b,16, sel?"[%s]":" %s ", T[i]);
        fb_text(fb,tx,ty,b, sel?COL_CYAN:COL_DGREEN);
        tx += (int)strlen(b)*cw + cw*2;
    }

    /* faint parasite watermark */
    skull_render(fb, W-skull_width_px(fb)/2-40, H/2, (int)(a->edit.t/180), 0x00351515);

    /* fields (scrolling window keeps the selected field visible) */
    EField f[200]; int n=build_fields(a,f,200);
    if(a->edit.field>=n) a->edit.field=n>0?n-1:0;
    if(a->edit.field<0)  a->edit.field=0;

    int top=ty+ch*2, bottom=H-ch*2-8;
    int rh=ch+6;
    int rowsfit=(bottom-top)/rh; if(rowsfit<1)rowsfit=1;

    /* TUI frame around the field column + decorative hex dump in the right margin */
    int panelw=W*3/5;
    fb_box(fb,24,top-8,panelw-24,(bottom-top)+14,0x00153015);
    int hdh=(H/2)-top-30;
    if(hdh>ch*3) gfx_hexdump(fb,&a->rng,panelw+24,top,W-panelw-48,hdh);
    int start=0;
    if(n>rowsfit){
        start=a->edit.field-rowsfit/2;
        if(start<0)start=0;
        if(start+rowsfit>n)start=n-rowsfit;
        if(start<0)start=0;
    }
    int y=top;
    for(int r=start;r<n && r<start+rowsfit;r++){
        EField *cf=&f[r];
        char val[24];
        if(cf->isdir) snprintf(val,24,"%s",(*cf->ip)?"DOWN":"UP");
        else snprintf(val,24,"%d",*cf->ip);
        int sel=(r==a->edit.field);
        char line[96];
        if(sel) snprintf(line,96,"> %-15s [%s]",cf->label,val);
        else    snprintf(line,96,"  %-15s  %s ",cf->label,val);
        fb_text(fb,40,y,line, sel?COL_WHITE:COL_DGREEN);
        if(sel && a->edit.typing && (((int)(a->edit.t/300))&1)){
            int cx=40+(int)strlen(line)*cw;
            fb_fill_rect(fb,cx,y,cw,ch-2,COL_CYAN);
        }
        y+=rh;
    }
    if(n>rowsfit){
        char note[48]; snprintf(note,48,"-- field %d / %d --",a->edit.field+1,n);
        fb_text(fb,40,bottom+2,note,COL_AMBER);
    }

    /* footer */
    int fy=H-ch-6;
    fb_fill_rect(fb,0,H-ch-10,W,ch+10,0x00101810);
    fb_text(fb,16,fy,
        "[UP/DN] field   [L/R] +/-   [0-9] set   [TAB] module   [ENTER] run   [ESC] shell",
        COL_DGREEN);

    if(a->edit.glitch>0) gfx_slice_tear(fb,&a->rng,18,2);
    gfx_scanlines(fb,86);
    gfx_vignette(fb);
}
