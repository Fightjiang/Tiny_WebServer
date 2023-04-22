#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <string>
#include <arpa/inet.h>    //for ntohl
#include <openssl/sha.h>
#include <mutex>
#include "base64.h"
#include "../Buffer/buffer.h"
#include "../Log/log.h"
#include "../Common/commonConfig.h"
#include "../Common/picojson.h" 
#include "../Common/lockList.h"

#define MAGIC_KEY "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
class WebSocket{
public : 
    WebSocket(const int fd , const int isET ,  Buffer* read , Buffer* write) :
     fd_(fd) , is_ET_(isET) , readBuff_(read) , writeBuff_(write) , is_Close_(false) {
        
    }

    ~WebSocket() {
        close() ; 
    }
    
    void init(){ 
        fin_ = 0 ; opcode_ = 0 ; mask_ = 0 ; payload_length_ = 0 ; responseName = "" ;
        memset(masking_key_ , 0 , sizeof(masking_key_)) ;
    }

    void close(){ 
        if(is_Close_ == true) return ; 
        std::unique_lock<std::mutex> locker(mtx_) ; 
        if(is_Close_ == false){
            messageList.clear() ; 
        }
        is_Close_ = true ;
    }

    bool is_Close() {
        std::unique_lock<std::mutex> locker(mtx_) ; 
        return is_Close_ ; 
    }
    
    // 获得握手需要的报文信息
    bool handshark(std::string WebSocket_key) {
        WebSocket_key += MAGIC_KEY ; // WebSocket 协议的规定，toBase64( sha1( Sec-WebSocket-Key + 258EAFA5-E914-47DA-95CA-C5AB0DC85B11 )  )
        
        unsigned char message_digest[SHA_DIGEST_LENGTH];
        SHA1(reinterpret_cast<const unsigned char *>(WebSocket_key.data()), WebSocket_key.size(), message_digest);
        WebSocket_key = base64_encode(reinterpret_cast<const unsigned char*>(message_digest) , SHA_DIGEST_LENGTH);
        
        writeBuff_->Append("HTTP/1.1 " + std::to_string(101) + " " + "Switching Protocols" + "\r\n") ; 
        writeBuff_->Append("Connection: upgrade\r\n");
        writeBuff_->Append("Upgrade: websocket\r\n") ;
        writeBuff_->Append("Sec-WebSocket-Accept: ") ;
        writeBuff_->Append(WebSocket_key + "\r\n\r\n") ; 
        return true ; 
    }

    STATUS_CODE dealWebSocketRequest(){
        int Errno = -1; 
        ssize_t len = -1;
        do {
            len = readBuff_->ReadFd(fd_, &Errno);   
            if (len <= 0) {
                if(Errno == EWOULDBLOCK ){// 读完了，跳出
                    break ; 
                }else if(Errno == EINTR) {// 被中断了，继续读
                    continue;
                }else { // 对端关闭，或者 read 出错了
                    return CLOSE_CONNECTION ;
                }
            }
        }while (is_ET_) ;
        // 解析 WebSocket 格式信息
        return paresWebSocket() ; 
    }

    void makeWebSocketResponse(std::string &message){
        messageList.push_back(message) ; 
    }

    void makeWebSocketResponse(std::string &&message){
        messageList.push_back(std::move(message)) ; 
    }

    STATUS_CODE dealWebSocketResponse() {  
        // 如果 writeBuff 本来就还有数据，则先写之前的数据，比如握手等
        if(writeBuff_->BufferUsedSize() <= 0){
            std::string message ; 
            if(messageList.tryPop(message) == false) return GOOD_CODE ; 
            writeBuff_->Append(message) ; 
        }
        ssize_t len = -1 ; 
        do{
            len = write(fd_ , writeBuff_->BufferStart() , writeBuff_->BufferUsedSize()) ;   
            if(len < 0){
                if(errno == EWOULDBLOCK) {// fd_缓冲区满了 EWOULDBLOCK 或者 被信号中断了，继续写，继续发送
                    return CONTINUE_CODE ; 
                }else if(errno == EINTR){
                    continue ; 
                } else { // 对端关闭,再写就会触发 SIGPIPE 信号 ，或者 Write 出错了
                    LOG_ERROR("Write FD Error") ;
                    close(); return CLOSE_CONNECTION ;
                }
            }else {
                writeBuff_->Retrieve(len) ; 
                if(writeBuff_->BufferUsedSize() <= 0) { // 传输完成 , 本次 websocket 请求应答结束，故关闭
                    if(messageList.empty()) return GOOD_CODE ; 
                    return CONTINUE_CODE ; // 消息队列中还有数据，要接着写
                } 
            }
        }while(is_ET_) ; 
    }

    std::string makeWebSocketHead(const size_t len) const{
        std::string header ; 
        header.push_back((char) TEXT_FRAME) ; 
        if(len <= 125) {

            header.push_back((char)len);

        } else if(len <= 65535) {

            header.push_back((char)126);//16 bit length follows
            header.push_back((char)((len >> 8) & 0xFF));// leftmost first
            header.push_back((char)(len & 0xFF));

        } else { // >2^16-1 (65535)
        
            header.push_back((char)127); //64 bit length follows
            // write the actual 64bit msg_length in the next 8 bytes
            for(int i=7; i>=0; i--) {
                header.push_back((char)((len >> 8*i) & 0xFF));
            }
        }
        return header ; 
    }

