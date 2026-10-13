// Native PS4 frontend using the stable direct VideoOut path. Its layout,
// assets, colors and controller routing are adapted from the PS5 frontend.
#include <stdbool.h>
#include <orbis/SystemService.h>
#include <orbis/Pad.h>
#include <orbis/UserService.h>
#include <orbis/VideoOut.h>
#include <orbis/libkernel.h>
#include <orbis/Http.h>
#include <orbis/Ssl.h>
#include <orbis/Net.h>
#include <orbis/Sysmodule.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>
#include <pthread.h>
#include "protocol.h"
#include "playback.h"
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb_image.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "third_party/hui/third_party/stb/stb_truetype.h"

uint64_t sceLibcHeapSize = 64 * 1024 * 1024;
enum { WIDTH = 1920, HEIGHT = 1080, BUFFER_COUNT = 2, PAGE_COUNT = 5, CARD_CAPACITY = 527, CLOUD_CARD_START = 15 };
static const uint32_t GROUND=0xff110b0cu, SURFACE=0xff292328u, SURFACE_HI=0xff3a333du;
static const uint32_t ACCENT=0xfff55b7bu, ACCENT_SOFT=0xffff8ba5u;
static const uint32_t INK=0xfff7f7f7u, INK_2=0xffc8c8c8u, INK_3=0xff8f8f8fu;
typedef struct Image { unsigned char *pixels; int width, height; } Image;
typedef struct AppState {
    int page, rail_focus, row, column, search, detail, streams, episodes, detail_index;
    int keyboard, search_cursor, result_focus, discover_series, account, stream_cursor, episode_cursor, season, library_page, library_filter, playing; char query[24]; int settings[5];
} AppState;
static const char *PAGE_NAMES[PAGE_COUNT]={"Board","Discover","Library","Addons","Settings"};
static const char *ICON_NAMES[PAGE_COUNT]={"nav_board","nav_discover","nav_library","nav_addons","nav_settings"};
static Image logo, wordmark, search_icon, cross_icon, circle_icon, triangle_icon, square_icon, options_icon;
static Image dpad_icon, l1_icon, r1_icon, play_icon, play_small_icon, pause_icon, check_icon, check_small_icon;
static Image chevron_icon, chevron_down_icon, logo_large, nav_line[PAGE_COUNT], nav_solid[PAGE_COUNT];static char hero_id[128];
static Image hero_background, hero_logo;static int animation_frames;static unsigned ui_phase;static char toast_text[96];static int toast_frames;
static uint32_t *video_canvas;static uint64_t progress_saved_ms;static unsigned playback_controls=180;
static float measured_fps,measured_frame_ms;static uint64_t last_flip_ns;
static unsigned char *font_atlas;static stbtt_bakedchar font_chars[96];static int font_ready;
typedef struct CatalogCard {
    char id[128], name[128], type[24], year[16], runtime[24], rating[12], genres[96];
    char description[640], poster_url[384]; Image poster; int saved, metadata_loaded,poster_attempted;
} CatalogCard;
static CatalogCard live_cards[CARD_CAPACITY];
typedef struct StreamSource {char name[128],title[512],info_hash[64],url[4096],addon[128];int file_index,needs_headers;} StreamSource;
static StreamSource stream_sources[128];static int stream_count,stream_status;
static int subtitle_count,subtitle_selected=-1;static char subtitle_label[128];
typedef struct Episode {char id[128],name[128],description[256];int season,number;} Episode;
static Episode episodes[2048];static int episode_count,max_season;static char episode_series_id[128],selected_video_id[128];
static int catalog_count;
static int search_count, search_status;
static char saved_ids[CARD_CAPACITY][128];static int saved_id_count,cloud_card_count;
static int catalog_status;
static int library_order[CARD_CAPACITY],library_count;
static int poster_count;
static int net_pool, ssl_context, http_context;
static char auth_key[256], link_code[16], link_url[128];
static int account_status, public_domain_enabled=1, public_domain_online;
static int account_sync_status,account_addon_count;
static PJson addon_document;
static int stream_failures,stream_queried,library_truncated;
static int network_cancel;
static void load_card_poster(CatalogCard *card);
static int saved_index(int slot);
static int saved_index_filtered(int slot,int filter);
static void save_library_cache(void);

static int id_saved(const char *id){for(int i=0;i<saved_id_count;++i)if(!strcmp(saved_ids[i],id))return 1;return 0;}
static void apply_saved(void){
    for(int i=0;i<CARD_CAPACITY;++i)if(live_cards[i].id[0])live_cards[i].saved=id_saved(live_cards[i].id);
    library_count=0;
    for(int j=0;j<saved_id_count;++j)for(int i=0;i<CARD_CAPACITY;++i)
        if(live_cards[i].name[0]&&!strcmp(live_cards[i].id,saved_ids[j])){
            int duplicate=0;for(int k=0;k<library_count;++k)if(library_order[k]==i)duplicate=1;
            if(!duplicate)library_order[library_count++]=i;break;
        }
}
static void load_state(AppState *s){
    FILE *f=fopen("/data/stremio-ps4.cfg","r");if(!f)return;char line[256];
    if(fgets(line,sizeof(line),f))sscanf(line,"%d %d %d %d %d",&s->settings[0],&s->settings[1],&s->settings[2],&s->settings[3],&s->settings[4]);
    while(saved_id_count<CARD_CAPACITY&&fgets(line,sizeof(line),f)){line[strcspn(line,"\r\n")]=0;if(line[0])strncpy(saved_ids[saved_id_count++],line,sizeof(saved_ids[0])-1);}fclose(f);
}
static void save_state(const AppState *s){
    /* Keep IDs absent from the current catalog. Only explicit save/remove changes them. */
    FILE *f=fopen("/data/stremio-ps4.cfg.tmp","w");if(!f)return;
    fprintf(f,"%d %d %d %d %d\n",s->settings[0],s->settings[1],s->settings[2],s->settings[3],s->settings[4]);
    for(int i=0;i<saved_id_count;++i)fprintf(f,"%s\n",saved_ids[i]);
    int failed=ferror(f);if(fclose(f)!=0)failed=1;
    if(!failed)rename("/data/stremio-ps4.cfg.tmp","/data/stremio-ps4.cfg");
    save_library_cache();
}
static void toggle_saved(CatalogCard *card){
    for(int i=0;i<saved_id_count;++i)if(!strcmp(saved_ids[i],card->id)){
        memmove(saved_ids[i],saved_ids[i+1],(size_t)(saved_id_count-i-1)*sizeof(saved_ids[0]));
        --saved_id_count;apply_saved();return;
    }
    if(saved_id_count<CARD_CAPACITY){snprintf(saved_ids[saved_id_count++],sizeof(saved_ids[0]),"%s",card->id);apply_saved();}
}
static void load_account(void){
    FILE *f=fopen("/data/stremio-ps4-auth.cfg","r");if(!f)return;char line[320];
    if(fgets(line,sizeof(line),f)){line[strcspn(line,"\r\n")]=0;if(!strncmp(line,"auth=",5))strncpy(auth_key,line+5,sizeof(auth_key)-1);}
    if(fgets(line,sizeof(line),f)&&!strncmp(line,"public_domain=",14))public_domain_enabled=atoi(line+14)!=0;fclose(f);
    if(auth_key[0])account_status=4;
}
static void save_account(void){
    FILE *f=fopen("/data/stremio-ps4-auth.cfg","w");if(!f)return;
    fprintf(f,"auth=%s\npublic_domain=%d\n",auth_key,public_domain_enabled);fclose(f);chmod("/data/stremio-ps4-auth.cfg",0600);
}

static int network_init(void){
    if(http_context>0)return 1;
    if(sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_NET)<0)return 0;
    if(sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_HTTP)<0)return 0;
    if(sceSysmoduleLoadModuleInternal(ORBIS_SYSMODULE_INTERNAL_SSL)<0)return 0;
    sceNetInit();
    net_pool=sceNetPoolCreate("stremio_net",256*1024,0);if(net_pool<0)return 0;
    ssl_context=sceSslInit(SSL_POOLSIZE);if(ssl_context<0)return 0;
    http_context=sceHttpInit(net_pool,ssl_context,LIBHTTP_POOLSIZE);return http_context>=0;
}

static unsigned char *http_get(const char *url,size_t limit,size_t *received){
    *received=0;if(__atomic_load_n(&network_cancel,__ATOMIC_ACQUIRE))return 0;int tpl=0,conn=0,req=0,status=0;unsigned char *body=0;size_t used=0,capacity=0;
    tpl=sceHttpCreateTemplate(http_context,"Stremio-PS4/0.31",ORBIS_HTTP_VERSION_1_1,1);if(tpl<0)goto done;
    sceHttpSetResolveTimeOut(tpl,5000000);sceHttpSetConnectTimeOut(tpl,5000000);sceHttpSetRecvTimeOut(tpl,7000000);
    conn=sceHttpCreateConnectionWithURL(tpl,url,1);if(conn<0)goto done;
    req=sceHttpCreateRequestWithURL(conn,ORBIS_METHOD_GET,url,0);if(req<0)goto done;
    sceHttpAddRequestHeader(req,"Accept","application/json,image/*",0);
    if(sceHttpSendRequest(req,0,0)<0||sceHttpGetStatusCode(req,&status)<0||status<200||status>=300)goto done;
    capacity=64*1024;if(capacity>limit)capacity=limit;body=(unsigned char*)malloc(capacity+1);if(!body)goto done;
    for(;;){
        if(used==capacity){size_t next=capacity*2;if(next>limit)next=limit;if(next<=capacity)goto failed;
            unsigned char *grown=(unsigned char*)realloc(body,next+1);if(!grown)goto failed;body=grown;capacity=next;}
        if(__atomic_load_n(&network_cancel,__ATOMIC_ACQUIRE))goto failed;int got=sceHttpReadData(req,body+used,(uint32_t)(capacity-used));if(got<0)goto failed;if(got==0)break;used+=(size_t)got;
    }
    body[used]=0;*received=used;goto done;
failed:
    free(body);body=0;used=0;
done:
    if(req>0)sceHttpDeleteRequest(req);if(conn>0)sceHttpDeleteConnection(conn);if(tpl>0)sceHttpDeleteTemplate(tpl);
    sceKernelDebugOutText(0,"[stremio-ps4] GET status=%d bytes=%lu\n",status,(unsigned long)used);return body;
}

static unsigned char *http_post_json(const char *url,const char *payload,size_t limit,size_t *received){
    *received=0;if(__atomic_load_n(&network_cancel,__ATOMIC_ACQUIRE))return 0;int tpl=0,conn=0,req=0,status=0;unsigned char *body=0;size_t used=0,capacity=0;size_t payload_size=strlen(payload);
    tpl=sceHttpCreateTemplate(http_context,"Stremio-PS4/0.31",ORBIS_HTTP_VERSION_1_1,1);if(tpl<0)goto done;
    sceHttpSetResolveTimeOut(tpl,5000000);sceHttpSetConnectTimeOut(tpl,5000000);sceHttpSetRecvTimeOut(tpl,10000000);
    conn=sceHttpCreateConnectionWithURL(tpl,url,1);if(conn<0)goto done;
    req=sceHttpCreateRequestWithURL(conn,ORBIS_METHOD_POST,url,payload_size);if(req<0)goto done;
    sceHttpAddRequestHeader(req,"Accept","application/json",0);sceHttpAddRequestHeader(req,"Content-Type","application/json",0);
    if(sceHttpSendRequest(req,payload,payload_size)<0||sceHttpGetStatusCode(req,&status)<0||status<200||status>=300)goto done;
    capacity=64*1024;if(capacity>limit)capacity=limit;body=(unsigned char*)malloc(capacity+1);if(!body)goto done;
    for(;;){if(used==capacity){size_t next=capacity*2;if(next>limit)next=limit;if(next<=capacity)goto failed;unsigned char *grown=(unsigned char*)realloc(body,next+1);if(!grown)goto failed;body=grown;capacity=next;}
        if(__atomic_load_n(&network_cancel,__ATOMIC_ACQUIRE))goto failed;int got=sceHttpReadData(req,body+used,(uint32_t)(capacity-used));if(got<0)goto failed;if(!got)break;used+=(size_t)got;}
    body[used]=0;*received=used;goto done;
failed: free(body);body=0;used=0;
done: if(req>0)sceHttpDeleteRequest(req);if(conn>0)sceHttpDeleteConnection(conn);if(tpl>0)sceHttpDeleteTemplate(tpl);
    sceKernelDebugOutText(0,"[stremio-ps4] POST %s status=%d bytes=%lu\n",url,status,(unsigned long)used);return body;
}

