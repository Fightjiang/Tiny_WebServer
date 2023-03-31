#include "threadPool.h"
using namespace std ;

mutex mtx ; 
void print(int num) { 
    mtx.lock() ; // 这个锁只是为了打印缓冲区完整
    std::cout<<num<<std::endl ; 
    mtx.unlock() ; 
}

// 测试监控线程是否有效，是否会自动扩缩容
void func(){
    SLEEP_SECOND(30) ; 
    std::cout<<"func over"<<std::endl ;
}

int main(){
    Log::Instance()->init(0, "./log", ".log", 1); ; 
    std::unique_ptr<ThreadPool> threadPool = make_unique<ThreadPool>() ; 
    for(int i = 0 ; i < 100 ; ++i){
        // 无序打印 0 - 99 
        threadPool->commitTask(std::bind(print , i)) ; // 默认放到每个线程自己的队列中
    }

    for(int i = 0 ; i < 10 ; ++i){// 应该会创建辅助两个线程帮忙，是否创建可以看日志
        threadPool->commitTask(std::bind(func) , -1) ; 
    }
    SLEEP_SECOND(50) ; // 监控线程每 3s 才检查一次，这里阻塞是为了检查辅助线程是否退出
    return 0 ;
}