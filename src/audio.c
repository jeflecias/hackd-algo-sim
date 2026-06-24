/* audio.c - procedural synth + mixer streamed via waveOut.
   No asset files: every sound is generated at runtime (tones / noise / sweeps /
   a sustained dread drone with an accelerating heartbeat). Graceful no-op if the
   audio device can't be opened. Refilled once per frame from audio_update(). */
#include "app.h"
#include <mmsystem.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#define SR      22050
#define ABUF    512        /* frames per buffer (~23 ms) */
#define NBUF    4
#define NVOICE  16
#define TAU     6.283185307179586

typedef struct {
    int      active, wave;     /* wave: 0 sine 1 square 2 saw 3 noise 4 tri */
    double   phase, freq, freq2, t, dur, amp, atk, rel;
    unsigned rng;
} Voice;

static int       aok = 0;
static HWAVEOUT  hwo;
static WAVEHDR   hdr[NBUF];
static short    *buf[NBUF];
static Voice     voices[NVOICE];
static unsigned  aseed = 0x13572468u;

/* drone / heartbeat state */
static double dr_t = 0, dr_tgt = 0;
static double dph1 = 0, dph2 = 0, dph3 = 0, dlfo = 0, hbt = 0, dbr = 0;
static unsigned hiss_rng = 0x2468ace0u;
static double mute_ms = 0;   /* hard-mute countdown for sudden-silence stings */

static double frand(unsigned *s){
    unsigned x = *s; x ^= x<<13; x ^= x>>17; x ^= x<<5; *s = x;
    return (double)((int)x) / 2147483648.0;   /* ~ -1..1 */
}

static void spawn(int wave,double f,double f2,double dur,double amp,
                  double atk,double rel,double delay){
    int idx = -1;
    for(int i=0;i<NVOICE;i++) if(!voices[i].active){ idx=i; break; }
    if(idx<0){ double mx=-1e9; for(int i=0;i<NVOICE;i++) if(voices[i].t>mx){ mx=voices[i].t; idx=i; } }
    Voice *v=&voices[idx];
    v->active=1; v->wave=wave; v->phase=0; v->freq=f; v->freq2=f2;
    v->t=-delay; v->dur=dur; v->amp=amp; v->atk=atk; v->rel=rel;
    aseed += 0x9E3779B9u; v->rng = aseed | 1u;
}

static double oscv(Voice *v){
    double p=v->phase;
    switch(v->wave){
    case 0: return sin(TAU*p);
    case 1: return p<0.5?1.0:-1.0;
    case 2: return 2.0*p-1.0;
    case 4: return 4.0*fabs(p-0.5)-1.0;
    default:return frand(&v->rng);
    }
}
static double envv(Voice *v){
    if(v->t<0) return 0;
    double e;
    if(v->t<v->atk)            e=v->atk>0? v->t/v->atk : 1.0;
    else if(v->t>v->dur-v->rel)e=v->rel>0? (v->dur-v->t)/v->rel : 0.0;
    else                       e=1.0;
    if(e<0)e=0;
    if(e>1)e=1;
    return e*v->amp;
}

static double drone(double dt){
    dr_t += (dr_tgt-dr_t)*dt*1.5;        /* smooth toward target tension */
    double ten=dr_t; if(ten<0)ten=0; if(ten>1)ten=1;

    double base=55.0;
    dph1+=base*dt;        if(dph1>=1)dph1-=1;
    dph2+=base*1.006*dt;  if(dph2>=1)dph2-=1;   /* detune -> slow beating */
    dlfo+=0.13*dt;        if(dlfo>=1)dlfo-=1;
    double trem=0.85+0.15*sin(TAU*dlfo);
    double amp=(0.05+0.06*ten)*trem;
    double s=(2*dph1-1 + 2*dph2-1)*0.5*amp;

    if(ten>0.05){                         /* uneasy high beating tone */
        double hf=220.0+ten*44.0;
        dph3+=hf*dt; if(dph3>=1)dph3-=1;
        s += sin(TAU*dph3)*0.03*ten;
    }
    dbr+=dt/4.0; if(dbr>=1)dbr-=1;         /* ~4s breathing cycle */
    double breath=sin(TAU*dbr)*0.5+0.5;
    s += frand(&hiss_rng)*(0.006+0.022*breath*ten);  /* breath-modulated hiss bed */

    /* heartbeat: faster as tension rises */
    hbt += dt;
    double period=1.2-ten*0.85; if(period<0.34)period=0.34;
    if(ten>0.25 && hbt>=period){
        hbt=0;
        spawn(0,54,40,0.16,0.16+0.10*ten,0.004,0.10,0.0);    /* lub */
        spawn(0,52,38,0.14,0.11+0.08*ten,0.004,0.10,0.17);   /* dub */
    }
    return s;
}

