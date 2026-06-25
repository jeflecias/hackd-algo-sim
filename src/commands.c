/* commands.c - command parsing and dispatch */
#include "app.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static void print_banner(App *a){
    Terminal *t = &a->term;
    term_print(t, COL_RED,   " ____    _____      _      ____    _        ___     ____   _  __");
    term_print(t, COL_RED,   "|  _ \\  | ____|    / \\    |  _ \\  | |      / _ \\   / ___| | |/ /");
    term_print(t, COL_RED,   "| | | | |  _|     / _ \\   | | | | | |     | | | | | |     | ' / ");
    term_print(t, COL_RED,   "| |_| | | |___   / ___ \\  | |_| | | |___  | |_| | | |___  | . \\ ");
    term_print(t, COL_RED,   "|____/  |_____| /_/   \\_\\ |____/  |_____|  \\___/   \\____| |_|\\_\\");
    term_print(t, COL_DGREEN," ::  s w a p   d a e m o n   v4.7.0   the cycle continues  ::");
}

static void print_help(App *a){
    Terminal *t = &a->term;
    term_print(t, COL_CYAN, "AVAILABLE COMMANDS");
    term_print(t, COL_GREEN, "  help [topic]      this menu  (topics: sched mem vmem disk)");
    term_print(t, COL_GREEN, "  man <algo>        explain an algorithm / concept");
    term_print(t, COL_GREEN, "  banner            redraw the logo");
    term_print(t, COL_GREEN, "  clear             wipe the screen");
    term_print(t, COL_GREEN, "  edit / data        open the live dataset editor (arrows + type)");
    term_print(t, COL_GREEN, "  data <module> set  edit a dataset from the shell (power users)");
    term_print(t, COL_GREEN, "  selftest          verify algorithms vs textbook results");
    term_print(t, COL_GREEN, "  exit              yield your frame and bail out (also ESC)");
    term_print(t, COL_AMBER, "MODULE 4 - CPU SCHEDULING");
    term_print(t, COL_GREEN, "  sched fcfs | sjf | srtf | npp | pp | rr [q] | hrrn | mlq | mlfq");
    term_print(t, COL_AMBER, "MODULE 5 - MEMORY MANAGEMENT");
    term_print(t, COL_GREEN, "  mem firstfit | bestfit | worstfit | bestavail | paging | swap");
    term_print(t, COL_AMBER, "MODULE 6 - VIRTUAL MEMORY");
    term_print(t, COL_GREEN, "  vmem fifo | opt | lru | belady | lfu | mfu | second | eat");
    term_print(t, COL_AMBER, "MODULE 7 - MASS STORAGE");
    term_print(t, COL_GREEN, "  disk fcfs | sstf | scan | cscan | look | clook  [start]");
    term_print(t, COL_DGREEN,"tip: try 'sched rr 2' or 'disk scan 53'  -- mind the skull.");
}

static void help_topic(App *a, const char *topic){
    Terminal *t = &a->term;
    if (!strcmp(topic, "sched")){
        term_print(t, COL_CYAN, "MODULE 4: CPU SCHEDULING");
        term_print(t, COL_GREEN,"Decides which ready process gets the CPU. Metrics: turnaround");
        term_print(t, COL_GREEN,"(=finish-arrival), waiting (=turnaround-burst), response time.");
        term_print(t, COL_GREEN,"Dataset: P1(AT0,BT8,Pr2) P2(AT1,BT4,Pr4) P3(AT2,BT9,Pr1) P4(AT3,BT5,Pr3)");
        term_print(t, COL_GREEN,"Run: sched fcfs | sjf | srtf | npp | pp | rr <q> | hrrn | mlq | mlfq");
    } else if (!strcmp(topic, "mem")){
        term_print(t, COL_CYAN, "MODULE 5: MEMORY MANAGEMENT");
        term_print(t, COL_GREEN,"Place jobs into memory holes. Policies: first/best/worst/best-avail fit.");
        term_print(t, COL_GREEN,"Paging: p=LA/P, d=LA%%P, PA=f*P+d. Swap time = words/rate + latency.");
        term_print(t, COL_GREEN,"Run: mem firstfit | bestfit | worstfit | bestavail | paging | swap");
    } else if (!strcmp(topic, "vmem")){
        term_print(t, COL_CYAN, "MODULE 6: VIRTUAL MEMORY");
        term_print(t, COL_GREEN,"Demand paging + page replacement. Lower fault rate = better.");
        term_print(t, COL_GREEN,"Ref string 7,0,1,2,0,3,0,4,2,3,0,3,2,1,2,0,1,7,0,1 / 3 frames.");
        term_print(t, COL_GREEN,"Run: vmem fifo | opt | lru | belady | lfu | mfu | second | eat");
    } else if (!strcmp(topic, "disk")){
        term_print(t, COL_CYAN, "MODULE 7: MASS STORAGE");
        term_print(t, COL_GREEN,"Disk-head scheduling. Minimize total head movement (tracks).");
        term_print(t, COL_GREEN,"Queue 98,183,37,122,14,124,65,67 from head 53 (disk 0-199).");
        term_print(t, COL_GREEN,"Run: disk fcfs | sstf | scan | cscan | look | clook <start>");
    } else {
        term_print(t, COL_RED, "no help topic '%s' (try sched/mem/disk/vmem)", topic);
    }
}

