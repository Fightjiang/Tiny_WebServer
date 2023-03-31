#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include "threadPoolCommon.h"
#include "Thread.h"

class ThreadPool {
private :
    bool is_init_ = false ;                                             // 是否初始化
    bool is_monitor_ = true ;                                           // 是否需要监控（如果不开启，辅助线程策略将失效。默认开启）
    int  cur_index = 0 ;                                                // 用循环派送任务到不同的线程队列中
    size_t input_task_num_ = 0 ;                                        // 记录放入的任务的个数
    ThreadPoolConfigInfo config_ ;                                      // 线程池配置信息
    atomicQueue<Task> task_queue_pool_ ;                                // 改进使用无锁队列存放任务
    std::vector<std::unique_ptr<Thread>> primary_threads_ ;             // 记录所有主线程
    std::list<std::unique_ptr<Thread>> secondary_threads_ ;             // 记录所有的辅助线程
    std::thread monitor_thread_ ;                                       // 监控线程(自动扩缩容机制)
    std::unordered_map<size_t ,int> thread_record_map_ ;                // 线程记录的信息
    std::mutex mtx_ ;                                                   // 互斥锁，用于读取线程池中的任务队列

public :
    explicit ThreadPool(const bool autoInit = true) noexcept {
        // 创建监控线程
        monitor_thread_ = std::move(std::thread(&ThreadPool::monitor , this)) ; 
        config_ = ThreadPoolConfigInfo() ;
        if(autoInit){
            if(this->init() == false){
                LOG_ERROR("Thread Pool Create Fail !!!") ;
            } else {
                LOG_INFO("Thread Pool Create Success !!!") ;
            }
        }
    }

    ~ThreadPool(){
        this->is_monitor_ = false ; // 先关闭监控线程
        if(this->monitor_thread_.joinable()){
            monitor_thread_.join() ; 
        }
        // 再关闭其他线程
        if(this->is_init_) {
            for(auto &ptr : primary_threads_){
                ptr->destroy() ; 
            }
            primary_threads_.clear() ; 
            for(auto &ptr : secondary_threads_){
                ptr->destroy() ; 
            }
            secondary_threads_.clear() ; 
            thread_record_map_.clear() ; 
            this->is_init_ = false ; 
            LOG_INFO("Thread Pool Already deal %d tasks" , this->input_task_num_) ; 
            LOG_INFO("Thread Pool Close over") ; 
        } 
        
    }

    // 初始化线程池，并创建主线程
    bool init() {
        assert(is_init_ == false) ; 
        this->thread_record_map_.clear() ; 
        this->primary_threads_.reserve(config_.default_thread_size_) ; 

        for(int i = 0 ; i < config_.default_thread_size_ ; ++i){
            std::unique_ptr<Thread> ptr = std::make_unique<Thread>(THREAD_TYPE_PRIMARY) ; 
            if(ptr == nullptr){
                LOG_ERROR("One Primary Thread Create Fail") ; 
                return false ;
            }
            ptr->init(i , &task_queue_pool_ ,  &config_) ; 
            // hash 线程 ID 号
            thread_record_map_[std::hash<std::thread::id>()(ptr->thread_.get_id())]  = i ; 
            primary_threads_.emplace_back(std::move(ptr)) ; 
        } 
        this->is_init_ = true ;
        return true ;
    }

    // 创建辅助线程
    bool createSecondaryThread(int size){
        int remainSize = static_cast<int>(config_.max_thread_size_ - config_.default_thread_size_ - secondary_threads_.size()) ; 
        int realCreateSize = std::min(remainSize , size) ; // 使用 realCreateSize 来确保所有的线程数量之和，不会超过设定max值
        if(realCreateSize == 0) {
            LOG_INFO("Secondary Thread Already Max Number") ; 
            return true ;
        }
        for(int i = 0 ; i < realCreateSize ; ++i){
            std::unique_ptr<Thread> ptr = std::make_unique<Thread>(THREAD_TYPE_SECONDARY) ; 
            if(ptr == nullptr){
                LOG_ERROR("One Secondary Thread Create Fail") ; 
                return false; 
            } 
            ptr->init(-1 , &task_queue_pool_ , &config_) ; // 辅助线程 id 号都是 -1 
            secondary_threads_.emplace_back(std::move(ptr)) ; 
        }
        return true ; 
    }

    void monitor(){
        while(is_monitor_){

            // 如果线程池没有初始化，则监控线程会一直处于空跑状态
            while(is_monitor_ && is_init_ == false){
                SLEEP_SECOND(2) ; 
            }
            
            // 监控线程的执行间隔时间
            int span = config_.monitor_span_ ; 
            while(is_monitor_ && is_init_ && span--){
                SLEEP_SECOND(1) ; 
            }

            // 如果 primary线程都在执行，则表示忙碌
            bool busy = true ;
            for(const auto& thread : primary_threads_){
                if(thread != nullptr){
                    busy = busy & thread->is_running_ ; 
                    if(busy == false) break ;
                }
            }

            // 主线程都在忙碌，线程池中还有待完成的任务
            if(busy){
                if(!task_queue_pool_.empty()){
                    if(createSecondaryThread(1) == false){
                        LOG_ERROR("create Secondary Thread Fail !!!") ; 
                    }else {
                        LOG_INFO("create Secondary Thread Success!!!") ; 
                    } 
                }
            }

            // 判断 secondary 线程是否需要退出
            for(auto iter = secondary_threads_.begin(); iter != secondary_threads_.end(); ) {
                if((*iter)->freeze()){ // 该辅助线程空闲了 TTL*span 秒
                    iter = secondary_threads_.erase(iter) ; // erase 会返回下一个迭代器的位置
                    LOG_INFO("Secondary Thread quit!!!") ; 
                }else {
                    ++iter ; 
                }
            }
        }
    }

    void commitTask(const Task &task , const int originIndex = 0){
        int realIndex = dispatch(originIndex) ; 
        if(realIndex >= 0 && realIndex < config_.default_thread_size_){
            primary_threads_[realIndex]->thread_task_queue_.push(std::move(task)) ;
        }else {
            this->task_queue_pool_.push(std::move(task)) ; 
        }
        input_task_num_++ ; // 计数
    }
    
    // 这个派送任务的方式，后续我想实现改进的是，尽量把相同 fd 的任务派送到之前处理过 fd 的线程队列中
    // 用一个 unordered_map 标记对应的线程ID即可，这样应该可以用到对应的线程缓存
    // 另外如果一个线程队列的任务数达到了最大值，则添加到公共的线程池队列中，给其他线程去取

    int dispatch(const int originIndex = 0){ // 派发该任务到那个线程任务队列 或者 线程池队列中
        if(config_.fair_lock_enable_){
            return -1 ;         // 默认添加到线程池的队列中
        }
        int realIndex = -1 ; 
        if(originIndex == 0){ // 默认派送的方式，则循环往每个线程的队列中添加
            realIndex = this->cur_index++ ; 
            if(this->cur_index >= config_.default_thread_size_){
                this->cur_index = 0 ; 
            }
        } 
        return realIndex ; 
    } 
} ; 

#endif