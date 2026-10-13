// C bridge to the original PS5 Stremio protocol and JSON implementation.
#include "protocol.h"
#include "stremio.h"
#include <cstring>
#include <memory>
#include <cmath>
namespace {
const json empty;
const json &value(PJson p) { return p ? *static_cast<const json *>(p) : empty; }
std::vector<Stream> streams;
json progress=json::object();
int copy(const std::string &s,char *out,size_t n) {
    if(!out||!n)return 0;
    if(s.size()>=n){out[0]=0;return 0;}
    std::memcpy(out,s.c_str(),s.size()+1);return 1;
}
}
extern "C" {
PJson pj_cache_read(const char *path){try{auto p=std::make_unique<json>();if(!load_json(path,*p)||!p->is_array())return nullptr;return p.release();}catch(...){return nullptr;}}
int pj_cache_write(const char *path,const PCard *cards,int count){try{
    if(count<0||count>527||(!cards&&count))return 0;json list=json::array();
    for(int i=0;i<count;++i){const auto &c=cards[i];list.push_back({{"id",c.id},{"name",c.name},{"type",c.type},{"poster",c.poster},{"releaseInfo",c.year},{"description",c.description}});}
    return save_json(path,list);
}catch(...){return 0;}}
PJson pj_parse(const char *text) {
    if(!text)return nullptr;
    try {auto p=std::make_unique<json>(json::parse(text,nullptr,false));
        if(p->is_discarded())return nullptr;return p.release();}catch(...){return nullptr;}
}
void pj_free(PJson p){delete static_cast<const json *>(p);}
PJson pj_get(PJson p,const char *key){auto &j=value(p);if(!j.is_object())return nullptr;auto i=j.find(key);return i==j.end()?nullptr:&*i;}
PJson pj_at(PJson p,int i){auto &j=value(p);return j.is_array()&&i>=0&&size_t(i)<j.size()?&j[size_t(i)]:nullptr;}
int pj_size(PJson p){auto &j=value(p);return j.is_array()?int(j.size()):0;}
int pj_array(PJson p){return value(p).is_array();}
const char *pj_string(PJson p){auto &j=value(p);return j.is_string()?j.get_ref<const std::string &>().c_str():"";}
double pj_number(PJson p,double fallback){auto &j=value(p);return j.is_number()?j.get<double>():fallback;}
int pj_bool(PJson p){auto &j=value(p);return j.is_boolean()&&j.get<bool>();}
int pj_copy(PJson p,char *out,size_t n){return copy(pj_string(p),out,n);}
int pj_request(char *out,size_t n,const char *auth,int addons){try{
    json j={{"authKey",auth}};
    if(addons){j["type"]="AddonCollectionGet";j["update"]=true;}
    else {j["collection"]="libraryItem";j["ids"]=json::array();j["all"]=true;}
    return copy(j.dump(),out,n);
}catch(...){if(n)out[0]=0;return 0;}}
int pj_resource(PJson m,const char *transport,const char *resource,const char *type,const char *id,char *out,size_t n){try{
    Addon addon;if(!parse_addon(value(m),transport,addon)||!addon.supports(resource,type,id))return 0;
    return copy(addon.resource_url(resource,type,id),out,n);
}catch(...){return 0;}}
void pj_stream_reset(void){streams.clear();}
int pj_streams(const char *text,const char *addon){try{
    auto j=json::parse(text,nullptr,false);if(j.is_discarded()||!j.is_object()||!j.contains("streams")||!j["streams"].is_array())return -1;
    auto result=parse_streams(j,addon);
    for(auto &s:result){if(streams.size()>=128)break;streams.push_back(std::move(s));}
    return int(streams.size());
}catch(...){return -1;}}
const char *pj_stream_field(int i,int field){if(i<0||size_t(i)>=streams.size())return "";auto &s=streams[size_t(i)];
    switch(field){case 0:return s.name.c_str();case 1:return s.description.c_str();case 2:return s.url.c_str();case 3:return s.info_hash.c_str();case 4:return s.addon.c_str();default:return "";}}
int pj_stream_file(int i){return i>=0&&size_t(i)<streams.size()?streams[size_t(i)].file_idx:-1;}
int pj_stream_needs_headers(int i){return i>=0&&size_t(i)<streams.size()&&!streams[size_t(i)].request_headers.empty();}
void pj_progress_load(const char *path){try{json loaded;if(load_json(path,loaded)&&loaded.is_object())progress=std::move(loaded);}catch(...){} }
double pj_progress_resume(const char *title,const char *video){try{
    const auto &item=progress.at(title);if(jstr(item,"video_id")!=video)return 0;
    double p=jnum(item,"time"),duration=jnum(item,"duration");return std::isfinite(p)&&p>5&&p<31536000&&(duration<=0||p<duration*.95)?p:0;
}catch(...){return 0;}}
int pj_progress_save(const char *path,const char *title,const char *video,double position,double duration){try{
    if(!title||!*title||!video||!*video||!std::isfinite(position)||!std::isfinite(duration)||position<0||duration<0)return 0;
    progress[title]={{"video_id",video},{"time",position},{"duration",duration},{"updated",now_epoch_ms()}};
    return save_json(path,progress);
}catch(...){return 0;}}
}
