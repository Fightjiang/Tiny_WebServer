#ifndef SQL_CONNECTION_POOL
#define SQL_CONNECTION_POOL

#include <mysql/mysql.h>
#include <queue>
#include <string>
#include <semaphore.h> 
#include "../Log/log.h"
#include "../Common.h"

// 主连接不能用队列，直接用 vector , 再加一个hashmap visit 来判断当前 SQL 是否在使用
// 辅助连接采用队列,
class SqlConnnectPool {
private :
    size_t secondary_Conn_ ;                                             // 辅助自动扩容连接数
    bool is_monitor_ ;                                                   // 是否开启监控线程
    std::atomic<int> is_close_ ;                                                     // 是否关闭连接池，关闭了就不再允许取连接池了
    std::condition_variable condition_close_ ;                           // 关闭条件变量用于等待其他 SQL 用完放回队列中再统一回收
    std::condition_variable condition_getSQL_  ;                      // 读取条件变量用于队列的生产者消费者模型同步
    ConfigInfo config_ ;                                       // 线程池配置信息
    std::queue<std::pair<MYSQL* , int>> primary_sqlConnectPool_ ;       // SQL 主连接池
    std::queue<std::pair<MYSQL* , int>> secondary_sqlConnectPool_ ;     // SQL 辅助连接池
    std::mutex mtx_ ;                                                    // 互斥锁
    std::thread monitor_thread_ ;                                        // 监控线程(用于自动扩缩容机制)

private : 
    
    explicit SqlConnnectPool( const bool autoInit = true) noexcept : 
        secondary_Conn_(0) , is_close_(false) , is_monitor_(true) {
        
        config_ = ConfigInfo() ; 
        if(autoInit){
            if(this->init() == false){
                LOG_ERROR("Sql Pool Create Fail !!!") ;
            } else {
                LOG_INFO("Sql Pool Create Success") ;
                // 创建监控线程
                monitor_thread_ = std::move(std::thread(&SqlConnnectPool::monitor , this)) ; 
            }
        }
    } 

    ~SqlConnnectPool() {
        this->ClosePool() ;
    }

public : 
    static SqlConnnectPool& Instance() {
        static SqlConnnectPool inst ; 
        return inst ; 
    }

    bool init(){
        assert(is_close_ == false) ; 
        for(int i = 0 ; i < config_.default_thread_size_ ; ++i){
            MYSQL *sql = nullptr ; 
            sql = mysql_init(sql) ; 
            if(sql == nullptr){
                LOG_ERROR("Mysql init error !!!") ; 
                return false ; 
            }
            sql = mysql_real_connect(sql, config_.mysql_host,
                                      config_.mysql_user, config_.mysql_pwd,
                                      config_.mysql_dbName, config_.mysql_port, nullptr, 0) ; 
            
            if (sql == nullptr) {
                LOG_ERROR("MySql Connect error!");
                return false ; 
            }
            primary_sqlConnectPool_.emplace(sql , TYPE_PRIMARY) ;
        }
        return true ;
    }
    
    void ClosePool(){
        // 先关闭监控线程 , 不再创建和回收辅助线程，交给主线程来统一回收处理
        this->is_monitor_ = false ; 
        if(this->monitor_thread_.joinable()){
            monitor_thread_.join() ; 
        }
        this->is_close_ = true ; // 不再分配数据库连接
        // 等待其他连接使用完
        std::unique_lock<std::mutex> locker(mtx_) ; 
        condition_close_.wait(locker , [&]{
            return primary_sqlConnectPool_.size() == config_.default_thread_size_
                    && secondary_sqlConnectPool_.size() == this->secondary_Conn_ ; 
        }) ; 
        while(!primary_sqlConnectPool_.empty()){
            MYSQL *sql = primary_sqlConnectPool_.front().first ; 
            primary_sqlConnectPool_.pop() ; 
            mysql_close(sql) ; 
        } 
        while(!secondary_sqlConnectPool_.empty()){
            MYSQL *sql = secondary_sqlConnectPool_.front().first ; 
            secondary_sqlConnectPool_.pop() ; 
            mysql_close(sql) ; 
        }
        mysql_library_end() ;  
        LOG_INFO("SQL Pool Close over") ; 
    }