static void create_device_link(void){
    account_status=1;link_code[0]=link_url[0]=0;if(!network_init()){account_status=5;return;}size_t size=0;
    unsigned char *raw=http_get("https://link.stremio.com/api/v2/create?type=Create",128*1024,&size);
    PJson doc=pj_parse((char*)raw);free(raw);
    PJson result=pj_get(doc,"result");pj_copy(pj_get(result,"code"),link_code,sizeof(link_code));pj_copy(pj_get(result,"link"),link_url,sizeof(link_url));
    pj_free(doc);account_status=link_code[0]?2:5;
}
static void poll_device_link(void){
    if(!link_code[0]){create_device_link();return;}account_status=3;char url[256];
    /* Link codes are server-issued alphanumeric identifiers, never URL syntax. */
    for(const char *p=link_code;*p;++p)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='-')){account_status=5;return;}
    snprintf(url,sizeof(url),"https://link.stremio.com/api/v2/read?type=Read&code=%s",link_code);size_t size=0;
    unsigned char *raw=http_get(url,256*1024,&size);PJson doc=pj_parse((char*)raw);free(raw);
    char key[sizeof(auth_key)]={0};pj_copy(pj_get(pj_get(doc,"result"),"authKey"),key,sizeof(key));pj_free(doc);
    if(key[0]){memcpy(auth_key,key,sizeof(key));account_status=4;save_account();}else account_status=2;
}
static void card_from_json(CatalogCard *card,PJson item,int library){
    pj_copy(pj_get(item,library?"_id":"id"),card->id,sizeof(card->id));
    if(!card->id[0])pj_copy(pj_get(item,"imdb_id"),card->id,sizeof(card->id));
    pj_copy(pj_get(item,"name"),card->name,sizeof(card->name));
    pj_copy(pj_get(item,"type"),card->type,sizeof(card->type));
    for(char *p=card->type;*p;++p)if(*p>='a'&&*p<='z')*p-=32;
    pj_copy(pj_get(item,"poster"),card->poster_url,sizeof(card->poster_url));
    pj_copy(pj_get(item,"releaseInfo"),card->year,sizeof(card->year));
    pj_copy(pj_get(item,"description"),card->description,sizeof(card->description));
}
static void load_library_cache(void){
    PJson list=pj_cache_read("/data/stremio-ps4-library.json");
    for(int i=0;i<pj_size(list)&&cloud_card_count<CARD_CAPACITY-CLOUD_CARD_START;++i){
        CatalogCard card={0};card_from_json(&card,pj_at(list,i),0);
        if(card.id[0]&&card.name[0]&&id_saved(card.id))live_cards[CLOUD_CARD_START+cloud_card_count++]=card;
    }
    pj_free(list);apply_saved();
}
static void save_library_cache(void){
    PCard cards[CARD_CAPACITY];int count=0,index;
    while((index=saved_index(count))>=0&&count<CARD_CAPACITY){
        CatalogCard *c=&live_cards[index];cards[count++]=(PCard){c->id,c->name,c->type,c->poster_url,c->year,c->description};
    }
    pj_cache_write("/data/stremio-ps4-library.json",cards,count);
}
static void sync_account_data(void){
    if(!auth_key[0]||!network_init())return;
    account_sync_status=1;char body[1024];size_t size=0;int library_ok=0,addons_ok=0;
    if(!pj_request(body,sizeof(body),auth_key,0)){account_sync_status=3;return;}
    unsigned char *raw=http_post_json("https://api.strem.io/api/datastoreGet",body,4*1024*1024,&size);
    PJson doc=pj_parse((char*)raw);free(raw);PJson list=pj_get(doc,"result");
    /* Validate the whole response before touching a previously usable library. */
    if(pj_array(list)){
        library_ok=1;library_truncated=0;
        /* Merge remote records; do not discard locally saved offline titles. */
        for(int i=0;i<pj_size(list);++i){
            PJson item=pj_at(list,i);if(pj_bool(pj_get(item,"removed"))||pj_bool(pj_get(item,"temp")))continue;
            CatalogCard parsed={0};card_from_json(&parsed,item,1);if(!parsed.id[0]||!parsed.name[0])continue;
            if(!id_saved(parsed.id)&&saved_id_count<CARD_CAPACITY)snprintf(saved_ids[saved_id_count++],sizeof(saved_ids[0]),"%s",parsed.id);
            int duplicate=0;for(int k=0;k<CLOUD_CARD_START+cloud_card_count;++k)if(!strcmp(live_cards[k].id,parsed.id)){duplicate=1;break;}
            if(!duplicate){if(CLOUD_CARD_START+cloud_card_count<CARD_CAPACITY)live_cards[CLOUD_CARD_START+cloud_card_count++]=parsed;else library_truncated=1;}
        }
        apply_saved();
    }
    pj_free(doc);
    if(pj_request(body,sizeof(body),auth_key,1)){
        raw=http_post_json("https://api.strem.io/api/addonCollectionGet",body,4*1024*1024,&size);
        doc=pj_parse((char*)raw);free(raw);
        list=pj_get(pj_get(doc,"result"),"addons");
        if(pj_array(list)){pj_free(addon_document);addon_document=doc;doc=0;account_addon_count=pj_size(list);addons_ok=1;}
        pj_free(doc);
    }
    account_sync_status=library_ok&&addons_ok?2:library_ok||addons_ok?4:3;
}
static void load_card_poster(CatalogCard *card){
    if(card->poster.pixels||card->poster_attempted||!card->poster_url[0]||__atomic_load_n(&network_cancel,__ATOMIC_ACQUIRE))return;
    card->poster_attempted=1;
    char url[768];snprintf(url,sizeof(url),"%s",card->poster_url);
    /* Metahub supports explicit JPEG; arbitrary addon URLs must stay intact. */
    if(strstr(url,"https://images.metahub.space/") == url && !strstr(url,".jpg") && !strchr(url,'?') && strlen(url)+4<sizeof(url))strcat(url,".jpg");
    size_t size=0;unsigned char *raw=http_get(url,3*1024*1024,&size);if(!raw)return;
    int w=0,h=0,ch=0;
    if(stbi_info_from_memory(raw,(int)size,&w,&h,&ch)&&w>0&&h>0&&(size_t)w*h<=4096*4096){
        unsigned char *pixels=stbi_load_from_memory(raw,(int)size,&w,&h,&ch,4);
        if(pixels){int tw=w>360?360:w,th=(int)((int64_t)h*tw/w);if(th<1)th=1;
            unsigned char *small=malloc((size_t)tw*th*4);
            if(small){for(int y=0;y<th;++y)for(int x=0;x<tw;++x)memcpy(small+((size_t)y*tw+x)*4,pixels+((size_t)(y*h/th)*w+x*w/tw)*4,4);
                card->poster=(Image){small,tw,th};}
            stbi_image_free(pixels);
        }
    }
    free(raw);
}
static void prepare_library_page(int page,int filter){
    int wanted[5];for(int j=0;j<5;++j)wanted[j]=saved_index_filtered(page*5+j,filter);
    for(int i=CLOUD_CARD_START;i<CARD_CAPACITY;++i){
        int keep=0;for(int j=0;j<5;++j)if(wanted[j]==i)keep=1;
        if(!keep){stbi_image_free(live_cards[i].poster.pixels);live_cards[i].poster=(Image){0};live_cards[i].poster_attempted=0;}
    }
    for(int j=0;j<5;++j)if(wanted[j]>=0)load_card_poster(&live_cards[wanted[j]]);
}
static void validate_public_domain(void){
    public_domain_online=0;if(!public_domain_enabled||!network_init())return;size_t size=0;
    unsigned char *json=http_get("https://caching.stremio.net/publicdomainmovies.now.sh/manifest.json",256*1024,&size);
    if(json){public_domain_online=strstr((char*)json,"org.stremio.pubdomainmovies")!=0;free(json);}
}

static int parse_catalog(char *text,int first,int maximum){
    PJson doc=pj_parse(text),list=pj_get(doc,"metas");int count=0;
    for(int i=0;i<pj_size(list)&&count<maximum&&first+count<CARD_CAPACITY;++i){
        CatalogCard card={0};card_from_json(&card,pj_at(list,i),0);
        if(card.id[0]&&card.name[0])live_cards[first+count++]=card;
    }
    pj_free(doc);return count;
}
static void load_live_catalog(void){
    catalog_status=1;if(!network_init()){catalog_status=3;return;}size_t size=0;
    // v3-cinemeta currently redirects this resource. SceHttp does not follow
    // redirects by default, so use the canonical catalog host from Location.
    unsigned char *json=http_get("https://cinemeta-catalogs.strem.io/top/catalog/movie/top.json",2*1024*1024,&size);
    if(!json){catalog_status=4;return;}int movies=parse_catalog((char*)json,0,5);free(json);if(movies==0){catalog_status=5;return;}
    json=http_get("https://cinemeta-catalogs.strem.io/top/catalog/series/top.json",2*1024*1024,&size);
    int series=0;if(json){series=parse_catalog((char*)json,movies,5);free(json);}catalog_count=movies+series;
    for(int i=0;i<catalog_count;++i){load_card_poster(&live_cards[i]);if(live_cards[i].poster.pixels)++poster_count;}
    apply_saved();catalog_status=2;
}

static void refresh_catalogs(void){
    for(int i=0;i<10;++i){if(live_cards[i].poster.pixels)stbi_image_free(live_cards[i].poster.pixels);memset(&live_cards[i],0,sizeof(live_cards[i]));}
    catalog_count=poster_count=0;load_live_catalog();
}

static void run_search(const char *query){
    search_status=1;search_count=0;for(int i=10;i<15;++i){if(live_cards[i].poster.pixels)stbi_image_free(live_cards[i].poster.pixels);memset(&live_cards[i],0,sizeof(live_cards[i]));}
    char url[320]="https://v3-cinemeta.strem.io/catalog/movie/top/search=";size_t used=strlen(url);
    for(const char *p=query;*p&&used+4<sizeof(url);++p){if(*p==' '){strcpy(url+used,"%20");used+=3;}else url[used++]=*p;}strcpy(url+used,".json");
    size_t size=0;unsigned char *json=http_get(url,1024*1024,&size);if(!json){search_status=3;return;}
    int movies=parse_catalog((char*)json,10,3);free(json);
    strcpy(url,"https://v3-cinemeta.strem.io/catalog/series/top/search=");used=strlen(url);
    for(const char *p=query;*p&&used+4<sizeof(url);++p){if(*p==' '){strcpy(url+used,"%20");used+=3;}else url[used++]=*p;}strcpy(url+used,".json");
    json=http_get(url,1024*1024,&size);int series=0;if(json){series=parse_catalog((char*)json,10+movies,5-movies);free(json);}search_count=movies+series;if(!search_count){search_status=3;return;}
    for(int i=0;i<search_count;++i)load_card_poster(&live_cards[10+i]);
    apply_saved();search_status=2;
}

