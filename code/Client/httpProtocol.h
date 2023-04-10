#ifndef HTTP_PROTOCOL_H
#define HTTP_PROTOCOL_H

#include <string>
#include <fcntl.h>       // open
#include <sys/mman.h>    // mmap, munmap
#include "../Buffer/buffer.h"
#include "../Log/log.h"
#include "../Common.h"
#include "../SqlPool/sqlConnectPool.h"

class HttpProtocol{
private : 
    int fd_ ; 
    int is_ET_ ; 
    bool is_Close_ ; 
    int response_code_ ; 
    std::string response_status_ ;  
    struct stat mmFileStat_ ;
    char* mmFile_ ; 
    int iovCnt_ ;
    struct iovec* write_iov_  ;
    Buffer* readBuff_;                      // 读缓冲区
    Buffer* writeBuff_;                     // 写缓冲区
    enum PARSE_STATE {
        REQUEST_LINE,
        HEADERS,
        BODY,
        FINISH,        
    };
    PARSE_STATE status_; // 处理 http 请求分析
    std::string method_ , path_, version_ ;
    std::unordered_map<std::string, std::string> header_;
    std::unordered_map<std::string, std::string> post_;
    HttpConfigInfo http_config_ ; 

public : 
    HttpProtocol(Buffer* read , Buffer* write , const int fd , const int isET) {
        http_config_ = HttpConfigInfo() ;  
        fd_ = fd ; 
        is_ET_ = isET ;
        readBuff_ = read ; 
        writeBuff_ = write ; 
    }

    ~HttpProtocol() {
        close() ; 
    }

    void init(){
        readBuff_->clear() ; 
        writeBuff_->clear() ; 
        is_Close_ = false ;  
        iovCnt_ = 0 ; 
        write_iov_ = nullptr ; 
        mmFile_ = nullptr ; 
        mmFileStat_ = { 0 }; 
        
    }

    void close(){
        if(is_Close_) return ;
        is_Close_ = true ; 
        method_.clear() ; path_.clear() ; version_.clear() ;
        header_.clear() ; post_.clear() ; 
        iovCnt_ = 0 ; 
        if(mmFile_ != nullptr) {
            munmap(mmFile_, mmFileStat_.st_size);
            mmFile_ = nullptr;
        }
        if(write_iov_ != nullptr) {
            delete write_iov_ ; 
            write_iov_ = nullptr ; 
        }
    }

    std::string get_WebSocket_key() const {
        if(header_.find("Sec-WebSocket-Key") != header_.end()){
            return header_.find("Sec-WebSocket-Key")->second ; 
        }
        return "" ;  
    }
    struct iovec* get_write_iovecAddr() const{
        return write_iov_ ; 
    }

    int get_iovec_len() const {
        return iovCnt_ ; 
    }

    bool IsKeepAlive() const {
        if(header_.find("Connection") != header_.end()) {
            return header_.find("Connection")->second == "keep-alive" || version_ >= "HTTP/1.1";
        }
        return false;
    }

    bool isUpgradeWebSocket() const {
        if(header_.find("Connection") != header_.end() && 
            header_.find("Upgrade") != header_.end() && 
            header_.find("Sec-WebSocket-Key") != header_.end() ) {

            return header_.find("Connection")->second == "keep-Upgrade" || 
                    header_.find("Upgrade")->second == "websocket" ; 
        
        }
        return false ; 
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
        const char* strBegin = readBuff_->BufferStart() ; 
        int i = 0 , flag = 0 , len = readBuff_->BufferUsedSize() ;  
        for(; i < len ; ++i) {
            if((*(strBegin + i)) == '\r' && i + 1 < len &&  (*(strBegin + i + 1)) == '\n')  break ; 
            if(*(strBegin + i) == ' '){
                ++flag ; continue ;
            }
            if(flag == 0) method_.push_back(*(strBegin + i)) ; 
            if(flag == 1) path_.push_back(*(strBegin + i)) ; 
            if(flag == 2) version_.push_back(*(strBegin + i)) ; 
        }
        readBuff_->Retrieve(i + 2) ; 
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
        this->status_ = HEADERS ; 
        return true ; 
    }

