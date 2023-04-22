#ifndef HTTP_PROTOCOL_H
#define HTTP_PROTOCOL_H

#include <string>
#include <unordered_map>
#include <fcntl.h>       // open
#include <sys/mman.h>    // mmap, munmap
#include "../Buffer/buffer.h"
#include "../Log/log.h"
#include "../Common/commonConfig.h"
#include "../SqlPool/sqlConnectPool.h"
#include "../JWT/jwt.h"
#include "../Common/picojson.h"

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
    bool is_JWToken_ ;                      // 是否颁发 JWToken 
    std::unique_ptr<JWT> Jwt_;              // JWT

public : 
    HttpProtocol(const int fd , const int isET , Buffer* read , Buffer* write) {
        http_config_ = HttpConfigInfo() ;  
        Jwt_ = std::make_unique<JWT>(http_config_.jwtSecret , http_config_.jwtExpire) ; 
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
        is_JWToken_ = false ; 
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
    
    std::string getUserName() {
       std::string userName = Jwt_->parseJWT(header_["Cookie"] , "name") ;
       return userName ;
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
        for(; i + 1 < len ; ++i) {
            if((*(strBegin + i)) == '\r'  &&  (*(strBegin + i + 1)) == '\n')  break ; 
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
        for(; i + 1 < len ; ++i) {
            if((*(strBegin + i)) == '\r' &&  (*(strBegin + i + 1)) == '\n') {
                break ;
            }
            if((*(strBegin + i)) == ':'){
                ++flag ; ++i ; continue ;  // ++i 这里的作用是除掉: 后面的空格
            }
            if(flag == 0) key.push_back(*(strBegin + i)) ; 
            if(flag == 1) value.push_back(*(strBegin + i)) ;  
        }
        readBuff_->Retrieve(i + 2) ;  // 除了最后的 "\r\n"; header 与 body 之间还有个空行
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
        if(readBuff_->BufferUsedSize() == 0 || header_.find("Content-Length") == header_.end()) return true ; 
        // body 数据包不完整
        if(readBuff_->BufferUsedSize() < stol(header_["Content-Length"])) return false ; 
 
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
        readBuff_->Retrieve(len + 2) ;  
        //LOG_DEBUG("Body:%s, len:%d", std::string(strBegin , len).data() , len);
        return true ;
    }

    bool parseHttpRequest() {
        if(readBuff_->BufferUsedSize() <= 0){
            LOG_ERROR("readBuff_->BufferUsedSize() <= 0") ; 
            return false ;  
        } 
        status_ = REQUEST_LINE ; 
        while(readBuff_->BufferUsedSize() && status_ != FINISH) {
            switch (status_)
            {
            case REQUEST_LINE:
                if(!paresRequestLine()){
                    LOG_ERROR("paresRequestLine Error %s" , readBuff_->BufferStart()) ; 
                    return false ;
                } 
                break;
            case HEADERS :
                if(!paresHeader()){
                    LOG_ERROR("paresHeader Error %s" , readBuff_->BufferStart()) ; 
                    return false ; 
                }
                break ; 
            case BODY :
                if(!ParseBody()){
                    LOG_ERROR("ParseBody Error %s" , readBuff_->BufferStart()) ; 
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
    STATUS_CODE dealHttpRequest(const int needRead = true){ 
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
                        LOG_ERROR("dealHttpRequest %d client already close or read Error!" , fd_) ;
                        return CLOSE_CONNECTION ;
                    }
                }
            }while (is_ET_) ;
        }
        if(parseHttpRequest() == false) {
            LOG_ERROR("server deal parse http request error !!") ; 
            return BAD_REQUEST ; 
        } 
        return GOOD_CODE ;
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
        if(is_JWToken_){
            std::unordered_map<std::string,std::string> keyValue ;
            keyValue["name"] = post_["username"] ; 
            std::string token = Jwt_->generateJWT(keyValue) ; 
            writeBuff_->Append("Set-Cookie: " + token + "\r\n");
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
        bool is_File = true ; 
        std::string json_string ; 
        /* 判断请求的资源文件 */
        if(numStatus == 200){
            // 处理 Post 请求,判断是否是用户登录，颁发 Token 
            if(method_ == "POST" && (path_ == "/login.html" || path_ == "/register.html") ) { 
                is_File = false ; // 返回 json 字符串
                if(UserVerify(post_["username"], post_["password"])) {
                    is_JWToken_ = true ; // 在头部的 Set-Cookie 里放入 Token 
                    json_string = R"({"status":true})"; 
                } else {
                    json_string = R"({"status":false})"; 
                }
            }
            // 处理 Get 请求，聊天界面，要验证 Token 
            if(method_ == "GET" && path_ == "/chat.html") { 
                if(Jwt_->judgeJWT(header_["Cookie"])) {
                    path_ = "/chat.html";
                }else {
                    path_ = "/login.html";
                }
            }
            // 获取用户名，返回 json 内容
            if(method_ == "GET" && path_ == "/getUsername") { 
                is_File = false ; // 返回 json 字符串
                std::string userName = Jwt_->parseJWT(header_["Cookie"] , "name") ; 
                if(userName.size() > 0){
                    json_string = R"({"name":")" + userName + R"("})";  
                }
            }

            if(is_File == true) {
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
        if(is_File == false) {
            writeBuff_->Append("Content-Length: " + std::to_string(json_string.size()) + "\r\n\r\n");
            writeBuff_->Append(json_string) ; 

        }else if(is_File == true && (response_code_ == 400 || AddBody() == false))  {
             
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

    STATUS_CODE dealHttpResponse() {   
        ssize_t len = -1 ; 
        do{
            len = writev(fd_ , write_iov_ , iovCnt_) ;   
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
                return GOOD_CODE ;
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