static double render_sample(double dt){
    if(mute_ms>0){ mute_ms-=dt*1000.0; return 0.0; }   /* sudden silence */
    double mix=0;
    for(int i=0;i<NVOICE;i++){
        Voice *v=&voices[i];
        if(!v->active) continue;
        if(v->t<0){ v->t+=dt; continue; }
        double f=v->freq;
        if(v->freq2>0) f=v->freq+(v->freq2-v->freq)*(v->t/v->dur);
        v->phase+=f*dt; while(v->phase>=1) v->phase-=1;
        mix += oscv(v)*envv(v);
        v->t+=dt;
        if(v->t>=v->dur) v->active=0;
    }
    mix += drone(dt);
    mix = mix/(1.0+0.4*fabs(mix));        /* soft clip */
    return mix;
}

static void fill(short *b){
    double dt=1.0/SR;
    for(int i=0;i<ABUF;i++){
        int v=(int)(render_sample(dt)*30000.0);
        if(v>32767)v=32767;
        if(v<-32768)v=-32768;
        b[i]=(short)v;
    }
}

void audio_init(void){
    WAVEFORMATEX wf; memset(&wf,0,sizeof(wf));
    wf.wFormatTag=WAVE_FORMAT_PCM; wf.nChannels=1;
    wf.nSamplesPerSec=SR; wf.wBitsPerSample=16;
    wf.nBlockAlign=2; wf.nAvgBytesPerSec=SR*2;
    if(waveOutOpen(&hwo,WAVE_MAPPER,&wf,0,0,CALLBACK_NULL)!=MMSYSERR_NOERROR){ aok=0; return; }
    for(int b=0;b<NBUF;b++){
        buf[b]=(short*)calloc(ABUF,sizeof(short));
        memset(&hdr[b],0,sizeof(WAVEHDR));
        hdr[b].lpData=(LPSTR)buf[b];
        hdr[b].dwBufferLength=ABUF*sizeof(short);
        waveOutPrepareHeader(hwo,&hdr[b],sizeof(WAVEHDR));
        fill(buf[b]);
        waveOutWrite(hwo,&hdr[b],sizeof(WAVEHDR));
    }
    aok=1;
}

void audio_update(void){
    if(!aok) return;
    for(int b=0;b<NBUF;b++){
        if(hdr[b].dwFlags & WHDR_DONE){
            fill(buf[b]);
            waveOutWrite(hwo,&hdr[b],sizeof(WAVEHDR));
        }
    }
}

void audio_shutdown(void){
    if(!aok) return;
    waveOutReset(hwo);
    for(int b=0;b<NBUF;b++){
        waveOutUnprepareHeader(hwo,&hdr[b],sizeof(WAVEHDR));
        free(buf[b]); buf[b]=NULL;
    }
    waveOutClose(hwo);
    aok=0;
}

void audio_drone(float tension){
    if(tension<0)tension=0;
    if(tension>1)tension=1;
    dr_tgt=tension;
}

void audio_silence(float ms){
    if(!aok) return;
    mute_ms=ms;
}

