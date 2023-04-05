#include "httpConn.h"
#include <iostream>
#include <fstream>
using namespace std ; 

// 测试解析 http request and response 
void test_parse_http(){
    Log& log = Log::Instance() ; 
    log.init(0, "./log", ".log", 0);
    HttpConn* httpconn = new HttpConn() ; 

    string http_head = "GET /login.html HTTP/1.1\r\nUser-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\nHost: www.example.com\r\nAccept-Language: en, mi\r\n" ; 
    //string http_head = "POST /login HTTP/1.1\r\nUser-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\nHost: www.example.com\r\nContent-Type: application/x-www-form-urlencoded\r\nAccept-Language: en, mi\r\n\r\nusername=test4&password=test%40" ; 
    std::ofstream outFile("./login.html" , std::ios::out) ; 
    outFile << http_head ; 
    outFile.close() ; 

    int fd = open("./login.html", O_RDWR  | O_CREAT , 0644) ;   
    struct  sockaddr_in addr_ = {0} ; 
    httpconn->init(fd , "/home/lec/File/Tiny_ChatRoom/resources" ,  addr_ , false) ; 
    int Erron = -1 ; 
    httpconn->dealHttpRequest(&Erron) ;
    httpconn->dealHttpResponse(&Erron) ; 
}

int main(){
    test_parse_http() ; 
    return 0 ; 
}