    bool paresHeader(){
        const char* strBegin = readBuff_->BufferStart() ; 
        int i = 0 , flag = 0 , len = readBuff_->BufferUsedSize() ;  
        std::string key , value ; 
        for(; i < len ; ++i) {
            if((*(strBegin + i)) == '\r' && i + 1 < len &&  (*(strBegin + i + 1)) == '\n') {
                break ;
            }
            if((*(strBegin + i)) == ':'){
                ++flag ; ++i ; continue ;  // ++i 这里的作用是除掉: 后面的空格
            }
            if(flag == 0) key.push_back(*(strBegin + i)) ; 
            if(flag == 1) value.push_back(*(strBegin + i)) ;  
        }
        readBuff_->Retrieve(i + 2) ;  
        if(flag == 0) {
            this->status_ = BODY ; return true ;
        }else if(value.size() == 0){
            return false ;
        }
        header_[key] = value ; 
        LOG_DEBUG("header key:%s; value:%s;" , key.data() , value.data()) ; 
        return true ; 
    }

    bool ParseBody() {
        status_ = FINISH ; 
        if(header_.find("Content-Length") == header_.end()) return true ; 
        // body 数据包不完整
        if(readBuff_->BufferUsedSize() < stol(header_["Content-Length"])) return false ; 

        //body_ = readBuff_->RetrieveAllToStr() ; 
        const char* strBegin = readBuff_->BufferStart() ; 
        size_t len = stol(header_["Content-Length"]) ; 

        // 如果是 urlencoded 则还需要解码步骤
        if(method_ == "POST") {
            // 1. 字符"a"-"z"，"A"-"Z"，"0"-"9"，"."，"-"，"*"，和"_" 都不会被编码;
            // 2. 将空格转换为加号 (+) ;
            // 3. 将非文本内容转换成"%xy"的形式,xy是两位16进制的数值;
            // 4. 在每个 name=value 对之间放置 & 符号。
            std::function<void()> ParseFromUrlencoded = [&](){ 
                
                auto ConverHex = [](const char ch) -> int {
                    if(ch >= 'A' && ch <= 'F') return ch -'A' + 10;
                    if(ch >= 'a' && ch <= 'f') return ch -'a' + 10;
                    return ch - '0' ;
                } ; 
                std::string key, value ;
                for(int i = 0 , flag = 1 ; i <= len ; ++i){ 
                    if(i == len || strBegin[i] == '&' || strBegin[i] == '=') {
                        if(i == len || strBegin[i] == '&'){
                            post_[key] = value ; 
                            LOG_DEBUG("Post key:%s, value:%s", key.data(), value.data());
                            key.clear() ; value.clear() ;
                        }
                        flag *= -1 ; continue ; 
                    } 
                    char ch = *(strBegin + i) ;
                    if(ch == '+'){
                        ch = ' ' ; 
                    }else if(ch == '%'){
                        ch = static_cast<char>(ConverHex(*(strBegin + i + 1)) * 16 + ConverHex(*(strBegin + i + 2))) ; 
                        i += 2 ;
                    } 
                    if(flag == 1) key.push_back(ch) ; 
                    if(flag == -1) value.push_back(ch) ; 
                }
            } ;
            ParseFromUrlencoded() ;
        }
        readBuff_->Retrieve(len) ;  
        LOG_DEBUG("Body:%s, len:%d", std::string(strBegin , len).data() , len);
        return true ;
    }

