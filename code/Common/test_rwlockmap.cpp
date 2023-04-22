#include "rwlockmap.h"
#include <iostream>
#include <string>
#include <thread>
#include <vector>
using namespace std ;

void write(RWLockMap<std::string , int> &mp , int i) {
    string tmp = "test" + to_string(i) ; 
    mp[tmp] = i ;
}

void read(RWLockMap<std::string , int> &mp , int i) {
    string tmp = "test" + to_string(i) ; 
    int value = mp.find(tmp); 
    cout<<tmp<<" "<<value<<endl ;
}

int main(){
    RWLockMap<std::string , int> mp ; 
    int value = mp.find("tmp");
    cout<<value<<endl ; 
    // std::vector<std::thread> thVec ;
    // for(int i = 0 ; i < 10 ; ++i){
    //     thVec.push_back(thread(write , ref(mp) , i)) ; 
    //     thVec.push_back(thread(read , ref(mp) , i)) ; 
    // }
    // for(int i = 0 ; i < 20 ; ++i){
    //     thVec[i].join() ; 
    // }
    
    // for(auto &iter : mp){
    //     cout<<iter.first <<" "<<iter.second<<endl ;
    // }
    // cout<<endl ; 
    // mp.erase("test1") ; 
    // for(auto &iter : mp){
    //     cout<<iter.first <<" "<<iter.second<<endl ;
    // }
    return 0 ; 
}