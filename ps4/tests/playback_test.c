/* Tests the adapter's lifecycle against a fake system API, not Sony's decoder. */
#define _POSIX_C_SOURCE 200809L
#include "videoout/playback.h"
#include "videoout/avplayer_abi.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>
#include <orbis/libkernel.h>
static Ps4AvInit init;
static void *texture;
static atomic_uint_fast64_t position;
static atomic_int audio_blocks;
static int allocations,mode,init_fail;
static void *mock_init(Ps4AvInit *p){if(init_fail)return NULL;init=*p;assert(p->auto_start&&p->buffers==4);texture=p->memory.allocate_texture(NULL,16384,6);assert(texture);memset(texture,128,6);return &init;}
static int mock_post(void *p,Ps4AvPost *post){assert(p==&init&&post->http==7&&post->ssl==8);return 0;}
static int mock_add(void *p,const char *url){assert(p==&init&&url);return 0;}
static int mock_control(void *p){assert(p==&init);return 0;}
static int mock_close(void *p){assert(p==&init);init.memory.deallocate_texture(NULL,texture);texture=NULL;return 0;}
static bool mock_video(void *p,Ps4AvFrameEx *f){assert(p==&init);f->data=mode==1?(void*)"bad":texture;f->details.video=(Ps4AvVideoEx){.width=2,.height=2,.pitch=2,.luma_bits=8,.chroma_bits=8};if(mode==2)f->details.video.left=UINT32_MAX;return true;}
static bool mock_audio(void *p,Ps4AvFrame *f){static int16_t pcm[2048];assert(p==&init);f->data=pcm;f->details.audio=(Ps4AvAudio){.channels=2,.rate=48000,.size=sizeof(pcm)};return true;}
static uint64_t mock_time(void *p){assert(p==&init);return atomic_load(&position);}
static int mock_seek(void *p,uint64_t t){assert(p==&init);atomic_store(&position,t);return 0;}
static int mock_count(void *p){assert(p==&init);return 3;}
static int mock_info(void *p,uint32_t i,Ps4AvStream *s){assert(p==&init&&i<3);s->type=i?1:0;s->duration=100000;if(i==1)memcpy(s->details.audio.language,"eng",4);if(i==2)memcpy(s->details.audio.language,"fre",4);return 0;}
static int mock_stream(void *p,uint32_t i){assert(p==&init&&i<3);return 0;}
size_t sceKernelGetDirectMemorySize(void){return 1024*1024;}
int sceKernelAllocateDirectMemory(off_t a,off_t b,size_t n,size_t align,int type,off_t *out){(void)a;(void)b;assert(n>=6&&align==16384&&type==3);++allocations;*out=16384;return 0;}
int sceKernelMapDirectMemory(void **out,size_t n,int prot,int flags,off_t offset,size_t align){(void)flags;(void)offset;assert(prot==0x33);return posix_memalign(out,align,n);}
int sceKernelReleaseDirectMemory(off_t o,size_t n){(void)o;(void)n;--allocations;return 0;}
int sceKernelMunmap(void *p,size_t n){(void)n;free(p);return 0;}
int sceKernelUsleep(unsigned us){struct timespec t={us/1000000,(long)(us%1000000)*1000};return nanosleep(&t,NULL);}
int sceSysmoduleLoadModule(unsigned id){(void)id;return 0;}
int sceKernelLoadStartModule(const char *s,size_t n,const void *a,unsigned f,void *o,int *r){(void)s;(void)n;(void)a;(void)f;(void)o;*r=0;return 1;}
int sceKernelDlsym(int m,const char *name,void **out){assert(m==1);
#define SYM(n,f) if(!strcmp(name,n)){*out=(void*)f;return 0;}
    SYM("sceAvPlayerInit",mock_init) SYM("sceAvPlayerPostInit",mock_post) SYM("sceAvPlayerAddSource",mock_add)
    SYM("sceAvPlayerClose",mock_close) SYM("sceAvPlayerStop",mock_control) SYM("sceAvPlayerPause",mock_control) SYM("sceAvPlayerResume",mock_control)
    SYM("sceAvPlayerGetVideoDataEx",mock_video) SYM("sceAvPlayerGetAudioData",mock_audio) SYM("sceAvPlayerCurrentTime",mock_time)
    SYM("sceAvPlayerJumpToTime",mock_seek) SYM("sceAvPlayerStreamCount",mock_count) SYM("sceAvPlayerGetStreamInfo",mock_info)
    SYM("sceAvPlayerEnableStream",mock_stream) SYM("sceAvPlayerDisableStream",mock_stream)
    return -1;
}
int sceUserServiceGetInitialUser(int *out){*out=1;return 0;}
int sceAudioOutInit(void){return 0;}
int sceAudioOutOpen(int u,int type,int index,unsigned frames,unsigned rate,unsigned format){(void)type;(void)index;(void)format;assert(u==1&&frames==1024&&rate==48000);return 1;}
int sceAudioOutClose(int p){assert(p==1);return 0;}
int sceAudioOutOutput(int p,const void *samples){assert(p==1);if(samples)atomic_fetch_add(&audio_blocks,1);sceKernelUsleep(1000);return 0;}
int main(void){uint32_t pixels[4];
    assert(!playback_open("file:///etc/passwd",7,8)&&playback_status().error);
    init_fail=1;assert(!playback_open("https://example.org/test.mp4",7,8));assert(!allocations);init_fail=0;
    for(int repeat=0;repeat<8;++repeat){
        mode=0;atomic_store(&position,0);assert(playback_open("https://example.org/test.mp4",7,8));
        playback_resume_at(12000);assert(playback_video(pixels,2,2));assert(atomic_load(&position)==12000);
        assert(playback_status().has_video&&playback_status().duration_ms==100000);
        assert(playback_status().audio_tracks==2&&playback_cycle_audio()&&playback_status().audio_track==2);
        playback_toggle_pause();assert(playback_status().paused);playback_toggle_pause();assert(!playback_status().paused);
        playback_seek(-60);assert(atomic_load(&position)==0);playback_seek(200);assert(atomic_load(&position)==99999);
        sceKernelUsleep(10000);assert(atomic_load(&audio_blocks)>0);
        mode=1;assert(!playback_video(pixels,2,2)&&playback_status().error);
        mode=2;assert(!playback_video(pixels,2,2));
        init.events.callback(NULL,1,0,NULL);mode=0;playback_video(pixels,2,2);assert(playback_status().ended);
        playback_close();playback_close();assert(!allocations&&!playback_status().open);
    }
    puts("Playback adapter lifecycle, seek, audio and buffer rejection tests passed");return 0;
}
