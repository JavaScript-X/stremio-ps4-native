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
#include <string.h>
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb_image.h"

uint64_t sceLibcHeapSize = 24 * 1024 * 1024;
enum { WIDTH = 1920, HEIGHT = 1080, BUFFER_COUNT = 2, PAGE_COUNT = 5 };
static const uint32_t GROUND=0xff110b0cu, SURFACE=0xff292328u, SURFACE_HI=0xff3a333du;
static const uint32_t ACCENT=0xfff55b7bu, ACCENT_SOFT=0xffff8ba5u;
static const uint32_t INK=0xfff7f7f7u, INK_2=0xffc8c8c8u, INK_3=0xff8f8f8fu;
typedef struct Image { unsigned char *pixels; int width, height; } Image;
typedef struct AppState {
    int page, rail_focus, row, column, search, detail, detail_index;
    int keyboard, search_cursor, result_focus; char query[24]; int settings[4];
} AppState;
static const char *PAGE_NAMES[PAGE_COUNT]={"BOARD","DISCOVER","LIBRARY","ADDONS","SETTINGS"};
static const char *ICON_NAMES[PAGE_COUNT]={"nav_board","nav_discover","nav_library","nav_addons","nav_settings"};
static Image logo, wordmark, search_icon, cross_icon, circle_icon, triangle_icon, square_icon;
static Image dpad_icon, l1_icon, r1_icon, play_icon, nav_line[PAGE_COUNT], nav_solid[PAGE_COUNT];
typedef struct CatalogCard {
    char id[24], name[40], type[12], year[16], description[640], poster_url[384];
    Image poster;
} CatalogCard;
static CatalogCard live_cards[15];
static int catalog_count;
static int search_count, search_status;
static int catalog_status;
static int poster_count;
static int net_pool, ssl_context, http_context;