    // 监控线程调用 Create
    bool createSecondarySqlConnect(){
        MYSQL *sql = nullptr ; 
        sql = mysql_init(sql) ; 
        if(sql == nullptr){
            LOG_ERROR("Mysql init error !!!") ; 
            return false ; 
        }
        sql = mysql_real_connect(sql, config_.mysql_host,
                                 config_.mysql_user, config_.mysql_pwd,
                                 config_.mysql_dbName, config_.mysql_port,
                                 nullptr, 0) ;

        if (!sql) {
            LOG_ERROR("MySql Connect error!");
            return false ; 
        }
        std::unique_lock<std::mutex> locker(mtx_) ; 
        secondary_sqlConnectPool_.emplace(std::move(sql) , TYPE_SECONDARY) ; 
        condition_getSQL_.notify_all() ; 
        return true ;
    }

    // 监控线程调用 free
    bool closeOneSqlConnect(){
        std::unique_lock<std::mutex> locker(mtx_) ; 
        if(!secondary_sqlConnectPool_.empty()){
            MYSQL *sql = secondary_sqlConnectPool_.front().first ; 
            secondary_sqlConnectPool_.pop() ; 
            if(sql == nullptr) return false ;
            mysql_close(sql) ; 
        }
        return true ;
    }

    // 客户端获得连接
    std::pair<MYSQL* , int> getSqlconnect(){
        if(is_close_ == true) return std::make_pair(nullptr , 0) ;
        std::unique_lock<std::mutex> locker(mtx_) ;
        condition_getSQL_.wait(locker , [&]{
            return primary_sqlConnectPool_.size() > 0 
                   || secondary_sqlConnectPool_.size() > 0 ; 
        }) ; 
        std::pair<MYSQL* , int> sql ;  
        if(!primary_sqlConnectPool_.empty()) {
            sql = primary_sqlConnectPool_.front() ; primary_sqlConnectPool_.pop() ; 
        }else if(!secondary_sqlConnectPool_.empty()) {
            sql = secondary_sqlConnectPool_.front() ; secondary_sqlConnectPool_.pop() ; 
        }
        return sql ; 
    }

    // 客户端释放连接，需要唤醒阻塞在 GetSqlconnect 上的连接，或者是阻塞在要关闭了的函数
    bool freeSqlconnect(std::pair<MYSQL* , int> sql){
        if(sql.first == nullptr){
            LOG_ERROR("freeSqlconnect sql is nullptr") ; 
            return false ;
        }
        std::unique_lock<std::mutex> locker(mtx_) ; 
        if(sql.second == TYPE_PRIMARY) {
            primary_sqlConnectPool_.emplace(sql) ; 
        }else {
            secondary_sqlConnectPool_.emplace(sql) ; 
        }
        if(is_close_ == false){
            condition_getSQL_.notify_all() ; 
        }else {
            condition_close_.notify_all() ; 
        }
    }
    bool queueEmpty(std::queue<std::pair<MYSQL* , int>> &que){
        std::lock_guard<std::mutex> locker(mtx_) ; 
        return que.empty() ; 
    }
    void monitor() {
        int secondary_ttl = config_.secondary_thread_ttl_ ; // 回收辅助线程的时间 ttl * span
        while(is_monitor_){
             
            // 监控线程的执行间隔时间
            int span = config_.monitor_span_ ; 
            while(is_monitor_ && span--){
                SLEEP_SECOND(1) ; 
            }
            
            // 如果 primary 线程都在执行，则表示忙碌
            
            if(queueEmpty(primary_sqlConnectPool_) && queueEmpty(secondary_sqlConnectPool_)) {    
                if(config_.default_thread_size_ + this->secondary_Conn_ < config_.max_thread_size_){
                    if(createSecondarySqlConnect() == false){
                        LOG_ERROR("create One Secondary SqlConnect Fail !!!") ; 
                    }else {
                        this->secondary_Conn_++ ; 
                        LOG_INFO("create One Secondary SqlConnect Success") ; 
                    } 
                }
            }

            // 如果辅助线程在空闲，并且主线程也在空闲
            if(queueEmpty(secondary_sqlConnectPool_) == false && queueEmpty(primary_sqlConnectPool_) == false) { 
                secondary_ttl = std::max(0 , secondary_ttl - 1) ; 
            }else {
                secondary_ttl = std::min(config_.secondary_thread_ttl_ , secondary_ttl + 1) ; 
            }

            // 如果辅助线程池中一直有线程在空闲，则回收一个连接 
            if(secondary_ttl == 0 && queueEmpty(secondary_sqlConnectPool_) == false){ 
                if(closeOneSqlConnect()){
                    LOG_INFO("close One Secondary SqlConnect Success") ; 
                    this->secondary_Conn_-- ;
                    secondary_ttl = config_.secondary_thread_ttl_ ;        
                }
            } 
        }
    }

