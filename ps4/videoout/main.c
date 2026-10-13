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
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb_image.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "third_party/hui/third_party/stb/stb_truetype.h"

uint64_t sceLibcHeapSize = 24 * 1024 * 1024;
enum { WIDTH = 1920, HEIGHT = 1080, BUFFER_COUNT = 2, PAGE_COUNT = 5 };
static const uint32_t GROUND=0xff110b0cu, SURFACE=0xff292328u, SURFACE_HI=0xff3a333du;
static const uint32_t ACCENT=0xfff55b7bu, ACCENT_SOFT=0xffff8ba5u;
static const uint32_t INK=0xfff7f7f7u, INK_2=0xffc8c8c8u, INK_3=0xff8f8f8fu;
typedef struct Image { unsigned char *pixels; int width, height; } Image;
typedef struct AppState {
    int page, rail_focus, row, column, search, detail, streams, detail_index;
    int keyboard, search_cursor, result_focus, discover_series, account, stream_cursor; char query[24]; int settings[4];
} AppState;
static const char *PAGE_NAMES[PAGE_COUNT]={"Board","Discover","Library","Addons","Settings"};
static const char *ICON_NAMES[PAGE_COUNT]={"nav_board","nav_discover","nav_library","nav_addons","nav_settings"};
static Image logo, wordmark, search_icon, cross_icon, circle_icon, triangle_icon, square_icon, options_icon;
static Image dpad_icon, l1_icon, r1_icon, play_icon, check_icon, chevron_icon, nav_line[PAGE_COUNT], nav_solid[PAGE_COUNT];
static unsigned char *font_atlas;static stbtt_bakedchar font_chars[96];static int font_ready;
typedef struct CatalogCard {
    char id[24], name[40], type[12], year[16], runtime[24], rating[12], genres[96];
    char description[640], poster_url[384]; Image poster; int saved, metadata_loaded;
} CatalogCard;
static CatalogCard live_cards[15];
typedef struct StreamSource {char name[32],title[96],info_hash[64];int file_index;} StreamSource;
static StreamSource stream_sources[8];static int stream_count,stream_status;
static int catalog_count;
static int search_count, search_status;
static char saved_ids[15][24];static int saved_id_count;
static int catalog_status;
static int poster_count;
static int net_pool, ssl_context, http_context;
static char auth_key[256], link_code[16], link_url[128];
static int account_status, public_domain_enabled=1, public_domain_online;

static int id_saved(const char *id){for(int i=0;i<saved_id_count;++i)if(!strcmp(saved_ids[i],id))return 1;return 0;}
static void apply_saved(void){for(int i=0;i<15;++i)if(live_cards[i].id[0])live_cards[i].saved=id_saved(live_cards[i].id);}
static void load_state(AppState *s){
    FILE *f=fopen("/data/stremio-ps4.cfg","r");if(!f)return;char line[128];
    if(fgets(line,sizeof(line),f))sscanf(line,"%d %d %d %d",&s->settings[0],&s->settings[1],&s->settings[2],&s->settings[3]);
    while(saved_id_count<15&&fgets(line,sizeof(line),f)){line[strcspn(line,"\r\n")]=0;if(line[0])strncpy(saved_ids[saved_id_count++],line,23);}fclose(f);
}
static void save_state(const AppState *s){
    saved_id_count=0;for(int i=0;i<15&&saved_id_count<15;++i)if(live_cards[i].saved&&live_cards[i].id[0])strncpy(saved_ids[saved_id_count++],live_cards[i].id,23);
    FILE *f=fopen("/data/stremio-ps4.cfg","w");if(!f)return;fprintf(f,"%d %d %d %d\n",s->settings[0],s->settings[1],s->settings[2],s->settings[3]);
    for(int i=0;i<saved_id_count;++i)fprintf(f,"%s\n",saved_ids[i]);fclose(f);
}
static void load_account(void){
    FILE *f=fopen("/data/stremio-ps4-auth.cfg","r");if(!f)return;char line[320];
    if(fgets(line,sizeof(line),f)){line[strcspn(line,"\r\n")]=0;if(!strncmp(line,"auth=",5))strncpy(auth_key,line+5,sizeof(auth_key)-1);}
    if(fgets(line,sizeof(line),f)&&!strncmp(line,"public_domain=",14))public_domain_enabled=atoi(line+14)!=0;fclose(f);
    if(auth_key[0])account_status=4;
}
static void save_account(void){
    FILE *f=fopen("/data/stremio-ps4-auth.cfg","w");if(!f)return;
    fprintf(f,"auth=%s\npublic_domain=%d\n",auth_key,public_domain_enabled);fclose(f);
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
    *received=0;int tpl=0,conn=0,req=0,status=0;unsigned char *body=0;size_t used=0,capacity=0;
    tpl=sceHttpCreateTemplate(http_context,"Stremio-PS4/0.10",ORBIS_HTTP_VERSION_1_1,1);if(tpl<0)goto done;
    sceHttpSetResolveTimeOut(tpl,5000000);sceHttpSetConnectTimeOut(tpl,5000000);sceHttpSetRecvTimeOut(tpl,7000000);
    conn=sceHttpCreateConnectionWithURL(tpl,url,1);if(conn<0)goto done;
    req=sceHttpCreateRequestWithURL(conn,ORBIS_METHOD_GET,url,0);if(req<0)goto done;
    sceHttpAddRequestHeader(req,"Accept","application/json,image/*",0);
    if(sceHttpSendRequest(req,0,0)<0||sceHttpGetStatusCode(req,&status)<0||status<200||status>=300)goto done;
    capacity=64*1024;if(capacity>limit)capacity=limit;body=(unsigned char*)malloc(capacity+1);if(!body)goto done;
    for(;;){
        if(used==capacity){size_t next=capacity*2;if(next>limit)next=limit;if(next<=capacity)goto failed;
            unsigned char *grown=(unsigned char*)realloc(body,next+1);if(!grown)goto failed;body=grown;capacity=next;}
        int got=sceHttpReadData(req,body+used,(uint32_t)(capacity-used));if(got<0)goto failed;if(got==0)break;used+=(size_t)got;
    }
    body[used]=0;*received=used;goto done;
failed:
    free(body);body=0;used=0;
done:
    if(req>0)sceHttpDeleteRequest(req);if(conn>0)sceHttpDeleteConnection(conn);if(tpl>0)sceHttpDeleteTemplate(tpl);
    sceKernelDebugOutText(0,"[stremio-ps4] GET %s status=%d bytes=%lu\n",url,status,(unsigned long)used);return body;
}