    const std::string &getResponseName() const{
        return responseName;
    }
    
    int GetFd() const {
        return fd_;
    } 

private :
    int fd_ ; 
    int is_ET_ ; 
    std::atomic<bool> is_Close_ ; 
    uint8_t fin_ ;                          // 1 个 bit 位
    uint8_t opcode_ ;                       // 4 个 bit 位
    uint8_t mask_ ;                         // 1 个 bit 位
    uint8_t masking_key_[4] ;               // masking-key 4 个字节
    uint64_t payload_length_ ;              // 最大 7 , 16 , 64 位 
    
    Buffer* readBuff_;                      // 读缓冲区
    Buffer* writeBuff_;                     // 写缓冲区
    std::string responseName ;              // 发送方名字
    atomicList<std::string> messageList ;    // 消息缓冲区
    std::mutex mtx_ ; 

    enum WSFrameType {
        ERROR_FRAME=0xFF00,
        INCOMPLETE_FRAME=0xFE00,

        OPENING_FRAME=0x3300,
        CLOSING_FRAME=0x3400,

        INCOMPLETE_TEXT_FRAME=0x01,
        INCOMPLETE_BINARY_FRAME=0x02,

        TEXT_FRAME=0x81,
        BINARY_FRAME=0x82,

        PING_FRAME=0x19,
        PONG_FRAME=0x1A
    };
    
    STATUS_CODE paresWebSocket() {
        // WebSocket 数据包不完整
        if(readBuff_->BufferUsedSize() < 2){
            LOG_ERROR("Websocket package incomplete") ; 
            return CLOSE_CONNECTION ; 
        }
        const char* strBegin = readBuff_->BufferStart() ; 
        int pos = 0  ;  
        fin_ = ((*(strBegin + pos)) >> 7) & 1 ; 
        opcode_ = (*(strBegin + pos)) & 0x0F;
        ++pos ; 
        mask_ = ((*(strBegin + pos)) >> 7) & 1 ;
        payload_length_ = (*(strBegin + pos)) & 0x7f;
        ++pos ; 
        if(payload_length_ == 126) {
            uint16_t length = 0;
            memcpy(&length, strBegin + pos , 2) ;
            pos += 2;
            payload_length_ = ntohs(length); // 网络字节序，大小端转化
        } else if(payload_length_ == 127) {
            uint32_t length = 0;
            memcpy(&length, strBegin + pos, 4);
            pos += 4;
            payload_length_ = ntohl(length); // 网络字节序，大小端转化
        }
        readBuff_->Retrieve(2) ;
        if(mask_ == 1) {
            if(readBuff_->BufferUsedSize() < 4){
                return BAD_REQUEST ; 
            }
            for(int i = 0 ; i < 4 ; ++i){
                masking_key_[i] = *(strBegin + pos + i) ; 
            }
            pos += 4 ; 
            readBuff_->Retrieve(4) ;
        }

        if(readBuff_->BufferUsedSize() < payload_length_){
            LOG_ERROR("Websocket package incomplete") ; 
            return BAD_REQUEST ; // 数据包不完整
        }

        if(opcode_ == 0x8){
            LOG_INFO("Websocket connect break off") ; 
            return CLOSE_CONNECTION ; // 断开 WeSocket 连接
        }

        std::string content = "" ; 
        strBegin = readBuff_->BufferStart() ; 
        if(mask_ == 0){
            content = std::string(strBegin , payload_length_) ;  
        }else {
            for(int i = 0 ; i < payload_length_ ; ++i){ 
                char ch = strBegin[i] ^ masking_key_[i % 4] ;  
                content.push_back(ch) ; 
            }
        }
        readBuff_->Retrieve(payload_length_) ;

        // 解析客户端发送过来的数据 , json 格式
        picojson::value value_;   
        std::string err = picojson::parse(value_, content);  // 解析JSON字符串，出错时err不为空
        
        if (!err.empty()) {
            LOG_ERROR("picojson parse error %s" , content.data()) ; 
            return CLOSE_CONNECTION ; 
        }
        
        // 从JSON对象中取出 name 和 message 的值 ,并设置推送消息格式
        picojson::object message_json ; 
        responseName = value_.get("toName").to_str() ; 
        message_json["isSystem"] = picojson::value(false) ; 
        message_json["fromName"] = picojson::value(responseName) ; 
        message_json["message"] = picojson::value(value_.get("message").to_str() ) ; 
        // 将 picojson 对象转换为字符串
        std::string resContent = picojson::value(message_json).serialize(); 
        writeBuff_->Append(makeWebSocketHead(resContent.size())) ; 
        if(writeBuff_->Append(resContent) == false){
            LOG_ERROR("writeBuff append response websocket Content") ; 
            return CLOSE_CONNECTION ; 
        }  
        LOG_DEBUG("WebSocket Protocol, FIN %d , OPCODE %d , MASK %d , PAYLOADLEN %d , content : %s"
            , fin_ , opcode_ , mask_ , resContent.size() , value_.get("message").to_str().data()) ;
        return GOOD_CODE ;
    }
} ; 


#endif