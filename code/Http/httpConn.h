#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

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

class HttpConn {
private:
   
    int fd_; 
    struct  sockaddr_in addr_;
    bool isET_ ; 
    int response_code_ ; 
    std::string response_status_ ; 
    const char *srcDir_ ; 
    struct stat mmFileStat_;
    char* mmFile_; 
    int iovCnt_;
    struct iovec iov_[2];
    Buffer readBuff_; // 读缓冲区
    Buffer writeBuff_; // 写缓冲区

    enum PARSE_STATE {
        REQUEST_LINE,
        HEADERS,
        BODY,
        FINISH,        
    };
    PARSE_STATE state_; // 处理 http 请求分析
    
    std::string method_ , path_, version_, body_;
    std::unordered_map<std::string, std::string> header_;
    std::unordered_map<std::string, std::string> post_;
    HttpConfigInfo http_config_ ; 
public : 

    HttpConn() {
        fd_ = -1 ; 
        response_code_ = -1 ; 
        mmFile_ = nullptr ;
        mmFileStat_ = { 0 };
        addr_ = { 0 };
        http_config_ = HttpConfigInfo() ;  
    }

    ~HttpConn() {
        Close() ; 
    } 
    
    void init(int fd , const char* srcDir , const sockaddr_in& addr , const bool isET = true){ 
        isET_ = isET ; 
        fd_ = fd ; 
        addr_ = addr;
        srcDir_ = srcDir ; 
        readBuff_.clear() ; 
        writeBuff_.clear() ; 
    }

