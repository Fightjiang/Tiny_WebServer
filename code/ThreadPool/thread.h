#ifndef THREAD_H
#define THREAD_H
#include "../Common.h"

class Thread{
private : 
    int index_ ;                                      // 线程 index 
    int type_  ;                                      // 线程类型 主线程: 1 ; 辅助线程: 2 ; 
    unsigned long total_task_num_    ;                // 处理的任务的个数
    int cur_ttl_  ;                                   // 辅助线程才有最大生存周期，主线程一直会存在
    bool is_running_ ;                                // 是否正在执行,这个是给监控进程看的
    bool is_starting ;                                // 线程运行启动的标记 
    bool is_init_    ;                                // 该线程是否已经进行了初始化
    std::thread thread_ ;                             // 执行任务的线程
    atomicQueue<Task>* pool_task_queue_ ;             // 线程池的总任务队列
    atomicQueue<Task>  thread_task_queue_ ;            // 本线程中的任务队列
    ConfigInfo *config_ ; 
    friend class ThreadPool ;

public:
    explicit Thread(const int type = TYPE_SECONDARY) noexcept {
        
        type_ = (type == TYPE_PRIMARY) ? TYPE_PRIMARY : TYPE_SECONDARY  ; 
        is_running_ = false ;
        is_init_ = false ;
        is_starting = true ;
        total_task_num_ = 0 ; 
    }

    ~Thread(){
        if(is_init_){
            destroy() ; 
        }
    }

    void destroy(){
        this->is_starting = false ; 
        if(this->thread_.joinable()){
            this->thread_.join() ; // 等待线程结束
        }
        this->is_init_ = false ; 
        this->is_running_ = false ;
        this->total_task_num_ = 0 ; 
    }

    bool init(int index , 
              atomicQueue<Task> *poolTaskQueue , 
              ConfigInfo* config) {

        if(is_init_ == true) return false ;
        this->index_ = index ; 
        this->pool_task_queue_ = poolTaskQueue ;
        this->config_ = config ; 
        if(this->type_ == TYPE_SECONDARY) { 
            this->cur_ttl_ = this->config_->secondary_thread_ttl_ ; 
        }
        this->thread_ = std::move(std::thread(&Thread::run , this)) ; 
        this->is_init_ = true ;
        return true ;
    }

    void processTask(){
        Task task = nullptr ;
        // 自身队列有的话，先从自身队列拿，不用加锁
        // 线程池 总队列里拿任务，需要加锁
        // 主线程可以从自身队列中拿，辅助线程只能去线程池队列中拿
        if(this->type_ == TYPE_PRIMARY) {
            if(popTask(task) || popPoolTask(task)) {
                runTask(task) ; 
            } else {
                std::this_thread::yield() ; // 让出 CPU 一小短时间，不要去抢锁了.
            }
        } else {
            if(popPoolTask(task)) {
                runTask(task) ; 
            } else {
                std::this_thread::yield() ; // 让出 CPU 一小短时间，不要去抢锁了.
            }
        }
        
    }

    // 自身的任务队列
    bool popTask(Task& task){
        return thread_task_queue_.tryPop(task) ; 
    }
    
    // 从线程池的大任务队列中拿任务
    bool popPoolTask(Task& task){
        return this->pool_task_queue_->tryPop(task) ; 
    }
    
    void runTask(Task &task){
        is_running_ = true ; 
        task() ; 
        total_task_num_++ ; 
        is_running_ = false ;
    }

    bool run() {
        assert(is_init_ = true) ;
        assert(config_ != nullptr) ;
        assert(pool_task_queue_ != nullptr) ; 
        while(is_starting){
            processTask() ; // 尝试获得任务执行
        }
    }

    // 检查辅助线程是否在使用中
    bool freeze() {
        if(is_running_){
            cur_ttl_++ ; 
            cur_ttl_ = std::min(cur_ttl_ , config_->secondary_thread_ttl_) ; 
        }else {
            cur_ttl_-- ; // 如果当前辅助线程没有在执行，则ttl-1
        }
        return cur_ttl_ <= 0 ;
    }
} ; 

#endif 