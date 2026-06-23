/* sched.c - Module 4 CPU scheduling algorithms with animated Gantt output */
#include "app.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define MAXP 16
#define MAXSEG 512

typedef struct {
    int seg_pid[MAXSEG];   /* -1 = idle */
    int seg_a[MAXSEG], seg_b[MAXSEG];
    int nseg;
    int completion[MAXP], turnaround[MAXP], waiting[MAXP], response[MAXP], first[MAXP];
} SResult;

static void add_seg(SResult *R, int pid, int a, int b){
    if (a >= b) return;
    if (R->nseg > 0 && R->seg_pid[R->nseg-1] == pid && R->seg_b[R->nseg-1] == a){
        R->seg_b[R->nseg-1] = b;  /* coalesce */
        return;
    }
    if (R->nseg < MAXSEG){
        R->seg_pid[R->nseg]=pid; R->seg_a[R->nseg]=a; R->seg_b[R->nseg]=b; R->nseg++;
    }
}

/* finalize per-process metrics from completion times */
static void finalize(SResult *R, SchedData *s){
    for (int i=0;i<s->n;i++){
        R->turnaround[i] = R->completion[i] - s->arrival[i];
        R->waiting[i]    = R->turnaround[i] - s->burst[i];
        R->response[i]   = R->first[i] - s->arrival[i];
    }
}

/* ---- non-preemptive: key picks next among arrived & unfinished ----
   mode: 0 FCFS(arrival), 1 SJF(burst), 2 NPP(prio), 3 HRRN(ratio) */
static void sim_np(SchedData *s, SResult *R, int mode){
    int done=0, t=0; int fin[MAXP]={0};
    memset(R,0,sizeof(*R));
    for(int i=0;i<s->n;i++) R->first[i]=-1;
    while (done < s->n){
        int best=-1; double bestkey=0;
        for (int i=0;i<s->n;i++){
            if (fin[i] || s->arrival[i] > t) continue;
            double key;
            switch(mode){
            case 0: key = s->arrival[i]*1000 + s->id[i]; break;      /* FCFS */
            case 1: key = s->burst[i]*1000 + s->id[i]; break;        /* SJF  */
            case 2: key = s->prio[i]*1000 + s->id[i]; break;         /* NPP smaller=higher */
            default:{ double wait=t-s->arrival[i];                    /* HRRN highest ratio */
                      double rr=(wait+s->burst[i])/(double)s->burst[i];
                      key = -rr*1000 + s->id[i]; } break;
            }
            if (best<0 || key<bestkey){ best=i; bestkey=key; }
        }
        if (best<0){ /* idle until next arrival */
            int na=1<<30; for(int i=0;i<s->n;i++) if(!fin[i]&&s->arrival[i]<na) na=s->arrival[i];
            add_seg(R,-1,t,na); t=na; continue;
        }
        if (R->first[best]<0) R->first[best]=t;
        add_seg(R,s->id[best],t,t+s->burst[best]);
        t += s->burst[best];
        R->completion[best]=t; fin[best]=1; done++;
    }
    finalize(R,s);
}

/* ---- preemptive unit-step: mode 0 SRTF(remaining), 1 PP(priority) ---- */
static void sim_pre(SchedData *s, SResult *R, int mode){
    int rem[MAXP]; int done=0, t=0;
    memset(R,0,sizeof(*R));
    for(int i=0;i<s->n;i++){ rem[i]=s->burst[i]; R->first[i]=-1; }
    int total=0; for(int i=0;i<s->n;i++) total+=s->burst[i];
    int guard=0;
    while (done<s->n && guard++ < 100000){
        int best=-1; double bestkey=0;
        for(int i=0;i<s->n;i++){
            if (rem[i]<=0 || s->arrival[i]>t) continue;
            double key = (mode==0)? rem[i]*1000.0+s->id[i] : s->prio[i]*1000.0+s->id[i];
            if (best<0||key<bestkey){best=i;bestkey=key;}
        }
        if (best<0){ add_seg(R,-1,t,t+1); t++; continue; }
        if (R->first[best]<0) R->first[best]=t;
        add_seg(R,s->id[best],t,t+1);
        rem[best]--; t++;
        if (rem[best]==0){ R->completion[best]=t; done++; }
    }
    finalize(R,s);
}

