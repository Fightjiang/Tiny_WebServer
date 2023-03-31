#include "log.h"
#include <string>
#include <vector>
using namespace std ;
// 测试单线程同步写
void test_SyncWrite() {
    Log *log = Log::Instance() ; 
    log->init(0, "./log", ".log", 0);
    LOG_INFO("========== Server init ==========");
    LOG_INFO("LogSys level: %d", 0);
    LOG_INFO("Port:%d, Method: %s", 80, "SyncWrite");   
    std::string str(2000 , 't') ; 
    str = str + std::string(40 , 'a') ; // 超过，会直接断开
    LOG_INFO("MAX String: %s" , str.data()) ;  
    LOG_INFO("MAX String Over") ;      
}

// 测试单线程异步写
void test_AsynWrite() { 
    Log *log = Log::Instance() ; 
    log->init(0, "./log", ".log", 1);
    LOG_INFO("========== Server init ==========");
    LOG_INFO("LogSys level: %d", 0);
    LOG_INFO("Port:%d, Method: %s", 80, "AsynWrite");  
    std::string str(2000 , 't') ; 
    str = str + std::string(40 , 'a') ; // 超过，会直接断开
    LOG_INFO("MAX String: %s" , str.data()) ;  
    LOG_INFO("MAX String Over") ;      
}

// 测试多线程同步写日志
void test_MultiThread_SyncWrite() {
    Log *log = Log::Instance() ; 
    log->init(0, "./log", ".log", 0);
    LOG_INFO("========== Server init ==========");
    std::vector<std::thread> thVec ;
    for(int i = 0 ; i < 10 ; ++i){
        thVec.push_back(thread([]{
            std::this_thread::sleep_for(std::chrono::seconds(2)) ;
            LOG_INFO("thread_ID: %d , Hello World 1" , std::this_thread::get_id()) ; 
            LOG_INFO("thread_ID: %d , Hello World 2" , std::this_thread::get_id()) ; 
            LOG_INFO("thread_ID: %d , Hello World 3" , std::this_thread::get_id()) ; 
            LOG_INFO("thread_ID: %d , Hello World 4" , std::this_thread::get_id()) ; 
            LOG_INFO("thread_ID: %d , Hello World 5" , std::this_thread::get_id()) ; 
        })) ; 
    }

    for(int i = 0 ; i < 10 ; ++i){
        thVec[i].join() ; 
    }
    LOG_INFO("Thread all SyncWrite over") ; 
}

// 测试多线程异步写日志
void test_MultiThread_AsynWrite() {
    Log *log = Log::Instance() ; 
    log->init(0, "./log", ".log", 1);
    LOG_INFO("========== Server init ==========");
    std::vector<std::thread> thVec ;
    for(int i = 0 ; i < 10 ; ++i){
        thVec.push_back(thread([]{
            std::this_thread::sleep_for(std::chrono::seconds(2)) ;
            LOG_INFO("thread_ID: %d , Hello World 1" , std::this_thread::get_id()) ; 
            LOG_INFO("thread_ID: %d , Hello World 2" , std::this_thread::get_id()) ; 
            LOG_INFO("thread_ID: %d , Hello World 3" , std::this_thread::get_id()) ; 
            LOG_INFO("thread_ID: %d , Hello World 4" , std::this_thread::get_id()) ; 
            LOG_INFO("thread_ID: %d , Hello World 5" , std::this_thread::get_id()) ; 
        })) ; 
    }

    for(int i = 0 ; i < 10 ; ++i){
        thVec[i].join() ; 
    }
    LOG_INFO("Thread all AsynWrite over") ; 
}

int main(){
    // test_SyncWrite() ;    
    // test_AsynWrite() ; 
    // test_MultiThread_SyncWrite() ; 
    test_MultiThread_AsynWrite() ; 
    return 0 ; 
}