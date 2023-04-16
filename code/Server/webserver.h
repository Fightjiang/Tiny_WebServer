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
#include "../Client/clientConn.h"
#include "../Common.h"

class WebServer {
private : 
    bool isClose_  ;
    int  listenFd_ ;
    std::atomic<int> userCount ; 
     
    uint32_t listenEvent_;
    uint32_t connEvent_;
    std::mutex mtx ; 
    ConfigInfo config_ ; 
    std::unique_ptr<HeapTimer> timer_;
    std::unique_ptr<ThreadPool> threadpool_;
    std::unique_ptr<Epoller> epoller_;
    std::unordered_map<int, ClientConn> users_; 

public:
    WebServer() : isClose_(false){
            config_ = ConfigInfo() ;
            InitEventMode_(config_.trigMode);
            if(config_.openLog) {
                Log::Instance().init(config_.logLevel, "./log", ".log", config_.logWriteMethod);
                if(isClose_) { LOG_ERROR("========== Server init error!=========="); }
                else {
                    LOG_INFO("========== Server init ==========");
                    LOG_INFO("Port:%d, OpenLinger: %s", config_.server_port, config_.optLinger? "true":"false");
                    LOG_INFO("Listen Mode: %s, Conn Mode: %s",
                                    (listenEvent_ & EPOLLET ? "ET": "LT"),
                                    (connEvent_ & EPOLLET ? "ET": "LT"));
                    LOG_INFO("LogSys level: %d", config_.logLevel); 
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
        int timeS = -1 ; // 秒为单位，epoll wait timeout == -1 无事件将一直阻塞 
        if(isClose_ == false) { LOG_INFO("========== Server start =========="); }
        while(isClose_ == false){
            // 定时器，等待 timeMS 时间后，必有一个连接事件到期，如果期间它没有重新发生交互的话。
            if(config_.timeoutS > 0) {
                timeS = timer_->GetNextTick(); // 发生一次心跳   
            }
            int eventCnt = epoller_->Wait(timeS) ;
            for(int i = 0 ; i < eventCnt ; ++i){
                if(isClose_) break ; 
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

        // 这个 511 表明的是完成了三次握手并处于 ESTABLISHABE 阶段等待 accept 取的队列最大个数
        ret = listen(listenFd_, 511) ;
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
        this->listenEvent_ = 0 ;
        this->connEvent_ = EPOLLONESHOT  ;
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
                    LOG_ERROR("server accept Fail , %d" , errno);  
                    isClose_ = true ; 
                    threadpool_->ClosePool() ; timer_->clear() ; 
                    close(listenFd_) ;   
                    return  ;  
                }
            } else if(userCount >= config_.server_max_fd) {
                SendError_(fd, "Server busy!");
                LOG_WARN("Clients is full!");
                return ;
            } 
            if(fd > 0){
                // 添加客户端的 fd  
                users_[fd].init(fd , addr , epoller_.get() , connEvent_);
                LOG_INFO("Client[%d](%s:%d) in, userCount:%d", fd, inet_ntoa(addr.sin_addr) , ntohs(addr.sin_port) , ++userCount);
                if(config_.timeoutS > 0) {// 小根堆，处理超时连接，绑定关闭的回调函数
                    timer_->add(fd, config_.timeoutS , std::bind(&WebServer::CloseConn_, this , &users_[fd])); 
                }
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

    void CloseConn_(ClientConn* client){
        assert(client); 
        if(client->Close()){
            LOG_INFO("Client[%d](%s:%d) out , userCount:%d", client->GetFd() , client->GetIP(), client->GetPort(), --userCount) ;
        } 
    }

    void DealWrite_(ClientConn* client) {
        assert(client); 
        // 线程池处理写任务
        threadpool_->commitTask(std::bind(&WebServer::OnWrite_, this, client));
    }
    
    void DealRead_(ClientConn* client){
        assert(client); 
        // 线程池处理读任务
        if(config_.timeoutS > 0){ // 先更新小根堆叭，避免极端情况线程异步处理的时候，主线程把 fd close 了
            timer_->adjust(client->GetFd() , Clock::now() + std::chrono::seconds(config_.timeoutS)) ; 
        }
        threadpool_->commitTask(std::bind(&WebServer::OnRead_, this, client));
    }

    // 读取客户端发送过来的消息
    void OnRead_(ClientConn* client) { 
        STATUS_CODE ret = client->dealRequest();
        if(ret == CLOSE_CONNECTION) { 
            LOG_INFO("Client[%d](%s:%d) out , userCount:%d", client->GetFd() , client->GetIP(), client->GetPort(), --userCount) ;
            client->Close();  return ; 
        }
        // 读完之后，同一个线程内重置EPOLLONESHOT事件： 把 client 置于可写 EPOLLOUT ，发送 response ；
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT) ;
    }

    void OnWrite_(ClientConn* client) { 
        STATUS_CODE ret = client->dealResponse() ;
        if(ret == CONTINUE_CODE) { // 数据没有写完，需要继续写 
            epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLOUT) ; return ;
        }else if(ret == CLOSE_CONNECTION || client->IsKeepAlive() == false){ // 是否出错，或者关闭连接
            LOG_INFO("Client[%d](%s:%d) out , userCount:%d", client->GetFd() , client->GetIP(), client->GetPort(), --userCount) ;
            client->Close(); return ;
        } 
        // 写完之后，同一个线程内重置EPOLLONESHOT事件： 把 client 置于可写 EPOLLIN ，接受 http request ；
        epoller_->ModFd(client->GetFd(), connEvent_ | EPOLLIN); 
    }
};


#endif //WEBSERVER_H