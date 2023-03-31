// 定时器测试
// Linux下的信号采用的异步处理机制，信号处理函数和当前进程是两条不同的执行路线。
// 具体的，当进程收到信号时，操作系统会中断进程当前的正常流程，转而进入信号处理函数执行操作，完成后再返回中断的地方继续执行。

#include "heaptimer.h"  
#include <signal.h>
#include <iostream>
#include <memory>
#include <unistd.h>  
#include <thread>
#include <math.h>

void func1(){
    std::cout<<"Hello 1 World"<<std::endl ;
}

void func2(){
    std::cout<<"Hello 2 World"<<std::endl ;
}

void func3(){
    std::cout<<"Hello 3 World"<<std::endl ;
}

std::shared_ptr<HeapTimer> ptr = std::make_shared<HeapTimer>() ; 
void handle_alarm(int sig) {
    int ret = ptr->GetNextTick();
    alarm(std::ceil(ret / 1000.0)) ; 
    std::cout<<ret<<std::endl ;
    std::cout << "Alarm signal received." << std::endl;
}

void test_add(){

    ptr->add(1 , 1000 * 20, func1) ; 
    ptr->add(2 , 1000 * 10, func2) ; // hello2 first execute
    ptr->add(3 , 1000 * 30, func3) ; 

    signal(SIGALRM, handle_alarm); // 注册信号处理函数
    
    alarm(10) ;  
    std::this_thread::sleep_for(std::chrono::seconds(35)) ;

}

int main(){
     
    std::thread th1(test_add) ;  
    th1.join() ;  
    std::cout<<"over"<<std::endl ;

    return 0 ; 
}