static const char *json_value(const char *from,const char *key,char *out,size_t out_size,int uppercase){
    const char *p=strstr(from,key);if(!p)return 0;p+=strlen(key);size_t n=0;
    while(*p&&*p!='\"'){if(*p=='\\'&&p[1])++p;if(n+1<out_size){char c=*p;out[n++]=uppercase&&c>='a'&&c<='z'?c-32:c;}++p;}
    out[n]=0;return *p?p+1:0;
}

static void create_device_link(void){
    account_status=1;link_code[0]=link_url[0]=0;if(!network_init()){account_status=5;return;}size_t size=0;
    unsigned char *json=http_get("https://link.stremio.com/api/v2/create?type=Create",128*1024,&size);
    if(!json){account_status=5;return;}const char *p=(char*)json,*v;
    if((v=strstr(p,"\"code\":\"")))json_value(v,"\"code\":\"",link_code,sizeof(link_code),1);
    if((v=strstr(p,"\"link\":\"")))json_value(v,"\"link\":\"",link_url,sizeof(link_url),0);
    free(json);account_status=link_code[0]?2:5;
}
static void poll_device_link(void){
    if(!link_code[0]){create_device_link();return;}account_status=3;char url[256];
    snprintf(url,sizeof(url),"https://link.stremio.com/api/v2/read?type=Read&code=%s",link_code);size_t size=0;
    unsigned char *json=http_get(url,256*1024,&size);if(!json){account_status=2;return;}const char *v=strstr((char*)json,"\"authKey\":\"");
    if(v){json_value(v,"\"authKey\":\"",auth_key,sizeof(auth_key),0);if(auth_key[0]){account_status=4;save_account();}}
    else account_status=2;free(json);
}
static void validate_public_domain(void){
    public_domain_online=0;if(!public_domain_enabled||!network_init())return;size_t size=0;
    unsigned char *json=http_get("https://caching.stremio.net/publicdomainmovies.now.sh/manifest.json",256*1024,&size);
    if(json){public_domain_online=strstr((char*)json,"org.stremio.pubdomainmovies")!=0;free(json);}
}

static int parse_catalog(char *json,int first,int maximum){
    const char *p=json;int count=0;while(count<maximum&&(p=strstr(p,"\"imdb_id\":\""))){
        CatalogCard *card=&live_cards[first+count];
        json_value(p,"\"imdb_id\":\"",card->id,sizeof(card->id),0);
        const char *name=strstr(p,"\"name\":\"");if(!name)break;
        const char *after=json_value(name,"\"name\":\"",card->name,sizeof(card->name),0);if(!after)break;
        const char *type=strstr(after,"\"type\":\"");if(type)json_value(type,"\"type\":\"",card->type,sizeof(card->type),1);
        const char *description=strstr(after,"\"description\":\"");if(description)json_value(description,"\"description\":\"",card->description,sizeof(card->description),0);
        const char *year=strstr(after,"\"releaseInfo\":\"");if(year)json_value(year,"\"releaseInfo\":\"",card->year,sizeof(card->year),1);
        const char *poster=strstr(after,"\"poster\":\"");if(!poster){p=after;continue;}
        p=json_value(poster,"\"poster\":\"",card->poster_url,sizeof(card->poster_url),0);if(!p)break;++count;
    }return count;
}

