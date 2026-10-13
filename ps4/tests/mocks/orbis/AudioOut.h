#pragma once
#define ORBIS_AUDIO_OUT_PORT_TYPE_MAIN 0
#define ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO 1
int sceAudioOutInit(void);
int sceAudioOutOpen(int,int,int,unsigned,unsigned,unsigned);
int sceAudioOutClose(int);
int sceAudioOutOutput(int,const void*);
