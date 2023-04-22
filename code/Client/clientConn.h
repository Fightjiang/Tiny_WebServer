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
#include "../Common/commonConfig.h"
#include "./httpProtocol.h"
#include "./webSocket.h"
#include "../Server/epoller.h"
#include "../Common/rwlockmap.h"
#include "../Common/picojson.h"
class ClientConn {
private:
   
    int fd_; 
    bool is_Close_ ; 
    uint32_t connEvent_ ;  
    bool is_HttpPotocol_ ;              // 判断是否是 HTTP 协议还是 WebSocket 协议
    bool is_KeepAlive_ ;                // 是否保持 tcp 连接
    struct sockaddr_in addr_;  
    std::unique_ptr<Buffer> readBuff_; // 读缓冲区
    std::unique_ptr<Buffer> writeBuff_; // 写缓冲区
    std::unique_ptr<HttpProtocol> http_ ; 
    std::unique_ptr<WebSocket> webSocket_ ; 
    std::mutex mtx_ ; 
    Epoller *epoller_ ;  
    RWLockMap<std::string , WebSocket*> &userNames_ ;
    std::string name ; 
public : 

    ClientConn(int fd , const sockaddr_in& addr , Epoller* epoll , RWLockMap<std::string , WebSocket*> &userName , const uint32_t connEvent): 
               fd_(fd) , addr_(addr) , epoller_(epoll) , userNames_(userName), connEvent_(connEvent) , is_Close_(false) , is_KeepAlive_(false) , is_HttpPotocol_(true) {  
        epoller_->AddFd(fd_, connEvent_ | EPOLLIN) ; 
        readBuff_ = std::make_unique<Buffer>() ; 
        writeBuff_ = std::make_unique<Buffer>() ; 
        http_ = std::make_unique<HttpProtocol>(fd_ , connEvent & EPOLLET , readBuff_.get() , writeBuff_.get() ) ; 
        webSocket_ = std::make_unique<WebSocket>(fd_ , connEvent & EPOLLET , readBuff_.get() , writeBuff_.get() ) ;
    }

    ~ClientConn() {
        Close() ; 
    } 

    bool Close() {
        if(is_Close_ == true) return false ;
        std::lock_guard<std::mutex> locker(mtx_) ; // 主线程也会析构也会 Close ；包括定时器 Close ，防止同时进行
        if(is_Close_ == false){
            // 先保证 Epoller_DelFd 删除了，再 close(fd_) ，这很重要，因为一旦先关闭了 fd ,主线程就能 accpet 新的相同 fd 了，这样就会导致 epoller->Del(fd) 删除了新连接的 fd 
            if(is_HttpPotocol_ == false){
                webSocket_->close() ; userNames_.erase(name) ; 
            }
            if(epoller_->DelFd(fd_)){
                int ret = close(fd_) ;
                if(ret == -1){
                    LOG_ERROR("close Fd %d error" , fd_) ; 
                }
                is_Close_ = true ;  
            } else {
                LOG_ERROR("epoller DelFd %d error" , fd_) ; 
                return false ; 
            }  
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
            http_->close() ; return CLOSE_CONNECTION ; 
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
                this->name = http_->getUserName() ; 
                http_->close() ; 
                if(webSocket_->handshark(WebSocket_key) == false){
                    LOG_ERROR("WebSocket update Error");
                    return CLOSE_CONNECTION ; 
                }  
                // 握手成功，系统推送群发消息，所有在线用户都需要更新在线群聊好友情况
                if(this->name.size() > 0){ 
                    userNames_[name] = webSocket_.get() ; // 问题，之前的连接还处于登录状态，但是不能接受到群聊消息，只有最新的连接才会加入到群聊中

                    picojson::object message_json , nameMessage;  
                    message_json["isSystem"] = picojson::value(true) ;  
                    std::vector<picojson::value> vecName ; 
                    for(const auto &iter : userNames_){
                        if(iter.second->is_Close() == false){
                            vecName.push_back(picojson::value(iter.first)) ;
                        }
                    } 
                    message_json["message"] = picojson::value(vecName) ;

                    // 将 picojson 对象转换为字符串
                    std::string systemMessage = picojson::value(message_json).serialize(); 
                    // 系统消息，也还要添加 WebSocket 头部字段
                    std::string systemMessageHead = webSocket_->makeWebSocketHead(systemMessage.size()) ;
                    systemMessage = systemMessageHead + systemMessage ;

                    // 群发系统消息
                    for(const auto &iter : userNames_){
                        LOG_INFO("%d name:%s , send system message to new client go online %s",iter.second->GetFd() , iter.first.data() , systemMessage.data())
                        if(iter.second->is_Close() == false){
                            iter.second->makeWebSocketResponse(systemMessage) ;   
                            epoller_->ModFd(iter.second->GetFd() , connEvent_ | EPOLLOUT) ;
                        }
                    }
                }
            }else if(http_->makeHttpResponse(200) == false){// 设置 http 应答报文出错，关闭连接
                LOG_ERROR("server make http response error !!") ; 
                http_->close() ;  return CLOSE_CONNECTION ; 
            } 
        }
        epoller_->ModFd(fd_ , connEvent_ | EPOLLOUT) ;
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
        } else if(ret == CLOSE_CONNECTION){ // 关闭连接
            LOG_ERROR("WebSocket close !!") ;  
            return CLOSE_CONNECTION ; 
        }// GOOD_CODE ; 
        std::string message ; writeBuff_->RefRetrieveAllToStr(message) ; 
        const std::string &responseName = webSocket_->getResponseName() ; 
        // 群发消息
        for(const auto &iter : userNames_){
            if(iter.first != responseName && iter.second->is_Close() == false) {
                iter.second->makeWebSocketResponse(message) ;   
                epoller_->ModFd(iter.second->GetFd() , connEvent_ | EPOLLOUT) ;
            }
        }
        // 接收消息
        epoller_->ModFd(fd_ , connEvent_ | EPOLLIN); 
        return GOOD_CODE ;
    }

    STATUS_CODE dealWebsocketResponse(){
        STATUS_CODE ret = webSocket_->dealWebSocketResponse() ; 
        if(ret == CONTINUE_CODE) return CONTINUE_CODE ; // 缓冲区满了，继续监听响应
        else if(ret == CLOSE_CONNECTION){ // 出错
            LOG_ERROR("server send WebSocket response error !!") ; 
            return CLOSE_CONNECTION ;
        } 
        // 传输完成 , 单次 WebSocket 解析结束 
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
        STATUS_CODE ret = CLOSE_CONNECTION ; 
        if(is_HttpPotocol_){
            ret = dealHttpResponse() ; 
        }else {
            ret = dealWebsocketResponse() ; 
        }
        if(ret == CLOSE_CONNECTION || is_KeepAlive_ == false){ // 是否出错，或者关闭连接
            return CLOSE_CONNECTION ; 
        }

        if(ret == CONTINUE_CODE) { // 数据没有写完，需要继续写 
            epoller_->ModFd(fd_, connEvent_ | EPOLLOUT) ;
        }else { // 写完之后，同一个线程内重置EPOLLONESHOT事件： 把 client 置于可写 EPOLLIN ，接受 request ；
            epoller_->ModFd(fd_ , connEvent_ | EPOLLIN); 
        } 
        return GOOD_CODE ;
    }
} ; 

#endif