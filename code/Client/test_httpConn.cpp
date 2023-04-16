#include "clientConn.h"
#include <iostream>
#include <fstream>
#include "../Server/epoller.h"
using namespace std ; 

// 测试解析 http request and response 
void test_parse_http(){
    Log& log = Log::Instance() ; 
    log.init(0, "./log", ".log", 0);
    
    string http_head1 = "GET / HTTP/1.1\r\nUser-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\nHost: www.example.com\r\nAccept-Language: en, mi\r\n" ; 
    string http_head2 = "POST /login HTTP/1.1\r\nUser-Agent: curl/7.16.3 libcurl/7.16.3 OpenSSL/0.9.7l zlib/1.2.3\r\nHost: www.example.com\r\nContent-Length: 31\r\nContent-Type: application/x-www-form-urlencoded\r\nAccept-Language: en, mi\r\n\r\nusername=test4&password=test%40" ; 
    // 粘包在一起
    string http_head = http_head2 + http_head1 ; 
    std::ofstream outFile("./login.html" , std::ios::out) ; 
    outFile << http_head ; 
    outFile.close() ; 
    
    int fd = open("./login.html", O_RDWR  | O_CREAT , 0644) ;   
    struct sockaddr_in addr_ = {0} ; 
 
    ClientConn* client = new ClientConn() ; 
    Epoller* epoller_ = new Epoller() ; 
    client->init(fd ,  addr_ , epoller_ , 0) ; 
    int ret = client->dealRequest() ;
    
    while(client->dealResponse() == 2) ;  
    
    delete client ; 
    close(fd) ;
}
// 测试解析 WebSocket handshake 
void test_parse_WebSocket(){ 
    Log& log = Log::Instance() ; 
    log.init(0, "./log", ".log", 0);
    string http_head = "GET ws://localhost:3000/ws/chat HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nOrigin: http://localhost:3000\r\nSec-WebSocket-Key: w4v7O6xFTi36lq3RNcgctw==\r\nSec-WebSocket-Version: 13\r\n" ; 
    
    std::ofstream outFile("./login.html" , std::ios::out) ; 
    outFile << http_head ; 
    outFile.close() ; 

    int fd = open("./login.html", O_RDWR  | O_CREAT , 0644) ;   
    struct sockaddr_in addr_ = {0} ; 
 
    ClientConn* client = new ClientConn() ; 
    Epoller* epoller_ = new Epoller() ; 
    client->init(fd ,  addr_ , epoller_ , 0) ; 
    int ret = client->dealRequest() ;  
    client->dealResponse() ; 
    
    delete client ; 
    close(fd) ;
}
int main(){
    //test_parse_http() ; 
    test_parse_WebSocket() ;
    return 0 ; 
}
