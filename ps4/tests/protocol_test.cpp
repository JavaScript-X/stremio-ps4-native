#include "videoout/protocol.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <cstdio>
int main(){
    assert(!pj_parse("{invalid"));
    PJson d=pj_parse(R"({"result":[{"name":"A \"quoted\" title","removed":true,"_id":"one"},{"_id":"two","state":{"timeOffset":1000},"name":"\u00e9pisode","temp":false}]})");
    auto list=pj_get(d,"result");assert(pj_array(list)&&pj_size(list)==2);
    assert(pj_bool(pj_get(pj_at(list,0),"removed")));
    assert(!pj_bool(pj_get(pj_at(list,1),"removed")));
    assert(!std::strcmp(pj_string(pj_get(pj_at(list,1),"name")),"\xc3\xa9pisode"));
    char small[2];assert(!pj_copy(pj_get(pj_at(list,0),"name"),small,sizeof(small))&&small[0]==0);
    assert(pj_number(pj_get(pj_get(pj_at(list,1),"state"),"timeOffset"),0)==1000);
    pj_free(d);
    char request[1024];assert(pj_request(request,sizeof(request),"token\"\\",0));
    d=pj_parse(request);assert(!std::strcmp(pj_string(pj_get(d,"authKey")),"token\"\\"));pj_free(d);
    d=pj_parse(R"({"id":"test","name":"Test","types":["movie"],"resources":[{"name":"stream","types":["series"],"idPrefixes":["tt"]}]})");
    char url[512];assert(pj_resource(d,"https://example.org/key/manifest.json","stream","series","tt123:1:2",url,sizeof(url)));
    assert(!std::strcmp(url,"https://example.org/key/stream/series/tt123%3A1%3A2.json"));
    assert(!pj_resource(d,"https://example.org/manifest.json","stream","movie","tt123",url,sizeof(url)));
    assert(!pj_resource(d,"https://example.org/manifest.json","stream","series","other",url,sizeof(url)));pj_free(d);
    pj_stream_reset();assert(pj_streams(R"({"streams":[{"url":"https://example.org/media.mp4","name":"Direct"},{"infoHash":"ABCDEF","fileIdx":7},{"ytId":"unsupported"}]})","Test")==2);
    assert(!std::strcmp(pj_stream_field(0,2),"https://example.org/media.mp4"));assert(pj_stream_file(0)==-1);
    assert(!std::strcmp(pj_stream_field(1,3),"abcdef")&&pj_stream_file(1)==7);
    assert(pj_streams("{broken","Test")==-1);assert(!std::strcmp(pj_stream_field(0,0),"Direct"));
    assert(pj_streams(R"({"error":"offline"})","Test")==-1);
    pj_stream_reset();assert(!*pj_stream_field(0,2));
    assert(pj_streams(R"({"streams":[{"url":"https://example.org/video","behaviorHints":{"proxyHeaders":{"request":{"Authorization":"secret"}}}}]})","Test")==1);
    assert(pj_stream_needs_headers(0));pj_stream_reset();
    assert(pj_subtitles(R"({"subtitles":[{"id":"eng","url":"https://example.org/sub.srt","lang":"eng"}]})","OpenSubtitles")==1);
    assert(!std::strcmp(pj_subtitle_field(0,0),"https://example.org/sub.srt"));
    assert(pj_subtitle_load("1\n00:00:01,000 --> 00:00:03,000\nHello <i>world</i> & more\n\n")==1);
    char subtitle[128];assert(!pj_subtitle_at(.5,subtitle,sizeof(subtitle)));
    assert(pj_subtitle_at(2,subtitle,sizeof(subtitle))&&!std::strcmp(subtitle,"Hello world & more"));
    pj_subtitle_reset();
    d=pj_parse(R"({"result":{"code":"Ab12","authKey":"abc"},"error":{"code":"wrong"}})");
    assert(!std::strcmp(pj_string(pj_get(pj_get(d,"result"),"code")),"Ab12"));pj_free(d);
    PCard card={"tt1","A quoted \"title\"","movie","https://example.org/a?key=1","1998","Description"};
    assert(pj_cache_write("build/tests/library.json",&card,1));d=pj_cache_read("build/tests/library.json");
    assert(pj_size(d)==1&&!std::strcmp(pj_string(pj_get(pj_at(d,0),"name")),card.name));pj_free(d);std::remove("build/tests/library.json");
    const char *path="build/tests/progress.json";
    assert(pj_progress_save(path,"tt1","tt1:1:1",120,600));pj_progress_load(path);
    assert(pj_progress_resume("tt1","tt1:1:1")==120);
    assert(pj_progress_resume("tt1","tt1:1:2")==0);
    assert(pj_progress_save(path,"tt1","tt1:1:1",580,600));
    assert(pj_progress_resume("tt1","tt1:1:1")==0);std::remove(path);
    std::cout<<"Shared protocol tests passed\n";
}
