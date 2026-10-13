#define _POSIX_C_SOURCE 200809L
#include "playback.h"
#include "avplayer_abi.h"
#include <orbis/libkernel.h>
#include <orbis/Sysmodule.h>
#include <orbis/AudioOut.h>
#include <orbis/UserService.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static struct {
    void *(*init)(Ps4AvInit *);int (*post)(void *,Ps4AvPost *);int (*add)(void *,const char *);
    int (*close)(void *);int (*stop)(void *);int (*pause)(void *);int (*resume)(void *);
    bool (*video)(void *,Ps4AvFrameEx *);bool (*audio)(void *,Ps4AvFrame *);
    uint64_t (*time)(void *);int (*seek)(void *,uint64_t);int (*count)(void *);
    int (*info)(void *,uint32_t,Ps4AvStream *);
} api;
static void *player;
static PlaybackStatus status;
static pthread_mutex_t api_mutex=PTHREAD_MUTEX_INITIALIZER,alloc_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_t audio_thread;static int audio_running,audio_started,event_state;
static uint64_t started_ms,resume_ms;
static struct {void *ptr;off_t offset;size_t size;} textures[32];
static uint64_t now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static void fail(const char *where,int error){status.error=error?error:-1;snprintf(status.message,sizeof(status.message),"%s (0x%08x)",where,(unsigned)error);}
static void *mem_alloc(void *ctx,uint32_t align,uint32_t size){(void)ctx;void *p=0;if(align<sizeof(void*))align=sizeof(void*);if(align&(align-1))return 0;return posix_memalign(&p,align,size)==0?p:0;}
static void mem_free(void *ctx,void *ptr){(void)ctx;free(ptr);}
static void *texture_alloc(void *ctx,uint32_t align,uint32_t size){
    (void)ctx;if(align<0x4000)align=0x4000;if(align&(align-1))return 0;
    size_t n=((size_t)size+align-1)&~((size_t)align-1);off_t offset=0;void *ptr=0;
    if(sceKernelAllocateDirectMemory(0,sceKernelGetDirectMemorySize(),n,align,3,&offset)<0)return 0;
    if(sceKernelMapDirectMemory(&ptr,n,0x33,0,offset,align)<0){sceKernelReleaseDirectMemory(offset,n);return 0;}
    pthread_mutex_lock(&alloc_mutex);for(int i=0;i<32;++i)if(!textures[i].ptr){textures[i].ptr=ptr;textures[i].offset=offset;textures[i].size=n;pthread_mutex_unlock(&alloc_mutex);return ptr;}
    pthread_mutex_unlock(&alloc_mutex);sceKernelMunmap(ptr,n);sceKernelReleaseDirectMemory(offset,n);return 0;
}
static void texture_free(void *ctx,void *ptr){(void)ctx;pthread_mutex_lock(&alloc_mutex);for(int i=0;i<32;++i)if(textures[i].ptr==ptr){sceKernelMunmap(ptr,textures[i].size);sceKernelReleaseDirectMemory(textures[i].offset,textures[i].size);memset(&textures[i],0,sizeof(textures[i]));break;}pthread_mutex_unlock(&alloc_mutex);}
static void on_event(void *ctx,int event,int source,void *data){(void)ctx;(void)source;(void)data;__atomic_store_n(&event_state,event,__ATOMIC_RELEASE);}
static int load_api(void){
    if(api.init)return 1;
    sceSysmoduleLoadModule(ORBIS_SYSMODULE_AV_PLAYER);
    int result=0,mod=(int)sceKernelLoadStartModule("/system/common/lib/libSceAvPlayer.sprx",0,0,0,0,&result);
    if(mod<0){fail("AVPlayer module unavailable",mod);return 0;}
#define RESOLVE(member,symbol) if(sceKernelDlsym(mod,symbol,(void**)&api.member)<0){memset(&api,0,sizeof(api));fail("AVPlayer export unavailable",-1);return 0;}
    RESOLVE(init,"sceAvPlayerInit");RESOLVE(post,"sceAvPlayerPostInit");RESOLVE(add,"sceAvPlayerAddSource");
    RESOLVE(close,"sceAvPlayerClose");RESOLVE(stop,"sceAvPlayerStop");RESOLVE(pause,"sceAvPlayerPause");RESOLVE(resume,"sceAvPlayerResume");
    RESOLVE(video,"sceAvPlayerGetVideoDataEx");RESOLVE(audio,"sceAvPlayerGetAudioData");RESOLVE(time,"sceAvPlayerCurrentTime");
    RESOLVE(seek,"sceAvPlayerJumpToTime");RESOLVE(count,"sceAvPlayerStreamCount");RESOLVE(info,"sceAvPlayerGetStreamInfo");
#undef RESOLVE
    return 1;
}
static void *audio_loop(void *unused){
    (void)unused;int port=-1,rate=0,user=-1;int16_t block[1024*2];unsigned filled=0;
    sceUserServiceGetInitialUser(&user);sceAudioOutInit();
    while(__atomic_load_n(&audio_running,__ATOMIC_ACQUIRE)){
        Ps4AvFrame frame={0};pthread_mutex_lock(&api_mutex);
        int got=player&&!status.paused&&api.audio(player,&frame);
        if(got){unsigned channels=frame.details.audio.channels,sample_rate=frame.details.audio.rate,bytes=frame.details.audio.size;
            if((channels!=1&&channels!=2)||(sample_rate!=48000&&sample_rate!=44100)||!frame.data||bytes>65536||bytes%(channels*2)){
                status.audio_error=1;pthread_mutex_unlock(&api_mutex);sceKernelUsleep(2000);continue;
            }
            if(port<0||rate!=(int)sample_rate){if(port>=0)sceAudioOutClose(port);rate=(int)sample_rate;filled=0;port=sceAudioOutOpen(user,ORBIS_AUDIO_OUT_PORT_TYPE_MAIN,0,1024,rate,ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);if(port<0)status.audio_error=port;}
            /* Copy the decoder-owned PCM before the next pull can recycle it. */
            int16_t local[32768];memcpy(local,frame.data,bytes);pthread_mutex_unlock(&api_mutex);
            unsigned frames=bytes/(channels*2);for(unsigned i=0;i<frames;++i){block[filled*2]=local[i*channels];block[filled*2+1]=local[i*channels+(channels==2)];
                if(++filled==1024){if(port>=0)sceAudioOutOutput(port,block);filled=0;}}
        }else{pthread_mutex_unlock(&api_mutex);sceKernelUsleep(2000);}
    }
    if(port>=0){sceAudioOutOutput(port,0);sceAudioOutClose(port);}return 0;
}
void playback_close(void){
    __atomic_store_n(&audio_running,0,__ATOMIC_RELEASE);if(audio_started){pthread_join(audio_thread,0);audio_started=0;}
    pthread_mutex_lock(&api_mutex);if(player){api.stop(player);api.close(player);player=0;}status.open=0;pthread_mutex_unlock(&api_mutex);
}
int playback_open(const char *url,int http,int ssl){
    playback_close();memset(&status,0,sizeof(status));resume_ms=0;__atomic_store_n(&event_state,0,__ATOMIC_RELEASE);
    if(!url||((strncmp(url,"https://",8)&&strncmp(url,"http://",7))&&strncmp(url,"/data/",6))){fail("Unsupported media URL",-1);return 0;}
    if(!load_api())return 0;
    Ps4AvInit init={0};init.memory=(Ps4AvMemory){0,mem_alloc,mem_free,texture_alloc,texture_free};init.events.callback=on_event;
    init.priority=0x2bc;init.buffers=4;init.auto_start=true;init.language="eng";
    player=api.init(&init);if(!player){fail("AVPlayer initialization failed",-1);return 0;}
    Ps4AvPost post={0};post.http=(uint32_t)http;post.ssl=(uint32_t)ssl;
    int r=api.post(player,&post);if(r<0){fail("AVPlayer network setup failed",r);playback_close();return 0;}
    r=api.add(player,url);if(r<0){fail("Cannot open this media",r);playback_close();return 0;}
    status.open=1;status.buffering=1;started_ms=now_ms();__atomic_store_n(&audio_running,1,__ATOMIC_RELEASE);
    r=pthread_create(&audio_thread,0,audio_loop,0);if(r){fail("Audio thread failed",r);playback_close();return 0;}audio_started=1;return 1;
}
void playback_toggle_pause(void){pthread_mutex_lock(&api_mutex);if(player){int r=status.paused?api.resume(player):api.pause(player);if(r>=0)status.paused=!status.paused;else fail("Pause failed",r);}pthread_mutex_unlock(&api_mutex);}
void playback_seek(int seconds){pthread_mutex_lock(&api_mutex);if(player){int64_t target=(int64_t)api.time(player)+(int64_t)seconds*1000;if(target<0)target=0;if(status.duration_ms&&(uint64_t)target>=status.duration_ms)target=status.duration_ms-1;int r=api.seek(player,(uint64_t)target);if(r<0)fail("Seek unavailable",r);}pthread_mutex_unlock(&api_mutex);}
void playback_resume_at(uint64_t milliseconds){resume_ms=milliseconds;}
int playback_video(uint32_t *out,int w,int h){
    pthread_mutex_lock(&api_mutex);int drawn=0;
    if(player){Ps4AvFrameEx f={0};if(api.video(player,&f)){
        Ps4AvVideoEx *v=&f.details.video;unsigned pitch=v->pitch?v->pitch:v->width;
        if(v->luma_bits!=8||v->chroma_bits!=8||pitch>8192||v->height>8192||v->left>=pitch||v->right>=pitch-v->left||v->top>=v->height||v->bottom>=v->height-v->top){fail("Unsupported decoded video format",-1);}
        else {size_t bytes=0;pthread_mutex_lock(&alloc_mutex);
            for(int i=0;i<32;++i)if(textures[i].ptr==f.data){bytes=textures[i].size;break;}
            pthread_mutex_unlock(&alloc_mutex);
            drawn=playback_nv12(out,w,h,f.data,bytes,pitch,v->height,v->left,v->top,pitch-v->left-v->right,v->height-v->top-v->bottom,v->full_range);
            if(!drawn)fail("Decoder buffer layout unsupported",-1);
            if(drawn){status.has_video=1;status.buffering=0;}}
        }
        status.position_ms=api.time(player);
        if(status.has_video&&resume_ms){int r=api.seek(player,resume_ms);resume_ms=0;if(r<0)fail("Resume seek unavailable",r);}
        if(!status.duration_ms){int n=api.count(player);for(int i=0;i<n&&i<64;++i){Ps4AvStream st={0};if(api.info(player,(uint32_t)i,&st)>=0&&st.duration>status.duration_ms)status.duration_ms=st.duration;}}
        int ev=__atomic_load_n(&event_state,__ATOMIC_ACQUIRE);status.buffering=ev==5||(!status.has_video&&!status.error);
        if(ev==1&&status.has_video)status.ended=1;
        if(!status.has_video&&now_ms()-started_ms>30000)fail("No video received within 30 seconds",-1);
    }
    pthread_mutex_unlock(&api_mutex);return drawn;
}
PlaybackStatus playback_status(void){pthread_mutex_lock(&api_mutex);PlaybackStatus s=status;pthread_mutex_unlock(&api_mutex);return s;}