    static std::string queryPwd(const std::string &name , const std::pair<MYSQL* , int>& sql){
        
        const char* query = "SELECT password FROM user_info WHERE username = ?" ;
        MYSQL *sqlconn = sql.first ; 
        // 准备参数化查询
        MYSQL_STMT *stmt = mysql_stmt_init(sqlconn) ; 
        if (!stmt || mysql_stmt_prepare(stmt, query, strlen(query)) ) {
            LOG_ERROR("Failed to prepare MySQL statement: %s", mysql_error(sqlconn)) ;
            mysql_stmt_close(stmt);
            return "" ; 
        }
        
        // 绑定参数
        MYSQL_BIND param[1];  
        memset(param, 0, sizeof(param)); 
        param[0].buffer_type = MYSQL_TYPE_STRING;
        param[0].buffer = const_cast<char *>(name.c_str()) ;
        param[0].buffer_length = name.size() ;
        
        if (mysql_stmt_bind_param(stmt, param) != 0) {
            LOG_ERROR("Failed to bind MySQL statement parameters: %s" , mysql_stmt_error(stmt));
            mysql_stmt_close(stmt); 
            return "" ; 
        }

        // 执行查询
        if (mysql_stmt_execute(stmt) != 0) {
            LOG_ERROR("Failed to execute MySQL statement: %s" , mysql_stmt_error(stmt));
            mysql_stmt_close(stmt); 
            return "" ; 
        }
        
        // 处理结果 
        char password[50];
        MYSQL_BIND result[1];  
        memset(result, 0, sizeof(result)); 
        result[0].buffer_type = MYSQL_TYPE_STRING;
        result[0].buffer = password;
        result[0].buffer_length = sizeof(password);

        if (mysql_stmt_bind_result(stmt, result) != 0) {
            LOG_ERROR("Failed to bind MySQL statement results: %s" , mysql_stmt_error(stmt));
            mysql_stmt_close(stmt);
            return "" ; 
        }

        if (mysql_stmt_fetch(stmt) != 0) {
            mysql_stmt_close(stmt);
            LOG_INFO("name isn't exists %s" , name.c_str());
            return "" ; 
        } 
        mysql_stmt_close(stmt); 
        return password ; 
    }
    
    static std::string insertUser(const std::string &name , const std::string &pwd , const std::pair<MYSQL* , int>& sql){
        if(name.size() <= 0 || pwd.size() <= 6 || name.size() > 20 || pwd.size() > 32){
            LOG_ERROR("name or password haven't been met: %s %s" , name.data() , pwd.data()) ;
            return "" ; 
        } 
        const char* insert = "INSERT INTO user_info (username , password) VALUES (? , ?)" ;
        MYSQL *sqlconn = sql.first ; 
        // 准备参数化查询
        MYSQL_STMT *stmt = mysql_stmt_init(sqlconn) ; 
        if (!stmt || mysql_stmt_prepare(stmt, insert, strlen(insert)) ) {
            LOG_ERROR("Failed to prepare MySQL statement: %s", mysql_error(sqlconn)) ;
            mysql_stmt_close(stmt);
            return "" ; 
        }
        // 绑定参数
        MYSQL_BIND param[2];  
        memset(param, 0, sizeof(param)); 
        param[0].buffer_type = MYSQL_TYPE_STRING;
        param[0].buffer = const_cast<char *>(name.data()) ;
        param[0].buffer_length = name.size() ;

        param[0].buffer_type = MYSQL_TYPE_STRING;
        param[0].buffer = const_cast<char *>(pwd.data()) ;
        param[0].buffer_length = pwd.size() ;

        if (mysql_stmt_bind_param(stmt, param) != 0) {
            LOG_ERROR("Failed to bind MySQL statement parameters: %s" , mysql_stmt_error(stmt));
            mysql_stmt_close(stmt); 
            return "" ; 
        }

        // 执行插入操作 
        if (mysql_stmt_execute(stmt) != 0) {
            LOG_ERROR("Failed to execute MySQL statement: %s" , mysql_stmt_error(stmt));
            mysql_stmt_close(stmt); 
            return "" ; 
        }
        mysql_stmt_close(stmt); 
        return pwd ; 
    }
} ; 

#endif