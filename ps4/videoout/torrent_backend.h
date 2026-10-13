#pragma once
#include "playback.h"
#ifdef __cplusplus
extern "C" {
#endif
int torrent_prepare(const char *hash,const char *const *sources,int source_count,int requested_file,int season,int episode,PlaybackFile *file,char *label,unsigned label_size);
void torrent_stop(void);
void torrent_cancel(void);
int torrent_stats(char *out,unsigned out_size);
#ifdef __cplusplus
}
#endif
