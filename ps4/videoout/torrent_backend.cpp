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
int file_open(void*,const char*){return 0;}
int file_close(void*){cancelled=true;std::lock_guard<std::mutex> g(lock);if(torrent&&reader>=0)bt::Engine::get().close_reader(torrent,reader);torrent.reset();reader=-1;return 0;}
int file_read(void*,uint8_t *buf,uint64_t pos,uint32_t size){std::shared_ptr<bt::Torrent> t;int r,idx;{std::lock_guard<std::mutex> g(lock);t=torrent;r=reader;idx=file_index;}if(!t||r<0)return -1;return bt::Engine::get().read(t,r,idx,(int64_t)pos,buf,(int)size,&cancelled);}
uint64_t file_size_cb(void*){return (uint64_t)file_size;}
}
extern "C" int torrent_prepare(const char *hash,const char *const *sources,int source_count,int requested_file,int season,int episode,PlaybackFile *file,char *label,unsigned label_size){
    if(!hash||!*hash||!file)return 0;torrent_stop();cancelled=false;std::vector<std::string> trackers;
    for(int i=0;i<source_count;++i)if(sources[i]&&*sources[i])trackers.emplace_back(sources[i]);
    auto &engine=bt::Engine::get();engine.configure("/data/stremio-torrent",768ll<<20);engine.start(hash,trackers);std::vector<bt::FileInfo> files;std::string error;
    if(!engine.wait_metadata(hash,files,&cancelled,45,&error))return 0;int chosen=requested_file;if(chosen<0||chosen>=(int)files.size())chosen=bt::Engine::guess_file(files,season,episode);if(chosen<0)return 0;
    engine.select_file(hash,chosen);int rid=-1;int64_t bytes=0;auto opened=engine.open_reader(hash,chosen,&rid,&bytes,&error);if(!opened||rid<0||bytes<=0)return 0;
    {std::lock_guard<std::mutex> g(lock);torrent=std::move(opened);reader=rid;file_index=chosen;file_size=bytes;active_hash=hash;}
    if(label&&label_size)std::snprintf(label,label_size,"%s",files[(size_t)chosen].path.c_str());*file=(PlaybackFile){nullptr,file_open,file_close,file_read,file_size_cb};return 1;
}
extern "C" void torrent_stop(void){file_close(nullptr);}
extern "C" void torrent_cancel(void){cancelled=true;}
extern "C" int torrent_stats(char *out,unsigned n){if(!out||!n)return 0;bt::Stats s=bt::Engine::get().stats(active_hash);std::snprintf(out,n,"%d peers  %.1f MB/s  %d%%",s.peers,s.download_rate/(1024*1024),int(s.file_progress*100));return s.found;}
