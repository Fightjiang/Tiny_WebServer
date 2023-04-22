#include <list>
#include <mutex>

template<typename T> 
class atomicList {
private : 
    std::list<T> list_ ; 
    std::mutex mtx ;  

public : 
     
    bool empty(){
        std::unique_lock<std::mutex> locker(mtx) ;
        return list_.empty() ;  
    }

    size_t size(){
        std::unique_lock<std::mutex> locker(mtx) ;
        return list_.size() ;  
    }

    void clear() {
        std::unique_lock<std::mutex> locker(mtx) ;
        list_.clear() ; 
    }

    bool tryPop(T& message) { 
        std::unique_lock<std::mutex> locker(mtx) ; 
        if(list_.empty()) return false ;  
        message = std::move(list_.front()) ; list_.pop_front() ;  
        return true ;
    }

    void push_back(T&& message){ 
        std::unique_lock<std::mutex> locker(mtx) ;
        list_.push_back(std::move(message)) ;  
    }

    void push_back(T& message){ 
        std::unique_lock<std::mutex> locker(mtx) ;
        list_.push_back(message) ;  
    }
} ; 