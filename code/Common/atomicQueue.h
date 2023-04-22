#include <mutex>
#include <atomic>
#include <queue>

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