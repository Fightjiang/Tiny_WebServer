#ifndef THREAD_POOL_COMMON_H
#define THREAD_POOL_COMMON_H

#include <thread>
#include <mutex>
#include <atomic>
#include <future>
#include <memory>
#include <functional>
#include <queue>
#include <vector>
#include <list> 
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <assert.h>
#include <iostream>
#include "./Log/log.h"

#define SLEEP_MILLISECOND(ms)        \
    std::this_thread::sleep_for(std::chrono::milliseconds(ms)); 

#define SLEEP_SECOND(s)              \
    std::this_thread::sleep_for(std::chrono::seconds(s));  

#define  SECONDARY_THREAD_COMMON_ID -1                 // 辅助线程统一id标识
#define  TYPE_PRIMARY   1                     // 主线程类型   1 
#define  TYPE_SECONDARY  2                  // 辅助线程类型 2

typedef std::function<void()> Task ; 

struct ConfigInfo{
    int  default_thread_size_ = 8 ;                                  // 默认开启主线程个数 
    int  max_thread_size_ = default_thread_size_ * 2 ;               // 最多线程个数
    int  max_thread_queue_size = 10 ;                                // 单个线程里面任务数超过 10 个，则放入公共线程池给其他线程处理
    int  secondary_thread_ttl_  = 3 ;                                // 辅助线程 ttl , 单位为 s
    int  monitor_span_ = 3 ;                                         // 监控线程执行时间间隔，单位为 s
    bool fair_lock_enable_ = false ;                                 // 是否开启公平锁，则所有的任务都是从线程池的中获取。（非必要不建议开启，因为这样所有线程又要争抢一个任务了）
    const char * mysql_host = "localhost" ;                          // 数据库 IP 
    int mysql_port = 3306 ;                                          // 数据库端口
    const char * mysql_user = "root" ;                               // 数据库账号
    const char * mysql_pwd = "root123" ;                             // 数据库密码
    const char * mysql_dbName = "chatroom" ;                         // 使用的 database
    // const char *server_IP = "127.0.0.1" ;                        // 服务器 Ip  
    const char *server_IP = "172.19.103.50" ;                        // 服务器 Ip  
    int server_port = 8080 ;                                         // 服务器端口
    int timeoutMS =  60 * 60 * 2 ;                                    // 是否超时断开不活跃连接
    //int timeoutMS = -1 ;                                               // 是否超时断开不活跃连接
    int server_max_fd = 4000 ;                                       // 服务器最大连接数量，因为我的 file fd 最大只有 4096 ，再保留 96 个，可能用于其他用途，如 open                                              
    bool openLog = true ;                                            // 默认打开日志
    int logLevel = 3 ;                                               // 日志信息级别
    bool logWriteMethod = false ;                                     // true 异步写入，false 同步写入
    bool optLinger = true ;                                          // 优雅关闭：close() 之后等待一定时间，等套接字发送缓冲区中的数据发送完成
    int trigMode = 3 ;                                               // 采用的触发模式，0 水平触发；1 客户端 ET 服务端 LT ; 2  客户端 LT 服务端 ET; 3 客户端 ET 服务端 ET ; default = 3 ; 
}; 

struct HttpConfigInfo { 
    
    const char *srcDir = "/home/lec/File/Tiny_ChatRoom/resources" ;  // 服务器文件所在的地址
    
    std::unordered_map<std::string, std::string> SUFFIX_TYPE = {
        { ".html",  "text/html" },
        { ".xml",   "text/xml" },
        { ".xhtml", "application/xhtml+xml" },
        { ".txt",   "text/plain" },
        { ".rtf",   "application/rtf" },
        { ".pdf",   "application/pdf" },
        { ".word",  "application/nsword" },
        { ".png",   "image/png" },
        { ".gif",   "image/gif" },
        { ".jpg",   "image/jpeg" },
        { ".jpeg",  "image/jpeg" },
        { ".au",    "audio/basic" },
        { ".mpeg",  "video/mpeg" },
        { ".mpg",   "video/mpeg" },
        { ".avi",   "video/x-msvideo" },
        { ".gz",    "application/x-gzip" },
        { ".tar",   "application/x-tar" },
        { ".css",   "text/css "},
        { ".js",    "text/javascript "},
    };
    std::unordered_map<int, std::string> CODE_STATUS = {
        { 200, "OK" },
        { 400, "Bad Request" },
        { 403, "Forbidden" },
        { 404, "Not Found" },
    }; 
    std::unordered_map<int, std::string> CODE_PATH = {
        { 400, "/400.html" },
        { 403, "/403.html" },
        { 404, "/404.html" },
    };
    std::unordered_set<std::string> DEFAULT_HTML{
            "/index", "/register", "/login", "/welcome"
             "/chat", "/video", "/picture", "/websocket"
    } ;
    std::unordered_map<std::string, int> DEFAULT_HTML_TAG {
            {"/register.html", 0}, {"/login.html", 1},  
    };
} ; 

// 自旋锁
template<typename T> 
class atomicQueue {
private : 
    std::queue<T> queue_ ; 
    std::atomic<bool> flag_ ; 

public : 
    atomicQueue() : flag_(false) {} ; 
    
    void lock(){
        bool expect = false ; 
        while(flag_.compare_exchange_weak(expect , true) == false){
            expect = false ; // 执行失败时expect结果是未定的
        }
        // 这里的 flag_ 已经是 true ; 故其他线程在上面的循环永远都会是 false , 进而操作不了队列
    }
    
    void unlock() {
        flag_.store(false) ; 
    }

    bool empty(){
        lock() ; 
        if(queue_.empty()) {
            unlock() ; 
            return true ;
        } ; 
        unlock() ; 
        return false ;
    }

    size_t size(){
        lock() ; 
        size_t size = queue_.size() ; 
        unlock() ; 
        return size ;
    }

    bool tryPop(T& task) {
        lock() ; 
        if(queue_.empty()){
            unlock() ; // 一定要记得解锁
            return false ;
        } 
        task = std::move(queue_.front()) ; queue_.pop() ;  
        unlock() ; 
        return true ;
    }

    // 添加只会有主线程一个添加，但是会有其他线程在取队列，故也要加锁
    void push(T&& tast){
        lock() ; 
        queue_.push(std::move(tast)) ;
        unlock() ; 
    }

    void push(const T&& tast){
        lock() ; 
        queue_.push(std::move(tast)) ;
        unlock() ; 
    }
} ;

// 互斥锁  实现的线程安全队列
// template<typename T> 
// class atomicQueue {
// private : 
//     std::queue<T> queue_ ; 
//     std::mutex mtx ;  

// public : 
     
//     bool empty(){
//         std::unique_lock<std::mutex> locker(mtx) ;
//         return queue_.empty() ;  
//     }

//     size_t size(){
//         std::unique_lock<std::mutex> locker(mtx) ;
//         return queue_.size() ;  
//     }

//     bool tryPop(T& task) { 
//         std::unique_lock<std::mutex> locker(mtx) ; 
//         if(queue_.empty()) return false ;  
//         task = std::move(queue_.front()) ; queue_.pop() ;  
//         return true ;
//     }

//     // 添加只会有主线程一个添加，但是会有其他线程在取队列，故也要加锁
//     void push(T&& tast){ 
//         std::unique_lock<std::mutex> locker(mtx) ;
//         queue_.push(std::move(tast)) ;  
//     }

//     void push(const T&& tast){ 
//         std::unique_lock<std::mutex> locker(mtx) ;
//         queue_.push(std::move(tast)) ;  
//     }
// } ; 

#endif