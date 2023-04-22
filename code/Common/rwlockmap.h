#ifndef RW_LOCK_MAP_H
#define RW_LOCK_MAP_H

#include <unordered_map>
#include <shared_mutex>
#include <mutex>
template<typename K, typename V>
class RWLockMap
{
public:
    RWLockMap() = default;

    V &operator[](const K &key) {
        std::unique_lock<std::shared_timed_mutex> lock(mutex_);
        return map_[key];
    }
    
    V find(const K &key) const {
        std::shared_lock<std::shared_timed_mutex> lock(mutex_);
        auto it = map_.find(key);
        if (it == map_.end()) {
            return V() ;
        } 
        return it->second;
    }

    typename std::unordered_map<K, V>::const_iterator  begin() const {
        std::shared_lock<std::shared_timed_mutex> lock(mutex_);
        return map_.begin() ; 
    }

    typename std::unordered_map<K, V>::const_iterator  end() const {
        std::shared_lock<std::shared_timed_mutex> lock(mutex_);
        return map_.end() ; 
    }

    bool erase(const K &key){
        std::unique_lock<std::shared_timed_mutex> lock(mutex_);
        return map_.erase(key) > 0 ;  
    }

    void clear() {
        std::unique_lock<std::shared_timed_mutex> lock(mutex_);
        map_.clear() ; 
    }
    
private:
    mutable std::shared_timed_mutex mutex_;
    std::unordered_map<K, V> map_;
};

#endif