/* selftest.c - verify algorithms against the PDF's published results */
#include "app.h"
#include <stdio.h>

int  vmem_faults(int mode, const int *ref, int n, int F);
int  disk_total(int mode,int start,const int*req,int n,int down,int dmin,int dmax);

static int g_pass, g_fail;
static void check(App *a, const char *what, long got, long want){
    Terminal *t=&a->term;
    int ok = (got==want);
    if(ok) g_pass++; else g_fail++;
    term_queue(t, 70, ok?COL_GREEN:COL_RED, "  [%s] %-34s got=%ld want=%ld",
               ok?"PASS":"FAIL", what, got, want);
}

void selftest_run(App *a){
    Terminal *t=&a->term;
    g_pass=g_fail=0;
    term_print(t, COL_CYAN, "=== SELF-TEST : algorithms vs textbook ===");

    /* ---- virtual memory (standard string, 3 frames) ---- */
    int rs[20]={7,0,1,2,0,3,0,4,2,3,0,3,2,1,2,0,1,7,0,1};
    check(a,"vmem FIFO faults",    vmem_faults(0,rs,20,3), 15);
    check(a,"vmem OPT faults",     vmem_faults(1,rs,20,3),  9);
    check(a,"vmem LRU faults",     vmem_faults(2,rs,20,3), 12);
    int bel[12]={1,2,3,4,1,2,5,1,2,3,4,5};
    check(a,"FIFO Belady 3 frames",vmem_faults(0,bel,12,3), 9);
    check(a,"FIFO Belady 4 frames",vmem_faults(0,bel,12,4),10);

    /* ---- disk scheduling (head 53, standard queue) ---- */
    int rq[8]={98,183,37,122,14,124,65,67};
    check(a,"disk FCFS movement",  disk_total(0,53,rq,8,1,0,199), 640);
    check(a,"disk SSTF movement",  disk_total(1,53,rq,8,1,0,199), 236);
    check(a,"disk SCAN(down) move",disk_total(2,53,rq,8,1,0,199), 236);
    check(a,"disk LOOK(up) move",  disk_total(4,53,rq,8,0,0,199), 299);
    check(a,"disk C-LOOK(up) move",disk_total(5,53,rq,8,0,0,199), 322);

    /* ---- paging address translation ---- */
    { int P=4, ftab[4]={5,6,1,2};
      int pa1 = ftab[3/P]*P + 3%P;     /* LA=3  -> PA 23 */
      int pa2 = ftab[10/P]*P + 10%P;   /* LA=10 -> PA 6  */
      check(a,"paging PA(LA=3)",  pa1, 23);
      check(a,"paging PA(LA=10)", pa2,  6);
    }

    /* ---- swapping ---- */
    { long words=20000, rate=250000; double lat=8.0;
      double one = (double)words/rate*1000.0 + lat; long total=(long)(one*2);
      check(a,"swap total ms", total, 176);
    }

    /* ---- EAT ---- */
    { double p=0.2, ma=5, pf=10000; long eat=(long)((1-p)*ma + p*pf);
      check(a,"EAT us", eat, 2004);
    }

    term_queue(t, 200, (g_fail==0)?COL_GREEN:COL_RED,
        ">> SELF-TEST COMPLETE : %d passed, %d failed", g_pass, g_fail);
}
