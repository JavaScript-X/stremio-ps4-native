#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Borrowed child values remain valid until the owning document is freed. */
typedef const void *PJson;
typedef struct {const char *id,*name,*type,*poster,*year,*description;} PCard;
PJson pj_cache_read(const char *path);
int pj_cache_write(const char *path,const PCard *cards,int count);
PJson pj_parse(const char *text);
void pj_free(PJson value);
PJson pj_get(PJson value,const char *key);
PJson pj_at(PJson value,int index);
int pj_size(PJson value);
int pj_array(PJson value);
const char *pj_string(PJson value);
double pj_number(PJson value,double fallback);
int pj_bool(PJson value);
int pj_copy(PJson value,char *out,size_t capacity);
int pj_request(char *out,size_t capacity,const char *auth,int addons);
int pj_resource(PJson manifest,const char *transport,const char *resource,const char *type,const char *id,char *out,size_t capacity);
int pj_streams(const char *text,const char *addon);
const char *pj_stream_field(int index,int field);
int pj_stream_file(int index);
int pj_stream_needs_headers(int index);
void pj_stream_reset(void);
void pj_subtitle_reset(void);
int pj_subtitles(const char *text,const char *addon);
const char *pj_subtitle_field(int index,int field);
int pj_subtitle_load(const char *text);
int pj_subtitle_at(double seconds,char *out,size_t capacity);
void pj_progress_load(const char *path);
double pj_progress_resume(const char *title,const char *video);
int pj_progress_save(const char *path,const char *title,const char *video,double position,double duration);
#ifdef __cplusplus
}
#endif
