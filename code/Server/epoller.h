#ifndef EPOLLER_H
#define EPOLLER_H

#include <sys/epoll.h> //epoll_ctl()
#include <fcntl.h>  // fcntl()
#include <unistd.h> // close()
#include <assert.h> // close()
#include <vector>
#include <errno.h>


class Epoller {
public:
    explicit Epoller(int maxEvent = 1024) : events_(maxEvent) {
        this->epollFd_ = epoll_create(maxEvent) ;
        assert(epollFd_ >= 0 && events_.size() > 0);
    } ; 

    ~Epoller() {
        close(epollFd_) ; 
    }

    bool AddFd(int fd, uint32_t events) {
        if(fd < 0) return false ;
        struct epoll_event ev ;
        ev.data.fd = fd ; 
        ev.events = events ; 
         // 设置为非阻塞
        SetFdNonblock(fd);
        return 0 == epoll_ctl(this->epollFd_ , EPOLL_CTL_ADD, fd, &ev);
    }

    bool ModFd(int fd, uint32_t events) {
        if(fd < 0) return false ; 
        struct epoll_event ev ; 
        ev.data.fd = fd ; 
        ev.events  = events ;
        return 0 == epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev);
    }

    bool DelFd(int fd) {
        if(fd < 0) return false ; 
        struct epoll_event ev ;  
        return 0 == epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, &ev);
    }

    int Wait(int timeoutMS = -1) {
        // epoll_wait 的等待单位是毫秒，而传递过来的单位是秒，所以要转化下
        if(timeoutMS != -1){
            timeoutMS = timeoutMS * 1000 ; 
        }
        return epoll_wait(epollFd_, &events_[0], static_cast<int>(events_.size()), timeoutMS);
    }

    int GetEventFd(size_t i) const {
        assert(i < events_.size() && i >= 0);
        return events_[i].data.fd;
    }

    uint32_t GetEvents(size_t i) const {
        assert(i < events_.size() && i >= 0);
        return events_[i].events;
    }
    // 将文件描述符设置为非阻塞
    static int SetFdNonblock(int fd) {
        assert(fd > 0) ; 
        return fcntl(fd, F_SETFL, fcntl(fd, F_GETFD, 0) | O_NONBLOCK);
    }
private:
    int epollFd_; 
    std::vector<struct epoll_event> events_;    
};

#endif //EPOLLER_H