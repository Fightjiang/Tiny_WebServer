#ifndef LOG_H
#define LOG_H

#include <mutex> 
#include <thread>
#include <condition_variable> 
#include <atomic>
#include <queue>
#include <iostream>
#include <sys/time.h>
#include <string.h>
#include <stdarg.h>           // vastart va_end
#include <sys/stat.h>         //mkdir  
#include <assert.h>


#define LOG_BASE(level, format, ...) \
    do {\
        Log& log = Log::Instance();\
        if (log.IsOpen() && log.GetLevel() <= level) {\
            log.write(level, format, ##__VA_ARGS__); \
        }\
    } while(0);

#define LOG_DEBUG(format, ...) do {LOG_BASE(0, format, ##__VA_ARGS__)} while(0);
#define LOG_INFO(format, ...) do {LOG_BASE(1, format, ##__VA_ARGS__)} while(0);
#define LOG_WARN(format, ...) do {LOG_BASE(2, format, ##__VA_ARGS__)} while(0);
#define LOG_ERROR(format, ...) do {LOG_BASE(3, format, ##__VA_ARGS__)} while(0);

// 线程安全
class Log {
public:
    static Log& Instance() {
        static Log inst ; 
        return inst ; 
    }

    void init(const int loglevel , const char* path = ".", const char* suffix =".log", const bool logWriteMethod = true) {
        
        if(this->isOpen_ == true) return ; 
        
        level_ = loglevel ;
        lineCount_ = 0 ; // first line 
        path_ = path;
        suffix_ = suffix;
        
        time_t timer = time(nullptr) ;
        struct tm *sysTime = localtime(&timer) ; 

        // 根据 路径+时间+后缀名创建log文件 
        char fileName[LOG_NAME_LEN] ;
        memset(fileName , '\0' , sizeof(fileName)) ;
        snprintf(fileName, LOG_NAME_LEN - 1, "%s/%04d_%02d_%02d%s", 
                path_, sysTime->tm_year + 1900, sysTime->tm_mon + 1, sysTime->tm_mday, suffix_) ;
        //std::cout<<fileName<<std::endl ;
        toDay_ = sysTime->tm_mday;
        fp_ = fopen(fileName, "w+");
        // 如果未创建成功，说明没有对应的目录，创建目录
        if(fp_ == nullptr) {
            mkdir(path_, 0755);
            fp_ = fopen(fileName, "w+");
        } 
        assert(fp_ != nullptr);
        isOpen_ = true ; // 成功打开 
        
        // 是否启用异步写入log 
        if(logWriteMethod) {
            this->isAsync_ = true;
            // 获取 writeThread_ 的 unique 智能指针 
            this->writeThread_ = std::make_unique<std::thread>(&Log::AsyncWrite_ , this);
        } else {
            isAsync_ = false;
        }
    }

    void write(int level, const char *format, ...) {
        // 操作缓冲区 & 文件时上锁，保证线程安全
        std::unique_lock<std::mutex> locker(mtx_);
        time_t timer = time(nullptr) ;
        struct tm *sysTime = localtime(&timer) ; 
    
        // 如果日期变了，也就是到第二天了，或者当前log文件行数达到规定的最大值时都需要创建一个新的log文件
        if (toDay_ != sysTime->tm_mday || (lineCount_ > 0 && (lineCount_  %  MAX_LINES == 0)))  {
            char newFile[LOG_NAME_LEN];
            // 第二天了
            if (toDay_ != sysTime->tm_mday) { 
                snprintf(newFile, LOG_NAME_LEN - 1, "%s/%04d_%02d_%02d%s", 
                        path_, sysTime->tm_year + 1900, sysTime->tm_mon + 1, sysTime->tm_mday, suffix_) ;
                toDay_ = sysTime->tm_mday ;
                lineCount_ = 0;
            }else { // 第一个 log 满了,需要分出
                snprintf(newFile, LOG_NAME_LEN - 1, "%s/%04d_%02d_%02d-%d%s", 
                path_, sysTime->tm_year + 1900, sysTime->tm_mon + 1, sysTime->tm_mday , (lineCount_  / MAX_LINES), suffix_);
            }
            this->flush(); // 上一个文件 flush 
            fp_ = fopen(newFile, "a");
            assert(fp_ != nullptr); 
        }

        // 写日志
        std::unique_ptr<char[]> tmpBuff = std::make_unique<char[]>(2048) ;
        lineCount_++;
        int n = snprintf(tmpBuff.get(), 2048, "%d-%02d-%02d %02d:%02d:%02d %s",
                        sysTime->tm_year + 1900, sysTime->tm_mon + 1, sysTime->tm_mday,
                        sysTime->tm_hour, sysTime->tm_min, sysTime->tm_sec , AppendLogLevelTitle_(level));

        // ... 使用的可变参数列表
        va_list vaList ; 
        // 根据用户传入的参数，向缓冲区添加数据 
        va_start(vaList, format);
        // m 是vaList 字符串的长度，并不是写入到 buff 中的长度
        int m = vsnprintf(tmpBuff.get() + n , 2048 - n, format, vaList);
        va_end(vaList); 
            
        int len = (n + m >= 2048) ? 2047 : n + m ;
        *(tmpBuff.get() + len) = '\n' ; 
        
        if(isAsync_) {
            this->que.push(std::move(tmpBuff)) ;
            condition_locker.notify_one() ; 
        }else {
            fputs(tmpBuff.get(), fp_); fflush(fp_) ;
        }
    }

    void flush() {
        if(isAsync_) { // 切换到同步保证之前的 fp_ 全部刷新完
            std::unique_lock<std::mutex> locker(conditional_mutex) ;
            while(!que.empty()) {
                fputs(que.front().get() , fp_) ; que.pop() ; 
            }; 
        }
        fflush(fp_) ; fclose(fp_); 
    }

    int GetLevel() {
        return level_ ;
    }

    bool IsOpen() { 
        return isOpen_ ; 
    }
    
    const char * AppendLogLevelTitle_(int level) {
        switch(level) {
        case 0:
            return "[debug]: " ; 
        case 1:
            return "[info] : " ;  
        case 2:
            return "[warn] : " ;  
        case 3:
            return "[error]: " ; 
        default:
            break ;    
        }
        return "[info] : " ;  
    }

private:
    Log(): path_(nullptr) , suffix_(nullptr) , lineCount_(0), toDay_(0), isOpen_(false) ,
           level_(0), isAsync_(false), writeThread_(nullptr) ,  fp_(nullptr) {}

    virtual ~Log() { 
        isOpen_ = false ; condition_locker.notify_one() ; 
        if(writeThread_ && writeThread_->joinable()){
            writeThread_->join();
        }
        while(!que.empty()) {
            fputs(que.front().get() , fp_) ; que.pop() ; 
        }; 
        if(fp_) { 
            fflush(fp_); fclose(fp_);
        }
        LOG_INFO("Server Close !!") ; 
    }

    void AsyncWrite_() {
        while(isOpen_){
            std::unique_lock<std::mutex> locker(conditional_mutex) ;
            if(!que.empty()){
                fputs(que.front().get() , fp_) ; que.pop() ; fflush(fp_) ;
            }else {
                condition_locker.wait(locker) ; 
            }
        }
    } 

private: 
    static const int LOG_NAME_LEN = 256 ;
    static const int MAX_LINES = 5000   ; 

    const char* path_;
    const char* suffix_;
    FILE* fp_ ;

    std::atomic<int> lineCount_;
    std::atomic<int> toDay_;

    std::atomic<int> isOpen_;
    std::atomic<int> level_;
    std::atomic<bool> isAsync_; 

    std::unique_ptr<std::thread> writeThread_;
    std::queue<std::unique_ptr<char[]>> que ;  // que 中存的都是要写入到磁盘里的 buff 
    
    std::mutex mtx_ , conditional_mutex ;
    std::condition_variable condition_locker ; 
};


#endif //LOG_H