static Image load_remote_art(const char *source,const char *extension,int max_width){
    Image out={0};if(!source||!*source)return out;char url[1024];snprintf(url,sizeof(url),"%s",source);
    if(strstr(url,"https://images.metahub.space/")==url&&!strstr(url,extension)&&!strchr(url,'?')&&strlen(url)+strlen(extension)<sizeof(url))strcat(url,extension);
    size_t size=0;unsigned char *raw=http_get(url,5*1024*1024,&size);if(!raw)return out;int w,h,ch;
    if(stbi_info_from_memory(raw,(int)size,&w,&h,&ch)&&w>0&&h>0&&(size_t)w*h<=4096*4096){
        unsigned char *decoded=stbi_load_from_memory(raw,(int)size,&w,&h,&ch,4);
        if(decoded){int tw=w>max_width?max_width:w,th=(int)((int64_t)h*tw/w);if(th<1)th=1;
            out.pixels=malloc((size_t)tw*th*4);if(out.pixels){out.width=tw;out.height=th;for(int y=0;y<th;++y)for(int x=0;x<tw;++x)memcpy(out.pixels+((size_t)y*tw+x)*4,decoded+((size_t)(y*h/th)*w+x*w/tw)*4,4);}
            stbi_image_free(decoded);
        }
    }free(raw);return out;
}
static void load_metadata(CatalogCard *card){
    if(!card->id[0]||(card->metadata_loaded&&!strcmp(hero_id,card->id)))return;
    char url[1024];snprintf(url,sizeof(url),"https://v3-cinemeta.strem.io/meta/%s/%s.json",!strcmp(card->type,"SERIES")?"series":"movie",card->id);
    size_t size=0;unsigned char *raw=http_get(url,2*1024*1024,&size);PJson doc=pj_parse((char*)raw);free(raw);PJson meta=pj_get(doc,"meta");
    if(!meta){pj_free(doc);return;}
    pj_copy(pj_get(meta,"description"),card->description,sizeof(card->description));
    pj_copy(pj_get(meta,"runtime"),card->runtime,sizeof(card->runtime));pj_copy(pj_get(meta,"imdbRating"),card->rating,sizeof(card->rating));
    PJson genres=pj_get(meta,"genres");card->genres[0]=0;
    for(int i=0;i<pj_size(genres);++i){const char *g=pj_string(pj_at(genres,i));size_t n=strlen(card->genres);if(n+strlen(g)+3>=sizeof(card->genres))break;if(n)strcat(card->genres,"  ");strcat(card->genres,g);}
    stbi_image_free(hero_background.pixels);stbi_image_free(hero_logo.pixels);
    hero_background=load_remote_art(pj_string(pj_get(meta,"background")),".jpg",1280);
    hero_logo=load_remote_art(pj_string(pj_get(meta,"logo")),".png",640);
    snprintf(hero_id,sizeof(hero_id),"%s",card->id);card->metadata_loaded=1;pj_free(doc);
}
static int compare_episode(const void *a,const void *b){const Episode *x=a,*y=b;if(x->season!=y->season)return (x->season>y->season)-(x->season<y->season);return (x->number>y->number)-(x->number<y->number);}
static void load_episode_catalog(CatalogCard *card){
    if(!strcmp(episode_series_id,card->id)&&episode_count)return;episode_count=max_season=0;episode_series_id[0]=0;
    char url[1024];snprintf(url,sizeof(url),"https://v3-cinemeta.strem.io/meta/series/%s.json",card->id);
    size_t size=0;unsigned char *raw=http_get(url,4*1024*1024,&size);PJson doc=pj_parse((char*)raw);free(raw);
    PJson videos=pj_get(pj_get(doc,"meta"),"videos");
    for(int i=0;i<pj_size(videos)&&episode_count<2048;++i){
        PJson item=pj_at(videos,i);Episode e={0};pj_copy(pj_get(item,"id"),e.id,sizeof(e.id));if(!e.id[0])continue;
        e.season=(int)pj_number(pj_get(item,"season"),0);e.number=(int)pj_number(pj_get(item,"episode"),pj_number(pj_get(item,"number"),0));
        if(e.season<0||e.number<0)continue;
        pj_copy(pj_get(item,"title"),e.name,sizeof(e.name));if(!e.name[0])pj_copy(pj_get(item,"name"),e.name,sizeof(e.name));
        pj_copy(pj_get(item,"overview"),e.description,sizeof(e.description));if(!e.description[0])pj_copy(pj_get(item,"description"),e.description,sizeof(e.description));
        episodes[episode_count++]=e;if(e.season>max_season)max_season=e.season;
    }
    qsort(episodes,episode_count,sizeof(episodes[0]),compare_episode);
    if(episode_count)snprintf(episode_series_id,sizeof(episode_series_id),"%s",card->id);pj_free(doc);
}
static int first_episode_in_season(int season){for(int i=0;i<episode_count;++i)if(episodes[i].season==season)return i;return -1;}
static void load_stream_sources(CatalogCard *card){
    stream_count=stream_failures=stream_queried=subtitle_count=0;subtitle_selected=-1;subtitle_label[0]=0;stream_status=1;pj_stream_reset();pj_subtitle_reset();
    memset(stream_sources,0,sizeof(stream_sources));
    const char *type=!strcmp(card->type,"SERIES")?"series":"movie";
    const char *id=!strcmp(type,"series")&&selected_video_id[0]?selected_video_id:card->id;
    PJson addons=pj_get(pj_get(addon_document,"result"),"addons");char url[8192];
    for(int i=0;i<pj_size(addons);++i){
        PJson addon=pj_at(addons,i),manifest=pj_get(addon,"manifest");
        const char *transport=pj_string(pj_get(addon,"transportUrl"));
        if(strncmp(transport,"https://",8)&&strncmp(transport,"http://",7))continue;
        size_t size=0;unsigned char *raw=0;
        if(pj_resource(manifest,transport,"stream",type,id,url,sizeof(url))){
            ++stream_queried;raw=http_get(url,2*1024*1024,&size);
            if(!raw||pj_streams((char*)raw,pj_string(pj_get(manifest,"name")))<0)++stream_failures;free(raw);
        }
        if(pj_resource(manifest,transport,"subtitles",type,id,url,sizeof(url))){
            raw=http_get(url,2*1024*1024,&size);if(raw)pj_subtitles((char*)raw,pj_string(pj_get(manifest,"name")));free(raw);
        }
    }
    if(public_domain_enabled&&public_domain_online){
        snprintf(url,sizeof(url),"https://caching.stremio.net/publicdomainmovies.now.sh/stream/%s/%s.json",type,id);
        ++stream_queried;size_t size=0;unsigned char *raw=http_get(url,2*1024*1024,&size);
        if(!raw||pj_streams((char*)raw,"Public Domain Movies")<0)++stream_failures;free(raw);
    }
    for(int i=0;i<128;++i){
        const char *name=pj_stream_field(i,0),*direct=pj_stream_field(i,2),*hash=pj_stream_field(i,3);
        if(!*direct&&!*hash)break;
        if(strlen(direct)>=sizeof(stream_sources[0].url)||strlen(hash)>=sizeof(stream_sources[0].info_hash)){++stream_failures;continue;}
        StreamSource *dst=&stream_sources[stream_count++];
        snprintf(dst->name,sizeof(dst->name),"%s",name);
        snprintf(dst->title,sizeof(dst->title),"%s",pj_stream_field(i,1));
        snprintf(dst->url,sizeof(dst->url),"%s",direct);snprintf(dst->info_hash,sizeof(dst->info_hash),"%s",hash);
        snprintf(dst->addon,sizeof(dst->addon),"%s",pj_stream_field(i,4));dst->file_index=pj_stream_file(i);dst->needs_headers=pj_stream_needs_headers(i);
    }
    for(int i=0;i<64&&*pj_subtitle_field(i,0);++i)++subtitle_count;
    stream_status=stream_count?2:!stream_queried?4:stream_failures?5:3;
}
static int load_subtitle_track(int index){
    pj_subtitle_load("");subtitle_selected=-1;subtitle_label[0]=0;if(index<0||index>=subtitle_count)return 1;
    const char *url=pj_subtitle_field(index,0);if(strncmp(url,"https://",8)&&strncmp(url,"http://",7))return 0;
    size_t size=0;unsigned char *raw=http_get(url,4*1024*1024,&size);if(!raw)return 0;
    int cues=pj_subtitle_load((char*)raw);free(raw);if(!cues)return 0;
    subtitle_selected=index;const char *label=pj_subtitle_field(index,2),*lang=pj_subtitle_field(index,1);
    snprintf(subtitle_label,sizeof(subtitle_label),"%s%s%s",label,*label&&*lang?" / ":"",lang);return 1;
}
static int saved_index(int slot){return slot>=0&&slot<library_count?library_order[slot]:-1;}
static int saved_index_filtered(int slot,int filter){int found=0;for(int i=0;i<library_count;++i){CatalogCard *c=&live_cards[library_order[i]];
    if(filter==1&&strcmp(c->type,"MOVIE"))continue;if(filter==2&&strcmp(c->type,"SERIES"))continue;if(found++==slot)return library_order[i];}return -1;}

static void park_error(const char *message,int code){
    sceKernelDebugOutText(0,"[stremio-ps4] %s: 0x%08x\n",message,(unsigned)code);
    for(;;) sceKernelUsleep(100000);
}
static void fill_buffer(void *buffer,uint32_t color){
    uint32_t *pixels=(uint32_t*)buffer;
    for(size_t i=0;i<(size_t)WIDTH*HEIGHT;++i) pixels[i]=color;
}
static void draw_rect(void *buffer,int x,int y,int w,int h,uint32_t color){
    if(x<0){w+=x;x=0;} if(y<0){h+=y;y=0;}
    if(x+w>WIDTH)w=WIDTH-x; if(y+h>HEIGHT)h=HEIGHT-y; if(w<=0||h<=0)return;
    uint32_t *pixels=(uint32_t*)buffer;
    for(int row=y;row<y+h;++row) for(int col=x;col<x+w;++col) pixels[(size_t)row*WIDTH+col]=color;
}
static void draw_round_rect(void *buffer,int x,int y,int w,int h,int radius,uint32_t color){
    if(radius<1){draw_rect(buffer,x,y,w,h,color);return;}if(radius>w/2)radius=w/2;if(radius>h/2)radius=h/2;
    draw_rect(buffer,x+radius,y,w-radius*2,h,color);draw_rect(buffer,x,y+radius,w,h-radius*2,color);
    for(int dy=0;dy<radius;++dy){int yy=radius-1-dy;int inset=radius-(int)sqrt((double)(radius*radius-yy*yy));
        draw_rect(buffer,x+inset,y+dy,w-inset*2,1,color);draw_rect(buffer,x+inset,y+h-1-dy,w-inset*2,1,color);}
}
static void draw_round_outline(void *b,int x,int y,int w,int h,int radius,int thickness,uint32_t color){
    draw_rect(b,x+radius,y,w-radius*2,thickness,color);draw_rect(b,x+radius,y+h-thickness,w-radius*2,thickness,color);
    draw_rect(b,x,y+radius,thickness,h-radius*2,color);draw_rect(b,x+w-thickness,y+radius,thickness,h-radius*2,color);
    uint32_t *pixels=(uint32_t*)b;int outer=radius*radius,inner=(radius-thickness)*(radius-thickness);
    for(int py=0;py<radius;++py)for(int px=0;px<radius;++px){int dx=radius-1-px,dy=radius-1-py,d=dx*dx+dy*dy;if(d<=outer&&d>=inner){
        int xs[4]={x+px,x+w-1-px,x+px,x+w-1-px},ys[4]={y+py,y+py,y+h-1-py,y+h-1-py};
        for(int q=0;q<4;++q)if(xs[q]>=0&&xs[q]<WIDTH&&ys[q]>=0&&ys[q]<HEIGHT)pixels[(size_t)ys[q]*WIDTH+xs[q]]=color;}}
}
static void draw_vgradient(void *b,int x,int y,int w,int h,uint32_t top,uint32_t bottom){
    for(int row=0;row<h;++row){unsigned t=h>1?(unsigned)(row*255/(h-1)):0;
        unsigned r=(((top)&255u)*(255-t)+((bottom)&255u)*t)/255,g=(((top>>8)&255u)*(255-t)+((bottom>>8)&255u)*t)/255,bl=(((top>>16)&255u)*(255-t)+((bottom>>16)&255u)*t)/255;
        draw_rect(b,x,y+row,w,1,0xff000000u|(bl<<16)|(g<<8)|r);}
}
static void draw_page_backdrop(void *b,const AppState *s);
static const uint8_t FONT[27][5]={
 {0,0,0,0,0},{0x7e,0x09,0x09,0x09,0x7e},{0x7f,0x49,0x49,0x49,0x36},
 {0x3e,0x41,0x41,0x41,0x22},{0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},
 {0x7f,0x09,0x09,0x09,0x01},{0x3e,0x41,0x49,0x49,0x7a},{0x7f,0x08,0x08,0x08,0x7f},
 {0,0x41,0x7f,0x41,0},{0x20,0x40,0x41,0x3f,0x01},{0x7f,0x08,0x14,0x22,0x41},
 {0x7f,0x40,0x40,0x40,0x40},{0x7f,0x02,0x0c,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},
 {0x3e,0x41,0x41,0x41,0x3e},{0x7f,0x09,0x09,0x09,0x06},{0x3e,0x41,0x51,0x21,0x5e},
 {0x7f,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},{1,1,0x7f,1,1},
 {0x3f,0x40,0x40,0x40,0x3f},{0x1f,0x20,0x40,0x20,0x1f},{0x3f,0x40,0x38,0x40,0x3f},
 {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}};
