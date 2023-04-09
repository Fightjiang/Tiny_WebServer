#ifndef CLIENT_CONNET_H
#define CLIENT_CONNET_H

#include <sys/types.h>
#include <sys/uio.h>     // readv/writev
#include <arpa/inet.h>   // sockaddr_in
#include <stdlib.h>      // atoi()
#include <fcntl.h>       // open
#include <unistd.h>      // close
#include <sys/stat.h>    // stat
#include <sys/mman.h>    // mmap, munmap
#include <errno.h>      
#include <regex>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include "../Buffer/buffer.h"
#include "../Log/log.h"
#include "../SqlPool/sqlConnectPool.h" 
#include "../Common.h"
#include "./httpProtocol.h"

class ClientConn {
private:
   
    int fd_; 
    bool is_Close_ ; 
    bool is_ET_ ;  
    bool is_HttpPotocol_ ;              // 判断是否是 HTTP 协议还是 WebSocket 协议
    bool is_KeepAlive_ ;                // 是否保持 tcp 连接
    struct sockaddr_in addr_;  
    std::unique_ptr<HttpProtocol> http_ ; 
public : 

    ClientConn() {  
        
    }

    ~ClientConn() {
        Close() ; 
    } 
    
    void init(int fd , const sockaddr_in& addr , const bool isET = true){ 
        fd_ = fd ;  
        is_ET_ = isET ; 
        is_Close_ = false ;
        is_KeepAlive_ = false ; 
        is_HttpPotocol_ = true ;  
        addr_ = addr;  
        http_ = std::make_unique<HttpProtocol>() ; 
    }

    void Close() {
        if(is_Close_ == true) return ;
        is_Close_ = true ;  
        close(fd_);  
    }

    int GetFd() const {
        return fd_;
    } 

    const char* GetIP() const{
        return inet_ntoa(addr_.sin_addr);
    }

    int GetPort() const{
        return addr_.sin_port;
    }

    bool IsClose() const {
        return is_Close_ ; 
    }

    bool IsKeepAlive() const {
        return is_KeepAlive_ ; 
    }

    bool dealHttpRequest(){
        http_->init(fd_ , is_ET_) ; 
        if(http_->dealHttpRequest() == false){ // 解析客户端 http 请求出错
            LOG_ERROR("server deal parse http request error !!") ; 
            if(http_->makeHttpResponse(400) == false){// 设置 http 应答报文出错，关闭连接
                LOG_ERROR("server make http response error !!") ; 
                http_->close() ;  return false ; 
            }    
        } else {
            if(http_->makeHttpResponse(200) == false){// 设置 http 应答报文出错，关闭连接
                LOG_ERROR("server make http response error !!") ; 
                http_->close() ;  return false ; 
            } 
        }
        return true ;
    }

    int dealHttpResponse() { 
        int ret = http_->dealHttpResponse() ;  
        if(ret == 2) return ret ; // 缓冲区满了，继续监听响应
        else if(ret == 0){ // 出错
            LOG_ERROR("server send http response error !!") ; 
            http_->close() ; return 0 ;
        } 
        if(http_->isUpgradeWebSocket()){// 是否升级成 WebSocket 协议了
            is_HttpPotocol_ = false ; 
        }   
        if(http_->IsKeepAlive()){       // 是否 保持连接
            is_KeepAlive_ = true ; 
        }
        // 传输完成 , 本次 http 请求应答结束，故 http 关闭 ，但是 tcp 并没有关闭
        http_->close() ;
        return 1 ;
    }

    bool dealWebSocketRequest(){ 
        
    }

    bool dealWebsocketResponse(){

    }
    
    bool dealRequest() {
        if(is_HttpPotocol_){
            return dealHttpRequest() ; 
        }else {
            return dealWebSocketRequest() ; 
        }  
    }

    int dealResponse() {
        if(is_HttpPotocol_){
            return dealHttpResponse() ; 
        }else {
            return dealWebsocketResponse() ; 
        }
    }
} ; 

#endif