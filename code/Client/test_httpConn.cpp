#include "clientConn.h"
#include <iostream>
#include <fstream>
using namespace std ; 

// 测试解析 http request and response 
void test_parse_http(){
    Log& log = Log::Instance() ; 
    log.init(0, "./log", ".log", 0);
    
    string http_head = "GET / HTTP/1.1\r\nUser-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\nHost: www.example.com\r\nAccept-Language: en, mi\r\n" ; 
    //string http_head = "POST /login HTTP/1.1\r\nUser-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\nHost: www.example.com\r\nContent-Type: application/x-www-form-urlencoded\r\nAccept-Language: en, mi\r\n\r\nusername=test4&password=test%40" ; 
    std::ofstream outFile("./login.html" , std::ios::out) ; 
    outFile << http_head ; 
    outFile.close() ; 
    
    int fd = open("./login.html", O_RDWR  | O_CREAT , 0644) ;   
    struct sockaddr_in addr_ = {0} ; 
 
    ClientConn* client = new ClientConn() ; 
    client->init(fd ,  addr_ , false) ; 
    int ret = client->dealRequest() ;  
    client->dealResponse() ; 
    
    delete client ; 
    close(fd) ;
}

int main(){
    test_parse_http() ; 
    return 0 ; 
}