    bool parseHttpRequest() {
        if(readBuff_->BufferUsedSize() <= 0) return false ;  
        status_ = REQUEST_LINE ; 
        while(readBuff_->BufferUsedSize() && status_ != FINISH) {
            switch (status_)
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
    // 需要根据请求头判断头文件是否读取完毕
    bool dealHttpRequest(const int needRead = true){ 
        if(needRead == true){
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
                        LOG_INFO("client already close or Error!") ;
                        return false ;
                    }
                }
            }while (is_ET_) ;
        }
        return parseHttpRequest() ; 
    }
    
    bool AddStateLine(){
        return writeBuff_->Append("HTTP/1.1 " + std::to_string(response_code_) + " " + response_status_ + "\r\n") ; 
    }

    bool AddHeader(){
        writeBuff_->Append("Connection: ");
        if(IsKeepAlive()) {
            writeBuff_->Append("keep-alive\r\n");
            writeBuff_->Append("keep-alive: max=6, timeout=120\r\n");
        } else{
            writeBuff_->Append("close\r\n");
        }
        return writeBuff_->Append("Content-type: " + GetFileType_() + "\r\n");
    }

    bool AddBody(){
        std::string file_ = http_config_.srcDir + path_ ; 
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
        writeBuff_->Append("Content-Length: " + std::to_string(mmFileStat_.st_size) + "\r\n\r\n");
        this->mmFile_ = dataFile ; 
        return true ;
    }

    bool makeHttpResponse(const int numStatus){
        /* 判断请求的资源文件 */
        if(numStatus == 200){
            // 判断是否是用户登录请求
            if(method_ == "POST" && (path_ == "/login.html" || path_ == "/register.html") ) { 
                if(UserVerify(post_["username"], post_["password"])) {
                    path_ = "/welcome.html";
                } else {
                    path_ = "/error.html";
                }
            }
            std::string filePath = http_config_.srcDir + path_ ;  
            if(stat(filePath.data(), &mmFileStat_) < 0 || S_ISDIR(mmFileStat_.st_mode)) { 
                LOG_WARN("requests file %s Not Found" , filePath.data()) ; 
                response_code_ = 404; // 文件不存在
                response_status_ = "File Not Found" ; 
                path_ = "/404.html"; 
            }else if(!(mmFileStat_.st_mode & S_IROTH)) {
                LOG_WARN("requests file %s Forbidden" , filePath.data()) ; 
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
             
            auto ErrorContent = [&]() -> bool {
                std::string body; 
                body += "<html><title>Error</title>";
                body += "<body bgcolor=\"ffffff\">";
                // response_code_ = 404; // 文件不存在
                // response_status_ = "Not Found" ;  
                body += std::to_string(response_code_) + " : " + response_status_  + "\n";
                body += "<p>" + response_status_ + "</p>";
                body += "<hr><em>TinyWebServer</em></body></html>";
                writeBuff_->Append("Content-Length: " + std::to_string(body.size()) + "\r\n\r\n");
                return writeBuff_->Append(body);
            } ; 

            if(ErrorContent()) {
                LOG_WARN("%s %d server send ErrorContent success!!!" , path_.data() , response_code_) ;  
            }else {
                LOG_ERROR("server add file content error !!!") ;  
                return false ;
            } 
        }
        // 响应头文件内容信息
        if(mmFile_ == nullptr){
            write_iov_ = new iovec[1] ; 
            if(write_iov_ == nullptr) {
                LOG_ERROR("write iov new Fail") ; 
                close() ; return false ;
            }
            write_iov_[0].iov_base = const_cast<char*> (writeBuff_->BufferStart()) ;
            write_iov_[0].iov_len = writeBuff_->BufferUsedSize() ; 
            iovCnt_ = 1 ; 
        }else {
            write_iov_ = new iovec[2] ; 
            if(write_iov_ == nullptr) {
                LOG_ERROR("write iov new Fail") ; 
                close() ; return false ;
            }
            write_iov_[0].iov_base = const_cast<char*> (writeBuff_->BufferStart()) ;
            write_iov_[0].iov_len = writeBuff_->BufferUsedSize() ; 
            /* mmap 文件 */
            write_iov_[1].iov_base = mmFile_ ;
            write_iov_[1].iov_len = mmFileStat_.st_size ;
            iovCnt_ = 2 ;
        }
        return true ; 
    }

    int dealHttpResponse() {   
        ssize_t len = -1 ; 
        do{
            len = writev(fd_ , write_iov_ , iovCnt_) ;   
            if(len < 0){
                if(errno == EWOULDBLOCK) {// fd_缓冲区满了 EWOULDBLOCK 或者 被信号中断了，继续写，继续发送
                    return 2 ; 
                }else if(errno == EINTR){
                    continue ; 
                } else { // 对端关闭,再写就会触发 SIGPIPE 信号 ，或者 Write 出错了
                    LOG_ERROR("Write FD Error") ;
                    close(); return 0 ;
                }
            }else {
                if(static_cast<size_t>(len) > write_iov_[0].iov_len){
                    write_iov_[1].iov_base = (uint8_t*) write_iov_[1].iov_base + (len - write_iov_[0].iov_len);
                    write_iov_[1].iov_len -= (len - write_iov_[0].iov_len);
                    write_iov_[0].iov_base = write_iov_[1].iov_base ; 
                    write_iov_[0].iov_len = write_iov_[1].iov_len ;
                    writeBuff_->clear(); --iovCnt_ ; 
                } else {
                    write_iov_[0].iov_base = (uint8_t*)write_iov_[0].iov_base + len; 
                    write_iov_[0].iov_len -= len; 
                }
            }
            if((write_iov_[0].iov_len <= 0)) { // 传输完成 , 本次 http 请求应答结束，故关闭
                return 1 ;
            } 
        }while(is_ET_) ; 
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
} ; 

#endif