#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <string>
#include <arpa/inet.h>    //for ntohl
#include "base64.h"
#include "sha1.h"
#include "../Buffer/buffer.h"
#include "../Log/log.h"
#include "../Common.h"

#define MAGIC_KEY "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
class WebSocket{
public : 
    WebSocket(Buffer* read , Buffer* write , const int fd , const int isET) {
        fd_ = fd ; 
        is_ET_ = isET ;
        readBuff_ = read ; 
        writeBuff_ = write ; 
    }

    ~WebSocket() {
        close() ; 
    }
    
    void init(){
        is_Close_ = false ;   
        fin_ = 0 ; opcode_ = 0 ; mask_ = 0 ; payload_length_ = 0 ; 
        memset(masking_key_ , 0 , sizeof(masking_key_)) ;
    }

    void close(){
        if(is_Close_) return ;
        is_Close_ = true ;
    }
    
    // 获得握手需要的报文信息
    bool handshark(std::string WebSocket_key) {
        WebSocket_key += MAGIC_KEY ; // 

        SHA1 sha1 ; sha1.Reset() ; 
        sha1 << WebSocket_key.data() ; 
        unsigned int message_digest[5] ;
        sha1.Result(message_digest);
        for (int i = 0; i < 5; i++) {
            message_digest[i] = htonl(message_digest[i]);
        }
        WebSocket_key = base64_encode(reinterpret_cast<const unsigned char*>(message_digest),20);
        
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
                    LOG_INFO("client already close or read Error!") ;
                    return CLOSE_CONNECTION ;
                }
            }
        }while (is_ET_) ;
        // 解析 WebSocket 格式信息
        return paresWebSocket() ; 
    }

    STATUS_CODE dealWebSocketResponse() {
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
                if(writeBuff_->BufferUsedSize() <= 0) { // 传输完成 , 本次 http 请求应答结束，故关闭
                    return GOOD_CODE ;
                } 
            }
        }while(is_ET_) ; 
    }

private :
    int fd_ ; 
    int is_ET_ ; 
    bool is_Close_ ; 
    uint8_t fin_ ;                          // 1 个 bit 位
    uint8_t opcode_ ;                       // 4 个 bit 位
    uint8_t mask_ ;                         // 1 个 bit 位
    uint8_t masking_key_[4] ;               // masking-key 4 个字节
    uint64_t payload_length_ ;              // 最大 7 , 16 , 64 位 
    
    Buffer* readBuff_;                      // 读缓冲区
    Buffer* writeBuff_;                     // 写缓冲区
      
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

    bool makeWebSocketHead(const uint64_t len){
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
        return writeBuff_->Append(header) ; 
    }
    
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

        if(makeWebSocketHead(payload_length_) == false){
            LOG_ERROR("make send Websocket package head error") ; 
            return CLOSE_CONNECTION ; 
        }
        int head_len = writeBuff_->BufferUsedSize() ; 
        strBegin = readBuff_->BufferStart() ; 
        if(mask_ == 0){
            writeBuff_->Append(strBegin , payload_length_) ; 
        }else {
            for(int i = 0 ; i < payload_length_ ; ++i){ 
                char ch = strBegin[i] ^ masking_key_[i % 4] ;  
                writeBuff_->Append(&ch , 1) ;
            }
        }
        readBuff_->Retrieve(payload_length_) ;
        
        LOG_DEBUG("WebSocket Protocol, FIN %d , OPCODE %d , MASK %d , PAYLOADLEN %d , content : %s"
            , fin_ , opcode_ , mask_ , payload_length_ , std::string(writeBuff_->BufferStart() + head_len , writeBuff_->BufferUsedSize() - head_len).data() ) ;
        
        return GOOD_CODE ;
    }
} ; 


#endif