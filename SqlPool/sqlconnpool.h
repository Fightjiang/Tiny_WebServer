#ifndef SQLCONNPOOL_H
#define SQLCONNPOOL_H

#include <mysql/mysql.h>
#include <string>
#include <queue>
#include <mutex>
#include <semaphore.h>
#include <thread>
#include "../Log/log.h"

class SqlConnPool {
public:
    static SqlConnPool *Instance();

    MYSQL *GetConn(){
        MYSQL *sql = nullptr ; 
        // 感觉这里需要加锁
        if(connQue_.empty()){
            LOG_WARN("SqlConnPool busy!");
            return nullptr;
        }
        // 信号量减一
        sem_wait(&semId_) ;
        {
            /*RAII的应用，加锁局部块作用域*/
            std::lock_guard<std::mutex> locker(mtx_);
            sql = connQue_.front();
            connQue_.pop();
        }
        assert(sql) ; 
        return sql ;
    }
    
    void FreeConn(MYSQL * sql) {
        assert(sql);
        std::lock_guard<std::mutex> locker(mtx_);
        connQue_.push(sql);
        sem_post(&semId_);
    }

    int GetFreeConnCount(){
        std::lock_guard<std::mutex> locker(mtx_);
        return connQue_.size();
    }

    void Init(const char* host, int port,
              const char* user,const char* pwd, 
              const char* dbName, int connSize){
        assert(connSize > 0) ; 
        for(int i = 0 ; i < connSize ; ++i){
            MYSQL *sql = nullptr ; 
            sql = mysql_init(sql) ;
            if(sql == nullptr){
                LOG_ERROR("MySql init error!");
                assert(sql);
            }
            sql = mysql_real_connect(sql , host , user , pwd , dbName , port , nullptr , 0) ; 
            if (sql == nullptr) {
                LOG_ERROR("MySql Connect error!");
            }
            connQue_.push(sql);
        }          

        // 信号量是多线程多任务同步，互斥锁是多线程多任务互斥。
        // 0 表示信号量只能用于同一进程内的线程间同步；非0值表示信号量可用于多个进程间的同步；
        MAX_CONN_ = connSize;
        sem_init(&semId_, 0, MAX_CONN_);
    }

    void ClosePool() {
        std::lock_guard<std::mutex> locker(mtx_) ; 
        while(!connQue_.empty()){
            MYSQL* sql = connQue_.front() ; 
            connQue_.pop() ; 
            mysql_close(sql) ; 
        }
        mysql_library_end() ; // 释放 Mysql 库使用的资源
    }

private:
    // 私有构造函数，单例模式
    SqlConnPool() : useCount_(0) , freeCount_(0) {} ; 
    ~SqlConnPool() {
        ClosePool() ; 
    }

    int MAX_CONN_;
    int useCount_;
    int freeCount_;

    std::queue<MYSQL *> connQue_;
    std::mutex mtx_;
    sem_t semId_;
};

class SqlConnRAII {
public:
    SqlConnRAII(MYSQL** sql, SqlConnPool *connpool) {
        assert(connpool);
        *sql = connpool->GetConn();
        sql_ = *sql;
        connpool_ = connpool;
    }
    
    ~SqlConnRAII() {
        if(sql_) { connpool_->FreeConn(sql_); }
    }
    
private:
    MYSQL *sql_;
    SqlConnPool* connpool_;
};

#endif // SQLCONNPOOL_H