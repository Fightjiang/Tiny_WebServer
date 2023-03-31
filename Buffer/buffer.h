#ifndef BUFFER_H
#define BUFFER_H

#include <iostream>
#include <string>
#include <atomic> 
#include <assert.h>
#include <unistd.h>  // write
#include <sys/uio.h> //readv
#include <string.h>
#include <memory>
// 线程不安全 Buffer
class Buffer {
public:
    Buffer(int initBuffSize = 256) : buffer_(std::make_unique<char[]>(initBuffSize)) , startPos_(0) , endPos_(0) , bufferLen(initBuffSize) {
         
    } 
    ~Buffer() = default;

    const char* BufferStart() const {
        return buffer_.get() + startPos_ ; 
    }

    char* BufferEnd() const {
        return buffer_.get() + endPos_;
    }

    const size_t BufferUsedSize() const {
        return this->endPos_ - this->startPos_ ; 
    }

    const size_t BufferRemainSize() const{
        return this->bufferLen - this->endPos_ ;
    }

    bool Append(const std::string& str) {
        return Append(str.data(), str.length());
    }

    bool Append(const void* data, size_t len){
        assert(data != nullptr);
        return Append(static_cast<const char*>(data), len);
    }

    bool Append(const Buffer& buff){
        return Append(buff.BufferStart(), buff.BufferUsedSize());
    }
   
    bool Append(const char* str, size_t len){
        assert(str != nullptr);
        // 不够，则需要扩容，指数扩容
        if(this->endPos_ + len >= this->bufferLen){
            // 总容量是够的，则重新整理 buffer 空间
            size_t buffer_used_size = BufferUsedSize() ; 
            if(buffer_used_size + len < this->bufferLen) {
                memcpy(this->buffer_.get() , this->buffer_.get() + this->startPos_ , buffer_used_size) ; 
                memcpy(this->buffer_.get() + buffer_used_size , str , len) ; 
                this->startPos_ = 0 ; this->endPos_ = buffer_used_size + len; 
            }else { // 幂增扩容方式
                while(this->bufferLen < len){
                    this->bufferLen = this->bufferLen * 2 ; 
                }
                std::unique_ptr<char[]> tmpBuffer = std::make_unique<char[]>(this->bufferLen)  ;
                memcpy(tmpBuffer.get() , this->buffer_.get() + this->startPos_ , buffer_used_size) ; 
                memcpy(tmpBuffer.get() + buffer_used_size , str , len) ; 
                this->buffer_ = std::move(tmpBuffer) ;
                this->startPos_ = 0 ; this->endPos_ = buffer_used_size + len; 
            }
        } else {
            // buffer 充足，则直接在后面添加
            memcpy(BufferEnd() , str , len) ; 
            this->endPos_ += len ; 
        }
        return true ;
    }
    // ssize_t 和 size_t 分别是：long int ; unsigned long int ; 
    ssize_t WriteFd(int fd , int *saveErrno){
        size_t buffer_used_size = BufferUsedSize() ; 
        ssize_t len = write(fd , BufferStart() , buffer_used_size) ; 
        if(len < 0) {
            *saveErrno = errno ; 
            return len ; 
        }
        this->startPos_ += len ; 
        return len ;
    }

    ssize_t ReadFd(int fd , int *saveErrno){
        char* buff = new char[128 * 1024]; // 为什么设置怎么大，因为本环境下套接字缓冲区的默认大小为: 128 * 1024 
        struct iovec iov[2];
        const size_t writable = BufferRemainSize();

        /* 分散读， 保证数据全部读完 */
        iov[0].iov_base = BufferEnd();
        iov[0].iov_len = writable;
        iov[1].iov_base = buff;
        iov[1].iov_len = sizeof(buff);

        const ssize_t len = readv(fd, iov, 2);
        if(len < 0) {
            *saveErrno = errno;
        } else if(static_cast<size_t>(len) <= writable) {
            this->endPos_ += len;
        } else {
            this->endPos_ += writable ; 
            Append(buff, len - writable);
        }
        return len;
    }

    void Retrieve(size_t len){
        assert(len <= BufferUsedSize()) ; 
        this->startPos_ += len ;
    }

    void RetrieveUntil(const char* end) {
        assert(BufferStart() <= end) ; 
        Retrieve(end - BufferStart()) ; 
    }

    void RetrieveAll() { 
        memset(this->buffer_.get() , '\0' , this->bufferLen) ; 
        this->startPos_ = 0;
        this->endPos_   = 0;
    }

    std::string RetrieveAllToStr() {
        std::string str(BufferStart(), BufferUsedSize());
        RetrieveAll();
        return str;
    }

private:
    
    std::unique_ptr<char[]> buffer_   ;
    std::atomic<std::size_t> startPos_;
    std::atomic<std::size_t> endPos_  ;
    std::atomic<std::size_t> bufferLen ; 
} ; 
#endif //BUFFER_H