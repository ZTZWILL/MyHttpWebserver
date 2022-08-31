#ifndef THREADPOOL_H__
#define THREADPOOL_H__
#include <pthread.h>
#include <semaphore.h>
#include <exception>
#include <list>
#include "locker.h"
#include <cstdio>
template<typename T>
class threadpool{
public:
    threadpool(int thread_number=8,int max_requests=10000);
    ~threadpool();
    bool append(T* request);
private://私有成员函数，只能由共有成员函数来调用（友元函数也可以，但对于友元函数，这个私有函数必须是静态的）
    static void* worker(void* arg);
    void run();
private:
    int m_thread_number;//池中線程數量
    pthread_t *m_threads;//線程池數組
    int m_max_requests;//請求隊列中能夠允許的待處理請求數量
    std::list<T*> m_workqueue;//請求隊列
    locker m_queuelocker;//用於給隊列上鎖的鎖對象
    sem m_queuestat;//信号量
    bool m_stop;//结束线程标志
};
template<typename T>
threadpool<T>::threadpool(int thread_number,int max_requests):
    m_thread_number(thread_number),
    m_max_requests(max_requests),
    m_stop(false),
    m_threads(NULL){
    if((thread_number<=0)||(max_requests<=0)){
        throw std::exception();
    }
    m_threads=new pthread_t[m_thread_number];//动态创建，别忘了析构时释放
    if(!m_threads){
        throw std::exception();
    }
    for(int i=0;i<thread_number;i++){
        printf("create  the %dth thread\n",i);
        if(pthread_create(m_threads+i,NULL,worker,this)!=0){//此处把this作为参数传递到worker中，使得静态成员函数可以访问类对象的成员
            delete []  m_threads;
            throw std::exception();
        }
        if(pthread_detach(m_threads[i])){//将线程脱离掉，不负责收拾
            delete [] m_threads;
            throw std::exception();
        }
    }
}
template<typename T>
threadpool<T>::~threadpool(){
    delete [] m_threads;
    m_stop=true;
}
template<typename T>
bool threadpool<T>::append(T *request){
    m_queuelocker.lock();//此处上锁，互斥访问请求队列
    if(m_workqueue.size()>=m_max_requests){//队列中等待的任务数量已经超过上限
        m_queuelocker.unlock();//发生异常同样要解锁
        return false;
    }
    m_workqueue.push_back(request);
    m_queuelocker.unlock();//此处解锁
    m_queuestat.post();//增加待处理任务信号量
    return true;
}
template<typename T>
void* threadpool<T>::worker(void *arg){
    threadpool *pool=(threadpool*)arg;
    pool->run();//这里把run分离出来是因为worker这个静态成员函数是不能访问对象的非静态成员的
    return pool;
}
template<typename T>
void threadpool<T>::run(){
    while(!m_stop){
        m_queuestat.wait();
        m_queuelocker.lock();
        if(m_workqueue.empty()){
            m_queuelocker.unlock();
            continue;
        }
        T* request=m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if(!request){
            continue;
        }
        request->process();
    }
}
#endif