/* ---- round robin ---- */
static void sim_rr(SchedData *s, SResult *R, int q){
    int rem[MAXP]; int t=0, done=0;
    memset(R,0,sizeof(*R));
    for(int i=0;i<s->n;i++){ rem[i]=s->burst[i]; R->first[i]=-1; }
    if (q<1) q=1;
    /* order procs by arrival for the initial queue */
    int order[MAXP]; for(int i=0;i<s->n;i++) order[i]=i;
    for(int i=0;i<s->n;i++)for(int j=i+1;j<s->n;j++)
        if (s->arrival[order[j]]<s->arrival[order[i]]){int k=order[i];order[i]=order[j];order[j]=k;}
    int queue[256], qh=0, qt=0; int inq[MAXP]={0};
    int idx=0;
    /* advance time to first arrival */
    int minA=1<<30; for(int i=0;i<s->n;i++) if(s->arrival[i]<minA)minA=s->arrival[i];
    if (t<minA) t=minA;
    while (idx<s->n && s->arrival[order[idx]]<=t){ queue[qt++]=order[idx]; inq[order[idx]]=1; idx++; }
    int guard=0;
    while (done<s->n && guard++<100000){
        if (qh==qt){ /* idle */
            int na=1<<30; for(int i=0;i<s->n;i++) if(rem[i]>0&&s->arrival[i]>t&&s->arrival[i]<na)na=s->arrival[i];
            if(na==(1<<30)) break;
            add_seg(R,-1,t,na); t=na;
            while(idx<s->n && s->arrival[order[idx]]<=t){queue[qt++]=order[idx];inq[order[idx]]=1;idx++;}
            continue;
        }
        int i=queue[qh++];
        if (R->first[i]<0) R->first[i]=t;
        int run = rem[i]<q?rem[i]:q;
        add_seg(R,s->id[i],t,t+run); t+=run; rem[i]-=run;
        /* enqueue arrivals that came during this slice */
        while(idx<s->n && s->arrival[order[idx]]<=t){queue[qt++]=order[idx];inq[order[idx]]=1;idx++;}
        if (rem[i]>0){ queue[qt++]=i; }        /* requeue at tail */
        else { rem[i]=0; R->completion[i]=t; done++; }
    }
    finalize(R,s);
}

/* ---- output: animated execution trace + Gantt + metrics ---- */
static void emit(App *a, SchedData *s, SResult *R, const char *name){
    Terminal *t=&a->term;
    term_print(t, COL_CYAN, "=== CPU SCHEDULING : %s ===", name);
    /* animated trace */
    for (int k=0;k<R->nseg;k++){
        char bars[40]; int w=R->seg_b[k]-R->seg_a[k]; if(w>32)w=32; int j;
        for(j=0;j<w;j++)bars[j]='#'; bars[j]=0;
        if (R->seg_pid[k]<0)
            term_queue(t,90,COL_GRAY,  "  t=%2d  --IDLE--   %-32s (%d->%d)", R->seg_a[k], bars, R->seg_a[k],R->seg_b[k]);
        else
            term_queue(t,150,COL_GREEN,"  t=%2d  >> P%-2d     %-32s (%d->%d)",
                       R->seg_a[k], R->seg_pid[k], bars, R->seg_a[k], R->seg_b[k]);
    }
    /* compact gantt */
    char g[TERM_MAXLINE]="GANTT: |"; int p=(int)strlen(g);
    for(int k=0;k<R->nseg;k++){
        if(R->seg_pid[k]<0) p+=snprintf(g+p,TERM_MAXLINE-p," ## %d-%d |",R->seg_a[k],R->seg_b[k]);
        else p+=snprintf(g+p,TERM_MAXLINE-p," P%d %d-%d |",R->seg_pid[k],R->seg_a[k],R->seg_b[k]);
        if(p>TERM_MAXLINE-24){snprintf(g+p,TERM_MAXLINE-p," ...");break;}
    }
    term_queue(t,250,COL_AMBER,"%s",g);
    /* metrics table */
    term_queue(t,200,COL_CYAN, "  ID  AT  BT  Pr  Comp  TAT  Wait  Resp");
    int swt=0,stat=0,sbt=0,mk=0;
    for(int i=0;i<s->n;i++){
        term_queue(t,80,COL_GREEN,"  P%-2d %3d %3d %3d  %4d %4d  %4d  %4d",
            s->id[i],s->arrival[i],s->burst[i],s->prio[i],
            R->completion[i],R->turnaround[i],R->waiting[i],R->response[i]);
        swt+=R->waiting[i]; stat+=R->turnaround[i]; sbt+=s->burst[i];
        if(R->completion[i]>mk)mk=R->completion[i];
    }
    double awt=(double)swt/s->n, atat=(double)stat/s->n;
    double util = mk? 100.0*sbt/mk : 0;
    double thru = mk? (double)s->n/mk : 0;
    term_queue(t,200,COL_AMBER,"  avg waiting = %.2f    avg turnaround = %.2f", awt, atat);
    term_queue(t,120,COL_AMBER,"  CPU util = %.1f%%   throughput = %.3f proc/unit   makespan = %d",
               util, thru, mk);
}

