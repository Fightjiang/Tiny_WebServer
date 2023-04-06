#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <unordered_map>
#include <fcntl.h>       // fcntl()
#include <unistd.h>      // close()
#include <assert.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "epoller.h"
#include "../Log/log.h"
#include "../Timer/heaptimer.h"
#include "../SqlPool/sqlConnectPool.h"
#include "../ThreadPool/threadPool.h" 
#include "../Http/httpConn.h"
#include "../Common.h"

class WebServer {
private : 
    bool isClose_  ;
    int  listenFd_ ;
    int  userCount ; 
     
    uint32_t listenEvent_;
    uint32_t connEvent_;
    std::mutex mtx ; 
    ThreadPoolConfigInfo config_ ; 
    std::unique_ptr<HeapTimer> timer_;
    std::unique_ptr<ThreadPool> threadpool_;
    std::unique_ptr<Epoller> epoller_;
    std::unordered_map<int, HttpConn> users_;
    std::unordered_map<int, Clock::time_point> users_Expires; //Clock::now() + MS(newExpires)

public:
    WebServer() : isClose_(false){
            config_ = ThreadPoolConfigInfo() ;
            InitEventMode_(config_.trigMode);
            if(config_.openLog) {
                Log::Instance().init(config_.logLevel, "./log", ".log", true);
                if(isClose_) { LOG_ERROR("========== Server init error!=========="); }
                else {
                    LOG_INFO("========== Server init ==========");
                    LOG_INFO("Port:%d, OpenLinger: %s", config_.server_port, config_.optLinger? "true":"false");
                    LOG_INFO("Listen Mode: %s, Conn Mode: %s",
                                    (listenEvent_ & EPOLLET ? "ET": "LT"),
                                    (connEvent_ & EPOLLET ? "ET": "LT"));
                    LOG_INFO("LogSys level: %d", config_.logLevel);
                    LOG_INFO("srcDir: %s", config_.srcDir);
                    LOG_INFO("SqlConnPool num: %d, ThreadPool num: %d", config_.default_thread_size_, config_.default_thread_size_);
                }
            }
            timer_ = std::make_unique<HeapTimer>() ;
            threadpool_ = std::make_unique<ThreadPool>();
            epoller_ = std::make_unique<Epoller>() ; 
            this->userCount = 0 ; 
            if(!InitSocket_()) { 
                close(listenFd_) ;
                isClose_ = true; 
                LOG_ERROR("server init socket fail") ;
            }
    }

    ~WebServer() {
        close(listenFd_) ;  
        isClose_ = true ; 
    }