static int network_init(void){
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

static int parse_catalog(char *json,int first,int maximum){
    const char *p=json;int count=0;while(count<maximum&&(p=strstr(p,"\"imdb_id\":\""))){
        CatalogCard *card=&live_cards[first+count];
        json_value(p,"\"imdb_id\":\"",card->id,sizeof(card->id),0);
        const char *name=strstr(p,"\"name\":\"");if(!name)break;
        const char *after=json_value(name,"\"name\":\"",card->name,sizeof(card->name),1);if(!after)break;
        const char *type=strstr(after,"\"type\":\"");if(type)json_value(type,"\"type\":\"",card->type,sizeof(card->type),1);
        const char *description=strstr(after,"\"description\":\"");if(description)json_value(description,"\"description\":\"",card->description,sizeof(card->description),1);
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
    catalog_status=2;
}

static void run_search(const char *query){
    search_status=1;search_count=0;for(int i=10;i<15;++i){if(live_cards[i].poster.pixels)stbi_image_free(live_cards[i].poster.pixels);memset(&live_cards[i],0,sizeof(live_cards[i]));}
    char url[320]="https://v3-cinemeta.strem.io/catalog/movie/top/search=";size_t used=strlen(url);
    for(const char *p=query;*p&&used+4<sizeof(url);++p){if(*p==' '){strcpy(url+used,"%20");used+=3;}else url[used++]=*p;}strcpy(url+used,".json");
    size_t size=0;unsigned char *json=http_get(url,1024*1024,&size);if(!json){search_status=3;return;}
    search_count=parse_catalog((char*)json,10,5);free(json);if(!search_count){search_status=3;return;}
    for(int i=0;i<search_count;++i){CatalogCard *card=&live_cards[10+i];size_t image_size=0;
        if(strlen(card->poster_url)+4<sizeof(card->poster_url))strcat(card->poster_url,".jpg");
        unsigned char *encoded=http_get(card->poster_url,3*1024*1024,&image_size);if(encoded){int channels=0;
            card->poster.pixels=stbi_load_from_memory(encoded,(int)image_size,&card->poster.width,&card->poster.height,&channels,4);free(encoded);}}
    search_status=2;
}

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
    for(;*text;++text,x+=6*scale){const uint8_t *bits=*text>='A'&&*text<='Z'?FONT[*text-'A'+1]:*text>='0'&&*text<='9'?DIGITS[*text-'0']:FONT[0];
        for(int c=0;c<5;++c)for(int r=0;r<7;++r)if(bits[c]&(1u<<r))
            draw_rect(buffer,x+c*scale,y+r*scale,scale,scale,color);}
}
static Image load_image(const char *name){
    Image image={0}; char path[160]; int channels=0;
    strcpy(path,"/app0/assets/");strcat(path,name);
    image.pixels=stbi_load(path,&image.width,&image.height,&channels,4);
    if(!image.pixels){strcpy(path,"assets/");strcat(path,name);image.pixels=stbi_load(path,&image.width,&image.height,&channels,4);}
    return image;
}
static void load_assets(void){
    logo=load_image("logo.png");wordmark=load_image("name.png");search_icon=load_image("search.png");
    cross_icon=load_image("cross.png");circle_icon=load_image("circle.png");triangle_icon=load_image("triangle.png");square_icon=load_image("square.png");
    dpad_icon=load_image("dpad.png");l1_icon=load_image("l1.png");r1_icon=load_image("r1.png");play_icon=load_image("play.png");char name[64];
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
static void draw_focus(void *b,int x,int y,int w,int h){
    draw_rect(b,x-5,y-5,w+10,5,INK);draw_rect(b,x-5,y+h,w+10,5,INK);
    draw_rect(b,x-5,y,5,h,INK);draw_rect(b,x+w,y,5,h,INK);
}
static void draw_card(void *b,int x,int y,int w,int h,int seed,int focused,const char *title){
    static const uint32_t colors[]={0xff7c304fu,0xff735239u,0xff3b626eu,0xff71415eu,0xff4c3978u};
    draw_rect(b,x,y,w,h,colors[seed%5]);
    if(seed<15&&live_cards[seed].poster.pixels)draw_image(b,&live_cards[seed].poster,x,y,w,h-70);
    draw_rect(b,x,y+h-70,w,70,0xff1a151cu);char shown[21]={0};strncpy(shown,title,sizeof(shown)-1);
    int scale=strlen(shown)>12?2:3;draw_text(b,x+14,y+h-46,shown,scale,INK);if(focused)draw_focus(b,x,y,w,h);
}
static void draw_rail(void *b,const AppState *s){
    draw_rect(b,0,0,140,HEIGHT,0xff171117u);if(logo.pixels)draw_image(b,&logo,42,30,58,58);else draw_text(b,36,42,"S",7,ACCENT_SOFT);
    for(int i=0;i<PAGE_COUNT;++i){int y=238+i*138,active=s->page==i,cursor=s->rail_focus&&s->row==i;
        if(active||cursor)draw_rect(b,20,y,100,104,cursor?ACCENT:SURFACE_HI);
        draw_image(b,(active||cursor)?&nav_solid[i]:&nav_line[i],46,y+14,48,48);
        draw_text(b,28,y+78,PAGE_NAMES[i],2,(active||cursor)?INK:INK_3);if(cursor)draw_focus(b,20,y,100,104);}
}
static void draw_topbar(void *b,const AppState *s){
    int x=190,y=34,w=620,h=60;draw_rect(b,x,y,w,h,s->search?SURFACE_HI:SURFACE);
    draw_image(b,&search_icon,x+22,y+16,28,28);draw_text(b,x+72,y+20,s->search?(s->query[0]?s->query:"SEARCH MODE"):"SEARCH MOVIES AND SERIES",3,s->search?INK:INK_3);
    draw_image(b,&triangle_icon,x+w-48,y+14,32,32);if(s->search)draw_focus(b,x,y,w,h);
    if(wordmark.pixels)draw_image(b,&wordmark,1640,42,220,42);else draw_text(b,1600,54,"STREMIO PS FOUR",3,INK_2);
}
static void draw_board(void *b,const AppState *s){
    draw_text(b,190,130,"POPULAR MOVIES",5,INK);draw_text(b,190,178,"LIVE CINEMETA CATALOGS",3,INK_2);
    const char *titles[]={"FEATURED","TRENDING","MOVIES","SERIES","RECENT"};
    for(int i=0;i<5;++i)draw_card(b,190+i*290,218,250,300,i,!s->rail_focus&&s->row==0&&s->column==i,catalog_status==2?live_cards[i].name:titles[i]);
    const char *status=catalog_status==1?"CONNECTING":catalog_status==2?(poster_count==catalog_count?"LIVE MOVIES AND SERIES":poster_count?"PARTIAL POSTERS":"LIVE CINEMETA"):
        catalog_status==3?"NETWORK INIT FAILED":catalog_status==4?"CATALOG REQUEST FAILED":catalog_status==5?"CATALOG PARSE FAILED":"OFFLINE CONTENT";
    draw_text(b,1110,150,status,3,catalog_status==2?ACCENT_SOFT:INK_3);
    draw_text(b,190,568,"POPULAR SERIES",5,INK);
    for(int i=0;i<5;++i){int index=5+i;const char *label=index<catalog_count?live_cards[index].name:"SERIES";
        draw_card(b,190+i*290,628,250,300,index,!s->rail_focus&&s->row==1&&s->column==i,label);}
}
static void draw_discover(void *b,const AppState *s){
    draw_text(b,190,154,"DISCOVER",7,INK);const char *filters[]={"MOVIES","POPULAR","ALL GENRES"};
    for(int i=0;i<3;++i){draw_rect(b,190+i*330,238,300,66,i==0?ACCENT:SURFACE);draw_text(b,214+i*330,258,filters[i],3,INK);}
    const char *titles[]={"ACTION","DRAMA","COMEDY","THRILLER","FAMILY"};
    for(int i=0;i<5;++i)draw_card(b,190+i*290,370,250,360,i,!s->rail_focus&&s->column==i,catalog_status==2?live_cards[i].name:titles[i]);
    draw_text(b,190,800,"CHANGE FILTERS WITH CROSS",3,INK_3);
}
static void draw_library(void *b,const AppState *s){
    draw_text(b,190,154,"LIBRARY",7,INK);draw_text(b,190,232,"YOUR SAVED MOVIES AND SERIES",3,INK_2);
    for(int i=0;i<5;++i)draw_card(b,190+i*290,330,250,360,i+1,!s->rail_focus&&s->column==i,"SAVED");
    draw_rect(b,190,780,540,82,ACCENT);draw_text(b,230,808,"SIGN IN TO SYNC LIBRARY",4,INK);
}
static void draw_addons(void *b,const AppState *s){
    draw_text(b,190,154,"ADDONS",7,INK);draw_text(b,190,232,"MANAGE YOUR STREMIO SOURCES",3,INK_2);
    const char *names[]={"CINEMETA","PUBLIC DOMAIN","LOCAL FILES","COMMUNITY"};
    for(int i=0;i<4;++i){int y=320+i*145;draw_rect(b,190,y,1340,110,!s->rail_focus&&s->row==i?SURFACE_HI:SURFACE);
        draw_rect(b,214,y+20,70,70,i==0?ACCENT:0xff584667u);draw_text(b,320,y+26,names[i],5,INK);
        draw_text(b,320,y+76,i<2?"INSTALLED":"AVAILABLE",3,i<2?ACCENT_SOFT:INK_3);if(!s->rail_focus&&s->row==i)draw_focus(b,190,y,1340,110);}
}
static void draw_settings(void *b,const AppState *s){
    draw_text(b,190,154,"SETTINGS",7,INK);draw_text(b,190,232,"PLAYBACK AND APPLICATION",3,INK_2);
    const char *names[]={"BUILTIN TORRENTS","AUTO SUBTITLES","AUTOPLAY NEXT","UI SOUNDS"};
    for(int i=0;i<4;++i){int y=320+i*130;draw_rect(b,190,y,1200,96,!s->rail_focus&&s->row==i?SURFACE_HI:SURFACE);
        draw_text(b,230,y+32,names[i],4,INK);draw_rect(b,1190,y+20,160,56,s->settings[i]?ACCENT:0xff514951u);
        draw_text(b,1230,y+38,s->settings[i]?"ON":"OFF",3,INK);if(!s->rail_focus&&s->row==i)draw_focus(b,190,y,1200,96);}
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
    draw_rect(b,190,220,360,540,SURFACE);if(card->poster.pixels)draw_image(b,&card->poster,190,220,360,540);
    char title[41]={0};strncpy(title,card->name,sizeof(title)-1);draw_text(b,610,230,title,strlen(title)>22?3:5,INK);
    draw_text(b,610,310,card->type,4,ACCENT_SOFT);draw_text(b,830,310,card->year,4,INK_2);
    draw_wrapped(b,610,390,card->description[0]?card->description:"DESCRIPTION NOT AVAILABLE",52,8);
    draw_rect(b,610,790,440,80,ACCENT);draw_image(b,&play_icon,638,808,44,44);draw_text(b,706,818,"FIND STREAMS",4,INK);
    draw_image(b,&cross_icon,610,902,38,38);draw_text(b,660,912,"STREAMS",3,INK_2);
    draw_image(b,&circle_icon,830,902,38,38);draw_text(b,880,912,"BACK",3,INK_2);
}
static int draw_prompt(void *b,const Image *icon,const char *label,int x,int y){
    draw_image(b,icon,x,y,38,38);draw_text(b,x+50,y+10,label,3,INK_2);return x+50+(int)strlen(label)*18+28;
}
static void draw_search(void *b,const AppState *s){
    draw_text(b,190,120,"SEARCH",6,INK);
    const char *status=search_status==1?"LOADING":search_status==2?"LIVE RESULTS":search_status==3?"NO RESULTS":"TYPE A TITLE";
    draw_text(b,1210,142,status,3,search_status==2?ACCENT_SOFT:INK_3);
    if(search_count){for(int i=0;i<search_count;++i)draw_card(b,190+i*290,190,250,300,10+i,s->result_focus&&s->search_cursor==i,live_cards[10+i].name);}
    else {draw_rect(b,190,190,1450,300,SURFACE);draw_text(b,250,310,"ENTER A MOVIE NAME THEN CHOOSE GO",4,INK_2);}
    static const char *special[]={"SPACE","DELETE","GO"};
    for(int i=0;i<29;++i){int col=i%10,row=i/10,x=190+col*145,y=550+row*100,w=125;
        int focused=!s->result_focus&&s->keyboard==i;draw_rect(b,x,y,w,72,focused?ACCENT:SURFACE_HI);
        char letter[2]={(char)('A'+i),0};const char *label=i<26?letter:special[i-26];int scale=i<26?4:2;
        draw_text(b,x+(i<26?48:14),y+24,label,scale,INK);if(focused)draw_focus(b,x,y,w,72);}
    draw_image(b,&cross_icon,190,890,38,38);draw_text(b,240,900,"TYPE OR OPEN",3,INK_2);
    draw_image(b,&square_icon,520,890,38,38);draw_text(b,570,900,"DELETE",3,INK_2);
    draw_image(b,&circle_icon,800,890,38,38);draw_text(b,850,900,"CLOSE",3,INK_2);
}
static void draw_footer(void *b,const AppState *s){
    draw_rect(b,140,1008,1780,72,0xff171117u);int x=190;
    x=draw_prompt(b,&dpad_icon,"NAVIGATE",x,1025);x=draw_prompt(b,&cross_icon,s->rail_focus?"OPEN":"SELECT",x,1025);
    x=draw_prompt(b,&triangle_icon,"SEARCH",x,1025);x=draw_prompt(b,&l1_icon,"PAGES",x,1025);
    x=draw_prompt(b,&r1_icon,"PAGES",x,1025);draw_prompt(b,&circle_icon,"BACK",x,1025);
}
static void draw_screen(void *b,const AppState *s){
    fill_buffer(b,GROUND);draw_rail(b,s);draw_topbar(b,s);
    if(s->detail)draw_detail(b,s);
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
    if(s->detail){if(pressed&ORBIS_PAD_BUTTON_CIRCLE)s->detail=0;return pressed!=0;}
    if(s->search){
        size_t length=strlen(s->query);
        if(pressed&ORBIS_PAD_BUTTON_CIRCLE){s->search=0;s->result_focus=0;return 1;}
        if((pressed&ORBIS_PAD_BUTTON_SQUARE)&&length){s->query[length-1]=0;return 1;}
        if(s->result_focus){
            if((pressed&ORBIS_PAD_BUTTON_LEFT)&&s->search_cursor>0)--s->search_cursor;
            if((pressed&ORBIS_PAD_BUTTON_RIGHT)&&s->search_cursor<search_count-1)++s->search_cursor;
            if(pressed&ORBIS_PAD_BUTTON_DOWN){s->result_focus=0;s->keyboard=0;}
            if(pressed&ORBIS_PAD_BUTTON_CROSS){s->detail_index=10+s->search_cursor;s->detail=1;}
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
    if(pressed&ORBIS_PAD_BUTTON_L1){s->page=(s->page+PAGE_COUNT-1)%PAGE_COUNT;s->rail_focus=0;reset_cursor(s);return 1;}
    if(pressed&ORBIS_PAD_BUTTON_R1){s->page=(s->page+1)%PAGE_COUNT;s->rail_focus=0;reset_cursor(s);return 1;}
    if(s->rail_focus){if((pressed&ORBIS_PAD_BUTTON_UP)&&s->row>0)--s->row;if((pressed&ORBIS_PAD_BUTTON_DOWN)&&s->row<PAGE_COUNT-1)++s->row;
        if(pressed&(ORBIS_PAD_BUTTON_RIGHT|ORBIS_PAD_BUTTON_CROSS)){s->page=s->row;s->rail_focus=0;reset_cursor(s);}if(pressed&ORBIS_PAD_BUTTON_CIRCLE){s->rail_focus=0;reset_cursor(s);}return pressed!=0;}
    if(pressed&ORBIS_PAD_BUTTON_LEFT){if(s->column>0&&s->page<3)--s->column;else{s->rail_focus=1;s->row=s->page;}}
    if((pressed&ORBIS_PAD_BUTTON_RIGHT)&&s->page<3&&s->column<4)++s->column;
    if(pressed&ORBIS_PAD_BUTTON_UP){if((s->page==0||s->page>=3)&&s->row>0)--s->row;}
    if(pressed&ORBIS_PAD_BUTTON_DOWN){if(s->page==0&&s->row<1){++s->row;if(s->column>3)s->column=3;}else if(s->page>=3&&s->row<3)++s->row;}
    if((pressed&ORBIS_PAD_BUTTON_CROSS)&&s->page==4)s->settings[s->row]^=1;
    else if((pressed&ORBIS_PAD_BUTTON_CROSS)&&catalog_status==2&&s->page<3){
        int index=s->page==0?s->row*5+s->column:s->column;if(index<catalog_count){s->detail_index=index;s->detail=1;}
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
    load_assets();AppState state={0};for(int i=0;i<4;++i)state.settings[i]=1;catalog_status=1;
    draw_screen(buffers[0],&state);draw_screen(buffers[1],&state);sceVideoOutSetFlipRate(video,0);
    result=sceVideoOutSubmitFlip(video,0,ORBIS_VIDEO_OUT_FLIP_VSYNC,1);if(result<0)park_error("sceVideoOutSubmitFlip",result);
    sceSystemServiceHideSplashScreen();
    load_live_catalog();draw_screen(buffers[1],&state);sceVideoOutSubmitFlip(video,1,ORBIS_VIDEO_OUT_FLIP_VSYNC,2);
    const int pad=open_pad();if(pad<0)park_error("scePadOpen",pad);
    int active_buffer=1;int64_t flip_id=3;uint32_t previous_buttons=0;
    for(;;){OrbisPadData input={0};if(scePadReadState(pad,&input)==0){uint32_t pressed=input.buttons&~previous_buttons;previous_buttons=input.buttons;
        int changed=route_input(&state,pressed);if(changed<0){scePadClose(pad);sceSystemServiceLoadExec("exit",0);}
        if(changed>0){active_buffer^=1;draw_screen(buffers[active_buffer],&state);sceVideoOutSubmitFlip(video,active_buffer,ORBIS_VIDEO_OUT_FLIP_VSYNC,flip_id++);}}
        sceKernelUsleep(16000);}
}
