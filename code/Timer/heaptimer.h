#ifndef HEAP_TIMER_H
#define HEAP_TIMER_H

#include <vector>
#include <unordered_map>
#include <time.h>
#include <algorithm>
#include <arpa/inet.h> 
#include <functional> 
#include <assert.h> 
#include <chrono> 
 
typedef std::function<void()> TimeoutCallBack;
typedef std::chrono::system_clock Clock; 
typedef std::chrono::seconds S ; 
typedef Clock::time_point TimeStamp;

struct TimerNode {
    int fd_;
    TimeStamp expires;
    TimeoutCallBack cb;
    bool operator < (const TimerNode& t) const {
        return expires < t.expires;
    }
    bool operator <= (const TimerNode& t) const {
        return expires <= t.expires;
    }
    TimerNode(const int fd , const TimeStamp _expires , TimeoutCallBack _cb) : fd_(fd) , expires(_expires) , cb(_cb) {} 
    
};

class HeapTimer {
private : 
    std::vector<TimerNode> heap_;
    std::unordered_map<int, int> fd_mapId; // ref_ 是映射 fd 对应的 堆中的 id 号

public:
    HeapTimer() { heap_.reserve(64); }

    ~HeapTimer() { clear(); }
    
    void adjust(const int fd, const Clock::time_point newExpires) {
        // 调整堆 
        assert(!heap_.empty()) ; 
        assert((fd_mapId.find(fd) != fd_mapId.end())) ;
        heap_[fd_mapId[fd]].expires = newExpires ; // Clock::now() 获得系统的当前时间， 秒为计算单位
        
        // 调整堆
        if(shiftdown_(fd_mapId[fd] , heap_.size()) == false) {
            shiftup_(fd_mapId[fd]) ; 
        }
    }

    void add(const int fd, const int timeOut, const TimeoutCallBack& cb) {
        assert(fd >= 0) ; 
        if(fd_mapId.find(fd) == fd_mapId.end()){// 不存在，插入堆尾
            fd_mapId[fd] = heap_.size() ; 
            heap_.emplace_back(fd , Clock::now() + S(timeOut) , cb) ; 
            shiftup_(fd_mapId[fd]) ; // 向上调整堆
        
        }else {// 已经存在了，则修改
            heap_[fd_mapId[fd]].expires = Clock::now() + S(timeOut) ; 
            heap_[fd_mapId[fd]].cb = cb ; 
            // 调整堆
            if(shiftdown_(fd_mapId[fd] , heap_.size()) == false) {
                shiftup_(fd_mapId[fd]) ; 
            }
        }
    }

    // // 删除指定 id 结点，并触发回调函数
    // void doWork(int id) {
    //     // id 不在堆里 
    //     if(heap_.empty() || ref_.find(id) == ref_.end()){
    //         return ; 
    //     }
    //     TimerNode node = heap_[ref_[id]] ; 
    //     node.cb() ; 
    //     del_(ref_[id]) ; 
    // }

    void clear() {
        fd_mapId.clear();
        heap_.clear();
    }

    // 清楚超时的结点
    void tick(){
        if(heap_.empty()) return;
        while(!heap_.empty()) {
            TimerNode& node = heap_.front();
            if(std::chrono::duration_cast<S>(node.expires - Clock::now()).count() > 0) { 
                break; 
            }
            node.cb(); // 已经超时了，调用回调函数
            popFront();
        }
    }

    int GetNextTick() {
        tick();
        int res = -1;
        if(!heap_.empty()) {
            res = std::chrono::duration_cast<S>(heap_.front().expires - Clock::now()).count();
            if(res < 0) { res = 0; }
        }
        return res;
    }

private:

    // 小根堆，堆顶已经过期，将堆顶的结点交换到队尾，然后调整堆
    void popFront() {
        assert(!heap_.empty());
        int HeapBack = heap_.size() - 1; 
        if(HeapBack != 0){
            SwapNode_(0 , HeapBack) ; 
            shiftdown_(0 , HeapBack) ; 
        }
        /* 队尾元素删除 */
        fd_mapId.erase(heap_.back().fd_);
        heap_.pop_back();
    }
    
    void shiftup_(int i) {
        assert(i >= 0 && i < heap_.size()); 
        int j = (i - 1) / 2; 
        while(j >= 0) {
            if(heap_[j] <= heap_[i]) break ;
            SwapNode_(i, j) ;
            i = j ;
            j = (i - 1) / 2;
        }
    }

    bool shiftdown_(size_t index, size_t End) {
        assert(index >= 0 && index < heap_.size()) ; 
        assert(End >= 0 && End <= heap_.size()) ; 
        size_t i = index ; 
        size_t j = i * 2 + 1 ; 
        while(j < End) {
            if(j + 1 < End && heap_[j + 1] < heap_[j]) ++j ; 
            if(heap_[i] <= heap_[j]) break; 
            SwapNode_(i, j);
            i = j ; 
            j = i * 2 + 1 ; 
        }
        // 判断是否下降了，如果下降了，那么就不用再往上调整了，因为肯定会比上面的过期时间小
        return i > index ; 
    }

    void SwapNode_(const int i, const int j) {
        assert(i >= 0 && i < heap_.size());
        assert(j >= 0 && j < heap_.size());
        std::swap(heap_[i], heap_[j]);
        fd_mapId[heap_[i].fd_] = i; // fd_ 永远都是对应的 heap 的下标，fd->i 
        fd_mapId[heap_[j].fd_] = j;
    }
};

#endif //HEAP_TIMER_H