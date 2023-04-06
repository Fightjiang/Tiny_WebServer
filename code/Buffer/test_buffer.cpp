#include "buffer.h"
#include <iostream>  
#include <fcntl.h>  
#include <assert.h>
using namespace std ; 

void test_append_Str(){
    string str1 = "Hello 1 World" ;
    const char* str2 = "Hello 2 World" ; 
    string str3 = "Hello 3 World" ;

    unique_ptr<Buffer> buff = make_unique<Buffer>() ; 
    buff->Append(str1) ; 
    buff->Append(str2 , strlen(str2)) ;
    buff->Append(str3) ; 

    string str = buff->RetrieveAllToStr() ; 
    assert(str == "Hello 1 WorldHello 2 WorldHello 3 World") ; 
    cout<<str<<endl ; 
}

void test_readfile(){
    int fd = open("buffer_test.txt" , O_RDONLY) ; 
    if (fd < 0) {
        std::cerr << "Failed to open file" << std::endl;
        return  ;
    }
    unique_ptr<Buffer> buff = make_unique<Buffer>() ; 
    ssize_t size = buff->ReadFd(fd , nullptr) ; 
    close(fd) ; 
    cout<<size<<endl ;

    string str1 = "Hello 1 World" ;
    const char* str2 = "Hello 2 World" ; 
    string str3 = "Hello 3 World" ;
 
    buff->Append(str1) ; 
    buff->Append(str2 , strlen(str2)) ;
    buff->Append(str3) ;

    string str = buff->RetrieveAllToStr() ; 
    cout<<str<<endl ;  

    buff->Append(str1) ; 
    buff->Append(str2 , strlen(str2)) ;
    buff->Append(str3) ;

    str = buff->RetrieveAllToStr() ; 
    assert(str == "Hello 1 WorldHello 2 WorldHello 3 World") ; 
    cout<<str<<endl ;  
}

void test_writefile(){
    int fd = open("./buffer_test.txt", O_WRONLY | O_CREAT);
    
    string str1 = "Hello 1 World" ;
    const char* str2 = "Hello 2 World" ; 
    string str3 = "Hello 3 World" ;
    int len1 = str1.size() + strlen(str2) + str3.size() ;  
    unique_ptr<Buffer> buff = make_unique<Buffer>() ; 
    buff->Append(str1) ; 
    buff->Append(str2 , strlen(str2)) ;
    buff->Append(str3) ; 
    int len2 = buff->WriteFd(fd , nullptr) ; 
    close(fd) ; 
    assert(len1 == len2) ; 
    cout<<len1<<" "<<len2<<endl ;
}
int main(){
    test_append_Str() ; 
    test_readfile() ;
    // test_writefile() ;
    // char buff[2048] ; 
    // FILE* fd = fopen("./buffer_test.txt", "a");
    // strcpy(buff , "test\n\0") ;  
    // fputs(buff, fd);
    // fclose(fd) ; 
    return 0 ; 
}