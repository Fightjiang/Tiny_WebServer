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
typedef std::chrono::milliseconds MS;
typedef Clock::time_point TimeStamp;

struct TimerNode {
    int id;
    TimeStamp expires;
    TimeoutCallBack cb;
    bool operator < (const TimerNode& t) const {
        return expires < t.expires;
    }
    TimerNode(int _id , TimeStamp _expires , TimeoutCallBack _cb) : id(_id) , expires(_expires) , cb(_cb) {} 
    
};

class HeapTimer {
public:
    HeapTimer() { heap_.reserve(64); }

    ~HeapTimer() { clear(); }
    
    void adjust(int id, int newExpires) {
        // 调整堆 
        assert(!heap_.empty()) ; 
        assert((ref_.find(id) != ref_.end())) ;
        heap_[ref_[id]].expires = Clock::now() + MS(newExpires) ; // Clock::now() 获得系统的当前时间， MS 毫秒 
        
        // 调整堆
        if(shiftdown_(ref_[id] , heap_.size()) == false) {
            shiftup_(ref_[id]) ; 
        }
    }

    void add(int id, int timeOut, const TimeoutCallBack& cb) {
        assert(id >= 0) ; 
        if(ref_.find(id) == ref_.end()){// 不存在，插入堆尾
            ref_[id] = heap_.size() ; 
            heap_.emplace_back(id , Clock::now() + MS(timeOut) , cb) ; 
            shiftup_(ref_[id]) ; // 向上调整堆
        
        }else {// 已经存在了，则修改
            heap_[ref_[id]].expires = Clock::now() + MS(timeOut) ; 
            heap_[ref_[id]].cb = cb ; 
            // 调整堆
            if(shiftdown_(ref_[id] , heap_.size()) == false) {
                shiftup_(ref_[id]) ; 
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
        ref_.clear();
        heap_.clear();
    }

    // 清楚超时的结点
    void tick(){
        if(heap_.empty()) return;
        while(!heap_.empty()) {
            TimerNode node = heap_.front();
            if(std::chrono::duration_cast<MS>(node.expires - Clock::now()).count() > 0) { 
                break; 
            }
            node.cb(); // 已经超时了，调用回调函数
            pop();
        }
    }

    void pop(){
        assert(!heap_.empty());
        del_(0);
    }

    int GetNextTick() {
        tick();
        int res = -1;
        if(!heap_.empty()) {
            res = std::chrono::duration_cast<MS>(heap_.front().expires - Clock::now()).count();
            if(res < 0) { res = 0; }
        }
        return res;
    }

private:
    // 将要删除的结点交换到队尾，然后调整堆
    void del_(size_t index) {
         
        assert(!heap_.empty() && index >= 0 && index < heap_.size());
        size_t i = index ; 
        size_t n = heap_.size() - 1 ; 
        if(i < n){
            SwapNode_(i , n) ; 
            shiftdown_(i , n) ; 
        }
        /* 队尾元素删除 */
        ref_.erase(heap_.back().id);
        heap_.pop_back();
    }
    
    void shiftup_(size_t i) {
        assert(i >= 0 && i < heap_.size());
        size_t j = (i - 1) / 2; 
        while(j >= 0) {
            if(heap_[j] < heap_[i]) { break; }
            SwapNode_(i, j) ;
            i = j ;
            j = (i - 1) / 2;
        }
    }

    bool shiftdown_(size_t index, size_t n) {
        assert(index >= 0 && index < heap_.size()) ; 
        assert(n >= 0 && n <= heap_.size()) ; 
        size_t i = index ; 
        size_t j = i * 2 + 1 ; 
        while(j < n) {
            if(j + 1 < n && heap_[j + 1] < heap_[j]) ++j ; 
            if(heap_[i] < heap_[j]) break; 
            SwapNode_(i, j);
            i = j ; 
            j = i * 2 + 1 ; 
        }
        return i > index ; 
    }

    void SwapNode_(size_t i, size_t j) {
        assert(i >= 0 && i < heap_.size());
        assert(j >= 0 && j < heap_.size());
        std::swap(heap_[i], heap_[j]);
        ref_[heap_[i].id] = i;
        ref_[heap_[j].id] = j;
    }

    std::vector<TimerNode> heap_;
    std::unordered_map<int, size_t> ref_;
};

#endif //HEAP_TIMER_H