/* data.c - shared editable datasets + 'data' command */
#include "app.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

Datasets g_data;

void data_reset(void){
    memset(&g_data, 0, sizeof(g_data));
    /* CPU scheduling textbook set: P1(0,8,2) P2(1,4,4) P3(2,9,1) P4(3,5,3) */
    SchedData *s = &g_data.sched;
    s->n = 4; s->quantum = 2;
    int at[4]={0,1,2,3}, bt[4]={8,4,9,5}, pr[4]={2,4,1,3};
    for (int i=0;i<4;i++){ s->id[i]=i+1; s->arrival[i]=at[i]; s->burst[i]=bt[i]; s->prio[i]=pr[i]; }

    /* Memory: 24K user split 10K,6K,4K,4K ; jobs 2,3,4,5,6 */
    MemData *m = &g_data.mem;
    m->total = 24; m->nreg = 4;
    m->reg[0]=10; m->reg[1]=6; m->reg[2]=4; m->reg[3]=4;
    m->njob = 5; int jb[5]={2,3,4,5,6};
    for (int i=0;i<5;i++){ m->job[i]=jb[i]; m->jid[i]=(char)('A'+i); }
    m->page_size = 4;

    /* Virtual memory reference string, 3 frames */
    VmemData *v = &g_data.vmem;
    int rs[20]={7,0,1,2,0,3,0,4,2,3,0,3,2,1,2,0,1,7,0,1};
    v->n = 20; v->frames = 3;
    for (int i=0;i<20;i++) v->ref[i]=rs[i];

    /* Disk: head 53, queue, range 0..199, direction toward 0 */
    DiskData *d = &g_data.disk;
    d->start = 53; d->dirdown = 1; d->dmin = 0; d->dmax = 199;
    int rq[8]={98,183,37,122,14,124,65,67};
    d->n = 8; for (int i=0;i<8;i++) d->req[i]=rq[i];
}

static void show_sched(App *a){
    Terminal *t = &a->term; SchedData *s = &g_data.sched;
    term_print(t, COL_CYAN, "[sched]  quantum=%d", s->quantum);
    term_print(t, COL_GREEN, "  ID  Arrival  Burst  Priority");
    for (int i=0;i<s->n;i++)
        term_print(t, COL_GREEN, "  P%-2d   %4d   %4d     %4d",
                   s->id[i], s->arrival[i], s->burst[i], s->prio[i]);
}
static void show_mem(App *a){
    Terminal *t = &a->term; MemData *m = &g_data.mem;
    term_print(t, COL_CYAN, "[mem]  total=%dK  page_size=%dK", m->total, m->page_size);
    char buf[128]={0}; int p=0;
    p+=snprintf(buf+p,128-p,"  regions:");
    for(int i=0;i<m->nreg;i++) p+=snprintf(buf+p,128-p," %dK",m->reg[i]);
    term_print(t, COL_GREEN, "%s", buf);
    p=0; p+=snprintf(buf+p,128-p,"  jobs:");
    for(int i=0;i<m->njob;i++) p+=snprintf(buf+p,128-p," %c=%dK",m->jid[i],m->job[i]);
    term_print(t, COL_GREEN, "%s", buf);
}
static void show_vmem(App *a){
    Terminal *t = &a->term; VmemData *v = &g_data.vmem;
    char buf[256]={0}; int p=0;
    p+=snprintf(buf+p,256-p,"[vmem] frames=%d  refs:", v->frames);
    for(int i=0;i<v->n;i++) p+=snprintf(buf+p,256-p,"%d ",v->ref[i]);
    term_print(t, COL_CYAN, "%s", buf);
}
static void show_disk(App *a){
    Terminal *t = &a->term; DiskData *d = &g_data.disk;
    char buf[256]={0}; int p=0;
    p+=snprintf(buf+p,256-p,"[disk] start=%d range=%d-%d dir=%s  queue:",
                d->start,d->dmin,d->dmax,d->dirdown?"down":"up");
    for(int i=0;i<d->n;i++) p+=snprintf(buf+p,256-p,"%d ",d->req[i]);
    term_print(t, COL_CYAN, "%s", buf);
}