void audio_sfx(int id, float param){
    if(!aok) return;
    if(param<0)param=0;
    if(param>1)param=1;
    switch(id){
    case SFX_KEY:
        spawn(3,0,0,0.018,0.06,0.001,0.012,0);
        spawn(1,3200,0,0.010,0.04,0.001,0.006,0);
        break;
    case SFX_BOOT:
        spawn(1,1100,1500,0.030,0.045,0.002,0.02,0);
        break;
    case SFX_PAGEFAULT:
        spawn(1,110,0,0.26,0.16,0.003,0.14,0);
        spawn(1,116,0,0.26,0.11,0.003,0.14,0);   /* detune buzz */
        spawn(3,0,0,0.12,0.14,0.002,0.10,0);
        break;
    case SFX_HIT:
        spawn(0,880,0,0.08,0.10,0.002,0.05,0);
        spawn(0,1320,0,0.07,0.06,0.002,0.05,0.01);
        break;
    case SFX_SEEK: {
        double f=300.0+param*1200.0;
        spawn(1,f,f*1.05,0.05,0.09,0.002,0.03,0);
        spawn(3,0,0,0.02,0.05,0.001,0.015,0);
        break; }
    case SFX_SWITCH:
        spawn(1,620,0,0.02,0.07,0.001,0.012,0);
        spawn(3,0,0,0.012,0.04,0.001,0.008,0);
        break;
    case SFX_ALLOC:
        spawn(0,420,720,0.12,0.09,0.004,0.06,0);
        break;
    case SFX_NOFIT:
        spawn(1,200,140,0.36,0.16,0.003,0.18,0);
        spawn(1,211,150,0.36,0.12,0.003,0.18,0);
        spawn(3,0,0,0.18,0.12,0.002,0.14,0);
        break;
    case SFX_SKULL:
        spawn(2,90,42,1.10,0.26,0.01,0.6,0);
        spawn(2,128,56,1.10,0.18,0.01,0.6,0);
        spawn(0,1500,300,0.60,0.13,0.01,0.4,0.02);   /* screech sweep */
        spawn(3,0,0,0.80,0.20,0.02,0.6,0);
        break;
    case SFX_CORRECT:
        spawn(0,523,0,0.42,0.11,0.004,0.18,0.00);
        spawn(0,659,0,0.42,0.11,0.004,0.18,0.09);
        spawn(0,784,0,0.50,0.12,0.004,0.22,0.18);
        break;
    case SFX_WRONG:
        spawn(1,220,150,0.50,0.13,0.004,0.24,0);
        spawn(1,233,150,0.50,0.10,0.004,0.24,0);
        spawn(1,247,150,0.50,0.09,0.004,0.24,0);
        spawn(3,0,0,0.30,0.12,0.004,0.22,0);
        break;
    case SFX_GLITCH:
        spawn(3,0,0,0.06,0.10+0.06*param,0.001,0.05,0);
        spawn(1,2800,0,0.02,0.05,0.001,0.012,0);
        break;
    case SFX_DECRYPT:
        for(int i=0;i<6;i++)
            spawn(1,800+i*220,0,0.03,0.05,0.001,0.02,i*0.04);
        break;
    case SFX_BREATH:                     /* slow filtered-noise inhale/exhale */
        spawn(3,0,0,2.0,0.045,0.85,0.95,0);
        break;
    case SFX_DRIP:                       /* sparse sine pluck in the quiet */
        spawn(0,720,560,0.12,0.09,0.002,0.10,0);
        spawn(0,300,0,0.16,0.05,0.002,0.13,0.01);
        break;
    case SFX_WHISPER:                    /* voice-like filtered-noise burst */
        spawn(3,0,0,0.50,0.05,0.15,0.22,0);
        spawn(3,0,0,0.36,0.035,0.10,0.18,0.12);
        break;
    case SFX_SCAN:                       /* creepy rising scanning ticks */
        for(int i=0;i<5;i++)
            spawn(1,180+i*55,0,0.035,0.045,0.001,0.02,i*0.06);
        break;
    case SFX_HEART:                      /* strong standalone lub-dub thump */
        spawn(0,58,42,0.18,0.34,0.004,0.12,0.00);   /* lub */
        spawn(0,55,40,0.16,0.26,0.004,0.12,0.16);   /* dub */
        spawn(3,0,0,0.05,0.06,0.002,0.04,0.00);     /* tiny body thud */
        break;
    case SFX_SCREAM:                     /* corrupted scream: detuned saw fall + screech + noise */
        spawn(2,540,150,0.55,0.20,0.01,0.30,0);
        spawn(2,560,160,0.55,0.16,0.01,0.30,0);     /* detune -> ugly beat */
        spawn(0,1700,420,0.45,0.12,0.01,0.28,0.02); /* ring-mod-ish screech sweep */
        spawn(3,0,0,0.55,0.20,0.02,0.40,0);         /* breathy noise body */
        break;
    case SFX_SIREN:                      /* Silent Hill air-raid wail (slow up then down) */
        spawn(0,300,560,1.8,0.16,0.5,0.9,0.0);      /* rise  */
        spawn(0,300,560,1.8,0.11,0.5,0.9,0.0);
        spawn(0,560,300,1.8,0.15,0.6,1.2,1.8);      /* fall  */
        spawn(0,560,300,1.8,0.10,0.6,1.2,1.8);
        break;
    case SFX_STATIC:                     /* radio static crackle (the monster is near) */
        spawn(3,0,0,0.12,0.07+0.10*param,0.003,0.08,0.0);
        spawn(3,0,0,0.08,0.05+0.08*param,0.002,0.06,0.05);
        spawn(1,5200,0,0.015,0.03*param,0.001,0.008,0.0);   /* hiss tick */
        break;
    default: break;
    }
}
