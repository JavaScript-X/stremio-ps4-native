// Exercise the actual PS4-profile engine and pthread workers on the host.
// All tracker DNS is deliberately stubbed; no public swarm is contacted.
#include "torrent/engine.h"
#include "torrent/bencode.h"
#include "torrent/crypto_compat.h"
#include "torrent/worker.h"
#include "util.h"
#include <netdb.h>
#include <unistd.h>
#include <cassert>
#include <cstdio>
#include <set>
#include <mutex>

extern "C" int getaddrinfo(const char *,const char *,const addrinfo *,addrinfo **out){*out=nullptr;return EAI_NONAME;}
static std::string hash_of(const std::string &s){
    unsigned char digest[20];assert(SHA1(reinterpret_cast<const unsigned char*>(s.data()),s.size(),digest));
    static const char digits[]="0123456789abcdef";std::string out;
    for(auto c:digest){out+=digits[c>>4];out+=digits[c&15];}return out;
}
int main(){
    std::mutex mutex;std::set<std::string> ids;std::vector<bt::Worker> workers;
    for(int i=0;i<4;++i)workers.emplace_back([&]{for(int j=0;j<1000;++j){unsigned char id[20];assert(RAND_bytes(id,sizeof(id)));std::lock_guard<std::mutex> g(mutex);assert(ids.emplace(reinterpret_cast<char*>(id),sizeof(id)).second);}});
    for(auto &w:workers)w.join();assert(ids.size()==4000);

    char path[]="build/tests/torrent-XXXXXX";assert(mkdtemp(path));std::string dir=path;
    assert(make_dirs(dir+"/torrents"));auto &e=bt::Engine::get();e.configure(dir,1024*1024);
    // Previously sscanf("%2x") accepted a valid half-byte followed by junk.
    std::string invalid(40,'a');invalid[1]='z';e.start(invalid,{});assert(!e.stats(invalid).found);
    e.start("short",{});assert(!e.stats("short").found);

    std::string absent(40,'1'),error;std::vector<bt::FileInfo> files;std::atomic<bool> cancel{false};
    e.start(absent,{});assert(e.stats(absent).found);
    assert(!e.wait_metadata(absent,files,&cancel,.15,&error)&&error=="timed out");
    cancel=true;assert(!e.wait_metadata(absent,files,&cancel,2,&error)&&error=="cancelled");
    e.shutdown();e.shutdown();assert(!e.stats(absent).found);

    BValue info=BValue::dict();info.d["name"]=BValue(std::string("sample.mp4"));
    info.d["length"]=BValue(int64_t(32768));info.d["piece length"]=BValue(int64_t(16384));
    info.d["pieces"]=BValue(std::string(40,'x'));std::string raw=bencode(info),hash=hash_of(raw);
    assert(write_file(dir+"/torrents/"+hash+".info",raw));
    for(int repeat=0;repeat<3;++repeat){
        cancel=false;e.start(hash,{});assert(e.wait_metadata(hash,files,&cancel,1,&error));
        assert(files.size()==1&&files[0].size==32768&&bt::Engine::guess_file(files,-1,-1)==0);
        int rid=-1;int64_t size=0;auto torrent=e.open_reader(hash,0,&rid,&size,&error);assert(torrent&&rid>=0&&size==32768);
        uint8_t buf[16];assert(e.read(torrent,rid,0,-1,buf,sizeof(buf),&cancel)==-1);
        assert(e.read(torrent,rid,0,size,buf,sizeof(buf),&cancel)==0);
        bt::Worker aborter([&]{usleep(100000);cancel=true;});
        assert(e.read(torrent,rid,0,0,buf,sizeof(buf),&cancel)==-1);aborter.join();
        e.close_reader(torrent,rid);torrent.reset();e.shutdown();assert(!e.stats(hash).found);
    }
    std::remove((dir+"/torrents/"+hash+".info").c_str());std::remove((dir+"/torrent-cache.bin").c_str());
    assert(rmdir((dir+"/torrents").c_str())==0);assert(rmdir(dir.c_str())==0);
    std::puts("PS4-profile torrent startup, cached metadata, cancellation and restart tests passed");
}