static void load_live_catalog(void){
    catalog_status=1;if(!network_init()){catalog_status=3;return;}size_t size=0;
    // v3-cinemeta currently redirects this resource. SceHttp does not follow
    // redirects by default, so use the canonical catalog host from Location.
    unsigned char *json=http_get("https://cinemeta-catalogs.strem.io/top/catalog/movie/top.json",2*1024*1024,&size);
    if(!json){catalog_status=4;return;}int movies=parse_catalog((char*)json,0,5);free(json);if(movies==0){catalog_status=5;return;}
    json=http_get("https://cinemeta-catalogs.strem.io/top/catalog/series/top.json",2*1024*1024,&size);
    int series=0;if(json){series=parse_catalog((char*)json,movies,5);free(json);}catalog_count=movies+series;
    for(int i=0;i<catalog_count;++i){size_t image_size=0;
        // The extensionless endpoint can redirect or return WebP. Its JPEG
        // representation is direct and supported by the native decoder.
        if(strlen(live_cards[i].poster_url)+4<sizeof(live_cards[i].poster_url))strcat(live_cards[i].poster_url,".jpg");
        unsigned char *encoded=http_get(live_cards[i].poster_url,3*1024*1024,&image_size);
        if(encoded){int channels=0;live_cards[i].poster.pixels=stbi_load_from_memory(encoded,(int)image_size,&live_cards[i].poster.width,&live_cards[i].poster.height,&channels,4);
            if(live_cards[i].poster.pixels)++poster_count;free(encoded);}}
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
    for(int i=0;i<search_count;++i){CatalogCard *card=&live_cards[10+i];size_t image_size=0;
        if(strlen(card->poster_url)+4<sizeof(card->poster_url))strcat(card->poster_url,".jpg");
        unsigned char *encoded=http_get(card->poster_url,3*1024*1024,&image_size);if(encoded){int channels=0;
            card->poster.pixels=stbi_load_from_memory(encoded,(int)image_size,&card->poster.width,&card->poster.height,&channels,4);free(encoded);}}
    apply_saved();search_status=2;
}

