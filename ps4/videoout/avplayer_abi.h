// SPDX-License-Identifier: GPL-2.0-or-later
// PS4 ABI declarations adapted from the shadPS4 Emulator Project (2024).
// Reference: shadps4-emu/shadPS4 commit 23c41333822fe8eca4c609b43119180af63ae4ac,
// src/core/libraries/avplayer/avplayer.h. No emulator implementation is linked.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef struct {
    void *object;
    void *(*allocate)(void *,uint32_t,uint32_t);
    void (*deallocate)(void *,void *);
    void *(*allocate_texture)(void *,uint32_t,uint32_t);
    void (*deallocate_texture)(void *,void *);
} Ps4AvMemory;
typedef struct {
    void *object;
    int (*open)(void *,const char *);
    int (*close)(void *);
    int (*read)(void *,uint8_t *,uint64_t,uint32_t);
    uint64_t (*size)(void *);
} Ps4AvFile;
typedef struct {void *object;void (*callback)(void *,int,int,void *);} Ps4AvEvents;
typedef struct {
    Ps4AvMemory memory;Ps4AvFile file;Ps4AvEvents events;
    int debug;uint32_t priority;int buffers;bool auto_start;uint8_t reserved[3];const char *language;
} Ps4AvInit;
typedef struct {uint16_t channels;uint8_t reserved[2];uint32_t rate,size;char language[4];} Ps4AvAudio;
typedef struct {uint32_t width,height;float aspect;char language[4];} Ps4AvVideo;
typedef union {uint8_t reserved[16];Ps4AvAudio audio;Ps4AvVideo video;} Ps4AvDetails;
typedef struct {void *data;uint8_t reserved[4];uint64_t timestamp;Ps4AvDetails details;} Ps4AvFrame;
typedef struct {uint32_t type;uint8_t reserved[4];Ps4AvDetails details;uint64_t duration,start;} Ps4AvStream;
typedef struct {
    uint32_t width,height;float aspect;uint8_t language[4];
    uint32_t framerate,left,right,top,bottom,pitch;
    uint8_t luma_bits,chroma_bits;bool full_range;uint8_t reserved[37];
} Ps4AvVideoEx;
typedef struct {void *data;uint8_t reserved[4];uint64_t timestamp;union {Ps4AvVideoEx video;uint8_t reserved[80];} details;} Ps4AvFrameEx;
typedef struct {uint32_t video_buffer;uint8_t video_decoder[32],audio_decoder[32];uint32_t http,ssl;uint8_t reserved[56];} Ps4AvPost;
_Static_assert(sizeof(Ps4AvInit)==120,"AVPlayer init ABI");
_Static_assert(offsetof(Ps4AvInit,language)==112,"AVPlayer language ABI");
_Static_assert(sizeof(Ps4AvFrame)==40,"AVPlayer frame ABI");
_Static_assert(sizeof(Ps4AvFrameEx)==104,"AVPlayer extended frame ABI");
_Static_assert(sizeof(Ps4AvStream)==40,"AVPlayer stream ABI");
_Static_assert(sizeof(Ps4AvPost)==132,"AVPlayer post-init ABI");
