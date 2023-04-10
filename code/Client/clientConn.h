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
#include "./webSocket.h"
class ClientConn {
private:
   
    int fd_; 
    bool is_Close_ ; 
    bool is_ET_ ;  
    bool is_HttpPotocol_ ;              // 判断是否是 HTTP 协议还是 WebSocket 协议
    bool is_KeepAlive_ ;                // 是否保持 tcp 连接
    struct sockaddr_in addr_;  
    std::unique_ptr<Buffer> readBuff_; // 读缓冲区
    std::unique_ptr<Buffer> writeBuff_; // 写缓冲区
    std::unique_ptr<HttpProtocol> http_ ; 
    std::unique_ptr<WebSocket> webSocket_ ; 
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
        readBuff_ = std::make_unique<Buffer>() ; 
        writeBuff_ = std::make_unique<Buffer>() ; 
        http_ = std::make_unique<HttpProtocol>(readBuff_.get() , writeBuff_.get() , fd_ , is_ET_) ; 
        webSocket_ = std::make_unique<WebSocket>(readBuff_.get() , writeBuff_.get() , fd_ , is_ET_) ;
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

    bool dealHttpRequest(const int needRead = true){
        
        if(http_->dealHttpRequest(needRead) == false){ // 解析客户端 http 请求出错
            LOG_ERROR("server deal parse http request error !!") ; 
            if(http_->makeHttpResponse(400) == false){// 设置 http 应答报文出错，关闭连接
                LOG_ERROR("server make http response error !!") ; 
                http_->close() ;  return false ; 
            }    
        } else {
            if(http_->isUpgradeWebSocket()){// 是否升级成 WebSocket 协议了
                is_HttpPotocol_ = false ; // 转交给 WebSocket 升级
                is_KeepAlive_ = true ; 
                std::string WebSocket_key = http_->get_WebSocket_key() ; 
                http_->close() ; 
                return webSocket_->handshark(WebSocket_key) ;  
            }

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
         
        if(http_->IsKeepAlive()){       // 是否 保持连接
            is_KeepAlive_ = true ; 
        }
        // 出现了粘包，缓冲区里还有数据，再次处理
        if(readBuff_->BufferUsedSize() > 0) {
            http_->close() ;// 关闭上一次的 http_ 请求资源，直接再处理这一次的
            if(this->dealHttpRequest(false) == true){
                return 2 ;
            }  
        }
        // 传输完成 , 本次 http 请求应答结束，故 http 关闭 ，但是 tcp 并没有关闭
        http_->close() ;
        return 1 ;
    }

    bool dealWebSocketRequest(){
        if(webSocket_->dealWebSocketRequest() == false){
            LOG_ERROR("server deal parse WebSocket request error !!") ; 
            return false ;
        }
        return true ;
    }

    bool dealWebsocketResponse(){
        int ret = webSocket_->dealWebSocketResponse() ; 
        if(ret == 2) return ret ; // 缓冲区满了，继续监听响应
        else if(ret == 0){ // 出错
            LOG_ERROR("server send http response error !!") ; 
            webSocket_->close() ; return 0 ;
        } 
        // 传输完成 , 单次 WebSocket 解析结束 
        webSocket_->close() ;
        return 1 ;
    }
    
    bool dealRequest() {
        if(is_HttpPotocol_){
            http_->init() ; 
            return dealHttpRequest() ; 
        }else {
            webSocket_->init() ; 
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