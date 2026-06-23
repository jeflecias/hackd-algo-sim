/* vmem.c - Module 6 demand paging & page replacement */
#include "app.h"
#include <string.h>
#include <stdio.h>

/* mode: 0 FIFO, 1 OPT, 2 LRU, 3 LFU, 4 MFU, 5 SECOND(clock).
   if a != NULL, queue an animated trace. returns total page faults. */
static int compute(App *a, int mode, const int *ref, int n, int F){
    int fr[16], load[16], last[16], cnt[16], rb[16];
    for(int i=0;i<F;i++){ fr[i]=-1; load[i]=last[i]=cnt[i]=rb[i]=0; }
    int filled=0, faults=0, clock=0;

    for(int i=0;i<n;i++){
        int r=ref[i], hit=-1;
        for(int k=0;k<F;k++) if(fr[k]==r){ hit=k; break; }
        if(hit>=0){
            last[hit]=i; if(mode==3||mode==4) cnt[hit]++; rb[hit]=1;
        } else {
            faults++;
            int slot=-1;
            if(filled<F){ slot=filled++; }
            else {
                if(mode==0){ int mn=1<<30; for(int k=0;k<F;k++) if(load[k]<mn){mn=load[k];slot=k;} }
                else if(mode==2){ int mn=1<<30; for(int k=0;k<F;k++) if(last[k]<mn){mn=last[k];slot=k;} }
                else if(mode==1){ /* OPT: farthest next use */
                    int best=-1, bestd=-1;
                    for(int k=0;k<F;k++){
                        int d=1<<30; /* INF if never used again */
                        for(int j=i+1;j<n;j++) if(ref[j]==fr[k]){ d=j; break; }
                        if(d>bestd){ bestd=d; best=k; }
                    }
                    slot=best;
                }
                else if(mode==3){ int mn=1<<30,ml=1<<30; for(int k=0;k<F;k++)
                    if(cnt[k]<mn||(cnt[k]==mn&&load[k]<ml)){mn=cnt[k];ml=load[k];slot=k;} }
                else if(mode==4){ int mx=-1,ml=1<<30; for(int k=0;k<F;k++)
                    if(cnt[k]>mx||(cnt[k]==mx&&load[k]<ml)){mx=cnt[k];ml=load[k];slot=k;} }
                else { /* SECOND CHANCE clock */
                    while(rb[clock]==1){ rb[clock]=0; clock=(clock+1)%F; }
                    slot=clock; clock=(clock+1)%F;
                }
            }
            fr[slot]=r; load[slot]=i; last[slot]=i; cnt[slot]=1; rb[slot]=1;
        }
        if(a){
            char box[64]={0}; int p=0; p+=snprintf(box+p,64-p,"[");
            for(int k=0;k<F;k++){ if(fr[k]<0) p+=snprintf(box+p,64-p," ."); else p+=snprintf(box+p,64-p," %d",fr[k]); }
            p+=snprintf(box+p,64-p," ]");
            term_queue(&a->term, 170, hit>=0?COL_DGREEN:COL_GREEN,
                "  ref %2d  %-22s %s  (faults=%d)", r, box, hit>=0?"hit ":"FAULT", faults);
        }
    }
    return faults;
}

/* public accessor for self-tests (no animation) */
int vmem_faults(int mode, const int *ref, int n, int F){
    return compute(NULL, mode, ref, n, F);
}

static const char *NAME(int m){
    const char *N[]={"FIFO","OPTIMAL (MIN)","LRU","LFU","MFU","SECOND-CHANCE (CLOCK)"};
    return N[m];
}

static void run_mode(App *a, int mode){
    VmemData *v=&g_data.vmem; Terminal *t=&a->term;
    term_print(t, COL_CYAN, "=== PAGE REPLACEMENT : %s  (%d frames) ===", NAME(mode), v->frames);
    char rs[256]={0}; int p=0; p+=snprintf(rs+p,256-p,"  ref string:");
    for(int i=0;i<v->n;i++) p+=snprintf(rs+p,256-p," %d",v->ref[i]);
    term_queue(t,120,COL_AMBER,"%s",rs);
    int f=compute(a,mode,v->ref,v->n,v->frames);
    double rate=100.0*f/v->n;
    term_queue(t,220,COL_RED,"  >> total page faults = %d / %d  (fault rate %.1f%%)",f,v->n,rate);
}

static void belady_demo(App *a){
    Terminal *t=&a->term;
    int rs[12]={1,2,3,4,1,2,5,1,2,3,4,5};
    term_print(t, COL_CYAN, "=== BELADY'S ANOMALY (FIFO) ===");
    term_queue(t,120,COL_AMBER,"  ref: 1 2 3 4 1 2 5 1 2 3 4 5");
    int f3=compute(a,0,rs,12,3);
    term_queue(t,200,COL_GREEN,"  FIFO with 3 frames -> %d faults",f3);
    int f4=compute(NULL,0,rs,12,4);
    term_queue(t,200,COL_RED,  "  FIFO with 4 frames -> %d faults  (MORE frames, MORE faults!)",f4);
    term_queue(t,160,COL_DGREEN,"  that anomaly is why LRU/OPT (stack algorithms) are preferred.");
}

static void eat_demo(App *a, const char *args){
    Terminal *t=&a->term;
    double pf=10.0, ma=5.0, prob=0.2;  /* pf ms, ma us, p */
    sscanf(args,"%*s %lf %lf %lf",&prob,&ma,&pf);
    double eat=(1.0-prob)*ma + prob*(pf*1000.0); /* us */
    term_print(t, COL_CYAN, "=== EFFECTIVE ACCESS TIME (demand paging) ===");
    term_queue(t,140,COL_GREEN,"  EAT = (1-p)*ma + p*page_fault_time");
    term_queue(t,140,COL_GREEN,"  p=%.2f   ma=%.0f us   page-fault=%.0f ms (%.0f us)",prob,ma,pf,pf*1000.0);
    term_queue(t,220,COL_AMBER,"  EAT = (%.2f)*%.0f + %.2f*%.0f = %.0f us",
               1.0-prob, ma, prob, pf*1000.0, eat);
}

void vmem_run(App *a, const char *args){
    Terminal *t=&a->term;
    char algo[24]={0}; sscanf(args,"%23s",algo);
    if(!algo[0]){ term_print(t,COL_AMBER,
        "usage: vmem <fifo|opt|lru|belady|lfu|mfu|second|eat>"); return; }
    if(!strcmp(algo,"fifo")) run_mode(a,0);
    else if(!strcmp(algo,"opt")||!strcmp(algo,"optimal")) run_mode(a,1);
    else if(!strcmp(algo,"lru")) run_mode(a,2);
    else if(!strcmp(algo,"lfu")) run_mode(a,3);
    else if(!strcmp(algo,"mfu")) run_mode(a,4);
    else if(!strcmp(algo,"second")||!strcmp(algo,"clock")) run_mode(a,5);
    else if(!strcmp(algo,"belady")) belady_demo(a);
    else if(!strcmp(algo,"eat")) eat_demo(a,args);
    else term_print(t,COL_RED,"unknown vmem algo '%s'",algo);
}
