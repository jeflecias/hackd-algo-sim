/* memory.c - Module 5 memory management: fits, paging, swapping */
#include "app.h"
#include <string.h>
#include <stdio.h>

/* fixed-region allocation with a placement policy.
   policy: 0 first, 1 best(smallest), 2 worst(largest), 3 best-available(smallest free) */
static void alloc_policy(App *a, int policy, const char *name){
    MemData *m=&g_data.mem;
    Anim *an=&a->anim; memset(&an->mem,0,sizeof an->mem);
    an->mem.mode=policy; an->mem.paging=0; an->mem.nreg=m->nreg;
    for(int i=0;i<m->nreg && i<16;i++) an->mem.reg[i]=m->reg[i];

    int occ[16]; for(int i=0;i<m->nreg;i++) occ[i]=0;
    int qjob[64]; char qid[64]; int qh=0,qt=0;
    for(int i=0;i<m->njob;i++){ qjob[qt]=m->job[i]; qid[qt]=m->jid[i]; qt++; }

    int fails=0, used=0, totfrag=0;
    while(qh<qt && fails<=(qt-qh)){
        int sz=qjob[qh]; char id=qid[qh];
        int pick=-1;
        for(int i=0;i<m->nreg;i++){
            if(occ[i] || m->reg[i]<sz) continue;
            if(pick<0){ pick=i; continue; }
            if(policy==0){ /* first fit */ }
            else if(policy==2){ if(m->reg[i]>m->reg[pick]) pick=i; }
            else { if(m->reg[i]<m->reg[pick]) pick=i; }
        }
        int st=an->mem.nstep;
        if(st<64){
            an->mem.step_job[st]=sz; an->mem.step_jid[st]=id;
            an->mem.step_region[st]=pick;
            an->mem.step_frag[st]=(pick>=0)?(m->reg[pick]-sz):0;
            an->mem.nstep++;
        }
        if(pick<0){ qjob[qt]=sz; qid[qt]=id; qt++; qh++; fails++; continue; }
        occ[pick]=1; fails=0; used++; totfrag+=m->reg[pick]-sz; qh++;
    }
    an->mem.used=used; an->mem.totfrag=totfrag;
    anim_begin(a, AV_MEM, name, "MEMORY ALLOCATION (internal frag = region - job)");
}

static void paging_demo(App *a){
    MemData *m=&g_data.mem;
    int P=m->page_size; if(P<1)P=4;
    int ftab[4]={5,6,1,2};        /* textbook page table */
    int LAs[4]={0,3,10,15};
    Anim *an=&a->anim; memset(&an->mem,0,sizeof an->mem);
    an->mem.paging=1; an->mem.page_size=P;
    int nc=0;
    for(int i=0;i<4;i++){
        int la=LAs[i], pp=la/P, d=la%P;
        if(pp>3) continue;
        an->mem.la[nc]=la; an->mem.pg[nc]=pp; an->mem.doff[nc]=d;
        an->mem.fr[nc]=ftab[pp]; an->mem.pa[nc]=ftab[pp]*P+d; nc++;
    }
    an->mem.nconv=nc;
    anim_begin(a, AV_MEM, "Paging", "Logical -> Physical Address Translation");
    int pages=(30+P-1)/P;
    anim_summary(a, COL_DGREEN, "internal-frag eg: 30K -> %d pages = %dK alloc, frag %dK",
                 pages, pages*P, pages*P-30);
    anim_summary(a, COL_DGREEN, "paging has NO external fragmentation.");
}

static void swap_demo(App *a, const char *args){
    long words=20000, rate=250000; double lat=8.0;
    sscanf(args,"%*s %ld %ld %lf",&words,&rate,&lat);
    double xfer=(double)words/rate*1000.0, one=xfer+lat;
    Anim *an=&a->anim; memset(&an->calc,0,sizeof an->calc);
    snprintf(an->calc.line[0],96,"job size      = %ld words", words);
    snprintf(an->calc.line[1],96,"transfer rate = %ld words/sec", rate);
    snprintf(an->calc.line[2],96,"avg latency   = %.0f ms", lat);
    snprintf(an->calc.line[3],96,"transfer time = %ld/%ld = %.0f ms", words, rate, xfer);
    snprintf(an->calc.line[4],96,"one-way swap  = %.0f + %.0f = %.0f ms", xfer, lat, one);
    an->calc.nline=5;
    snprintf(an->calc.result,96,"%.0f ms (swap in + swap out)", one*2);
    anim_begin(a, AV_CALC, "SWAP TIME", "Backing-store swap (size/rate + latency)");
}

void mem_run(App *a, const char *args){
    Terminal *t=&a->term;
    char algo[24]={0}; sscanf(args,"%23s",algo);
    if(!algo[0]){ term_print(t,COL_AMBER,
        "usage: mem <firstfit|bestfit|worstfit|bestavail|paging|swap>"); return; }
    if(!strcmp(algo,"firstfit")||!strcmp(algo,"first")) alloc_policy(a,0,"First Fit");
    else if(!strcmp(algo,"bestfit")||!strcmp(algo,"best")) alloc_policy(a,1,"Best Fit");
    else if(!strcmp(algo,"worstfit")||!strcmp(algo,"worst")) alloc_policy(a,2,"Worst Fit");
    else if(!strcmp(algo,"bestavail")) alloc_policy(a,3,"Best Available Fit");
    else if(!strcmp(algo,"paging")) paging_demo(a);
    else if(!strcmp(algo,"swap")) swap_demo(a,args);
    else term_print(t,COL_RED,"unknown mem algo '%s'",algo);
}