/* ---- MLFQ: Q0 q=4, Q1 q=8, Q2 FCFS; demote on quantum expiry; preemptive ---- */
static void sched_mlfq(App *a){
    SchedData *s=&g_data.sched; SResult R; memset(&R,0,sizeof R);
    int rem[MAXP],lvl[MAXP],slice[MAXP],seq[MAXP],fin[MAXP]={0};
    int q[3]={4,8,1<<30}; int seqc=0,t=0,done=0,cur=-1,guard=0;
    for(int i=0;i<s->n;i++){ rem[i]=s->burst[i]; lvl[i]=0; slice[i]=0; seq[i]=-1; R.first[i]=-1; }
    while(done<s->n && guard++<200000){
        for(int i=0;i<s->n;i++) if(!fin[i]&&s->arrival[i]<=t&&seq[i]<0) seq[i]=seqc++;
        int ml=99; for(int i=0;i<s->n;i++) if(!fin[i]&&s->arrival[i]<=t&&rem[i]>0&&lvl[i]<ml) ml=lvl[i];
        if(ml==99){ int na=1<<30; for(int i=0;i<s->n;i++) if(!fin[i]&&s->arrival[i]>t&&s->arrival[i]<na)na=s->arrival[i];
            if(na==(1<<30))break; add_seg(&R,-1,t,na); t=na; continue; }
        int pick=-1;
        if(cur>=0&&!fin[cur]&&lvl[cur]==ml&&rem[cur]>0&&slice[cur]<q[ml]) pick=cur;
        if(pick<0){ int bs=1<<30; for(int i=0;i<s->n;i++)
            if(!fin[i]&&s->arrival[i]<=t&&rem[i]>0&&lvl[i]==ml&&seq[i]<bs){bs=seq[i];pick=i;} }
        if(R.first[pick]<0)R.first[pick]=t;
        add_seg(&R,s->id[pick],t,t+1); rem[pick]--; slice[pick]++; t++; cur=pick;
        if(rem[pick]==0){ fin[pick]=1; done++; R.completion[pick]=t; cur=-1; }
        else if(slice[pick]>=q[ml] && ml<2){ lvl[pick]=ml+1; slice[pick]=0; seq[pick]=seqc++; cur=-1; }
    }
    finalize(&R,s); emit(a,s,&R,"MLFQ (Q0 q=4, Q1 q=8, Q2 FCFS)");
    term_queue(&a->term, 150, COL_DGREEN,
        "  note: new jobs enter Q0; a job using its quantum is demoted to a lower queue.");
}

/* ---- MLQ: fixed queues by priority, preemptive between queues ----
   Q0(prio1)=RR q=2, Q1(prio2-3)=FCFS, Q2(prio>=4)=SJF ---- */