static void do_man(App *a, const char *algo){
    Terminal *t = &a->term;
    struct { const char *k, *d; } M[] = {
      {"fcfs","FCFS: first process to request the CPU runs first (FIFO). Nonpreemptive. Suffers convoy effect."},
      {"sjf","SJF: run the shortest next CPU burst first. Optimal avg waiting time but long jobs may starve."},
      {"srtf","SRTF: preemptive SJF. Preempt running job if a new arrival has smaller remaining time."},
      {"npp","Non-preemptive Priority: smallest integer = highest priority; ties broken by FCFS."},
      {"pp","Preemptive Priority: a higher-priority arrival preempts the running process. Aging fixes starvation."},
      {"rr","Round Robin: each process gets a time quantum q, then is preempted to the tail of the queue."},
      {"hrrn","HRRN: pick highest ratio (wait+service)/service. Favors short jobs, ages long ones."},
      {"mlq","Multilevel Queue: ready queue split into fixed queues each with its own algorithm."},
      {"mlfq","MLFQ: processes move between queues; long jobs are demoted. Aging promotes starved jobs."},
      {"firstfit","First Fit: allocate the first hole big enough."},
      {"bestfit","Best Fit: allocate the smallest hole that fits."},
      {"worstfit","Worst Fit: allocate the largest hole available."},
      {"paging","Paging: logical pages map to physical frames via a page table. No external fragmentation."},
      {"swap","Swapping: roll a process out to backing store and back. Swap time = size/rate + latency."},
      {"fifo","FIFO replacement: evict the oldest page. Simple; can suffer Belady's anomaly."},
      {"opt","OPT/MIN: evict the page not used for the longest future time. Lowest faults; needs the future."},
      {"lru","LRU: evict the least-recently-used page. Good approximation of OPT; no Belady anomaly."},
      {"belady","Belady's Anomaly: with FIFO, more frames can mean MORE page faults."},
      {"lfu","LFU: evict the page with the smallest reference count."},
      {"mfu","MFU: evict the page with the largest reference count."},
      {"second","Second-Chance (Clock): FIFO but a referenced page (bit=1) gets a second chance."},
      {"eat","EAT = (1-p)*ma + p*page_fault_time. Effective access time under demand paging."},
      {"fcfs-disk","Disk FCFS: serve requests in arrival order. Wild head swings."},
      {"sstf","SSTF: serve the closest track next. May starve far requests."},
      {"scan","SCAN (elevator): sweep to one end servicing requests, then reverse."},
      {"cscan","C-SCAN: sweep one way, jump back to start without servicing, repeat."},
      {"look","LOOK: like SCAN but reverse at the last request, not the disk end."},
      {"clook","C-LOOK: like C-SCAN but jump from last request back to first request."},
      {NULL,NULL}
    };
    for (int i = 0; M[i].k; i++)
        if (!strcmp(M[i].k, algo)){ term_print(t, COL_GREEN, "%s", M[i].d); return; }
    term_print(t, COL_RED, "no manual entry for '%s'", algo);
}

void cmd_execute(App *a, const char *line){
    Terminal *t = &a->term;
    char cmd[64] = {0}, rest[TERM_INPUTMAX] = {0};

    /* skip leading spaces */
    while (*line == ' ') line++;
    if (!*line) return;

    /* first token */
    int i = 0;
    while (line[i] && line[i] != ' ' && i < 63){ cmd[i] = (char)tolower(line[i]); i++; }
    cmd[i] = 0;
    while (line[i] == ' ') i++;
    strncpy(rest, line + i, sizeof(rest)-1);

    if (!strcmp(cmd, "help")){
        if (rest[0]) help_topic(a, rest); else print_help(a);
    } else if (!strcmp(cmd, "banner")){
        print_banner(a);
    } else if (!strcmp(cmd, "clear") || !strcmp(cmd, "cls")){
        term_clear(t);
    } else if (!strcmp(cmd, "man")){
        if (rest[0]) do_man(a, rest); else term_print(t, COL_RED, "usage: man <algo>");
    } else if (!strcmp(cmd, "exit") || !strcmp(cmd, "quit")){
        term_print(t, COL_RED, "severing connection... goodbye.");
        a->state = ST_QUIT;
    } else if (!strcmp(cmd, "sched")){
        sched_run(a, rest);
    } else if (!strcmp(cmd, "mem")){
        mem_run(a, rest);
    } else if (!strcmp(cmd, "vmem")){
        vmem_run(a, rest);
    } else if (!strcmp(cmd, "disk")){
        disk_run(a, rest);
    } else if (!strcmp(cmd, "data")){
        if (rest[0]) data_run(a, rest);     /* power-user 'data <mod> set ...' */
        else dataedit_open(a);              /* bare 'data' opens the editor */
    } else if (!strcmp(cmd, "edit") || !strcmp(cmd, "editor")){
        dataedit_open(a);
    } else if (!strcmp(cmd, "selftest")){
        selftest_run(a);
    } else if (!strcmp(cmd, "scare")){          /* debug trigger */
        term_print(t, COL_RED, "scheduling interrupt...");
        a->scare_at = a->now_ms;                 /* fire ASAP */
    } else if (!strcmp(cmd, "echo")){
        term_print(t, COL_GREEN, "%s", rest);
    } else if (!strcmp(cmd, "whoami")){
        term_print(t, COL_GREEN, "root  (you are not in control)");
    } else {
        term_print(t, COL_RED, "%s: command not found  -- the system rejects you.", cmd);
    }
}
