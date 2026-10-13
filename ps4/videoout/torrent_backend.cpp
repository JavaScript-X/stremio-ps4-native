#include "torrent_backend.h"
#include "torrent/engine.h"
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>
namespace {
std::mutex lock;
std::shared_ptr<bt::Torrent> torrent;
int reader=-1,file_index=-1;int64_t file_size=0;std::string active_hash;
std::atomic<bool> cancelled{false};
void phase(const char *text){FILE *f=std::fopen("/data/stremio-torrent-status.log","a");if(f){timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);std::fprintf(f,"%lld.%03ld %s\n",(long long)t.tv_sec,t.tv_nsec/1000000,text);std::fclose(f);}std::fprintf(stderr,"[stremio-ps4] torrent: %s\n",text);}
int file_open(void*,const char*){return 0;}
int file_close(void*){cancelled=true;std::lock_guard<std::mutex> g(lock);if(torrent&&reader>=0)bt::Engine::get().close_reader(torrent,reader);torrent.reset();reader=-1;return 0;}
int file_read(void*,uint8_t *buf,uint64_t pos,uint32_t size){std::shared_ptr<bt::Torrent> t;int r,idx;{std::lock_guard<std::mutex> g(lock);t=torrent;r=reader;idx=file_index;}if(!t||r<0)return -1;return bt::Engine::get().read(t,r,idx,(int64_t)pos,buf,(int)size,&cancelled);}
uint64_t file_size_cb(void*){return (uint64_t)file_size;}
}
extern "C" int torrent_prepare(const char *hash,const char *const *sources,int source_count,int requested_file,int season,int episode,PlaybackFile *file,char *label,unsigned label_size){
    try {if(!hash||!*hash||!file)return 0;phase("prepare");torrent_stop();cancelled=false;std::vector<std::string> trackers;
    for(int i=0;i<source_count;++i)if(sources[i]&&*sources[i])trackers.emplace_back(sources[i]);
    auto &engine=bt::Engine::get();engine.configure("/data/stremio-torrent",512ll<<20);phase("engine start");engine.start(hash,trackers);phase("waiting metadata");std::vector<bt::FileInfo> files;std::string error;
    if(!engine.wait_metadata(hash,files,&cancelled,45,&error)){phase(error.c_str());return 0;}phase("metadata ready");int chosen=requested_file;if(chosen<0||chosen>=(int)files.size())chosen=bt::Engine::guess_file(files,season,episode);if(chosen<0){phase("no playable file");return 0;}
    engine.select_file(hash,chosen);int rid=-1;int64_t bytes=0;auto opened=engine.open_reader(hash,chosen,&rid,&bytes,&error);if(!opened||rid<0||bytes<=0)return 0;
    {std::lock_guard<std::mutex> g(lock);torrent=std::move(opened);reader=rid;file_index=chosen;file_size=bytes;active_hash=hash;}
    if(label&&label_size)std::snprintf(label,label_size,"%s",files[(size_t)chosen].path.c_str());*file=(PlaybackFile){nullptr,file_open,file_close,file_read,file_size_cb};phase("reader ready");return 1;
    }catch(const std::exception &e){phase(e.what());torrent_stop();return 0;}catch(...){phase("unknown exception");torrent_stop();return 0;}
}
extern "C" void torrent_stop(void){file_close(nullptr);}
extern "C" void torrent_cancel(void){cancelled=true;}
extern "C" int torrent_stats(char *out,unsigned n){if(!out||!n)return 0;bt::Stats s=bt::Engine::get().stats(active_hash);std::snprintf(out,n,"%d peers  %.1f MB/s  %d%%",s.peers,s.download_rate/(1024*1024),int(s.file_progress*100));return s.found;}
