#include "sqlConnectPool.h"
#include <iostream>
using namespace std ;

// 参数化查询，防止 Mysql 注入问题
void queryName(const unique_ptr<SqlConnnectPool>& SqlPool , const std::string& name){
   
    std::pair<MYSQL* , int> sql = SqlPool->getSqlconnect() ;  
    SLEEP_SECOND(30) ; // 线程阻塞，测试监控线程是否有用
    MYSQL *sqlconn = sql.first ; 
    const char* query = "SELECT password FROM user_info WHERE username = ?" ;
    MYSQL_STMT *stmt = mysql_stmt_init(sqlconn) ; 
    if (!stmt) {
        LOG_ERROR("Failed to initialize MySQL statement: %s", mysql_error(sqlconn)) ;
        SqlPool->freeSqlconnect(sql) ; 
        return  ; 
    }
    
    // 准备参数化查询
    if (mysql_stmt_prepare(stmt, query, strlen(query)) != 0) {
        LOG_ERROR("Failed to prepare MySQL statement: %s", mysql_stmt_error(stmt)) ;
        mysql_stmt_close(stmt); 
        SqlPool->freeSqlconnect(sql) ; 
        return  ;
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
        SqlPool->freeSqlconnect(sql) ; 
        return  ;
    }

    // 执行查询
    if (mysql_stmt_execute(stmt) != 0) {
        LOG_ERROR("Failed to execute MySQL statement: %s" , mysql_stmt_error(stmt));
        mysql_stmt_close(stmt); 
        SqlPool->freeSqlconnect(sql) ; 
        return  ;
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
        SqlPool->freeSqlconnect(sql) ; 
        return  ;
    }

    if (mysql_stmt_fetch(stmt) != 0) {
        mysql_stmt_close(stmt);
        LOG_INFO("name isn't exists %s" , name.c_str());
        SqlPool->freeSqlconnect(sql) ; 
        return  ; 
    } 
    mysql_stmt_close(stmt); 
    SqlPool->freeSqlconnect(sql) ; 
    cout<<"password : "<<password<<endl ;
    
    // return password ;
}

// 测试单线程数据库查询功能是否有效
void test_single_query(){
    Log::Instance().init(0, "./log", ".log", 1);  
    unique_ptr<SqlConnnectPool> SqlPool = make_unique<SqlConnnectPool>() ; 
    queryName(SqlPool , "test") ;
    
}

// 测试多线程数据库查询功能是否有效
void test_multi_query(){
    Log::Instance().init(0, "./log", ".log", 1);  
    unique_ptr<SqlConnnectPool> SqlPool = make_unique<SqlConnnectPool>() ; 
    std::vector<std::thread> thVec ;
    for(int i = 0 ; i < 10 ; ++i){
        thVec.push_back(thread(queryName , ref(SqlPool) , "test" + to_string(i))) ; 
    }
    for(int i = 0 ; i < 10 ; ++i){
        thVec[i].join() ; 
    }
}

// 测试监控线程是否会自动扩缩
void test_monitor(){
    Log::Instance().init(0, "./log", ".log", 1);  
    unique_ptr<SqlConnnectPool> SqlPool = make_unique<SqlConnnectPool>() ; 
    std::vector<std::thread> thVec ;
    for(int i = 0 ; i < 10 ; ++i){
        thVec.push_back(thread(queryName , ref(SqlPool) , "test" + to_string(i))) ; 
    }
    for(int i = 0 ; i < 10 ; ++i){
        thVec[i].join() ; 
    }
    SLEEP_SECOND(60) ;
}

int main(){
    // test_single_query() ; 
    // test_multi_query() ; 
    test_monitor() ; 
    return 0 ; 
}