    void Close() {
        close(fd_);  
        if(mmFile_) {
            munmap(mmFile_, mmFileStat_.st_size);
            mmFile_ = nullptr;
        } 
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

    bool IsKeepAlive() const {
        if(header_.count("Connection") == 1) {
            return header_.find("Connection")->second == "keep-alive" || version_ >= "HTTP/1.1";
        }
        return false;
    }

    std::string GetFileType_() {
        /* 判断文件类型 */
        std::string::size_type idx = path_.find_last_of('.');
        if(idx == std::string::npos) {
            return "text/plain";
        }
        std::string suffix = path_.substr(idx);
        if(http_config_.SUFFIX_TYPE.count(suffix) == 1) {
            return http_config_.SUFFIX_TYPE.find(suffix)->second;
        }
        return "text/plain";
    }
    
    bool paresRequestLine(){
        method_.clear() ; path_.clear() ; version_.clear() ;
        const char* strBegin = readBuff_.BufferStart() ; 
        int i = 0 , flag = 0 ; 
        for(; i < readBuff_.BufferUsedSize() ; ++i) {
            if(*(strBegin + i) == '\r' && *(strBegin + i + 1) == '\n') break ;
            if(*(strBegin + i) == ' '){
                ++flag ; continue ;
            }
            if(flag == 0) method_.push_back(*(strBegin + i)) ; 
            if(flag == 1) path_.push_back(*(strBegin + i)) ; 
            if(flag == 2) version_.push_back(*(strBegin + i)) ; 
        }
         
        readBuff_.Retrieve(i + 2) ;
        if(flag != 2) return false ;
        LOG_DEBUG("method: %s; path : %s; version: %s;" , method_.data() , path_.data() , version_.data()) ; 
        // 路径进一步处理
        if(path_ == "/") {
            path_ = "/index.html" ; 
        }else {
            if(http_config_.DEFAULT_HTML.count(path_) != 0){
                path_ = path_ + ".html" ; 
            } 
        }
        this->state_ = HEADERS ; 
        return true ; 
    }

    bool paresHeader(){
        const char* strBegin = readBuff_.BufferStart() ; 
        int i = 0 , flag = 0 ; 
        std::string key , value ; 
        for(; i < readBuff_.BufferUsedSize() ; ++i) {
            if((*(strBegin + i)) == '\r' && (*(strBegin + i + 1)) == '\n') {
                break ;
            }
            if((*(strBegin + i)) == ':'){
                ++flag ; ++i ; continue ;  // ++i 这里的作用是除掉: 后面的空格
            }
            if(flag == 0) key.push_back(*(strBegin + i)) ; 
            if(flag == 1) value.push_back(*(strBegin + i)) ;  
        }
        readBuff_.Retrieve(i + 2) ;
        if(flag == 0) {
            state_ = BODY ; return true ;
        }else if(key.size() == 0 || value.size() == 0){
            return false ;
        }
        header_[key] = value ; 
        LOG_DEBUG("header key:%s; value:%s;" , key.data() , value.data()) ; 
        return true ; 
    }

    
    // 用户认证
    bool UserVerify(const std::string &name , const std::string &pwd) {
        if(name.size() <= 0 || pwd.size() <= 0 || name.size() > 20 || pwd.size() > 32) return false ; 
        
        SqlConnnectPool& SqlPool = SqlConnnectPool::Instance() ;  
        std::pair<MYSQL* , int> sql = SqlPool.getSqlconnect() ; 
        std::string password = SqlConnnectPool::queryPwd(name , sql) ; 
        // 用户不存在，则默认注册
        if(password.size() == 0){
            password = SqlConnnectPool::insertUser(name , pwd , sql) ;             
        }
        SqlPool.freeSqlconnect(sql) ; 
        if(password == pwd){
            LOG_DEBUG("name: %s , pwd: %s user verify pass" , name.data() , pwd.data()) ; 
            return true ; 
        } 
        LOG_DEBUG("name: %s , pwd: %s user verify fail" , name.data() , pwd.data()) ; 
        return false ;
    }

    bool ParseBody(){
        state_ = FINISH ; 
        if(readBuff_.BufferRemainSize() <= 0) return true ; 
        body_ = readBuff_.RetrieveAllToStr() ; 
        // 如果是 urlencoded 则还需要解码步骤
        if(method_ == "POST" && header_["Content-Type"] == "application/x-www-form-urlencoded") {
            // 1. 字符"a"-"z"，"A"-"Z"，"0"-"9"，"."，"-"，"*"，和"_" 都不会被编码;
            // 2. 将空格转换为加号 (+) ;
            // 3. 将非文本内容转换成"%xy"的形式,xy是两位16进制的数值;
            // 4. 在每个 name=value 对之间放置 & 符号。
            std::function<void()> ParseFromUrlencoded = [&](){
                if(body_.size() == 0) { return ; }
                
                auto ConverHex = [](const char ch) -> int {
                    if(ch >= 'A' && ch <= 'F') return ch -'A' + 10;
                    if(ch >= 'a' && ch <= 'f') return ch -'a' + 10;
                    return ch - '0' ;
                } ; 
                std::string key, value ;
                for(int i = 0 , flag = 1 ; i <= body_.size() ; ++i){ 
                    if(i == body_.size() || body_[i] == '&' || body_[i] == '=') {
                        if(i == body_.size() || body_[i] == '&'){
                            post_[key] = value ; 
                            LOG_DEBUG("Post key:%s, value:%s", key.data(), value.data());
                            key.clear() ; value.clear() ;
                        }
                        flag *= -1 ; continue ; 
                    } 
                    char ch = body_[i] ;
                    if(ch == '+'){
                        ch = ' ' ; 
                    }else if(ch == '%'){
                        ch = static_cast<char>(ConverHex(body_[i + 1]) * 16 + ConverHex(body_[i + 2])) ; 
                        i += 2 ;
                    } 
                    if(flag == 1) key.push_back(ch) ; 
                    if(flag == -1) value.push_back(ch) ; 
                }
            } ;
            ParseFromUrlencoded() ;
            if(path_ == "/register.html" || path_ == "/login.html") {
               // bool isLogin = (tag == 1) ;
                if(UserVerify(post_["username"], post_["password"])) {
                    path_ = "/welcome.html";
                } else {
                    path_ = "/error.html";
                }
            }
        }
        LOG_DEBUG("Body:%s, len:%d", body_.data(), body_.size());
        return true ;
    }

    bool parseHttp(){
        
        if(readBuff_.BufferUsedSize() <= 0) return false ;  
        while(readBuff_.BufferUsedSize() && state_ != FINISH) {
            switch (state_)
            {
            case REQUEST_LINE:
                if(!paresRequestLine()){
                    return false ;
                } 
                break;
            case HEADERS :
                if(!paresHeader()){
                    return false ; 
                }
                break ; 
            case BODY :
                if(!ParseBody()){
                    return false ; 
                } 
                break ;
            default:
                break;
            }
        }
        return true ;
    }

    bool ErrorContent(Buffer& buff)  {
        std::string body; 
        body += "<html><title>Error</title>";
        body += "<body bgcolor=\"ffffff\">";
        // response_code_ = 404; // 文件不存在
        // response_status_ = "Not Found" ;  
        body += std::to_string(response_code_) + " : " + response_status_  + "\n";
        body += "<p>" + response_status_ + "</p>";
        body += "<hr><em>TinyWebServer</em></body></html>";
        buff.Append("Content-length: " + std::to_string(body.size()) + "\r\n\r\n");
        return buff.Append(body);
    }

    bool AddStateLine(){
        return writeBuff_.Append("HTTP/1.1 " + std::to_string(response_code_) + " " + response_status_ + "\r\n") ; 
    }

    bool AddHeader(){
        writeBuff_.Append("Connection: ");
        if(IsKeepAlive()) {
            writeBuff_.Append("keep-alive\r\n");
            writeBuff_.Append("keep-alive: max=6, timeout=120\r\n");
        } else{
            writeBuff_.Append("close\r\n");
        }
        return writeBuff_.Append("Content-type: " + GetFileType_() + "\r\n");
    }

    bool AddBody(){
        std::string file_ = srcDir_ + path_ ; 
        bool file_exist = stat(file_.data() , &mmFileStat_) == 0 ;
        if(!file_exist || S_ISDIR(mmFileStat_.st_mode)) { 
            LOG_ERROR("file %s not exist!!!" , file_.data()); 
            return false ;
        }

        auto close_func = [](int* fd) {
            if (fd) {
                ::close(*fd);
                delete fd ;
                fd = nullptr ; 
            }
        };
        std::unique_ptr<int , decltype(close_func) > fd(new int(open(file_.data(), O_RDONLY)) , close_func);
        if (*fd == -1) {
            LOG_ERROR("open file %s error !!!" ,  file_.data()); 
            return false ; 
        }

        char *dataFile = reinterpret_cast<char*>(mmap(nullptr, mmFileStat_.st_size, PROT_READ, MAP_PRIVATE , *fd , 0)) ;

        if(dataFile  == MAP_FAILED) {
            munmap(dataFile, mmFileStat_.st_size); dataFile = nullptr ; 
            LOG_ERROR("mmap file %s error %d !!!" ,  file_.data() , errno); 
            return false ; 
        }
        writeBuff_.Append("Content-length: " + std::to_string(mmFileStat_.st_size) + "\r\n\r\n");
        this->mmFile_ = dataFile ; 
        return true ;
    }

    bool makeResponse(int numStatus){
        /* 判断请求的资源文件 */
        if(numStatus == 200){
            if(stat((srcDir_ + path_).data(), &mmFileStat_) < 0 || S_ISDIR(mmFileStat_.st_mode)) { 
                LOG_WARN("requests file %s Not Found" , (srcDir_ + path_).data()) ; 
                response_code_ = 404; // 文件不存在
                response_status_ = "File Not Found" ; 
                path_ = "/404.html"; 
            }else if(!(mmFileStat_.st_mode & S_IROTH)) {
                LOG_WARN("requests file %s Forbidden" , (srcDir_ + path_).data()) ; 
                response_code_ = 403; // 没有权限访问
                response_status_ = "Forbidden" ; 
                path_ = "/403.html"; 
            }else { 
                response_code_ = 200; // 成功 
                response_status_ = "OK" ; 
            }
        }else {
            response_code_ = 400; // 客户端请求错误
            response_status_ = "Bad Request" ; 
        }
        
        if(AddStateLine() == false ){
            LOG_ERROR("server add state line error !!!") ; 
            return false ; 
        } 
        if(AddHeader() == false){
            LOG_ERROR("server add header error !!!") ; 
            return false ; 
        }
        if(response_code_ == 400 || AddBody() == false) {
            if(ErrorContent(writeBuff_)) {
                LOG_WARN("%s %d server send ErrorContent success!!!" , path_.data() , response_code_) ;  
            }else {
                LOG_ERROR("server add file content error !!!") ;  
                return false ;
            }
            // 不直接返回，因为 false 的时候会添加 ErrorContent 保底
        }
        // 响应头
        iov_[0].iov_base = const_cast<char*> (writeBuff_.BufferStart()) ;
        iov_[0].iov_len = writeBuff_.BufferUsedSize() ; 
        iovCnt_ = 1; 
        /* 文件 */
        if(mmFile_ != nullptr) {
            iov_[1].iov_base = mmFile_ ;
            iov_[1].iov_len = mmFileStat_.st_size ;
            iovCnt_ = 2;
        }
        
        return true ; 
    }
    
    bool dealHttpRequest() {
        int Errno = -1; 
        ssize_t len = -1;
        do {
            len = readBuff_.ReadFd(fd_, &Errno);  
            if (len <= 0) {
                if(Errno == EWOULDBLOCK ){// 读完了，跳出
                    break ; 
                }else if(Errno == EINTR) {// 被中断了，继续读
                    continue;
                }else { // 对端关闭，或者 read 出错了
                    LOG_ERROR("ReadRd Error") ;
                    return false ;
                }
            }
        }while (isET_) ;
        
        this->state_ = REQUEST_LINE ; 
        bool ret = parseHttp() ; // 处理 Http 请求的信息
        // 成功处理完毕，发送 Http 响应信息 
        if(ret == true){
           return makeResponse(200) ; 
        }// http 解析错误，客户端请求报文错误
        return makeResponse(400) ;   
    }

    bool dealHttpResponse() {
        // std::cout<<"file :"<<path_<<" size = "<<mmFileStat_.st_size<<std::endl ;
        ssize_t len = -1 ; 
        do{
            len = writev(fd_ , iov_ , iovCnt_) ;  
            // std::cout<<"Write : "<<len<<" "<<errno<<" "<<iov_[0].iov_len<<std::endl ;
            if(len < 0){
                if(errno == EWOULDBLOCK || errno == EINTR) {// EWOULDBLOCK 或者 被中断了，继续读
                    continue;
                }else { // 对端关闭,再写就会触发 SIGPIPE 信号 ，或者 Write 出错了
                    LOG_ERROR("Write FD Error") ;
                    return false ;
                }
            }else {
                if(static_cast<size_t>(len) > iov_[0].iov_len){
                    iov_[1].iov_base = (uint8_t*) iov_[1].iov_base + (len - iov_[0].iov_len);
                    iov_[1].iov_len -= (len - iov_[0].iov_len);
                    iov_[0].iov_base = iov_[1].iov_base ; 
                    iov_[0].iov_len = iov_[1].iov_len ;
                    writeBuff_.clear(); --iovCnt_ ; 
                } else {
                    iov_[0].iov_base = (uint8_t*)iov_[0].iov_base + len; 
                    iov_[0].iov_len -= len; 
                }
            }
            if((iov_[0].iov_len <= 0)) { // 传输完成
                return true ; 
            } 
        }while(isET_) ;
        return true ;
    }
} ; 

#endif