static void sched_mlq(App *a){
    SchedData *s=&g_data.sched; SResult R; memset(&R,0,sizeof R);
    int rem[MAXP],lvl[MAXP],slice[MAXP],seq[MAXP],fin[MAXP]={0};
    int seqc=0,t=0,done=0,cur=-1,guard=0;
    for(int i=0;i<s->n;i++){ rem[i]=s->burst[i]; slice[i]=0; seq[i]=-1; R.first[i]=-1;
        lvl[i] = (s->prio[i]<=1)?0 : (s->prio[i]<=3?1:2); }
    while(done<s->n && guard++<200000){
        for(int i=0;i<s->n;i++) if(!fin[i]&&s->arrival[i]<=t&&seq[i]<0) seq[i]=seqc++;
        int ml=99; for(int i=0;i<s->n;i++) if(!fin[i]&&s->arrival[i]<=t&&rem[i]>0&&lvl[i]<ml) ml=lvl[i];
        if(ml==99){ int na=1<<30; for(int i=0;i<s->n;i++) if(!fin[i]&&s->arrival[i]>t&&s->arrival[i]<na)na=s->arrival[i];
            if(na==(1<<30))break; add_seg(&R,-1,t,na); t=na; continue; }
        int pick=-1;
        if(cur>=0&&!fin[cur]&&lvl[cur]==ml&&rem[cur]>0){
            if(ml==0 && slice[cur]>=2){ seq[cur]=seqc++; slice[cur]=0; } /* RR rotate */
            else pick=cur;
        }
        if(pick<0){
            if(ml==2){ int br=1<<30; for(int i=0;i<s->n;i++)   /* SJF */
                if(!fin[i]&&s->arrival[i]<=t&&rem[i]>0&&lvl[i]==ml&&rem[i]<br){br=rem[i];pick=i;} }
            else { int bs=1<<30; for(int i=0;i<s->n;i++)        /* RR / FCFS by seq */
                if(!fin[i]&&s->arrival[i]<=t&&rem[i]>0&&lvl[i]==ml&&seq[i]<bs){bs=seq[i];pick=i;} }
        }
        if(pick!=cur) slice[pick]=0;
        if(R.first[pick]<0)R.first[pick]=t;
        add_seg(&R,s->id[pick],t,t+1); rem[pick]--; slice[pick]++; t++; cur=pick;
        if(rem[pick]==0){ fin[pick]=1; done++; R.completion[pick]=t; cur=-1; }
    }
    finalize(&R,s); emit(a,s,&R,"MLQ (Q0=RR2 prio1, Q1=FCFS prio2-3, Q2=SJF prio>=4)");
    term_queue(&a->term, 150, COL_DGREEN,
        "  note: higher-priority queues fully preempt lower ones (fixed-priority).");
}

void sched_run(App *a, const char *args){
    Terminal *t=&a->term;
    SchedData *s=&g_data.sched;
    char algo[24]={0}; int q=s->quantum;
    sscanf(args,"%23s %d",algo,&q);
    if(!algo[0]){
        term_print(t,COL_AMBER,"usage: sched <fcfs|sjf|srtf|npp|pp|rr [q]|hrrn|mlq|mlfq>");
        return;
    }
    SResult R;
    if(!strcmp(algo,"fcfs")){ sim_np(s,&R,0); emit(a,s,&R,"FCFS (First-Come First-Served)"); }
    else if(!strcmp(algo,"sjf")){ sim_np(s,&R,1); emit(a,s,&R,"SJF non-preemptive"); }
    else if(!strcmp(algo,"npp")||!strcmp(algo,"priority")){ sim_np(s,&R,2); emit(a,s,&R,"Priority (non-preemptive)"); }
    else if(!strcmp(algo,"hrrn")){ sim_np(s,&R,3); emit(a,s,&R,"HRRN (Highest Response Ratio Next)"); }
    else if(!strcmp(algo,"srtf")){ sim_pre(s,&R,0); emit(a,s,&R,"SRTF (preemptive SJF)"); }
    else if(!strcmp(algo,"pp")){ sim_pre(s,&R,1); emit(a,s,&R,"Priority (preemptive)"); }
    else if(!strcmp(algo,"rr")){ char nm[32]; snprintf(nm,32,"Round Robin (q=%d)",q);
                                 sim_rr(s,&R,q); emit(a,s,&R,nm); }
    else if(!strcmp(algo,"mlq")){ sched_mlq(a); }
    else if(!strcmp(algo,"mlfq")){ sched_mlfq(a); }
    else term_print(t,COL_RED,"unknown sched algo '%s'",algo);
}