static const uint8_t DIGITS[10][5]={
 {0x3e,0x51,0x49,0x45,0x3e},{0,0x42,0x7f,0x40,0},{0x42,0x61,0x51,0x49,0x46},
 {0x21,0x41,0x45,0x4b,0x31},{0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
 {0x3c,0x4a,0x49,0x49,0x30},{1,0x71,9,5,3},{0x36,0x49,0x49,0x49,0x36},{6,0x49,0x49,0x29,0x1e}};
static void draw_text(void *buffer,int x,int y,const char *text,int scale,uint32_t color){
    if(font_ready){float factor=(float)(scale*8)/48.0f,cursor=(float)x;uint32_t *target=(uint32_t*)buffer;
        for(;*text;++text){unsigned ch=(unsigned char)*text;if(ch<32||ch>127)ch='?';stbtt_bakedchar *g=&font_chars[ch-32];
            int dw=(int)((g->x1-g->x0)*factor),dh=(int)((g->y1-g->y0)*factor),gx=(int)(cursor+g->xoff*factor),gy=(int)(y+(g->yoff+48.0f)*factor);
            for(int dy=0;dy<dh;++dy){int py=gy+dy;if(py<0||py>=HEIGHT)continue;int sy=g->y0+(int)(dy/factor);for(int dx=0;dx<dw;++dx){int px=gx+dx;if(px<0||px>=WIDTH)continue;int sx=g->x0+(int)(dx/factor);unsigned a=font_atlas[sy*1024+sx];if(!a)continue;
                uint32_t *d=&target[(size_t)py*WIDTH+px];unsigned dr=*d&255u,dg=(*d>>8)&255u,db=(*d>>16)&255u,cr=color&255u,cg=(color>>8)&255u,cb=(color>>16)&255u;
                *d=0xff000000u|(((cb*a+db*(255-a))/255)<<16)|(((cg*a+dg*(255-a))/255)<<8)|((cr*a+dr*(255-a))/255);}}
            cursor+=g->xadvance*factor;}return;}
    for(;*text;++text,x+=6*scale){const uint8_t *bits=*text>='A'&&*text<='Z'?FONT[*text-'A'+1]:*text>='0'&&*text<='9'?DIGITS[*text-'0']:FONT[0];
        for(int c=0;c<5;++c)for(int r=0;r<7;++r)if(bits[c]&(1u<<r))draw_rect(buffer,x+c*scale,y+r*scale,scale,scale,color);}
}
static void load_font(void){
    const char *paths[]={"/app0/assets/fonts/Inter-SemiBold.ttf","assets/fonts/Inter-SemiBold.ttf"};FILE *f=0;for(int i=0;i<2&&!f;++i)f=fopen(paths[i],"rb");if(!f)return;
    fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);unsigned char *ttf=(unsigned char*)malloc((size_t)size);font_atlas=(unsigned char*)calloc(1024*1024,1);
    if(ttf&&font_atlas&&fread(ttf,1,(size_t)size,f)==(size_t)size)font_ready=stbtt_BakeFontBitmap(ttf,0,48.0f,font_atlas,1024,1024,32,96,font_chars)>0;
    fclose(f);free(ttf);sceKernelDebugOutText(0,"[stremio-ps4] Inter font atlas=%d\n",font_ready);
}
static Image load_image(const char *name){
    Image image={0}; char path[160]; int channels=0;
    strcpy(path,"/app0/assets/");strcat(path,name);
    image.pixels=stbi_load(path,&image.width,&image.height,&channels,4);
    if(!image.pixels){strcpy(path,"assets/");strcat(path,name);image.pixels=stbi_load(path,&image.width,&image.height,&channels,4);}
    return image;
}
static void load_assets(void){
    load_font();
    logo=load_image("logo.png");wordmark=load_image("name.png");search_icon=load_image("search.png");
    cross_icon=load_image("cross.png");circle_icon=load_image("circle.png");triangle_icon=load_image("triangle.png");square_icon=load_image("square.png");options_icon=load_image("options.png");
    dpad_icon=load_image("dpad.png");l1_icon=load_image("l1.png");r1_icon=load_image("r1.png");play_icon=load_image("play.png");
    play_small_icon=load_image("play_s.png");pause_icon=load_image("pause.png");check_icon=load_image("check.png");check_small_icon=load_image("check_s.png");
    chevron_icon=load_image("chevron_right.png");chevron_down_icon=load_image("chevron_down.png");logo_large=load_image("logo_l.png");char name[64];
    for(int i=0;i<PAGE_COUNT;++i){strcpy(name,ICON_NAMES[i]);strcat(name,".png");nav_line[i]=load_image(name);
        strcpy(name,ICON_NAMES[i]);strcat(name,"_o.png");nav_solid[i]=load_image(name);}
    sceKernelDebugOutText(0,"[stremio-ps4] PS5 assets: logo=%dx%d\n",logo.width,logo.height);
}
static void draw_image(void *buffer,const Image *image,int x,int y,int w,int h){
    if(!image->pixels||image->width<=0||image->height<=0)return; uint32_t *target=(uint32_t*)buffer;
    for(int dy=0;dy<h;++dy){int py=y+dy;if(py<0||py>=HEIGHT)continue;int sy=dy*image->height/h;
        for(int dx=0;dx<w;++dx){int px=x+dx;if(px<0||px>=WIDTH)continue;int sx=dx*image->width/w;
            const unsigned char *s=image->pixels+((size_t)sy*image->width+sx)*4;unsigned a=s[3];if(!a)continue;
            uint32_t *d=&target[(size_t)py*WIDTH+px];if(a==255){*d=0xff000000u|((uint32_t)s[0]<<16)|((uint32_t)s[1]<<8)|s[2];continue;}unsigned dr=*d&255u,dg=(*d>>8)&255u,db=(*d>>16)&255u;
            unsigned r=(s[0]*a+dr*(255-a))/255,g=(s[1]*a+dg*(255-a))/255,b=(s[2]*a+db*(255-a))/255;
            *d=0xff000000u|(r<<16)|(g<<8)|b;}}
}
static void draw_image_rounded(void *buffer,const Image *image,int x,int y,int w,int h,int radius){
    if(!image->pixels||image->width<=0||image->height<=0)return;uint32_t *target=(uint32_t*)buffer;
    for(int dy=0;dy<h;++dy){int py=y+dy;if(py<0||py>=HEIGHT)continue;int sy=dy*image->height/h;for(int dx=0;dx<w;++dx){int px=x+dx;if(px<0||px>=WIDTH)continue;
        int cx=dx<radius?radius-dx-1:dx>=w-radius?dx-(w-radius):0,cy=dy<radius?radius-dy-1:dy>=h-radius?dy-(h-radius):0;if(cx&&cy&&cx*cx+cy*cy>radius*radius)continue;
        int sx=dx*image->width/w;const unsigned char *s=image->pixels+((size_t)sy*image->width+sx)*4;unsigned a=s[3];if(!a)continue;uint32_t *d=&target[(size_t)py*WIDTH+px];if(a==255){*d=0xff000000u|((uint32_t)s[0]<<16)|((uint32_t)s[1]<<8)|s[2];continue;}unsigned dr=*d&255u,dg=(*d>>8)&255u,db=(*d>>16)&255u;
        *d=0xff000000u|(((s[0]*a+db*(255-a))/255)<<16)|(((s[1]*a+dg*(255-a))/255)<<8)|((s[2]*a+dr*(255-a))/255);}}
}
static void draw_image_alpha(void *buffer,const Image *image,int x,int y,int w,int h,unsigned opacity){
    if(!image->pixels||!opacity)return;uint32_t *target=(uint32_t*)buffer;for(int dy=0;dy<h;++dy){int py=y+dy;if(py<0||py>=HEIGHT)continue;int sy=dy*image->height/h;
        for(int dx=0;dx<w;++dx){int px=x+dx;if(px<0||px>=WIDTH)continue;int sx=dx*image->width/w;const unsigned char *s=image->pixels+((size_t)sy*image->width+sx)*4;unsigned a=s[3]*opacity/255;if(!a)continue;
            uint32_t *d=&target[(size_t)py*WIDTH+px];unsigned blue=*d&255u,green=(*d>>8)&255u,red=(*d>>16)&255u;*d=0xff000000u|(((s[0]*a+red*(255-a))/255)<<16)|(((s[1]*a+green*(255-a))/255)<<8)|((s[2]*a+blue*(255-a))/255);}}
}
static void draw_page_backdrop(void *b,const AppState *s){
    fill_buffer(b,GROUND);unsigned pulse=(ui_phase%40<20)?(ui_phase%20):(40-ui_phase%40);uint32_t violet=0xff000000u|((0x25u+pulse/3)<<16)|(0x13u<<8)|(0x38u+pulse);
    draw_vgradient(b,140,0,1780,280,violet,GROUND);if(animation_frames==0&&(s->detail||s->episodes||s->streams)&&hero_background.pixels)draw_image_alpha(b,&hero_background,720,80,1200,850,42);draw_vgradient(b,140,820,1780,260,0xff0c0b11u,0xff080609u);
}
static void draw_focus(void *b,int x,int y,int w,int h){
    uint32_t glow=(ui_phase%20<10)?0xff704bddu:0xff9a70ffu;draw_round_outline(b,x-8,y-8,w+16,h+16,24,4,glow);
    draw_round_outline(b,x-4,y-4,w+8,h+8,20,3,INK);
}
static void draw_card(void *b,int x,int y,int w,int h,int seed,int focused,const char *title){
    int grow=focused?4+animation_frames/5:0;if(grow){x-=grow;y-=grow;w+=grow*2;h+=grow*2;}
    static const uint32_t colors[]={0xff49325fu,0xff41305au,0xff34284du,0xff3d2d55u,0xff2c2342u};int poster_h=h-58;
    draw_round_rect(b,x+8,y+14,w,poster_h,20,0xff050405u);draw_round_rect(b,x,y,w,poster_h,18,colors[seed%5]);
    if(seed<CARD_CAPACITY&&live_cards[seed].poster.pixels)draw_image_rounded(b,&live_cards[seed].poster,x,y,w,poster_h,18);
    char shown[21]={0};strncpy(shown,title,sizeof(shown)-1);int scale=strlen(shown)>12?2:3;
    draw_text(b,x,y+poster_h+20,shown,scale,focused?INK:INK_2);if(focused)draw_focus(b,x,y,w,poster_h);
    if(seed<CARD_CAPACITY&&live_cards[seed].saved){draw_round_rect(b,x+w-48,y+12,36,36,18,0xff65b322u);draw_image(b,&check_small_icon,x+w-40,y+20,20,20);}
}
static void draw_rail(void *b,const AppState *s){
    draw_vgradient(b,0,0,140,HEIGHT,0xff211724u,0xff100d12u);if(logo.pixels)draw_image(b,&logo,40,28,60,60);else draw_text(b,36,42,"S",7,ACCENT_SOFT);
    for(int i=0;i<PAGE_COUNT;++i){int y=242+i*120,active=s->page==i,cursor=s->rail_focus&&s->row==i;
        if(active||cursor)draw_round_rect(b,20,y,100,104,26,cursor?0xff563aa7u:SURFACE_HI);
        draw_image(b,(active||cursor)?&nav_solid[i]:&nav_line[i],46,y+14,48,48);
        draw_text(b,28,y+78,PAGE_NAMES[i],2,(active||cursor)?INK:INK_3);if(cursor)draw_focus(b,20,y,100,104);}
}
static void draw_topbar(void *b,const AppState *s){
    int x=190,y=34,w=620,h=60;draw_round_rect(b,x,y,w,h,30,s->search?SURFACE_HI:SURFACE);
    draw_image(b,&search_icon,x+22,y+16,28,28);draw_text(b,x+72,y+20,s->search?(s->query[0]?s->query:"Search mode"):"Search movies, series and more",3,s->search?INK:INK_3);
    draw_image(b,&triangle_icon,x+w-48,y+14,32,32);if(s->search)draw_focus(b,x,y,w,h);
    if(s->settings[4]){char perf[64];snprintf(perf,sizeof(perf),"%.1f FPS   %.1f MS",measured_fps,measured_frame_ms);draw_round_rect(b,1390,34,300,60,30,SURFACE);draw_text(b,1430,54,perf,2,measured_fps>=28.0f?0xff8cffaau:ACCENT_SOFT);}
    else if(wordmark.pixels)draw_image(b,&wordmark,1640,42,220,42);else draw_text(b,1600,54,"STREMIO PS FOUR",3,INK_2);
}
static void draw_board(void *b,const AppState *s){
    draw_text(b,190,130,"Popular movies",5,INK);draw_text(b,190,178,"Live Cinemeta catalogs",3,INK_2);
    const char *titles[]={"FEATURED","TRENDING","MOVIES","SERIES","RECENT"};
    for(int i=0;i<5;++i)draw_card(b,190+i*290,218,250,300,i,!s->rail_focus&&s->row==0&&s->column==i,catalog_status==2?live_cards[i].name:titles[i]);
    const char *status=catalog_status==1?"CONNECTING":catalog_status==2?(poster_count==catalog_count?"LIVE MOVIES AND SERIES":poster_count?"PARTIAL POSTERS":"LIVE CINEMETA"):
        catalog_status==3?"NETWORK INIT FAILED":catalog_status==4?"CATALOG REQUEST FAILED":catalog_status==5?"CATALOG PARSE FAILED":"OFFLINE CONTENT";
    draw_text(b,1110,150,status,3,catalog_status==2?ACCENT_SOFT:INK_3);
    draw_round_rect(b,1540,134,176,44,22,SURFACE);draw_text(b,1570,146,"10 titles",2,INK_2);
    draw_text(b,190,568,"Popular series",5,INK);
    for(int i=0;i<5;++i){int index=5+i;const char *label=index<catalog_count?live_cards[index].name:"SERIES";
        draw_card(b,190+i*290,628,250,300,index,!s->rail_focus&&s->row==1&&s->column==i,label);}
    draw_image(b,&options_icon,1510,950,38,38);draw_text(b,1562,960,"REFRESH",3,INK_3);
}
static void draw_discover(void *b,const AppState *s){
    draw_text(b,190,154,"Discover",6,INK_2);const char *filters[]={"Movies","Popular","All genres"};
    filters[0]=s->discover_series?"Series":"Movies";
    for(int i=0;i<3;++i){draw_round_rect(b,190+i*330,228,300,66,33,i==0?0xff563aa7u:SURFACE);draw_text(b,224+i*330,248,filters[i],3,INK);draw_image(b,&chevron_down_icon,438+i*330,247,24,24);}
    const char *titles[]={"ACTION","DRAMA","COMEDY","THRILLER","FAMILY"};
    int first=s->discover_series?5:0;
    for(int i=0;i<5;++i){int index=first+i;draw_card(b,190+i*290,370,250,360,index,!s->rail_focus&&s->column==i,catalog_status==2&&index<catalog_count?live_cards[index].name:titles[i]);}
    draw_image(b,&square_icon,190,800,38,38);draw_text(b,244,810,"SWITCH MOVIES SERIES",3,INK_3);
}
static void draw_library(void *b,const AppState *s){
    draw_text(b,190,154,"Library",6,INK_2);int count=0;while(saved_index_filtered(count,s->library_filter)>=0)++count;
    char summary[64];snprintf(summary,sizeof(summary),"%d saved title%s",count,count==1?"":"s");draw_text(b,190,210,summary,3,INK_3);
    const char *filters[]={"All","Movies","Series"};for(int i=0;i<3;++i){draw_round_rect(b,190+i*210,252,190,54,27,i==s->library_filter?0xff563aa7u:SURFACE);draw_text(b,225+i*210,268,filters[i],3,INK);}
    int shown=0;for(int i=0;i<5;++i){int index=saved_index_filtered(s->library_page*5+i,s->library_filter);if(index<0)break;draw_card(b,190+i*290,350,250,360,index,!s->rail_focus&&s->column==i,live_cards[index].name);++shown;}if(count>5){char page[40];snprintf(page,sizeof(page),"PAGE %d OF %d",s->library_page+1,(count+4)/5);draw_round_rect(b,1450,260,250,50,25,SURFACE);draw_text(b,1490,275,page,2,INK_2);}
    if(!shown){draw_round_rect(b,190,350,1100,190,30,SURFACE);draw_text(b,250,405,"Your library is empty",4,INK);draw_text(b,250,462,"Save any title with Square",3,INK_3);}
    draw_round_rect(b,190,805,500,64,32,SURFACE_HI);draw_image(b,&check_icon,212,818,36,36);draw_text(b,266,824,"Saved locally on PS4",3,INK_2);draw_image(b,&square_icon,760,818,36,36);draw_text(b,810,824,"CHANGE FILTER",3,INK_3);
}
static void draw_addons(void *b,const AppState *s){
    draw_text(b,190,154,"Addons",6,INK);
    PJson addons=pj_get(pj_get(addon_document,"result"),"addons");int total=pj_size(addons)+2;
    char summary[96];snprintf(summary,sizeof(summary),"%d account addons  /  Options: sync",account_addon_count);draw_text(b,190,218,summary,3,INK_3);
    int start=(s->row/5)*5;
    for(int i=start;i<total&&i<start+5;++i){
        int y=290+(i-start)*126;const char *name,*description;
        if(i<2){name=i?"Public Domain Movies":"Cinemeta";description=i?(public_domain_enabled?"Enabled - Cross to disable":"Disabled - Cross to enable"):"Built-in movie and series catalogs";}
        else {PJson a=pj_at(addons,i-2),m=pj_get(a,"manifest");name=pj_string(pj_get(m,"name"));description=pj_string(pj_get(m,"description"));}
        draw_round_rect(b,190,y,1470,102,24,SURFACE);
        draw_image(b,&nav_line[3],212,y+24,48,48);char label[65],desc[94];snprintf(label,sizeof(label),"%.64s",name);snprintf(desc,sizeof(desc),"%.93s",description);
        draw_text(b,294,y+16,label,4,INK);draw_text(b,294,y+62,desc,2,INK_3);
        if(!s->rail_focus&&s->row==i)draw_focus(b,190,y,1470,102);
    }
}
static void draw_wrapped(void *b,int x,int y,const char *text,int columns,int lines);
static void draw_settings(void *b,const AppState *s){
    draw_text(b,190,154,"Settings",6,INK_2);
    const char *names[]={"BUILTIN TORRENTS","AUTO SUBTITLES","AUTOPLAY NEXT","UI SOUNDS","PERFORMANCE OVERLAY"};
    for(int i=0;i<5;++i){int y=200+i*100;draw_round_rect(b,190,y,950,78,22,!s->rail_focus&&s->row==i?SURFACE_HI:SURFACE);
        draw_text(b,226,y+32,names[i],4,!s->rail_focus&&s->row==i?INK:INK_2);draw_round_rect(b,1000,y+24,104,42,21,s->settings[i]?0xff563aa7u:0xff514951u);
        draw_text(b,1025,y+32,(i==1||i==2||i==4)?(s->settings[i]?"ON":"OFF"):"N/A",3,INK);if(!s->rail_focus&&s->row==i)draw_focus(b,190,y,950,78);}
    int y=710;draw_round_rect(b,190,y,950,78,22,!s->rail_focus&&s->row==5?SURFACE_HI:SURFACE);draw_text(b,226,y+28,"STREMIO ACCOUNT",4,INK_2);
    draw_text(b,890,y+30,auth_key[0]?"LINKED":"LINK DEVICE",3,auth_key[0]?ACCENT_SOFT:INK_3);if(!s->rail_focus&&s->row==5)draw_focus(b,190,y,950,78);
    const char *help_title=s->row==0?"BUILTIN TORRENTS":s->row==1?"AUTO SUBTITLES":s->row==2?"AUTOPLAY NEXT":s->row==3?"UI SOUNDS":s->row==4?"PERFORMANCE OVERLAY":"STREMIO ACCOUNT";
    const char *help=s->row==0?"Torrent sources resolve, but the torrent engine is not yet ported.":s->row==1?"LOAD TEXT SUBTITLES FROM YOUR INSTALLED ADDONS":s->row==2?"Automatic next-episode playback is not available yet.":s->row==3?"The PS5 sound mixer is not connected to this renderer yet.":s->row==4?"SHOW MEASURED FPS AND FRAME TIME. GOLDHEN GAME OVERLAY CAN RUN ALONGSIDE IT":"LINK YOUR ACCOUNT WITH A SECURE SHORT CODE";
    draw_round_outline(b,1200,200,620,568,32,2,0xff514651u);draw_text(b,1245,260,help_title,4,INK);draw_text(b,1245,330,s->row==1||s->row==2||s->row==4?(s->settings[s->row]?"ON":"OFF"):s->row<4?"NOT PORTED":(auth_key[0]?"LINKED":"NOT LINKED"),4,ACCENT_SOFT);
    draw_wrapped(b,1245,410,help,31,5);draw_round_rect(b,1245,650,230,58,29,INK);draw_image(b,&cross_icon,1260,660,38,38);draw_text(b,1312,670,s->row==5?"OPEN":"SWITCH",3,GROUND);
}
static void draw_account(void *b,const AppState *s){
    (void)s;draw_text(b,190,140,"Stremio account",6,INK_2);draw_text(b,190,205,"Link this PS4 to your Stremio account",3,INK_3);
    draw_round_rect(b,190,270,1420,430,34,SURFACE);draw_round_rect(b,230,310,180,180,36,0xff3d2a73u);draw_image(b,logo_large.pixels?&logo_large:&logo,270,350,100,100);
    if(account_status==4){draw_round_rect(b,460,320,270,48,24,0xff315d3fu);draw_text(b,500,334,"ACCOUNT LINKED",3,0xff8cffaau);
        draw_text(b,460,410,"WELCOME BACK",6,INK);draw_text(b,460,500,"YOUR SECURE ACCOUNT KEY IS SAVED LOCALLY",3,INK_2);char sync[96];snprintf(sync,sizeof(sync),account_sync_status==1?"SYNCING LIBRARY AND ADDONS":account_sync_status==2?"SYNCED  %d LIBRARY ITEMS  %d ADDONS":account_sync_status==4?"PARTIAL SYNC  PRESS OPTIONS TO RETRY":account_sync_status==3?"SYNC FAILED  PRESS OPTIONS TO RETRY":"PRESS OPTIONS TO SYNC",saved_id_count,account_addon_count);draw_text(b,460,550,sync,3,account_sync_status==3?ACCENT_SOFT:INK_3);}
    else {draw_text(b,460,330,account_status==1?"CREATING CODE":account_status==3?"CHECKING APPROVAL":account_status==5?"LINK SERVICE UNAVAILABLE":"ENTER THIS CODE",4,INK_2);
        if(link_code[0]){draw_round_rect(b,460,390,420,116,28,0xff35264fu);draw_text(b,510,418,link_code,10,ACCENT_SOFT);draw_text(b,460,560,"OPEN LINK STREMIO COM ON YOUR PHONE",3,INK);}}
    draw_round_rect(b,230,760,350,62,31,INK);draw_image(b,&cross_icon,250,772,38,38);draw_text(b,305,782,account_status==4?"LINKED":"CHECK APPROVAL",3,GROUND);
    draw_round_rect(b,610,760,290,62,31,SURFACE_HI);draw_image(b,&square_icon,630,772,38,38);draw_text(b,685,782,account_status==4?"UNLINK":"NEW CODE",3,INK);
    draw_round_rect(b,930,760,220,62,31,SURFACE_HI);draw_image(b,&circle_icon,950,772,38,38);draw_text(b,1005,782,"BACK",3,INK);if(account_status==4){draw_image(b,&options_icon,1200,772,38,38);draw_text(b,1252,782,"SYNC NOW",3,INK_2);}
}
static void draw_wrapped(void *b,int x,int y,const char *text,int columns,int lines){
    char line[64];const char *p=text;
    for(int row=0;row<lines&&*p;++row){int n=0,last_space=-1;
        while(p[n]&&n<columns){if(p[n]==' ')last_space=n;++n;}
        if(p[n]&&last_space>0)n=last_space;memcpy(line,p,(size_t)n);line[n]=0;draw_text(b,x,y+row*42,line,3,INK_2);
        p+=n;while(*p==' ')++p;}
}
static void draw_detail(void *b,const AppState *s){
    int index=s->detail_index;if(index<0||index>=CARD_CAPACITY||!live_cards[index].name[0])return;CatalogCard *card=&live_cards[index];
    draw_text(b,190,140,"TITLE DETAILS",5,ACCENT_SOFT);
    draw_round_rect(b,190,220,360,540,24,SURFACE);if(card->poster.pixels)draw_image_rounded(b,&card->poster,190,220,360,540,24);
    char title[41]={0};strncpy(title,card->name,sizeof(title)-1);if(hero_logo.pixels)draw_image(b,&hero_logo,610,205,470,90);else draw_text(b,610,230,title,strlen(title)>22?3:5,INK);
    draw_round_rect(b,610,305,150,48,24,0xff432d6fu);draw_text(b,642,318,card->type,3,ACCENT_SOFT);draw_text(b,800,318,card->year,3,INK_2);
    draw_text(b,610,375,card->runtime,3,INK_2);draw_round_rect(b,770,364,104,40,10,0xff18c5f5u);draw_text(b,790,375,card->rating,3,0xff0c0b11u);draw_text(b,915,375,card->genres,2,INK_3);
    draw_wrapped(b,610,420,card->description[0]?card->description:"DESCRIPTION NOT AVAILABLE",43,7);
    draw_round_rect(b,1300,220,430,380,30,SURFACE);draw_text(b,1340,260,"About",4,INK);draw_text(b,1340,330,"Type",2,INK_3);draw_text(b,1480,327,card->type,3,INK_2);
    draw_text(b,1340,390,"Released",2,INK_3);draw_text(b,1480,387,card->year,3,INK_2);draw_text(b,1340,450,"Runtime",2,INK_3);draw_text(b,1480,447,card->runtime,3,INK_2);
    draw_text(b,1340,510,"Rating",2,INK_3);draw_text(b,1480,507,card->rating,3,0xff18c5f5u);
    draw_round_rect(b,610,790,440,80,40,0xff563aa7u);draw_image(b,&play_icon,638,808,44,44);draw_text(b,706,818,!strcmp(card->type,"SERIES")?"CHOOSE EPISODE":"FIND STREAMS",4,INK);
    if(card->saved){draw_round_rect(b,1080,790,260,80,40,0xff315d3fu);draw_image(b,&check_icon,1105,810,40,40);draw_text(b,1160,818,"SAVED",4,0xff8cffaau);}
    draw_image(b,&cross_icon,610,902,38,38);draw_text(b,660,912,"STREAMS",3,INK_2);
    draw_image(b,&square_icon,830,902,38,38);draw_text(b,880,912,card->saved?"REMOVE":"SAVE",3,INK_2);
    draw_image(b,&circle_icon,1090,902,38,38);draw_text(b,1140,912,"BACK",3,INK_2);
}
static void draw_episodes(void *b,const AppState *s){
    CatalogCard *card=&live_cards[s->detail_index];draw_text(b,190,140,card->name,6,INK);draw_text(b,190,205,"Episodes",4,ACCENT_SOFT);
    draw_round_rect(b,190,255,1420,74,37,SURFACE);draw_image(b,&l1_icon,220,272,40,40);char season[32];snprintf(season,sizeof(season),"Season %d",s->season);
    draw_text(b,300,274,season,4,INK);draw_image(b,&r1_icon,1510,272,40,40);char count[32];int total=0;for(int i=0;i<episode_count;++i)if(episodes[i].season==s->season)++total;
    snprintf(count,sizeof(count),"%d episodes",total);draw_text(b,1260,278,count,3,INK_3);
    if(!episode_count){draw_round_rect(b,190,370,1420,190,30,SURFACE);draw_text(b,250,430,"Episode data is unavailable",5,INK);draw_text(b,250,500,"Return to details and try again",3,INK_3);}
    else {int ordinal=0;for(int i=0;i<s->episode_cursor;++i)if(episodes[i].season==s->season)++ordinal;int skip=ordinal>5?ordinal-5:0,seen=0,visible=0;
        for(int i=0;i<episode_count&&visible<6;++i)if(episodes[i].season==s->season){if(seen++<skip)continue;int y=360+visible*100,focused=i==s->episode_cursor;draw_round_rect(b,190,y,1420,82,24,focused?SURFACE_HI:SURFACE);
            char number[20];snprintf(number,sizeof(number),"E%d",episodes[i].number);draw_round_rect(b,215,y+18,78,46,16,0xff563aa7u);draw_text(b,235,y+30,number,3,INK);
            draw_text(b,330,y+18,episodes[i].name,4,focused?INK:INK_2);char short_description[76]={0};strncpy(short_description,episodes[i].description,sizeof(short_description)-1);draw_text(b,330,y+54,short_description,2,INK_3);
            draw_image(b,&play_small_icon,1535,y+27,28,28);if(focused)draw_focus(b,190,y,1420,82);++visible;}}
    draw_image(b,&cross_icon,190,950,38,38);draw_text(b,244,960,"FIND STREAMS",3,INK_2);draw_image(b,&circle_icon,1240,950,38,38);draw_text(b,1294,960,"BACK TO DETAILS",3,INK_2);
}
static void draw_streams(void *b,const AppState *s){
    CatalogCard *card=&live_cards[s->detail_index];draw_text(b,190,140,"STREAM SOURCES",6,INK);
    draw_text(b,190,220,card->name,4,ACCENT_SOFT);
    if(stream_count){
        for(int i=(s->stream_cursor/5)*5;i<stream_count&&i<(s->stream_cursor/5)*5+5;++i){int y=290+(i%5)*120,focused=i==s->stream_cursor;StreamSource *source=&stream_sources[i];
            draw_round_rect(b,190,y,1420,96,26,focused?SURFACE_HI:SURFACE);draw_round_rect(b,220,y+20,120,56,18,0xff563aa7u);
            draw_text(b,245,y+36,source->name[0]?source->name:"SOURCE",3,INK);draw_text(b,380,y+24,source->addon,4,INK);
            draw_text(b,380,y+62,source->title[0]?source->title:"Torrent source",3,INK_3);draw_image(b,&play_small_icon,1320,y+34,28,28);draw_text(b,1360,y+38,source->url[0]?"HTTP":"TORRENT",3,ACCENT_SOFT);
            if(focused)draw_focus(b,190,y,1420,96);}
        draw_image(b,&cross_icon,190,920,38,38);draw_text(b,244,930,"SELECT SOURCE",3,INK_2);
    }else {draw_round_rect(b,190,300,1420,280,32,SURFACE);
        if(stream_status==3){draw_round_rect(b,230,340,84,84,42,0xff315d3fu);draw_image(b,&check_icon,252,362,40,40);draw_text(b,350,360,"No compatible streams found",5,INK);draw_text(b,350,435,"Installed sources were checked for this title",3,INK_2);}
        else if(stream_status==5){draw_text(b,250,370,"Source request failed",5,INK);draw_text(b,250,450,"Check the network and try again",3,INK_2);}
        else {draw_text(b,250,370,"No stream addon is enabled",5,INK);draw_text(b,250,450,"Open Addons to configure a source",3,INK_2);}
        draw_round_rect(b,250,630,500,82,41,0xff563aa7u);draw_image(b,&cross_icon,278,652,38,38);draw_text(b,340,654,"OPEN ADDONS",4,INK);}
    draw_image(b,&circle_icon,1250,920,38,38);draw_text(b,1304,930,"BACK TO DETAILS",3,INK_2);
}
static int draw_prompt(void *b,const Image *icon,const char *label,int x,int y){
    draw_image(b,icon,x,y,38,38);draw_text(b,x+50,y+10,label,3,INK_2);return x+50+(int)strlen(label)*18+28;
}
static void draw_search(void *b,const AppState *s){
    draw_text(b,190,120,"SEARCH",6,INK);
    const char *status=search_status==1?"LOADING":search_status==2?"LIVE RESULTS":search_status==3?"NO RESULTS":"TYPE A TITLE";
    draw_text(b,1210,142,status,3,search_status==2?ACCENT_SOFT:INK_3);
    if(search_count){for(int i=0;i<search_count;++i)draw_card(b,190+i*290,190,250,300,10+i,s->result_focus&&s->search_cursor==i,live_cards[10+i].name);}
    else {draw_round_rect(b,190,190,1450,300,32,SURFACE);draw_text(b,250,310,"ENTER A MOVIE NAME THEN CHOOSE GO",4,INK_2);}
    static const char *special[]={"SPACE","DELETE","GO"};
    for(int i=0;i<29;++i){int col=i%10,row=i/10,x=190+col*145,y=550+row*100,w=125;
        int focused=!s->result_focus&&s->keyboard==i;draw_round_rect(b,x,y,w,72,18,focused?0xff563aa7u:SURFACE_HI);
        char letter[2]={(char)('A'+i),0};const char *label=i<26?letter:special[i-26];int scale=i<26?4:2;
        draw_text(b,x+(i<26?48:14),y+24,label,scale,INK);if(focused)draw_focus(b,x,y,w,72);}
    draw_image(b,&cross_icon,190,890,38,38);draw_text(b,240,900,"TYPE OR OPEN",3,INK_2);
    draw_image(b,&square_icon,520,890,38,38);draw_text(b,570,900,"DELETE",3,INK_2);
    draw_image(b,&circle_icon,800,890,38,38);draw_text(b,850,900,"CLOSE",3,INK_2);
}
static void draw_footer(void *b,const AppState *s){
    draw_vgradient(b,140,990,1780,90,GROUND,0xff171117u);int x=190;
    x=draw_prompt(b,&dpad_icon,"NAVIGATE",x,1025);x=draw_prompt(b,&cross_icon,s->rail_focus?"OPEN":"SELECT",x,1025);
    x=draw_prompt(b,&triangle_icon,"SEARCH",x,1025);x=draw_prompt(b,&l1_icon,"PAGES",x,1025);
    x=draw_prompt(b,&r1_icon,"PAGES",x,1025);draw_prompt(b,&circle_icon,"BACK",x,1025);
}
static void show_toast(const char *message){strncpy(toast_text,message,sizeof(toast_text)-1);toast_text[sizeof(toast_text)-1]=0;toast_frames=90;}
static void draw_toast(void *b){if(!toast_frames||!toast_text[0])return;int w=520,x=WIDTH-w-70,y=900;draw_round_rect(b,x+8,y+10,w,70,35,0xff050405u);draw_round_rect(b,x,y,w,70,35,0xff3b343du);draw_image(b,&check_small_icon,x+24,y+21,28,28);draw_text(b,x+70,y+22,toast_text,3,INK);}
static void draw_player(void *b){
    PlaybackStatus p=playback_status();
    if(video_canvas)memcpy(b,video_canvas,(size_t)WIDTH*HEIGHT*4);else fill_buffer(b,0xff000000u);
    char subtitle[1024]={0};if(subtitle_selected>=0&&pj_subtitle_at((double)p.position_ms/1000.0,subtitle,sizeof(subtitle))){
        char *second=strchr(subtitle,'\n');if(second)*second++=0;int w=(int)strlen(subtitle)*16+80;if(w>1700)w=1700;
        draw_round_rect(b,(WIDTH-w)/2,770,w,58,18,0xd9110b0cu);draw_text(b,(WIDTH-w)/2+40,786,subtitle,3,INK);
        if(second&&*second){int w2=(int)strlen(second)*16+80;if(w2>1700)w2=1700;draw_round_rect(b,(WIDTH-w2)/2,832,w2,58,18,0xd9110b0cu);draw_text(b,(WIDTH-w2)/2+40,848,second,3,INK);}
    }
    if(playback_controls||p.paused||p.buffering||p.error){
        draw_rect(b,0,0,WIDTH,115,0xff110b0cu);draw_text(b,80,32,"Stremio player",5,INK);
        char timeline[96];snprintf(timeline,sizeof(timeline),"%llu:%02llu / %llu:%02llu", (unsigned long long)(p.position_ms/60000),(unsigned long long)(p.position_ms/1000%60),(unsigned long long)(p.duration_ms/60000),(unsigned long long)(p.duration_ms/1000%60));
        draw_rect(b,0,900,WIDTH,180,0xff110b0cu);draw_text(b,90,920,timeline,4,INK);
        int played=p.duration_ms?(int)((double)p.position_ms/p.duration_ms*1740):0;if(played>1740)played=1740;
        draw_round_rect(b,90,980,1740,8,4,SURFACE_HI);if(played>0)draw_round_rect(b,90,980,played,8,4,ACCENT_SOFT);
        int x=90;x=draw_prompt(b,&cross_icon,p.paused?"PLAY":"PAUSE",x,1020);x=draw_prompt(b,&dpad_icon,"10 SEC",x,1020);x=draw_prompt(b,&square_icon,subtitle_count?"SUBTITLES":"NO SUBS",x,1020);x=draw_prompt(b,&triangle_icon,p.audio_tracks>1?"AUDIO":"AUDIO",x,1020);x=draw_prompt(b,&l1_icon,"-1 MIN",x,1020);x=draw_prompt(b,&r1_icon,"+1 MIN",x,1020);draw_prompt(b,&circle_icon,"STOP",x,1020);
        if(subtitle_selected>=0&&subtitle_label[0])draw_text(b,1450,55,subtitle_label,2,INK_2);if(p.audio_tracks>1&&p.audio_language[0])draw_text(b,1250,55,p.audio_language,2,INK_2);
        if(p.error||p.buffering){draw_round_rect(b,310,440,1300,150,30,SURFACE);draw_text(b,360,485,p.error?p.message:"Opening stream...",4,p.error?ACCENT_SOFT:INK);}
        if(p.audio_error)draw_text(b,900,55,"Audio format/output unavailable",3,ACCENT_SOFT);
    }
}
static void draw_screen(void *b,const AppState *s){
    if(s->playing){draw_player(b);draw_toast(b);return;}
    draw_page_backdrop(b,s);draw_rail(b,s);draw_topbar(b,s);
    if(s->account)draw_account(b,s);
    else if(s->episodes)draw_episodes(b,s);
    else if(s->streams)draw_streams(b,s);
    else if(s->detail)draw_detail(b,s);
    else if(s->search)draw_search(b,s);
    else switch(s->page){case 0:draw_board(b,s);break;case 1:draw_discover(b,s);break;case 2:draw_library(b,s);break;case 3:draw_addons(b,s);break;default:draw_settings(b,s);}
    draw_footer(b,s);draw_toast(b);
}
static int open_pad(void){
    int result=scePadInit();if(result!=0)return result;OrbisUserServiceInitializeParams p={0};p.priority=ORBIS_KERNEL_PRIO_FIFO_LOWEST;
    result=sceUserServiceInitialize(&p);if(result!=0&&result!=(int)0x80960003)return result;int user=-1;
    result=sceUserServiceGetInitialUser(&user);if(result!=0)return result;return scePadOpen(user,0,0,0);
}
static void reset_cursor(AppState *s){s->row=0;s->column=0;s->library_page=0;}
static void save_playback_progress(const AppState *s){
    PlaybackStatus p=playback_status();if(p.has_video&&s->detail_index>=0&&s->detail_index<CARD_CAPACITY){
        CatalogCard *card=&live_cards[s->detail_index];const char *video=selected_video_id[0]?selected_video_id:card->id;
        pj_progress_save("/data/stremio-ps4-progress.json",card->id,video,(double)p.position_ms/1000.0,(double)p.duration_ms/1000.0);
        progress_saved_ms=p.position_ms;
    }
}
static int start_stream_source(AppState *s,int index){
    if(index<0||index>=stream_count)return 0;StreamSource *source=&stream_sources[index];
    if(source->needs_headers){show_toast("This source requires HTTP header support");return 0;}
    if(!source->url[0]){show_toast("Torrent backend still requires porting");return 0;}
    if(!video_canvas)video_canvas=malloc((size_t)WIDTH*HEIGHT*4);if(!video_canvas){show_toast("Not enough memory for video");return 0;}
    fill_buffer(video_canvas,0xff000000u);
    if(s->settings[1]&&subtitle_count&&!load_subtitle_track(0))show_toast("Subtitle download failed");else if(!s->settings[1])load_subtitle_track(-1);
    playback_open(source->url,http_context,ssl_context);CatalogCard *card=&live_cards[s->detail_index];const char *video=selected_video_id[0]?selected_video_id:card->id;
    playback_resume_at((uint64_t)(pj_progress_resume(card->id,video)*1000));progress_saved_ms=0;s->playing=1;playback_controls=180;return 1;
}
static int autoplay_next_episode(AppState *s){
    int current=-1;for(int i=0;i<episode_count;++i)if(!strcmp(episodes[i].id,selected_video_id)){current=i;break;}
    if(current<0||current+1>=episode_count)return 0;
    ++current;s->episode_cursor=current;s->season=episodes[current].season;snprintf(selected_video_id,sizeof(selected_video_id),"%s",episodes[current].id);
    load_stream_sources(&live_cards[s->detail_index]);s->stream_cursor=0;
    for(int i=0;i<stream_count;++i)if(stream_sources[i].url[0]&&!stream_sources[i].needs_headers){s->stream_cursor=i;return start_stream_source(s,i);}
    s->playing=0;s->streams=1;s->episodes=0;show_toast("Choose a source for the next episode");return 1;
}
static int route_input(AppState *s,uint32_t pressed){
    if(s->playing){
        if(pressed)playback_controls=180;
        if(pressed&ORBIS_PAD_BUTTON_CIRCLE){save_playback_progress(s);playback_close();s->playing=0;return 1;}
        if(pressed&ORBIS_PAD_BUTTON_CROSS)playback_toggle_pause();
        if(pressed&ORBIS_PAD_BUTTON_LEFT)playback_seek(-10);
        if(pressed&ORBIS_PAD_BUTTON_RIGHT)playback_seek(10);
        if(pressed&ORBIS_PAD_BUTTON_L1)playback_seek(-60);
        if(pressed&ORBIS_PAD_BUTTON_R1)playback_seek(60);
        if((pressed&ORBIS_PAD_BUTTON_SQUARE)&&subtitle_count){int next=subtitle_selected+1;if(next>=subtitle_count)next=-1;if(load_subtitle_track(next))show_toast(next<0?"Subtitles off":"Subtitle track selected");else show_toast("Subtitle download failed");}
        if(pressed&ORBIS_PAD_BUTTON_TRIANGLE)show_toast(playback_cycle_audio()?"Audio track changed":"No alternate audio track");
        return pressed!=0;
    }
    if(s->account){
        if(pressed&ORBIS_PAD_BUTTON_CIRCLE)s->account=0;
        if((pressed&ORBIS_PAD_BUTTON_CROSS)&&account_status!=4){poll_device_link();if(account_status==4)sync_account_data();show_toast(account_status==4?(account_sync_status==2?"Account linked and synced":"Linked; sync needs retry"):"Approval not received yet");}
        if((pressed&ORBIS_PAD_BUTTON_OPTIONS)&&account_status==4){sync_account_data();save_state(s);show_toast(account_sync_status==2?"Account data synced":"Account sync failed");}
        if(pressed&ORBIS_PAD_BUTTON_SQUARE){if(account_status==4){auth_key[0]=link_code[0]=0;account_status=account_sync_status=account_addon_count=0;pj_free(addon_document);addon_document=0;save_account();show_toast("Account unlinked");}else{create_device_link();show_toast("New link code created");}}return pressed!=0;
    }
    if(s->episodes){
        if(pressed&ORBIS_PAD_BUTTON_CIRCLE){s->episodes=0;return 1;}
        if(pressed&ORBIS_PAD_BUTTON_L1){for(int season=s->season-1;season>=0;--season){int first=first_episode_in_season(season);if(first>=0){s->season=season;s->episode_cursor=first;break;}}}
        if(pressed&ORBIS_PAD_BUTTON_R1){for(int season=s->season+1;season<=max_season;++season){int first=first_episode_in_season(season);if(first>=0){s->season=season;s->episode_cursor=first;break;}}}
        if(pressed&ORBIS_PAD_BUTTON_UP){for(int i=s->episode_cursor-1;i>=0;--i)if(episodes[i].season==s->season){s->episode_cursor=i;break;}}
        if(pressed&ORBIS_PAD_BUTTON_DOWN){for(int i=s->episode_cursor+1;i<episode_count;++i)if(episodes[i].season==s->season){s->episode_cursor=i;break;}}
        if((pressed&ORBIS_PAD_BUTTON_CROSS)&&episode_count&&s->episode_cursor>=0&&s->episode_cursor<episode_count){strncpy(selected_video_id,episodes[s->episode_cursor].id,sizeof(selected_video_id)-1);load_stream_sources(&live_cards[s->detail_index]);s->streams=1;s->episodes=0;s->stream_cursor=0;}
        return pressed!=0;
    }
    if(s->streams){
        if(pressed&ORBIS_PAD_BUTTON_CIRCLE)s->streams=0;
        if(stream_count){if((pressed&ORBIS_PAD_BUTTON_UP)&&s->stream_cursor>0)--s->stream_cursor;if((pressed&ORBIS_PAD_BUTTON_DOWN)&&s->stream_cursor<stream_count-1)++s->stream_cursor;
            if(pressed&ORBIS_PAD_BUTTON_CROSS){
                start_stream_source(s,s->stream_cursor);
            }}
        else if(pressed&ORBIS_PAD_BUTTON_CROSS){s->streams=0;s->detail=0;s->page=3;s->rail_focus=0;reset_cursor(s);}return pressed!=0;
    }
    if(s->detail){
        if(pressed&ORBIS_PAD_BUTTON_CIRCLE)s->detail=0;
        if(pressed&ORBIS_PAD_BUTTON_CROSS){CatalogCard *card=&live_cards[s->detail_index];s->stream_cursor=0;selected_video_id[0]=0;
            if(!strcmp(card->type,"SERIES")){load_episode_catalog(card);s->season=1;s->episode_cursor=first_episode_in_season(1);if(s->episode_cursor<0&&episode_count){s->season=episodes[0].season;s->episode_cursor=0;}s->episodes=1;}
            else {load_stream_sources(card);s->streams=1;}}
        if(pressed&ORBIS_PAD_BUTTON_SQUARE){toggle_saved(&live_cards[s->detail_index]);save_state(s);show_toast(live_cards[s->detail_index].saved?"Added to Library":"Removed from Library");}
        return pressed!=0;
    }
    if(s->search){
        size_t length=strlen(s->query);
        if(pressed&ORBIS_PAD_BUTTON_CIRCLE){s->search=0;s->result_focus=0;return 1;}
        if((pressed&ORBIS_PAD_BUTTON_SQUARE)&&length){s->query[length-1]=0;return 1;}
        if(s->result_focus){
            if((pressed&ORBIS_PAD_BUTTON_LEFT)&&s->search_cursor>0)--s->search_cursor;
            if((pressed&ORBIS_PAD_BUTTON_RIGHT)&&s->search_cursor<search_count-1)++s->search_cursor;
            if(pressed&ORBIS_PAD_BUTTON_DOWN){s->result_focus=0;s->keyboard=0;}
            if(pressed&ORBIS_PAD_BUTTON_CROSS){s->detail_index=10+s->search_cursor;load_metadata(&live_cards[s->detail_index]);s->detail=1;}
            return pressed!=0;
        }
        int col=s->keyboard%10,row=s->keyboard/10;
        if((pressed&ORBIS_PAD_BUTTON_LEFT)&&col>0)--s->keyboard;
        if((pressed&ORBIS_PAD_BUTTON_RIGHT)&&col<9&&s->keyboard<28)++s->keyboard;
        if(pressed&ORBIS_PAD_BUTTON_UP){if(row>0)s->keyboard-=10;else if(search_count){s->result_focus=1;s->search_cursor=0;}}
        if((pressed&ORBIS_PAD_BUTTON_DOWN)&&row<2&&s->keyboard+10<29)s->keyboard+=10;
        if(pressed&ORBIS_PAD_BUTTON_TRIANGLE){if(length)run_search(s->query);return 1;}
        if(pressed&ORBIS_PAD_BUTTON_CROSS){
            if(s->keyboard<26&&length+1<sizeof(s->query)){s->query[length]=(char)('A'+s->keyboard);s->query[length+1]=0;}
            else if(s->keyboard==26&&length+1<sizeof(s->query)&&length){s->query[length]=' ';s->query[length+1]=0;}
            else if(s->keyboard==27&&length)s->query[length-1]=0;
            else if(s->keyboard==28&&length){run_search(s->query);s->search_cursor=0;if(search_count)s->result_focus=1;}
        }
        return pressed!=0;
    }
    if(pressed&ORBIS_PAD_BUTTON_TRIANGLE){s->search=1;return 1;}
    if((pressed&ORBIS_PAD_BUTTON_OPTIONS)&&s->page==2){for(int i=0;i<CARD_CAPACITY;++i)live_cards[i].poster_attempted=0;show_toast("Refreshing Library artwork");return 1;}
    if((pressed&ORBIS_PAD_BUTTON_OPTIONS)&&s->page==0){refresh_catalogs();show_toast("Catalogs refreshed");return 1;}
    if((pressed&ORBIS_PAD_BUTTON_SQUARE)&&s->page==1){s->discover_series^=1;s->column=0;return 1;}
    if((pressed&ORBIS_PAD_BUTTON_SQUARE)&&s->page==2){s->library_filter=(s->library_filter+1)%3;s->library_page=0;s->column=0;return 1;}
    if(pressed&ORBIS_PAD_BUTTON_L1){s->page=(s->page+PAGE_COUNT-1)%PAGE_COUNT;s->rail_focus=0;reset_cursor(s);return 1;}
    if(pressed&ORBIS_PAD_BUTTON_R1){s->page=(s->page+1)%PAGE_COUNT;s->rail_focus=0;reset_cursor(s);return 1;}
    if(s->rail_focus){if((pressed&ORBIS_PAD_BUTTON_UP)&&s->row>0)--s->row;if((pressed&ORBIS_PAD_BUTTON_DOWN)&&s->row<PAGE_COUNT-1)++s->row;
        if(pressed&(ORBIS_PAD_BUTTON_RIGHT|ORBIS_PAD_BUTTON_CROSS)){s->page=s->row;s->rail_focus=0;reset_cursor(s);}if(pressed&ORBIS_PAD_BUTTON_CIRCLE){s->rail_focus=0;reset_cursor(s);}return pressed!=0;}
    if(s->page==3){
        int total=account_addon_count+2;
        if(pressed&ORBIS_PAD_BUTTON_LEFT){s->rail_focus=1;s->row=s->page;return 1;}
        if((pressed&ORBIS_PAD_BUTTON_UP)&&s->row>0)--s->row;
        if((pressed&ORBIS_PAD_BUTTON_DOWN)&&s->row+1<total)++s->row;
        if(pressed&ORBIS_PAD_BUTTON_OPTIONS){sync_account_data();save_state(s);show_toast(account_sync_status==2?"Account sources refreshed":"Check account sync");s->row=0;}
        if((pressed&ORBIS_PAD_BUTTON_CROSS)&&s->row==1){public_domain_enabled^=1;save_account();validate_public_domain();show_toast(public_domain_enabled?"Public Domain enabled":"Public Domain disabled");}
        if(pressed&ORBIS_PAD_BUTTON_CIRCLE){s->page=0;reset_cursor(s);}return pressed!=0;
    }

    if(pressed&ORBIS_PAD_BUTTON_LEFT){if(s->page==2&&s->column==0&&s->library_page>0){--s->library_page;s->column=4;}else if(s->column>0&&s->page<3)--s->column;else{s->rail_focus=1;s->row=s->page;}}
    if((pressed&ORBIS_PAD_BUTTON_RIGHT)&&s->page<3){if(s->page==2&&s->column==4&&saved_index_filtered((s->library_page+1)*5,s->library_filter)>=0){++s->library_page;s->column=0;}else if(s->column<4&&(s->page!=2||saved_index_filtered(s->library_page*5+s->column+1,s->library_filter)>=0))++s->column;}
    if(pressed&ORBIS_PAD_BUTTON_UP){if((s->page==0||s->page>=3)&&s->row>0)--s->row;}
    if(pressed&ORBIS_PAD_BUTTON_DOWN){if(s->page==0&&s->row<1){++s->row;if(s->column>3)s->column=3;}else if(s->page==3&&s->row<3)++s->row;else if(s->page==4&&s->row<5)++s->row;}
    if((pressed&ORBIS_PAD_BUTTON_CROSS)&&s->page==4){if(s->row==5){s->account=1;if(!auth_key[0]&&!link_code[0])create_device_link();}else if(s->row!=1&&s->row!=2&&s->row!=4){show_toast("This feature is not ported yet");}else{s->settings[s->row]^=1;save_state(s);show_toast(s->settings[s->row]?"Setting enabled":"Setting disabled");}}
    else if((pressed&ORBIS_PAD_BUTTON_CROSS)&&(catalog_status==2||s->page==2)&&s->page<3){
        int index=s->page==0?s->row*5+s->column:s->page==2?saved_index_filtered(s->library_page*5+s->column,s->library_filter):s->column+(s->page==1&&s->discover_series?5:0);
        if(index>=0&&index<CARD_CAPACITY&&live_cards[index].name[0]){s->detail_index=index;load_metadata(&live_cards[index]);s->detail=1;}
    }
    if(pressed&ORBIS_PAD_BUTTON_CIRCLE){if(s->page!=0){s->page=0;reset_cursor(s);}else return -1;}return pressed!=0;
}
/* Only the worker mutates network/catalog state while a job is active.
 * The main thread presents a cached framebuffer; it must not render live data
 * until the acquire/join handoff completes. Fonts and bundled icons are immutable. */