/* data <module> [op...] */
void data_run(App *a, const char *args){
    Terminal *t = &a->term;
    char mod[16]={0}, op[16]={0}, rest[128]={0};
    int n = sscanf(args, "%15s %15s %127[^\n]", mod, op, rest);

    if (n < 1){
        term_print(t, COL_AMBER, "usage: data <sched|mem|vmem|disk> [reset|show|set ...]");
        term_print(t, COL_GREEN, "  data sched set p2 burst 6   |  data sched set quantum 4");
        term_print(t, COL_GREEN, "  data vmem set frames 4      |  data vmem set refs 1 2 3 4 1 2 5");
        term_print(t, COL_GREEN, "  data disk set start 50      |  data disk set queue 98 183 37");
        term_print(t, COL_GREEN, "  data mem set job A 5        |  data <mod> reset / show");
        return;
    }

    int is_sched=!strcmp(mod,"sched"), is_mem=!strcmp(mod,"mem"),
        is_vmem=!strcmp(mod,"vmem"), is_disk=!strcmp(mod,"disk");
    if (!(is_sched||is_mem||is_vmem||is_disk)){
        term_print(t, COL_RED, "unknown module '%s'", mod); return;
    }

    if (n == 1 || !strcmp(op,"show")){
        if(is_sched)show_sched(a); else if(is_mem)show_mem(a);
        else if(is_vmem)show_vmem(a); else show_disk(a);
        return;
    }
    if (!strcmp(op,"reset")){
        Datasets save = g_data; data_reset();
        if(is_sched) save.sched=g_data.sched; else if(is_mem) save.mem=g_data.mem;
        else if(is_vmem) save.vmem=g_data.vmem; else save.disk=g_data.disk;
        g_data = save;
        term_print(t, COL_GREEN, "[%s] reset to textbook defaults", mod);
        return;
    }
    if (strcmp(op,"set")!=0){ term_print(t, COL_RED,"unknown op '%s'", op); return; }

    /* ---- set handlers ---- */
    if (is_sched){
        char field[16]={0}, what[16]={0}; int val=0;
        if (sscanf(rest,"p%d %15s %d",&val,what,&val)==0){}
        int pid=0; char w[16]={0}; int v=0;
        if (sscanf(rest,"p%d %15s %d",&pid,w,&v)==3){
            SchedData *s=&g_data.sched; int idx=-1;
            for(int i=0;i<s->n;i++) if(s->id[i]==pid) idx=i;
            if(idx<0){ term_print(t,COL_RED,"no P%d",pid); return; }
            if(!strcmp(w,"burst")) s->burst[idx]=v;
            else if(!strcmp(w,"arrival")) s->arrival[idx]=v;
            else if(!strcmp(w,"prio")||!strcmp(w,"priority")) s->prio[idx]=v;
            else { term_print(t,COL_RED,"field?"); return; }
            term_print(t,COL_GREEN,"P%d %s = %d",pid,w,v); show_sched(a); return;
        }
        if (sscanf(rest,"%15s %d",field,&val)==2 && !strcmp(field,"quantum")){
            g_data.sched.quantum=val; term_print(t,COL_GREEN,"quantum=%d",val); return;
        }
        term_print(t,COL_RED,"usage: data sched set p<n> <burst|arrival|prio> <v> | set quantum <v>");
    } else if (is_mem){
        char jid[8]={0}; int v=0;
        if (sscanf(rest,"job %7s %d",jid,&v)==2){
            MemData *m=&g_data.mem; int idx=-1;
            for(int i=0;i<m->njob;i++) if(toupper(m->jid[i])==toupper(jid[0])) idx=i;
            if(idx<0){ term_print(t,COL_RED,"no job %s",jid); return; }
            m->job[idx]=v; term_print(t,COL_GREEN,"job %c=%dK",toupper(jid[0]),v); show_mem(a); return;
        }
        if (sscanf(rest,"page %d",&v)==1 || sscanf(rest,"pagesize %d",&v)==1){
            g_data.mem.page_size=v; term_print(t,COL_GREEN,"page_size=%dK",v); return;
        }
        term_print(t,COL_RED,"usage: data mem set job <A..> <K> | set page <K>");
    } else if (is_vmem){
        int v=0;
        if (sscanf(rest,"frames %d",&v)==1){
            if(v<1)v=1; if(v>10)v=10; g_data.vmem.frames=v;
            term_print(t,COL_GREEN,"frames=%d",v); return;
        }
        if (!strncmp(rest,"refs",4)){
            VmemData *vm=&g_data.vmem; const char *p=rest+4; int cnt=0,x;
            while(cnt<64 && sscanf(p," %d",&x)==1){ vm->ref[cnt++]=x;
                while(*p==' ')p++; while(*p && *p!=' ')p++; }
            if(cnt>0){ vm->n=cnt; term_print(t,COL_GREEN,"refs set (%d)",cnt); show_vmem(a);} return;
        }
        term_print(t,COL_RED,"usage: data vmem set frames <n> | set refs <p1 p2 ...>");
    } else { /* disk */
        int v=0;
        if (sscanf(rest,"start %d",&v)==1){ g_data.disk.start=v;
            term_print(t,COL_GREEN,"start=%d",v); return; }
        if (!strncmp(rest,"dir",3)){
            g_data.disk.dirdown = (strstr(rest,"down")!=NULL);
            term_print(t,COL_GREEN,"dir=%s",g_data.disk.dirdown?"down":"up"); return;
        }
        if (!strncmp(rest,"queue",5)){
            DiskData *d=&g_data.disk; const char *p=rest+5; int cnt=0,x;
            while(cnt<32 && sscanf(p," %d",&x)==1){ d->req[cnt++]=x;
                while(*p==' ')p++; while(*p && *p!=' ')p++; }
            if(cnt>0){ d->n=cnt; term_print(t,COL_GREEN,"queue set (%d)",cnt); show_disk(a);} return;
        }
        term_print(t,COL_RED,"usage: data disk set start <n> | set queue <t1 t2 ...> | set dir up|down");
    }
}
