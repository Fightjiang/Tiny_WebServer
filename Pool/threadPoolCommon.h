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
#include <assert.h>
#include "../Log/log.h"

#define SLEEP_MILLISECOND(ms)        \
    std::this_thread::sleep_for(std::chrono::milliseconds(ms)); 

#define SLEEP_SECOND(s)              \
    std::this_thread::sleep_for(std::chrono::seconds(s));  

#define  SECONDARY_THREAD_COMMON_ID -1                 // 辅助线程统一id标识
#define  THREAD_TYPE_PRIMARY   1                     // 主线程类型   1 
#define  THREAD_TYPE_SECONDARY  2                  // 辅助线程类型 2

typedef std::function<void()> Task ; 

struct ThreadPoolConfigInfo{
    int  default_thread_size_ = 8 ;                                  // 默认主线程个数
    int  secondary_thread_size_ = 0 ;                                // 默认开启辅助线程个数
    int  max_thread_size_ = default_thread_size_ * 2 ;               // 最多线程个数
    int  secondary_thread_ttl_  = 3 ;                                // 辅助线程 ttl , 单位为 s
    int  monitor_span_ = 3 ;                                         // 监控线程执行时间间隔，单位为 s
    bool fair_lock_enable_ = false ;                                 // 是否开启公平锁，则所有的任务都是从线程池的中获取。（非必要不建议开启，因为这样所有线程又要争抢一个任务了）
}; 

// 无锁队列
// template<typename T> 
// class atomicQueue {
// private : 
//     std::queue<T> queue_ ; 
//     std::atomic<bool> flag_ ; 

// public : 
//     atomicQueue() : flag_(false) {} ; 
    
//     void lock(){
//         bool expect = false ; 
//         while(flag_.compare_exchange_weak(expect , true) == false){
//             expect = false ; // 执行失败时expect结果是未定的
//         }
//         // 这里的 flag_ 已经是 true ; 故其他线程在上面的循环永远都会是 false , 进而操作不了队列
//     }
    
//     void unlock() {
//         flag_.store(false) ; 
//     }

//     bool empty(){
//         lock() ; 
//         if(queue_.empty()) {
//             unlock() ; 
//             return true ;
//         } ; 
//         unlock() ; 
//         return false ;
//     }

//     bool tryPop(T& task) {
//         lock() ; 
//         if(queue_.empty()){
//             unlock() ; // 一定要记得解锁
//             return false ;
//         } 
//         task = std::move(queue_.front()) ; queue_.pop() ;  
//         unlock() ; 
//         return true ;
//     }

//     // 添加只会有主线程一个添加，但是会有其他线程在取队列，故也要加锁
//     void push(T&& tast){
//         lock() ; 
//         queue_.push(std::move(tast)) ;
//         unlock() ; 
//     }

//     // 添加只会有主线程一个添加，所以不用加锁
//     void push(const T&& tast){
//         lock() ; 
//         queue_.push(std::move(tast)) ;
//         unlock() ; 
//     }
// } ;

template<typename T> 
class atomicQueue {
private : 
    std::queue<T> queue_ ; 
    std::mutex mtx ; 

public : 
     
    bool empty(){
        std::unique_lock<std::mutex> locker(mtx) ;
        return queue_.empty() ;  
    }

    bool tryPop(T& task) {
        std::unique_lock<std::mutex> locker(mtx) ;
        if(queue_.empty()){
            return false ;
        } 
        task = std::move(queue_.front()) ; queue_.pop() ;  
        return true ;
    }

    // 添加只会有主线程一个添加，但是会有其他线程在取队列，故也要加锁
    void push(T&& tast){
        std::unique_lock<std::mutex> locker(mtx) ;
        queue_.push(std::move(tast)) ; 
    }

    // 添加只会有主线程一个添加，所以不用加锁
    void push(const T&& tast){
        std::unique_lock<std::mutex> locker(mtx) ;
        queue_.push(std::move(tast)) ; 
    }
} ; 

#endif