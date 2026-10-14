#pragma once
#ifdef PLATFORM_PS4
#include <pthread.h>
#include <memory>
#include <system_error>
#include <type_traits>
#include <utility>

namespace bt {
// Use the same pthread entry path as the native frontend. std::thread adds
// libc++ thread-local bookkeeping before reaching our worker/catch boundary.
class Worker {
    pthread_t id{};
    bool active=false;
    template<class F> static void *entry(void *p) {
        std::unique_ptr<F> task(static_cast<F *>(p));
        (*task)(); // Engine entries contain their own exception boundary.
        return nullptr;
    }
public:
    Worker()=default;
    Worker(const Worker&)=delete;
    Worker& operator=(const Worker&)=delete;
    Worker(Worker&& other) noexcept : id(other.id),active(other.active){other.active=false;}
    Worker& operator=(Worker&& other) noexcept {
        if(active)pthread_detach(id);
        id=other.id;active=other.active;other.active=false;return *this;
    }
    template<class F> explicit Worker(F&& f) {
        using Task=typename std::decay<F>::type;
        std::unique_ptr<Task> task(new Task(std::forward<F>(f)));
        pthread_attr_t attr;
        int err=pthread_attr_init(&attr);
        if(err)throw std::system_error(err,std::generic_category(),"torrent thread attributes");
        // Peer parsing uses a 64 KiB receive buffer; reserve a known stack.
        err=pthread_attr_setstacksize(&attr,1024*1024);
        if(!err)err=pthread_create(&id,&attr,entry<Task>,task.get());
        pthread_attr_destroy(&attr);
        if(err)throw std::system_error(err,std::generic_category(),"torrent thread creation");
        task.release();active=true;
    }
    ~Worker(){if(active)pthread_detach(id);}
    bool joinable() const noexcept{return active;}
    void join(){if(active){int err=pthread_join(id,nullptr);if(err)throw std::system_error(err,std::generic_category(),"torrent thread join");active=false;}}
};
}
#else
#include <thread>
namespace bt {using Worker=std::thread;}
#endif
