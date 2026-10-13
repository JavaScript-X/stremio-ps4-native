#pragma once
#include <stdint.h>
#include <stdint.h>
#include <stddef.h>
typedef struct {
    int open,paused,buffering,has_video,ended,error,audio_error,audio_tracks,audio_track;
    uint64_t position_ms,duration_ms;
    char message[128];
    char audio_language[8];
} PlaybackStatus;
int playback_open(const char *url,int http,int ssl);
typedef struct {void *object;int (*open)(void *,const char *);int (*close)(void *);int (*read)(void *,uint8_t *,uint64_t,uint32_t);uint64_t (*size)(void *);} PlaybackFile;
int playback_open_file(const char *name,const PlaybackFile *file,int http,int ssl);
void playback_close(void);
void playback_toggle_pause(void);
void playback_seek(int seconds);
void playback_resume_at(uint64_t milliseconds);
int playback_cycle_audio(void);
int playback_video(uint32_t *pixels,int width,int height);
PlaybackStatus playback_status(void);
/* Portable conversion boundary, tested independently of the console API. */
int playback_nv12(uint32_t *out,int ow,int oh,const uint8_t *data,size_t bytes,
    unsigned pitch,unsigned height,unsigned left,unsigned top,unsigned width,unsigned visible_height,int full_range);