static void parse_genres(const char *json,char *out,size_t capacity){
    const char *p=strstr(json,"\"genres\":[");size_t used=0;if(!p){out[0]=0;return;}p+=10;
    while(*p&&*p!=']'&&used+2<capacity){while(*p&&*p!='\"'&&*p!=']')++p;if(*p!= '\"')break;++p;
        if(used&&used+3<capacity){out[used++]=' ';out[used++]=' ';}while(*p&&*p!='\"'&&used+1<capacity){char c=*p++;out[used++]=c>='a'&&c<='z'?c-32:c;}if(*p=='\"')++p;}out[used]=0;
}
static void load_metadata(CatalogCard *card){
    if(card->metadata_loaded||!card->id[0])return;char url[256];strcpy(url,"https://v3-cinemeta.strem.io/meta/");
    strcat(url,card->type[0]?card->type:"MOVIE");for(char *p=url;*p;++p)if(*p>='A'&&*p<='Z')*p+=32;
    strcat(url,"/");strcat(url,card->id);strcat(url,".json");size_t size=0;unsigned char *json=http_get(url,1024*1024,&size);
    if(!json)return;const char *p=(char*)json;const char *v;
    if((v=strstr(p,"\"description\":\"")))json_value(v,"\"description\":\"",card->description,sizeof(card->description),0);
    if((v=strstr(p,"\"runtime\":\"")))json_value(v,"\"runtime\":\"",card->runtime,sizeof(card->runtime),1);
    if((v=strstr(p,"\"imdbRating\":\"")))json_value(v,"\"imdbRating\":\"",card->rating,sizeof(card->rating),1);
    parse_genres(p,card->genres,sizeof(card->genres));card->metadata_loaded=1;free(json);
}
static void load_stream_sources(CatalogCard *card){
    stream_count=0;stream_status=1;memset(stream_sources,0,sizeof(stream_sources));
    if(!public_domain_enabled||!public_domain_online){stream_status=4;return;}char url[512];
    snprintf(url,sizeof(url),"https://caching.stremio.net/publicdomainmovies.now.sh/stream/%s/%s.json",!strcmp(card->type,"SERIES")?"series":"movie",card->id);
    size_t size=0;unsigned char *json=http_get(url,512*1024,&size);if(!json){stream_status=5;return;}const char *p=(char*)json;
    while(stream_count<8&&(p=strstr(p,"\"infoHash\":\""))){StreamSource *source=&stream_sources[stream_count];
        const char *next=json_value(p,"\"infoHash\":\"",source->info_hash,sizeof(source->info_hash),0);const char *v;
        if((v=strstr(p,"\"title\":\""))&&(!next||v<next+300))json_value(v,"\"title\":\"",source->title,sizeof(source->title),0);
        if((v=strstr(p,"\"name\":\""))&&(!next||v<next+300))json_value(v,"\"name\":\"",source->name,sizeof(source->name),0);
        if((v=strstr(p,"\"fileIdx\":"))&&(!next||v<next+300))source->file_index=atoi(v+10);
        ++stream_count;p=next?next:p+12;}
    free(json);stream_status=stream_count?2:3;
}
static int saved_index(int slot){int found=0;for(int i=0;i<15;++i)if(live_cards[i].saved&&live_cards[i].name[0]){if(found==slot)return i;++found;}return -1;}

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
static void draw_page_backdrop(void *b){
    fill_buffer(b,GROUND);draw_vgradient(b,140,0,1780,260,0xff251338u,GROUND);draw_vgradient(b,140,900,1780,180,GROUND,0xff080609u);
}
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
    check_icon=load_image("check.png");chevron_icon=load_image("chevron_right.png");char name[64];
    for(int i=0;i<PAGE_COUNT;++i){strcpy(name,ICON_NAMES[i]);strcat(name,".png");nav_line[i]=load_image(name);
        strcpy(name,ICON_NAMES[i]);strcat(name,"_o.png");nav_solid[i]=load_image(name);}
    sceKernelDebugOutText(0,"[stremio-ps4] PS5 assets: logo=%dx%d\n",logo.width,logo.height);
}
static void draw_image(void *buffer,const Image *image,int x,int y,int w,int h){
    if(!image->pixels||image->width<=0||image->height<=0)return; uint32_t *target=(uint32_t*)buffer;
    for(int dy=0;dy<h;++dy){int py=y+dy;if(py<0||py>=HEIGHT)continue;int sy=dy*image->height/h;
        for(int dx=0;dx<w;++dx){int px=x+dx;if(px<0||px>=WIDTH)continue;int sx=dx*image->width/w;
            const unsigned char *s=image->pixels+((size_t)sy*image->width+sx)*4;unsigned a=s[3];if(!a)continue;
            uint32_t *d=&target[(size_t)py*WIDTH+px];unsigned dr=*d&255u,dg=(*d>>8)&255u,db=(*d>>16)&255u;
            unsigned r=(s[0]*a+dr*(255-a))/255,g=(s[1]*a+dg*(255-a))/255,b=(s[2]*a+db*(255-a))/255;
            *d=0xff000000u|(b<<16)|(g<<8)|r;}}
}
static void draw_image_rounded(void *buffer,const Image *image,int x,int y,int w,int h,int radius){
    if(!image->pixels||image->width<=0||image->height<=0)return;uint32_t *target=(uint32_t*)buffer;
    for(int dy=0;dy<h;++dy){int py=y+dy;if(py<0||py>=HEIGHT)continue;int sy=dy*image->height/h;for(int dx=0;dx<w;++dx){int px=x+dx;if(px<0||px>=WIDTH)continue;
        int cx=dx<radius?radius-dx-1:dx>=w-radius?dx-(w-radius):0,cy=dy<radius?radius-dy-1:dy>=h-radius?dy-(h-radius):0;if(cx&&cy&&cx*cx+cy*cy>radius*radius)continue;
        int sx=dx*image->width/w;const unsigned char *s=image->pixels+((size_t)sy*image->width+sx)*4;unsigned a=s[3];if(!a)continue;uint32_t *d=&target[(size_t)py*WIDTH+px];unsigned dr=*d&255u,dg=(*d>>8)&255u,db=(*d>>16)&255u;
        *d=0xff000000u|(((s[2]*a+db*(255-a))/255)<<16)|(((s[1]*a+dg*(255-a))/255)<<8)|((s[0]*a+dr*(255-a))/255);}}
}
static void draw_focus(void *b,int x,int y,int w,int h){
    draw_round_outline(b,x-8,y-8,w+16,h+16,24,4,0xff704bddu);
    draw_round_outline(b,x-4,y-4,w+8,h+8,20,3,INK);
}
static void draw_card(void *b,int x,int y,int w,int h,int seed,int focused,const char *title){
    static const uint32_t colors[]={0xff49325fu,0xff41305au,0xff34284du,0xff3d2d55u,0xff2c2342u};int poster_h=h-58;
    draw_round_rect(b,x+8,y+14,w,poster_h,20,0xff050405u);draw_round_rect(b,x,y,w,poster_h,18,colors[seed%5]);
    if(seed<15&&live_cards[seed].poster.pixels)draw_image_rounded(b,&live_cards[seed].poster,x,y,w,poster_h,18);
    char shown[21]={0};strncpy(shown,title,sizeof(shown)-1);int scale=strlen(shown)>12?2:3;
    draw_text(b,x,y+poster_h+20,shown,scale,focused?INK:INK_2);if(focused)draw_focus(b,x,y,w,poster_h);
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
    if(wordmark.pixels)draw_image(b,&wordmark,1640,42,220,42);else draw_text(b,1600,54,"STREMIO PS FOUR",3,INK_2);
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
    for(int i=0;i<3;++i){draw_round_rect(b,190+i*330,228,300,66,33,i==0?0xff563aa7u:SURFACE);draw_text(b,224+i*330,248,filters[i],3,INK);}
    const char *titles[]={"ACTION","DRAMA","COMEDY","THRILLER","FAMILY"};
    int first=s->discover_series?5:0;
    for(int i=0;i<5;++i){int index=first+i;draw_card(b,190+i*290,370,250,360,index,!s->rail_focus&&s->column==i,catalog_status==2&&index<catalog_count?live_cards[index].name:titles[i]);}
    draw_image(b,&square_icon,190,800,38,38);draw_text(b,244,810,"SWITCH MOVIES SERIES",3,INK_3);
}
static void draw_library(void *b,const AppState *s){
    draw_text(b,190,154,"Library",6,INK_2);int count=0;for(int i=0;i<15;++i)if(live_cards[i].saved&&live_cards[i].name[0])++count;
    char summary[64];snprintf(summary,sizeof(summary),"%d saved title%s",count,count==1?"":"s");draw_text(b,190,210,summary,3,INK_3);
    const char *filters[]={"All","Movies","Series"};for(int i=0;i<3;++i){draw_round_rect(b,190+i*210,252,190,54,27,i==0?0xff563aa7u:SURFACE);draw_text(b,225+i*210,268,filters[i],3,INK);}
    int shown=0;for(int i=0;i<5;++i){int index=saved_index(i);if(index<0)break;draw_card(b,190+i*290,350,250,360,index,!s->rail_focus&&s->column==i,live_cards[index].name);++shown;}
    if(!shown){draw_round_rect(b,190,350,1100,190,30,SURFACE);draw_text(b,250,405,"Your library is empty",4,INK);draw_text(b,250,462,"Save any title with Square",3,INK_3);}
    draw_round_rect(b,190,805,500,64,32,SURFACE_HI);draw_image(b,&check_icon,212,818,36,36);draw_text(b,266,824,"Saved locally on PS4",3,INK_2);
}
static void draw_addons(void *b,const AppState *s){
    draw_text(b,190,154,"Addons",6,INK_2);draw_text(b,190,210,"2 installed addons",3,INK_3);
    const char *names[]={"CINEMETA","PUBLIC DOMAIN","LOCAL FILES","COMMUNITY"};
    const char *descriptions[]={"Movies and series metadata","Classic films with legal streams","Play media from USB storage","Install addons from a manifest URL"};
    for(int i=0;i<4;++i){int col=i%2,row=i/2,x=190+col*690,y=270+row*280;draw_round_rect(b,x,y,650,240,30,!s->rail_focus&&s->row==i?SURFACE_HI:SURFACE);
        int enabled=i==0||(i==1&&public_domain_enabled);draw_round_rect(b,x+26,y+26,86,86,22,enabled?0xff563aa7u:0xff584667u);draw_text(b,x+145,y+32,names[i],4,INK);
        const char *status=i==0?"INSTALLED":i==1?(public_domain_enabled?(public_domain_online?"ONLINE":"ENABLED - CROSS TO CHECK"):"DISABLED"):"AVAILABLE";
        draw_text(b,x+145,y+74,status,3,enabled?ACCENT_SOFT:INK_3);draw_text(b,x+28,y+150,descriptions[i],3,INK_2);
        draw_round_rect(b,x+28,y+194,110,30,15,0xff3b343du);draw_text(b,x+48,y+201,i<2?"Movies":"Source",2,INK_2);
        if(i>=2)draw_image(b,&chevron_icon,x+590,y+100,30,30);if(!s->rail_focus&&s->row==i)draw_focus(b,x,y,650,240);}
}
static void draw_wrapped(void *b,int x,int y,const char *text,int columns,int lines);
static void draw_settings(void *b,const AppState *s){
    draw_text(b,190,154,"Settings",6,INK_2);
    const char *names[]={"BUILTIN TORRENTS","AUTO SUBTITLES","AUTOPLAY NEXT","UI SOUNDS"};
    for(int i=0;i<4;++i){int y=220+i*112;draw_round_rect(b,190,y,950,88,24,!s->rail_focus&&s->row==i?SURFACE_HI:SURFACE);
        draw_text(b,226,y+32,names[i],4,!s->rail_focus&&s->row==i?INK:INK_2);draw_round_rect(b,1000,y+24,104,42,21,s->settings[i]?0xff563aa7u:0xff514951u);
        draw_text(b,1025,y+36,s->settings[i]?"ON":"OFF",3,INK);if(!s->rail_focus&&s->row==i)draw_focus(b,190,y,950,88);}
    int y=668;draw_round_rect(b,190,y,950,88,24,!s->rail_focus&&s->row==4?SURFACE_HI:SURFACE);draw_text(b,226,y+32,"STREMIO ACCOUNT",4,INK_2);
    draw_text(b,890,y+34,auth_key[0]?"LINKED":"LINK DEVICE",3,auth_key[0]?ACCENT_SOFT:INK_3);if(!s->rail_focus&&s->row==4)draw_focus(b,190,y,950,88);
    const char *help_title=s->row==0?"BUILTIN TORRENTS":s->row==1?"AUTO SUBTITLES":s->row==2?"AUTOPLAY NEXT":s->row==3?"UI SOUNDS":"STREMIO ACCOUNT";
    const char *help=s->row==0?"USE STREAM ADDONS DIRECTLY ON THE CONSOLE":s->row==1?"LOAD AVAILABLE SUBTITLES AUTOMATICALLY":s->row==2?"CONTINUE TO THE NEXT EPISODE":s->row==3?"PLAY SOFT SOUNDS WHILE NAVIGATING":"LINK YOUR ACCOUNT WITH A SECURE SHORT CODE";
    draw_round_outline(b,1200,220,620,536,32,2,0xff514651u);draw_text(b,1245,280,help_title,4,INK);draw_text(b,1245,350,s->row<4?(s->settings[s->row]?"ON":"OFF"):(auth_key[0]?"LINKED":"NOT LINKED"),4,ACCENT_SOFT);
    draw_wrapped(b,1245,430,help,31,4);draw_round_rect(b,1245,650,230,58,29,INK);draw_image(b,&cross_icon,1260,660,38,38);draw_text(b,1312,670,s->row==4?"OPEN":"SWITCH",3,GROUND);
}
static void draw_account(void *b,const AppState *s){
    (void)s;draw_text(b,190,140,"Stremio account",6,INK_2);draw_text(b,190,205,"Link this PS4 to your Stremio account",3,INK_3);
    draw_round_rect(b,190,270,1420,430,34,SURFACE);draw_round_rect(b,230,310,180,180,36,0xff3d2a73u);draw_image(b,&logo,280,360,80,80);
    if(account_status==4){draw_round_rect(b,460,320,270,48,24,0xff315d3fu);draw_text(b,500,334,"ACCOUNT LINKED",3,0xff8cffaau);
        draw_text(b,460,410,"WELCOME BACK",6,INK);draw_text(b,460,500,"YOUR SECURE ACCOUNT KEY IS SAVED LOCALLY",3,INK_2);draw_text(b,460,550,"LIBRARY AND ADDON SYNC IS BEING PREPARED",3,INK_3);}
    else {draw_text(b,460,330,account_status==1?"CREATING CODE":account_status==3?"CHECKING APPROVAL":account_status==5?"LINK SERVICE UNAVAILABLE":"ENTER THIS CODE",4,INK_2);
        if(link_code[0]){draw_round_rect(b,460,390,420,116,28,0xff35264fu);draw_text(b,510,418,link_code,10,ACCENT_SOFT);draw_text(b,460,560,"OPEN LINK STREMIO COM ON YOUR PHONE",3,INK);}}
    draw_round_rect(b,230,760,350,62,31,INK);draw_image(b,&cross_icon,250,772,38,38);draw_text(b,305,782,account_status==4?"LINKED":"CHECK APPROVAL",3,GROUND);
    draw_round_rect(b,610,760,290,62,31,SURFACE_HI);draw_image(b,&square_icon,630,772,38,38);draw_text(b,685,782,account_status==4?"UNLINK":"NEW CODE",3,INK);
    draw_round_rect(b,930,760,220,62,31,SURFACE_HI);draw_image(b,&circle_icon,950,772,38,38);draw_text(b,1005,782,"BACK",3,INK);
}
static void draw_wrapped(void *b,int x,int y,const char *text,int columns,int lines){
    char line[64];const char *p=text;
    for(int row=0;row<lines&&*p;++row){int n=0,last_space=-1;
        while(p[n]&&n<columns){if(p[n]==' ')last_space=n;++n;}
        if(p[n]&&last_space>0)n=last_space;memcpy(line,p,(size_t)n);line[n]=0;draw_text(b,x,y+row*42,line,3,INK_2);
        p+=n;while(*p==' ')++p;}
}
static void draw_detail(void *b,const AppState *s){
    int index=s->detail_index;if(index<0||index>=15||!live_cards[index].name[0])return;CatalogCard *card=&live_cards[index];
    draw_text(b,190,140,"TITLE DETAILS",5,ACCENT_SOFT);
    draw_round_rect(b,190,220,360,540,24,SURFACE);if(card->poster.pixels)draw_image_rounded(b,&card->poster,190,220,360,540,24);
    char title[41]={0};strncpy(title,card->name,sizeof(title)-1);draw_text(b,610,230,title,strlen(title)>22?3:5,INK);
    draw_round_rect(b,610,305,150,48,24,0xff432d6fu);draw_text(b,642,318,card->type,3,ACCENT_SOFT);draw_text(b,800,318,card->year,3,INK_2);
    draw_text(b,610,375,card->runtime,3,INK_2);draw_round_rect(b,770,364,104,40,10,0xff18c5f5u);draw_text(b,790,375,card->rating,3,0xff0c0b11u);draw_text(b,915,375,card->genres,2,INK_3);
    draw_wrapped(b,610,420,card->description[0]?card->description:"DESCRIPTION NOT AVAILABLE",43,7);
    draw_round_rect(b,1300,220,430,380,30,SURFACE);draw_text(b,1340,260,"About",4,INK);draw_text(b,1340,330,"Type",2,INK_3);draw_text(b,1480,327,card->type,3,INK_2);
    draw_text(b,1340,390,"Released",2,INK_3);draw_text(b,1480,387,card->year,3,INK_2);draw_text(b,1340,450,"Runtime",2,INK_3);draw_text(b,1480,447,card->runtime,3,INK_2);
    draw_text(b,1340,510,"Rating",2,INK_3);draw_text(b,1480,507,card->rating,3,0xff18c5f5u);
    draw_round_rect(b,610,790,440,80,40,0xff563aa7u);draw_image(b,&play_icon,638,808,44,44);draw_text(b,706,818,"FIND STREAMS",4,INK);
    draw_image(b,&cross_icon,610,902,38,38);draw_text(b,660,912,"STREAMS",3,INK_2);
    draw_image(b,&square_icon,830,902,38,38);draw_text(b,880,912,card->saved?"REMOVE":"SAVE",3,INK_2);
    draw_image(b,&circle_icon,1090,902,38,38);draw_text(b,1140,912,"BACK",3,INK_2);
}
static void draw_streams(void *b,const AppState *s){
    CatalogCard *card=&live_cards[s->detail_index];draw_text(b,190,140,"STREAM SOURCES",6,INK);
    draw_text(b,190,220,card->name,4,ACCENT_SOFT);
    if(stream_count){
        for(int i=0;i<stream_count&&i<5;++i){int y=290+i*120,focused=i==s->stream_cursor;StreamSource *source=&stream_sources[i];
            draw_round_rect(b,190,y,1420,96,26,focused?SURFACE_HI:SURFACE);draw_round_rect(b,220,y+20,120,56,18,0xff563aa7u);
            draw_text(b,245,y+36,source->name[0]?source->name:"SOURCE",3,INK);draw_text(b,380,y+24,"Public Domain Movies",4,INK);
            draw_text(b,380,y+62,source->title[0]?source->title:"Torrent source",3,INK_3);draw_text(b,1360,y+38,"READY",3,ACCENT_SOFT);
            if(focused)draw_focus(b,190,y,1420,96);}
        draw_image(b,&cross_icon,190,920,38,38);draw_text(b,244,930,stream_status==6?"PLAYER IN DEVELOPMENT":"SELECT SOURCE",3,INK_2);
    }else {draw_round_rect(b,190,300,1420,280,32,SURFACE);
        if(stream_status==3){draw_round_rect(b,230,340,84,84,42,0xff315d3fu);draw_image(b,&check_icon,252,362,40,40);draw_text(b,350,360,"No compatible streams found",5,INK);draw_text(b,350,435,"Public Domain was checked for this title",3,INK_2);}
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
static void draw_screen(void *b,const AppState *s){
    draw_page_backdrop(b);draw_rail(b,s);draw_topbar(b,s);
    if(s->account)draw_account(b,s);
    else if(s->streams)draw_streams(b,s);
    else if(s->detail)draw_detail(b,s);
    else if(s->search)draw_search(b,s);
    else switch(s->page){case 0:draw_board(b,s);break;case 1:draw_discover(b,s);break;case 2:draw_library(b,s);break;case 3:draw_addons(b,s);break;default:draw_settings(b,s);}
    draw_footer(b,s);
}
static int open_pad(void){
    int result=scePadInit();if(result!=0)return result;OrbisUserServiceInitializeParams p={0};p.priority=ORBIS_KERNEL_PRIO_FIFO_LOWEST;
    result=sceUserServiceInitialize(&p);if(result!=0&&result!=(int)0x80960003)return result;int user=-1;
    result=sceUserServiceGetInitialUser(&user);if(result!=0)return result;return scePadOpen(user,0,0,0);
}
static void reset_cursor(AppState *s){s->row=0;s->column=0;}
static int route_input(AppState *s,uint32_t pressed){
    if(s->account){
        if(pressed&ORBIS_PAD_BUTTON_CIRCLE)s->account=0;
        if((pressed&ORBIS_PAD_BUTTON_CROSS)&&account_status!=4)poll_device_link();
        if(pressed&ORBIS_PAD_BUTTON_SQUARE){if(account_status==4){auth_key[0]=link_code[0]=0;account_status=0;save_account();}else create_device_link();}return pressed!=0;
    }
    if(s->streams){
        if(pressed&ORBIS_PAD_BUTTON_CIRCLE)s->streams=0;
        if(stream_count){if((pressed&ORBIS_PAD_BUTTON_UP)&&s->stream_cursor>0)--s->stream_cursor;if((pressed&ORBIS_PAD_BUTTON_DOWN)&&s->stream_cursor<stream_count-1)++s->stream_cursor;
            if(pressed&ORBIS_PAD_BUTTON_CROSS)stream_status=6;}
        else if(pressed&ORBIS_PAD_BUTTON_CROSS){s->streams=0;s->detail=0;s->page=3;s->rail_focus=0;reset_cursor(s);}return pressed!=0;
    }
    if(s->detail){
        if(pressed&ORBIS_PAD_BUTTON_CIRCLE)s->detail=0;
        if(pressed&ORBIS_PAD_BUTTON_CROSS){s->stream_cursor=0;load_stream_sources(&live_cards[s->detail_index]);s->streams=1;}
        if(pressed&ORBIS_PAD_BUTTON_SQUARE){live_cards[s->detail_index].saved^=1;save_state(s);}
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
    if((pressed&ORBIS_PAD_BUTTON_OPTIONS)&&s->page==0){refresh_catalogs();return 1;}
    if((pressed&ORBIS_PAD_BUTTON_SQUARE)&&s->page==1){s->discover_series^=1;s->column=0;return 1;}
    if(pressed&ORBIS_PAD_BUTTON_L1){s->page=(s->page+PAGE_COUNT-1)%PAGE_COUNT;s->rail_focus=0;reset_cursor(s);return 1;}
    if(pressed&ORBIS_PAD_BUTTON_R1){s->page=(s->page+1)%PAGE_COUNT;s->rail_focus=0;reset_cursor(s);return 1;}
    if(s->rail_focus){if((pressed&ORBIS_PAD_BUTTON_UP)&&s->row>0)--s->row;if((pressed&ORBIS_PAD_BUTTON_DOWN)&&s->row<PAGE_COUNT-1)++s->row;
        if(pressed&(ORBIS_PAD_BUTTON_RIGHT|ORBIS_PAD_BUTTON_CROSS)){s->page=s->row;s->rail_focus=0;reset_cursor(s);}if(pressed&ORBIS_PAD_BUTTON_CIRCLE){s->rail_focus=0;reset_cursor(s);}return pressed!=0;}
    if(s->page==3){
        if(pressed&ORBIS_PAD_BUTTON_LEFT){if((s->row%2)==1)--s->row;else{s->rail_focus=1;s->row=s->page;}}
        if((pressed&ORBIS_PAD_BUTTON_RIGHT)&&(s->row%2)==0)++s->row;
        if((pressed&ORBIS_PAD_BUTTON_UP)&&s->row>=2)s->row-=2;if((pressed&ORBIS_PAD_BUTTON_DOWN)&&s->row<2)s->row+=2;
        if((pressed&ORBIS_PAD_BUTTON_CROSS)&&s->row==1){public_domain_enabled^=1;save_account();validate_public_domain();}
        if(pressed&ORBIS_PAD_BUTTON_CIRCLE){s->page=0;reset_cursor(s);}return pressed!=0;
    }
    if(pressed&ORBIS_PAD_BUTTON_LEFT){if(s->column>0&&s->page<3)--s->column;else{s->rail_focus=1;s->row=s->page;}}
    if((pressed&ORBIS_PAD_BUTTON_RIGHT)&&s->page<3&&s->column<4)++s->column;
    if(pressed&ORBIS_PAD_BUTTON_UP){if((s->page==0||s->page>=3)&&s->row>0)--s->row;}
    if(pressed&ORBIS_PAD_BUTTON_DOWN){if(s->page==0&&s->row<1){++s->row;if(s->column>3)s->column=3;}else if(s->page==3&&s->row<3)++s->row;else if(s->page==4&&s->row<4)++s->row;}
    if((pressed&ORBIS_PAD_BUTTON_CROSS)&&s->page==4){if(s->row==4){s->account=1;if(!auth_key[0]&&!link_code[0])create_device_link();}else{s->settings[s->row]^=1;save_state(s);}}
    else if((pressed&ORBIS_PAD_BUTTON_CROSS)&&catalog_status==2&&s->page<3){
        int index=s->page==0?s->row*5+s->column:s->page==2?saved_index(s->column):s->column+(s->page==1&&s->discover_series?5:0);
        if(index>=0&&index<15&&live_cards[index].name[0]){s->detail_index=index;load_metadata(&live_cards[index]);s->detail=1;}
    }
    if(pressed&ORBIS_PAD_BUTTON_CIRCLE){if(s->page!=0){s->page=0;reset_cursor(s);}else return -1;}return pressed!=0;
}
int main(void){
    const size_t frame_size=(size_t)WIDTH*HEIGHT*4,alignment=0x200000;
    const size_t allocation_size=((frame_size*BUFFER_COUNT+alignment-1)/alignment)*alignment;
    int video=sceVideoOutOpen(ORBIS_VIDEO_USER_MAIN,ORBIS_VIDEO_OUT_BUS_MAIN,0,0);if(video<0)park_error("sceVideoOutOpen",video);
    off_t direct_offset=0;int result=sceKernelAllocateDirectMemory(0,sceKernelGetDirectMemorySize(),allocation_size,alignment,3,&direct_offset);
    if(result<0)park_error("sceKernelAllocateDirectMemory",result);void *video_memory=0;
    result=sceKernelMapDirectMemory(&video_memory,allocation_size,0x33,0,direct_offset,alignment);if(result<0)park_error("sceKernelMapDirectMemory",result);
    void *buffers[BUFFER_COUNT]={video_memory,(uint8_t*)video_memory+frame_size};OrbisVideoOutBufferAttribute attribute;
    sceVideoOutSetBufferAttribute(&attribute,0x80000000,1,0,WIDTH,HEIGHT,WIDTH);
    result=sceVideoOutRegisterBuffers(video,0,buffers,BUFFER_COUNT,&attribute);if(result<0)park_error("sceVideoOutRegisterBuffers",result);
    load_assets();AppState state={0};for(int i=0;i<4;++i)state.settings[i]=1;load_state(&state);load_account();catalog_status=1;
    draw_screen(buffers[0],&state);draw_screen(buffers[1],&state);sceVideoOutSetFlipRate(video,0);
    result=sceVideoOutSubmitFlip(video,0,ORBIS_VIDEO_OUT_FLIP_VSYNC,1);if(result<0)park_error("sceVideoOutSubmitFlip",result);
    sceSystemServiceHideSplashScreen();
    load_live_catalog();validate_public_domain();draw_screen(buffers[1],&state);sceVideoOutSubmitFlip(video,1,ORBIS_VIDEO_OUT_FLIP_VSYNC,2);
    const int pad=open_pad();if(pad<0)park_error("scePadOpen",pad);
    int active_buffer=1;int64_t flip_id=3;uint32_t previous_buttons=0;
    for(;;){OrbisPadData input={0};if(scePadReadState(pad,&input)==0){uint32_t pressed=input.buttons&~previous_buttons;previous_buttons=input.buttons;
        int changed=route_input(&state,pressed);if(changed<0){scePadClose(pad);sceSystemServiceLoadExec("exit",0);}
        if(changed>0){active_buffer^=1;draw_screen(buffers[active_buffer],&state);sceVideoOutSubmitFlip(video,active_buffer,ORBIS_VIDEO_OUT_FLIP_VSYNC,flip_id++);}}
        sceKernelUsleep(16000);}
}