static struct {pthread_t thread;AppState state;uint32_t pressed;int kind,done,result,active;} job;
static void *run_job(void *unused){
    (void)unused;
    if(job.kind==1){load_live_catalog();validate_public_domain();if(auth_key[0])sync_account_data();save_state(&job.state);job.result=1;}
    else if(job.kind==2)job.result=autoplay_next_episode(&job.state);
    else job.result=route_input(&job.state,job.pressed);
    if(job.state.page==2&&!job.state.detail&&!job.state.search)prepare_library_page(job.state.library_page,job.state.library_filter);
    __atomic_store_n(&job.done,1,__ATOMIC_RELEASE);return 0;
}
static int begin_job(const AppState *state,uint32_t pressed,int kind){
    job.state=*state;job.pressed=pressed;job.kind=kind;job.result=0;
    __atomic_store_n(&network_cancel,0,__ATOMIC_RELEASE);__atomic_store_n(&job.done,0,__ATOMIC_RELEASE);
    pthread_attr_t attr;pthread_attr_init(&attr);pthread_attr_setstacksize(&attr,2*1024*1024);
    int r=pthread_create(&job.thread,&attr,run_job,0);pthread_attr_destroy(&attr);job.active=r==0;return job.active;
}
static void draw_busy(void *b,unsigned ticks){
    draw_round_rect(b,510,440,900,180,32,SURFACE);
    draw_text(b,570,480,__atomic_load_n(&network_cancel,__ATOMIC_ACQUIRE)?"Cancelling remaining requests...":"Loading...",5,INK);
    draw_text(b,570,555,"Circle cancels after the current request",3,INK_2);
    draw_round_rect(b,560+(int)(ticks%60)*12,603,90,5,2,ACCENT_SOFT);
}
int main(void){
    const size_t frame_size=(size_t)WIDTH*HEIGHT*4,alignment=0x200000;
    const size_t allocation_size=((frame_size*BUFFER_COUNT+alignment-1)/alignment)*alignment;
    int video=sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN,ORBIS_VIDEO_OUT_BUS_MAIN,0,0);if(video<0)park_error("sceVideoOutOpen",video);
    off_t direct_offset=0;int result=sceKernelAllocateDirectMemory(0,sceKernelGetDirectMemorySize(),allocation_size,alignment,3,&direct_offset);
    if(result<0)park_error("sceKernelAllocateDirectMemory",result);void *video_memory=0;
    result=sceKernelMapDirectMemory(&video_memory,allocation_size,0x33,0,direct_offset,alignment);if(result<0)park_error("sceKernelMapDirectMemory",result);
    void *buffers[BUFFER_COUNT]={video_memory,(uint8_t*)video_memory+frame_size};void *canvas=malloc(frame_size);if(!canvas)park_error("malloc canvas",-1);OrbisVideoOutBufferAttribute attribute;
    sceVideoOutSetBufferAttribute(&attribute,0x80000000,1,0,WIDTH,HEIGHT,WIDTH);
    result=sceVideoOutRegisterBuffers(video,0,buffers,BUFFER_COUNT,&attribute);if(result<0)park_error("sceVideoOutRegisterBuffers",result);
    load_assets();AppState state={0};for(int i=0;i<5;++i)state.settings[i]=1;load_state(&state);load_library_cache();load_account();pj_progress_load("/data/stremio-ps4-progress.json");catalog_status=1;
    draw_screen(canvas,&state);memcpy(buffers[0],canvas,frame_size);memcpy(buffers[1],canvas,frame_size);sceVideoOutSetFlipRate(video,0);
    result=sceVideoOutSubmitFlip(video,0,ORBIS_VIDEO_OUT_FLIP_VSYNC,1);if(result<0)park_error("sceVideoOutSubmitFlip",result);
    sceSystemServiceHideSplashScreen();
    const int pad=open_pad();if(pad<0)park_error("scePadOpen",pad);
    int active_buffer=0;int64_t flip_id=2;uint32_t previous_buttons=0;unsigned busy_ticks=0;
    if(!begin_job(&state,0,1))park_error("Network worker unavailable",-1);
    for(;;){
        struct timespec tick;clock_gettime(CLOCK_MONOTONIC,&tick);uint64_t start_us=(uint64_t)tick.tv_sec*1000000+tick.tv_nsec/1000;
        OrbisPadData input={0};uint32_t pressed=0;
        if(scePadReadState(pad,&input)==0){pressed=input.buttons&~previous_buttons;previous_buttons=input.buttons;}
        int changed=0,new_video=0,toast_expired=0,busy_frame=0;
        if(job.active){
            if(pressed&ORBIS_PAD_BUTTON_CIRCLE)__atomic_store_n(&network_cancel,1,__ATOMIC_RELEASE);
            if(__atomic_load_n(&job.done,__ATOMIC_ACQUIRE)){
                pthread_join(job.thread,0);state=job.state;changed=job.result;job.active=0;busy_ticks=0;
                if(__atomic_load_n(&network_cancel,__ATOMIC_ACQUIRE)){show_toast("Request cancelled");changed=1;}
            }else {++busy_ticks;if(busy_ticks>8)busy_frame=1;}
        }else if(pressed){
            if(state.playing)changed=route_input(&state,pressed);
            else if(!begin_job(&state,pressed,0)){show_toast("Unable to start background task");changed=1;}
        }
        if(!job.active){
            if(changed<0){playback_close();scePadClose(pad);sceSystemServiceLoadExec("exit",0);}
            if(changed>0)animation_frames=7;
            if(state.playing){
                new_video=playback_video(video_canvas,WIDTH,HEIGHT);PlaybackStatus ps=playback_status();
                if(ps.position_ms>progress_saved_ms+10000)save_playback_progress(&state);
                if(ps.ended){save_playback_progress(&state);playback_close();state.playing=0;changed=1;
                    if(state.settings[2]&&selected_video_id[0]&&begin_job(&state,0,2))show_toast("Loading next episode");else show_toast("Playback finished");}
                if(playback_controls)--playback_controls;
            }
            if(toast_frames>0){--toast_frames;if(!toast_frames)toast_expired=1;}
        }
        if(busy_frame||(!job.active&&(changed>0||animation_frames>0||toast_expired||new_video||state.playing))){
            if(busy_frame)draw_busy(canvas,busy_ticks);
            else {
                if(animation_frames>0)--animation_frames;++ui_phase;
                struct timespec now;if(clock_gettime(CLOCK_MONOTONIC,&now)==0){
                    uint64_t ns=(uint64_t)now.tv_sec*1000000000ull+(uint64_t)now.tv_nsec;
                    if(last_flip_ns&&ns>last_flip_ns&&ns-last_flip_ns<500000000ull){
                        float ms=(float)(ns-last_flip_ns)/1000000.0f;
                        measured_frame_ms=measured_frame_ms?measured_frame_ms*0.7f+ms*0.3f:ms;measured_fps=1000.0f/measured_frame_ms;
                    }last_flip_ns=ns;
                }
                draw_screen(canvas,&state);
            }
            unsigned waits=0;while(sceVideoOutIsFlipPending(video)>0&&waits++<2000)sceKernelUsleep(1000);
            if(waits>=2000)park_error("VideoOut flip timeout",-1);
            active_buffer^=1;memcpy(buffers[active_buffer],canvas,frame_size);
            result=sceVideoOutSubmitFlip(video,active_buffer,ORBIS_VIDEO_OUT_FLIP_VSYNC,flip_id++);
            if(result<0)park_error("sceVideoOutSubmitFlip",result);
        }
        clock_gettime(CLOCK_MONOTONIC,&tick);uint64_t elapsed=(uint64_t)tick.tv_sec*1000000+tick.tv_nsec/1000-start_us;
        if(elapsed<16667)sceKernelUsleep((unsigned)(16667-elapsed));
    }
}
