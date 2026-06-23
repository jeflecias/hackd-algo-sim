/* test_algos.c - console verification of pure algorithm cores vs the PDFs */
#include <stdio.h>

int vmem_faults(int mode, const int *ref, int n, int F);
int disk_total(int mode,int start,const int*req,int n,int down,int dmin,int dmax);

static int pass=0, fail=0;
static void chk(const char *name, long got, long want){
    int ok = (got==want);
    if(ok)pass++; else fail++;
    printf("  [%s] %-28s got=%ld want=%ld\n", ok?"PASS":"FAIL", name, got, want);
}

int main(void){
    int rs[20]={7,0,1,2,0,3,0,4,2,3,0,3,2,1,2,0,1,7,0,1};
    printf("VIRTUAL MEMORY (ref string, 3 frames)\n");
    chk("FIFO faults",  vmem_faults(0,rs,20,3), 15);
    chk("OPT faults",   vmem_faults(1,rs,20,3),  9);
    chk("LRU faults",   vmem_faults(2,rs,20,3), 12);
    chk("LFU faults",   vmem_faults(3,rs,20,3), 13); /* correct LFU (FIFO tie-break);
                                                         PDF slide's count bookkeeping reports 15 */
    int bel[12]={1,2,3,4,1,2,5,1,2,3,4,5};
    chk("FIFO Belady 3f",vmem_faults(0,bel,12,3), 9);
    chk("FIFO Belady 4f",vmem_faults(0,bel,12,4),10);

    printf("DISK SCHEDULING (head 53, standard queue)\n");
    int rq[8]={98,183,37,122,14,124,65,67};
    chk("FCFS movement",  disk_total(0,53,rq,8,1,0,199), 640);
    chk("SSTF movement",  disk_total(1,53,rq,8,1,0,199), 236);
    chk("SCAN(down) move",disk_total(2,53,rq,8,1,0,199), 236);
    chk("LOOK(up) move",  disk_total(4,53,rq,8,0,0,199), 299);
    chk("C-LOOK(up) move",disk_total(5,53,rq,8,0,0,199), 322);

    printf("PAGING / SWAP / EAT\n");
    { int P=4, ft[4]={5,6,1,2};
      chk("paging PA(LA=3)",  ft[3/P]*P+3%P, 23);
      chk("paging PA(LA=10)", ft[10/P]*P+10%P, 6); }
    { long words=20000,rate=250000; double lat=8.0;
      double one=(double)words/rate*1000.0+lat; chk("swap total ms",(long)(one*2),176); }
    { double p=0.2,ma=5,pf=10000; chk("EAT us",(long)((1-p)*ma+p*pf),2004); }

    printf("\n==== %d passed, %d failed ====\n", pass, fail);
    return fail?1:0;
}
