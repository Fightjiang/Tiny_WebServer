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
#include "../Server/epoller.h"
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
    std::mutex mtx_ ; 
    Epoller *epoller_ ;  
public : 

    ClientConn(): is_Close_(true) {  
        
    }

    ~ClientConn() {
        Close() ; 
    } 
    
    void init(int fd , const sockaddr_in& addr , Epoller* epoll , const uint32_t connEvent ){ 
        
        {
            std::lock_guard<std::mutex> locker(mtx_) ; 
            fd_ = fd ;  
            epoller_ = epoll ;  
            epoller_->AddFd(fd_, EPOLLIN | connEvent);
            is_Close_ = false ;
        }
        
        is_ET_ = connEvent & EPOLLET ;  
        is_KeepAlive_ = false ; 
        is_HttpPotocol_ = true ;  
        addr_ = addr;  
        readBuff_ = std::make_unique<Buffer>() ; 
        writeBuff_ = std::make_unique<Buffer>() ; 
        http_ = std::make_unique<HttpProtocol>(readBuff_.get() , writeBuff_.get() , fd_ , is_ET_) ; 
        webSocket_ = std::make_unique<WebSocket>(readBuff_.get() , writeBuff_.get() , fd_ , is_ET_) ;
    }

    bool Close() {
        if(is_Close_ == true) return false ;
        std::lock_guard<std::mutex> locker(mtx_) ; 
        if(is_Close_ == false){
            close(fd_) ;
            epoller_->DelFd(fd_) ;  
            is_Close_ = true ;  
        }
        return true ;
    }

    int GetFd() const {
        return fd_;
    } 

    const char* GetIP() const{
        return inet_ntoa(addr_.sin_addr);
    }

    int GetPort() const{
        return ntohs(addr_.sin_port);
    }

    bool IsClose() const {
        return is_Close_ ; 
    }

    bool IsKeepAlive() const {
        return is_KeepAlive_ ; 
    }

    STATUS_CODE dealHttpRequest(const int needRead = true){
        STATUS_CODE retCode = http_->dealHttpRequest(needRead) ; 
        if(retCode == CLOSE_CONNECTION){ // 客户端已经关闭了
            LOG_INFO("Client[%d](%s:%d) already close", this->GetFd() , this->GetIP(), this->GetPort()) ;
            http_->close() ; return retCode ; 
        }else if(retCode == BAD_REQUEST){ // 客户端请求报文错误
            if(http_->makeHttpResponse(400) == false){// 设置 http 应答报文出错，关闭连接
                LOG_ERROR("server make http response error !!") ; 
                http_->close() ;  return CLOSE_CONNECTION ; 
            }   
        }else {
            if(http_->isUpgradeWebSocket()){// 是否升级成 WebSocket 协议了
                is_HttpPotocol_ = false ; // 转交给 WebSocket 升级
                is_KeepAlive_ = true ; 
                std::string WebSocket_key = http_->get_WebSocket_key() ; 
                http_->close() ; 
                if(webSocket_->handshark(WebSocket_key) == false){
                    LOG_ERROR("WebSocket update Error");
                    return CLOSE_CONNECTION ; 
                }  
                return GOOD_CODE ; 
            }
            if(http_->makeHttpResponse(200) == false){// 设置 http 应答报文出错，关闭连接
                LOG_ERROR("server make http response error !!") ; 
                http_->close() ;  return CLOSE_CONNECTION ; 
            } 
        }
        return GOOD_CODE ;
    }

    STATUS_CODE dealHttpResponse() { 
        STATUS_CODE ret = http_->dealHttpResponse() ;  
        if(ret == CONTINUE_CODE) return ret ; // 缓冲区满了，继续监听响应，等待继续写数据
        else if(ret == CLOSE_CONNECTION){ // 出错
            LOG_ERROR("server send http response error !!") ; 
            http_->close() ; return CLOSE_CONNECTION ;
        } 
         
        if(http_->IsKeepAlive()){       // 是否 保持连接
            is_KeepAlive_ = true ; 
        }
        // 出现了粘包，缓冲区里还有数据，再次处理
        if(readBuff_->BufferUsedSize() > 0) {
            http_->close() ;// 关闭上一次的 http_ 请求资源，直接再处理这一次的
            LOG_ERROR("maybe have Sticky package") ; 
            ret = this->dealHttpRequest(false) ; 
            if(ret == CLOSE_CONNECTION){
                return CLOSE_CONNECTION ;
            }if(ret == GOOD_CODE){
                return CONTINUE_CODE ; // 请求处理成功了，所以 return continue_code 继续写这次的应答报文，
            }  
        }
        // 传输完成 , 本次 http 请求应答结束，故 http 关闭 ，但是 tcp 并没有关闭
        http_->close() ;
        return GOOD_CODE ;
    }

    STATUS_CODE dealWebSocketRequest(){
        STATUS_CODE ret = webSocket_->dealWebSocketRequest() ; 
        if(ret == BAD_REQUEST) {// WebSocket 请求解析出错，直接关闭连接
            LOG_ERROR("server deal parse WebSocket request error !!") ;   
            return CLOSE_CONNECTION ; 
        } else if(ret == CLOSE_CONNECTION){ // 切换成 http 连接
            LOG_ERROR("WebSocket close !!") ;  
            return CLOSE_CONNECTION ; 
        }
        return GOOD_CODE ;
    }

    STATUS_CODE dealWebsocketResponse(){
        STATUS_CODE ret = webSocket_->dealWebSocketResponse() ; 
        if(ret == CONTINUE_CODE) return ret ; // 缓冲区满了，继续监听响应
        else if(ret == CLOSE_CONNECTION){ // 出错
            LOG_ERROR("server send http response error !!") ; 
            webSocket_->close() ; return CLOSE_CONNECTION ;
        } 
        // 传输完成 , 单次 WebSocket 解析结束 
        webSocket_->close() ;
        return GOOD_CODE ;
    }
    
    STATUS_CODE dealRequest() {
        if(is_HttpPotocol_){
            http_->init() ; 
            return dealHttpRequest() ; 
        }else {
            webSocket_->init() ; 
            return dealWebSocketRequest() ; 
        }  
    }

    STATUS_CODE dealResponse() {
        if(is_HttpPotocol_){
            return dealHttpResponse() ; 
        }else {
            return dealWebsocketResponse() ; 
        }
    }
} ; 

#endif