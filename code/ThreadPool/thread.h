#ifndef THREAD_H
#define THREAD_H
#include "../Common/commonConfig.h"
#include "../Common/atomicQueue.h"
class Thread{
private : 
    int index_ ;                                      // 线程 index 
    int type_  ;                                      // 线程类型 主线程: 1 ; 辅助线程: 2 ; 
    unsigned long total_task_num_    ;                // 处理的任务的个数
    int cur_ttl_  ;                                   // 辅助线程才有最大生存周期，主线程一直会存在
    bool is_busy_ ;                                   // 是否正在执行任务，
    bool is_starting ;                                // 线程运行启动的标记 
    bool is_init_    ;                                // 该线程是否已经进行了初始化
    std::thread thread_ ;                             // 执行任务的线程
    atomicQueue<Task>* pool_task_queue_ ;             // 线程池的总任务队列
    atomicQueue<Task>  thread_task_queue_ ;           // 本线程中的任务队列
    ConfigInfo *config_ ;                             
    std::mutex mtx_  ;                                // 同步唤醒线程  
    std::condition_variable condition_ ; 
    friend class ThreadPool ; 

public:
    explicit Thread(const int type = TYPE_SECONDARY) noexcept {
        
        type_ = (type == TYPE_PRIMARY) ? TYPE_PRIMARY : TYPE_SECONDARY  ; 
        is_busy_ = false ;
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
        condition_.notify_one() ; 
        if(this->thread_.joinable()){
            this->thread_.join() ; // 等待线程结束
        }
        this->is_init_ = false ; 
        this->is_busy_ = false ;
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
        // 自身队列有的话，先从自身队列拿，也要加锁存取，因为主线程随时都会 push 队列新的任务
        // 自身任务队列跑完了，再尝试到线程池 总队列里拿任务  
        if(popTask(task)) {
            total_task_num_++ ; 
            is_busy_ = true ;
            task() ; 
        }else if(popPoolTask(task) == false){// 线程池 总队列队列中也没有任务了，那就阻塞等待唤醒叭
            if(type_ == TYPE_PRIMARY) {
                is_busy_ = false ; // 阻塞了，就不忙了，
                std::unique_lock<std::mutex> locker(mtx_) ; 
                condition_.wait(locker , [this]{ return !thread_task_queue_.empty() || is_starting == false;}) ;  
            }else {
                // 辅助线程就别阻塞了，让出 CPU 一小段时间就接着运行，因为都开辅助线程了，肯定任务很多了
                is_busy_ = false ;
                std::this_thread::yield() ; // 任务经常有，适合比较密集，让出 CPU 一小短时间，然后接着判断是否有任务获取
            } 
        }
    }

    // 自身的任务队列
    bool popTask(Task& task){
        return thread_task_queue_.tryPop(task) ; 
    }
    
    // 从线程池的大任务队列中拿任务，一次多拿几个放在自己的线程中，因为向大线程池取任务了，说明本线程任务队列已经执行完了
    bool popPoolTask(Task& task){
        int size = config_->pick_task_size ; 
        while(size > 0) {
            if(this->pool_task_queue_->tryPop(task)){
                thread_task_queue_.push(std::move(task)) ; 
                --size ; 
            }else {
                break ; 
            }
        }
        return size != config_->pick_task_size ;  // 判断是否取走了任务 
    }

    bool run() {
        assert(is_init_ = true) ;
        assert(config_ != nullptr) ;
        assert(pool_task_queue_ != nullptr) ; 
        while(is_starting){
            processTask() ; // 尝试获得任务执行
        }
        LOG_INFO("%d thread achieve %d tasks" , std::this_thread::get_id()  , total_task_num_) ; 
    }

    // 检查辅助线程是否在使用中
    bool freeze() {
        if(is_busy_){
            cur_ttl_++ ; 
            cur_ttl_ = std::min(cur_ttl_ , config_->secondary_thread_ttl_) ; 
        }else {
            cur_ttl_-- ; // 如果当前辅助线程没有在执行，则ttl-1
        }
        return cur_ttl_ <= 0 ;
    }
} ; 

#endif 