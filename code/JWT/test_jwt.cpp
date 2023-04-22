#include "jwt.h"
#include <iostream>
#include "../Common.h"
using namespace std ;

int main(){
    Log& log = Log::Instance() ; 
    log.init(0, "./log", ".log", false );
    HttpConfigInfo config_ ; 
    JWT jwt(config_.jwtSecret , config_.jwtExpire) ; 
    
    unordered_map<string,string> keyValue ;
    keyValue["name"] = "ChatRoom" ; 
    keyValue["name2"] = "ChatRoom2" ; 
    keyValue["name3"] = "ChatRoom3" ; 
    std::string token = jwt.generateJWT(keyValue) ; 
    cout<<token<<endl ;
    keyValue = jwt.parseJWT(token) ; 
    for(auto iter : keyValue){
        cout<<iter.first<<":"<<iter.second<<endl ;
    }
    return 0 ; 
}