/* memory.c - Module 5 memory management: fits, paging, swapping */
#include "app.h"
#include <string.h>
#include <stdio.h>

/* fixed-region allocation with a placement policy.
   policy: 0 first, 1 best(smallest), 2 worst(largest), 3 best-available(smallest free) */
static void alloc_policy(App *a, int policy, const char *name){
    Terminal *t=&a->term; MemData *m=&g_data.mem;
    int occ[16]; char who[16];
    for(int i=0;i<m->nreg;i++){ occ[i]=0; who[i]=' '; }

    /* job queue (FIFO), skip-to-tail when first job can't fit */
    int qjob[64]; char qid[64]; int qh=0,qt=0;
    for(int i=0;i<m->njob;i++){ qjob[qt]=m->job[i]; qid[qt]=m->jid[i]; qt++; }

    term_print(t, COL_CYAN, "=== MEMORY ALLOCATION : %s ===", name);
    char hdr[128]={0}; int p=0; p+=snprintf(hdr+p,128-p,"  regions:");
    for(int i=0;i<m->nreg;i++)p+=snprintf(hdr+p,128-p," [%dK]",m->reg[i]);
    term_queue(t,120,COL_GREEN,"%s",hdr);

    int fails=0, placed=0;
    while(qh<qt && fails<=(qt-qh)){
        int sz=qjob[qh]; char id=qid[qh];
        int pick=-1;
        for(int i=0;i<m->nreg;i++){
            if(occ[i] || m->reg[i]<sz) continue;
            if(pick<0){ pick=i; continue; }
            if(policy==0){ /* first fit: keep earliest */ }
            else if(policy==2){ if(m->reg[i]>m->reg[pick]) pick=i; }   /* worst */
            else { if(m->reg[i]<m->reg[pick]) pick=i; }                /* best / best-avail */
        }
        if(pick<0){
            term_queue(t,150,COL_RED,"  job %c (%dK) -> no free region fits, SKIP to tail",id,sz);
            qjob[qt]=sz; qid[qt]=id; qt++; qh++; fails++;
            continue;
        }
        occ[pick]=1; who[pick]=id; fails=0; placed++;
        int frag=m->reg[pick]-sz;
        term_queue(t,200,COL_GREEN,"  job %c (%dK) -> region[%d]=%dK   (internal frag %dK)",
                   id,sz,pick,m->reg[pick],frag);
        qh++;
    }

    /* final memory map */
    term_queue(t,200,COL_AMBER,"  --- final memory map ---");
    int totfrag=0, used=0;
    for(int i=0;i<m->nreg;i++){
        if(occ[i]){ int frag=0; /* find job size */
            /* recompute frag: region - jobsize; jobsize unknown here, recompute by matching id */
            for(int j=0;j<m->njob;j++) if(m->jid[j]==who[i]) frag=m->reg[i]-m->job[j];
            char bar[24]; int n=m->reg[i]; if(n>20)n=20; int k; for(k=0;k<n;k++)bar[k]='#'; bar[k]=0;
            term_queue(t,90,COL_GREEN,"  region[%d] %2dK |%-20s| job %c  (free %dK)",
                       i,m->reg[i],bar,who[i],frag);
            totfrag+=frag; used++;
        } else {
            term_queue(t,90,COL_GRAY ,"  region[%d] %2dK |%-20s| FREE", i, m->reg[i], "");
        }
    }
    term_queue(t,180,COL_AMBER,"  regions used = %d/%d   total internal fragmentation = %dK",
               used,m->nreg,totfrag);
    /* unplaced */
    if(qh<qt){
        char buf[128]={0}; int q2=0; q2+=snprintf(buf+q2,128-q2,"  could NOT place:");
        for(int i=qh;i<qt;i++) q2+=snprintf(buf+q2,128-q2," %c(%dK)",qid[i],qjob[i]);
        term_queue(t,120,COL_RED,"%s",buf);
    }
}

static void paging_demo(App *a){
    Terminal *t=&a->term; MemData *m=&g_data.mem;
    int P=m->page_size; if(P<1)P=4;
    term_print(t, COL_CYAN, "=== PAGING : address translation (page size = %dK units) ===", P);
    term_queue(t,120,COL_GREEN,"  formulas:  p = LA / P     d = LA %% P     PA = f*P + d");
    /* sample page table (textbook: 0->5 1->6 2->1 3->2) */
    int ftab[4]={5,6,1,2};
    term_queue(t,120,COL_AMBER,"  page table:  0->f5  1->f6  2->f1  3->f2");
    int LAs[4]={0,3,10,15};
    for(int i=0;i<4;i++){
        int la=LAs[i]; int pp=la/P, d=la%P;
        if(pp>3){ term_queue(t,140,COL_RED,"  LA=%d -> p=%d (out of range)",la,pp); continue; }
        int f=ftab[pp]; int pa=f*P+d;
        term_queue(t,220,COL_GREEN,"  LA=%2d -> p=%d d=%d -> f=%d -> PA = %d*%d+%d = %d",
                   la,pp,d,f,P,d,pa);
    }
    term_queue(t,160,COL_AMBER,"  internal fragmentation example: job 30K in %d-K pages",P*8);
    int pages=(30 + P -1)/P; int alloc=pages*P;
    term_queue(t,120,COL_GREEN,"  30K needs %d pages = %dK allocated -> frag = %dK",
               pages, alloc, alloc-30);
    term_queue(t,120,COL_DGREEN,"  paging => NO external fragmentation, only some internal.");
}

static void swap_demo(App *a, const char *args){
    Terminal *t=&a->term;
    long words=20000, rate=250000; double lat=8.0;
    sscanf(args,"%*s %ld %ld %lf",&words,&rate,&lat);  /* "swap [words rate lat]" */
    double xfer = (double)words/rate*1000.0;  /* ms */
    double one = xfer + lat;
    term_print(t, COL_CYAN, "=== SWAPPING : swap time ===");
    term_queue(t,160,COL_GREEN,"  job size      = %ld words", words);
    term_queue(t,120,COL_GREEN,"  transfer rate = %ld words/sec", rate);
    term_queue(t,120,COL_GREEN,"  avg latency   = %.0f ms", lat);
    term_queue(t,220,COL_AMBER,"  transfer time = %ld/%ld = %.0f ms", words, rate, xfer);
    term_queue(t,160,COL_AMBER,"  one-way swap  = %.0f + %.0f = %.0f ms", xfer, lat, one);
    term_queue(t,200,COL_RED,  "  TOTAL (in+out)= %.0f x 2 = %.0f ms", one, one*2);
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