    void Start() {
        int timeMS = -1 ; // epoll wait timeout == -1 无事件将一直阻塞 
        if(isClose_ == false) { LOG_INFO("========== Server start =========="); }
        while(isClose_ == false){
            // 定时器，等待 timeMS 时间后，必有一个连接事件到期
            if(config_.timeoutMS > 0) {
                // 统一调节更新所有结点的过期时间
                for(const auto &client : users_Expires) {
                    timer_->adjust(client.first , client.second) ; 
                }
                timeMS = timer_->GetNextTick(); // 发生一次心跳
            }
            int eventCnt = epoller_->Wait(timeMS) ;
            for(int i = 0 ; i < eventCnt ; ++i){
                // 处理事件
                int fd = epoller_->GetEventFd(i);
                uint32_t events = epoller_->GetEvents(i);
                if(fd == listenFd_) {
                    DealListen_();
                }else if(events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) { 
                    CloseConn_(&users_[fd]);  // 断开连接 
                }else if(events & EPOLLIN) { 
                    DealRead_(&users_[fd]);   // 处理读请求
                }else if(events & EPOLLOUT) { 
                    DealWrite_(&users_[fd]);  // 处理写请求
                } else {
                    LOG_ERROR("Unexpected event");
                }
            }
        }
    }

private:
    bool InitSocket_() {
        int ret ; 
        struct sockaddr_in addr ;
        this->listenFd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if(listenFd_ < 0) {
            LOG_ERROR("Create socket IP: %s  port:%d error", config_.server_IP , config_.server_port);
            return false;
        }

        /* 优雅关闭: 直到所剩数据发送完毕或超时 */
        struct linger optLinger ; 
        if(config_.optLinger) {
            optLinger.l_onoff = 1;   // 指定选项是否生效，0为不生效，非0为生效
            optLinger.l_linger = 1;  // 指定等待时间，单位为秒 , 超时了则强制关闭
        }
        ret = setsockopt(listenFd_, SOL_SOCKET, SO_LINGER, &optLinger, sizeof(optLinger));
        if(ret < 0) {
            close(listenFd_);
            LOG_ERROR("Init linger IP: %s  port:%d error", config_.server_IP , config_.server_port);
            return false;
        }

        int optval = 1;
        /* 端口复用，可以直接重启在 TIME_WAIT 阶段的套接字 */
        /* 只有最后一个套接字会正常接收数据。 */
        ret = setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int));
        if(ret == -1) {
            LOG_ERROR("set socket setsockopt IP: %s  port:%d error", config_.server_IP , config_.server_port);
            close(listenFd_);
            return false;
        }

        // 绑定地址 IP 和 端口
        addr.sin_family = AF_INET;
        // addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_addr.s_addr = inet_addr(config_.server_IP);
        addr.sin_port = htons(config_.server_port) ;
        ret = bind(listenFd_, (struct sockaddr *)&addr, sizeof(addr));
        if(ret < 0) {
            LOG_ERROR("Bind IP: %s  port:%d error", config_.server_IP , config_.server_port);
            close(listenFd_);
            return false;
        }

        // 这个 6 表明的是完成了三次握手并处于 ESTABLISHABE 阶段等待 accept 取的队列最大个数
        ret = listen(listenFd_, 6) ;
        if(ret < 0) {
            LOG_ERROR("Listen IP: %s  port:%d error", config_.server_IP , config_.server_port);
            close(listenFd_);
            return false;
        }
        
        // 注册 listenFd_
        ret = epoller_->AddFd(listenFd_,  listenEvent_ | EPOLLIN);
        if(ret == 0) {
            LOG_ERROR("Add listenFd error!");
            close(listenFd_);
            return false;
        }
        LOG_INFO("Server IP: %s  port:%d", config_.server_IP , config_.server_port);
        return true;
    }

    void InitEventMode_(int trigMode) {
        // EPOLLRDHUP作用： 当使用边沿触发（EPOLLET）时，EPOLLRDHUP将会在连接断开时触发一次EPOLLIN事件， 此时epoll_wait()函数将返回一个EPOLLIN事件，并且read()操作将返回一个零字节的值。
        // EPOLLONESHOT 作用： 使用EPOLLONESHOT事件类型可以避免并发问题，确保一个文件描述符在任何时候都只会被一个线程处理，防止多个线程同时对一个文件描述符进行操作。
        this->listenEvent_ = EPOLLRDHUP ;
        this->connEvent_ = EPOLLONESHOT | EPOLLRDHUP ;

        switch (trigMode)
        {
        case 0 : 
            break; 
        case 1 : 
            this->connEvent_ |= EPOLLET ; 
            break ;
        case 2 : 
            this->listenEvent_ |= EPOLLET ; 
            break ;
        case 3 : 
            listenEvent_ |= EPOLLET;
            connEvent_ |= EPOLLET;
            break;
        default:
            listenEvent_ |= EPOLLET;
            connEvent_ |= EPOLLET;
            break;
        }
    }

    void DealListen_() {
        struct sockaddr_in addr;
        socklen_t len = sizeof(addr);
        do {
            int fd = accept(listenFd_, (struct sockaddr *)&addr , &len) ; 
            if(fd < 0) {
                if(errno == EWOULDBLOCK){// 非阻塞模式下，没有连接了  
                    break ; 
                }else if(errno == EINTR){ // 或者 被信号中断等待重新调用一次accept即可  
                    continue ;
                }else { // 出错
                    LOG_ERROR("server accept Fail");  
                    close(listenFd_) ;   isClose_ = true ;
                    return  ;  
                }
            } else if(userCount >= config_.server_max_fd) {
                SendError_(fd, "Server busy!");
                LOG_WARN("Clients is full!");
                return ;
            } 
            if(fd > 0){
                // 添加客户端的 fd  
                users_[fd].init(fd, config_.srcDir , addr , connEvent_ |= EPOLLET);
                if(config_.timeoutMS > 0) {// 小根堆，处理超时连接，绑定关闭的回调函数
                    timer_->add(fd, config_.timeoutMS , std::bind(&WebServer::CloseConn_, this , &users_[fd]));
                    users_Expires[fd] = Clock::now() + std::chrono::seconds(config_.timeoutMS) ; 
                }
                epoller_->AddFd(fd, EPOLLIN | connEvent_);
                LOG_INFO("Client[%d](%s:%d) in, userCount:%d", fd, inet_ntoa(addr.sin_addr) , addr.sin_port , ++userCount);
            }
        } while(listenEvent_ & EPOLLET);
    }
    
    void SendError_(int fd, const char*info) {
        assert(fd > 0) ; 
        int ret = send(fd , info , strlen(info) , 0) ; 
        if(ret < 0) {
            LOG_WARN("send error to client[%d] error!", fd);
        }
        // 关闭连接
        close(fd) ; 
    }

    void CloseConn_(HttpConn* client){
        assert(client);
        int fd = client->GetFd() ; 
        LOG_INFO("Client[%d](%s:%d) out , userCount:%d", fd , client->GetIP(), client->GetPort(), --userCount) ;   
        users_.erase(fd) ;
        if(config_.timeoutMS > 0){
            users_Expires.erase(fd) ;
        }
        epoller_->DelFd(fd);
    }

    void DealWrite_(HttpConn* client) {
        assert(client); 
        // 线程池处理写任务
        threadpool_->commitTask(std::bind(&WebServer::OnWrite_, this, client));
        if(client->IsClose() ){
            CloseConn_(client) ; return ;
        }
    }
    
    void DealRead_(HttpConn* client){
        assert(client); 
        // 线程池处理读任务
        threadpool_->commitTask(std::bind(&WebServer::OnRead_, this, client));
        if(client->IsClose()){
            CloseConn_(client) ; return ; 
        }
        if(config_.timeoutMS > 0){
            users_Expires[client->GetFd()] = Clock::now() + std::chrono::seconds(config_.timeoutMS) ; 
        }
    }

    // 读取客户端发送过来的消息
    void OnRead_(HttpConn* client) { 
        bool ret = client->dealHttpRequest();
        if(ret == false) { 
            client->Close(); return ; 
        }
        // 读完之后，同一个线程内重置EPOLLONESHOT事件： 把 client 置于可写 EPOLLOUT ，发送 response ；
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT) ;
    }

    void OnWrite_(HttpConn* client) { 
        bool ret = client->dealHttpResponse() ;
        if(ret == false || client->IsKeepAlive() == false){ 
            client->Close();  return ;
        }
        // 写完之后，同一个线程内重置EPOLLONESHOT事件： 把 client 置于可写 EPOLLIN ，接受 http request ；
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN);
    }
};


#endif //WEBSERVER_H