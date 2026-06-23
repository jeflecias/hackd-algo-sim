/* disk.c - Module 7 disk-head scheduling */
#include "app.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static int icmp(const void *a, const void *b){ return *(const int*)a - *(const int*)b; }
static int iabs(int x){ return x<0?-x:x; }

/* build the head path (positions visited, including endpoints/jumps) for a mode.
   mode 0 FCFS,1 SSTF,2 SCAN,3 CSCAN,4 LOOK,5 CLOOK.
   returns total head movement; fills path[]/pcount and req-stop flags isr[]. */
static int disk_compute(int mode, int start, const int *req, int n, int down,
                        int dmin, int dmax, int *path, int *isr, int *pcount){
    int s[32]; memcpy(s, req, n*sizeof(int)); qsort(s, n, sizeof(int), icmp);
    int pc=0;
    path[pc]=start; isr[pc]=0; pc++;

    if(mode==0){ /* FCFS */
        for(int i=0;i<n;i++){ path[pc]=req[i]; isr[pc]=1; pc++; }
    } else if(mode==1){ /* SSTF */
        int used[32]={0}; int cur=start;
        for(int c=0;c<n;c++){ int best=-1,bd=1<<30;
            for(int i=0;i<n;i++) if(!used[i]&&iabs(req[i]-cur)<bd){bd=iabs(req[i]-cur);best=i;}
            used[best]=1; cur=req[best]; path[pc]=cur; isr[pc]=1; pc++;
        }
    } else {
        /* split sorted into left(<start) and right(>=start) */
        int li[32],ri[32],ln=0,rn=0;
        for(int i=0;i<n;i++){ if(s[i]<start) li[ln++]=s[i]; else ri[rn++]=s[i]; }
        if(down){
            /* service decreasing first */
            for(int i=ln-1;i>=0;i--){ path[pc]=li[i]; isr[pc]=1; pc++; }
            if(mode==2){ path[pc]=dmin; isr[pc]=0; pc++; }                 /* SCAN to end */
            if(mode==3){ path[pc]=dmin; isr[pc]=0; pc++; path[pc]=dmax; isr[pc]=0; pc++;
                         for(int i=rn-1;i>=0;i--){ path[pc]=ri[i]; isr[pc]=1; pc++; } }
            else if(mode==5){ if(rn){ for(int i=rn-1;i>=0;i--){ path[pc]=ri[i]; isr[pc]=1; pc++; } } }
            else { for(int i=0;i<rn;i++){ path[pc]=ri[i]; isr[pc]=1; pc++; } } /* SCAN/LOOK reverse up */
        } else {
            /* service increasing first */
            for(int i=0;i<rn;i++){ path[pc]=ri[i]; isr[pc]=1; pc++; }
            if(mode==2){ path[pc]=dmax; isr[pc]=0; pc++; }
            if(mode==3){ path[pc]=dmax; isr[pc]=0; pc++; path[pc]=dmin; isr[pc]=0; pc++;
                         for(int i=0;i<ln;i++){ path[pc]=li[i]; isr[pc]=1; pc++; } }
            else if(mode==5){ if(ln){ for(int i=0;i<ln;i++){ path[pc]=li[i]; isr[pc]=1; pc++; } } }
            else { for(int i=ln-1;i>=0;i--){ path[pc]=li[i]; isr[pc]=1; pc++; } }
        }
    }
    int total=0; for(int i=1;i<pc;i++) total+=iabs(path[i]-path[i-1]);
    *pcount=pc; return total;
}

int disk_total(int mode,int start,const int*req,int n,int down,int dmin,int dmax){
    int path[64],isr[64],pc; return disk_compute(mode,start,req,n,down,dmin,dmax,path,isr,&pc);
}

static void emit(App *a, int mode, const char *name, int down){
    DiskData *d=&g_data.disk;
    int path[64],isr[64],pc;
    int total=disk_compute(mode,d->start,d->req,d->n,down,d->dmin,d->dmax,path,isr,&pc);

    Anim *an=&a->anim; memset(&an->disk,0,sizeof an->disk);
    an->disk.start=d->start; an->disk.dmin=d->dmin; an->disk.dmax=d->dmax;
    an->disk.n=d->n; for(int i=0;i<d->n && i<32;i++) an->disk.req[i]=d->req[i];
    an->disk.np = pc>64?64:pc;
    for(int i=0;i<an->disk.np;i++){ an->disk.path[i]=path[i]; an->disk.isreq[i]=isr[i]; }
    an->disk.total=total;
    anim_begin(a, AV_DISK, name, "DISK SCHEDULING");
}

void disk_run(App *a, const char *args){
    Terminal *t=&a->term; DiskData *d=&g_data.disk;
    char algo[24]={0}; int start=d->start;
    if(sscanf(args,"%23s %d",algo,&start)>=2) d->start=start;
    if(!algo[0]){ term_print(t,COL_AMBER,
        "usage: disk <fcfs|sstf|scan|cscan|look|clook> [start]"); return; }
    int down=d->dirdown;
    if(!strcmp(algo,"fcfs")) emit(a,0,"FCFS",down);
    else if(!strcmp(algo,"sstf")) emit(a,1,"SSTF (Shortest Seek Time First)",down);
    else if(!strcmp(algo,"scan")) emit(a,2,"SCAN (elevator)",down);
    else if(!strcmp(algo,"cscan")||!strcmp(algo,"c-scan")) emit(a,3,"C-SCAN (circular)",down);
    else if(!strcmp(algo,"look")) emit(a,4,"LOOK",down);
    else if(!strcmp(algo,"clook")||!strcmp(algo,"c-look")) emit(a,5,"C-LOOK",down);
    else term_print(t,COL_RED,"unknown disk algo '%s'",algo);
}
