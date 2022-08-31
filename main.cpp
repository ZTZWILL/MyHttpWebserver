#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include "locker.h"
#include "threadpool.h"
#include "http_conn.h"
#define MAX_FD 65535
#define MAX_EVENT_NUMBER 10000//监听最大事件数量
void addsig(int sig,void(handler)(int)){
    //sig是指定信号编号
    //handler是信号处理函数
    struct sigaction sa;
    memset(&sa,'\0',sizeof(sa));
    sa.sa_handler=handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig,&sa,NULL);
}
extern void addfd(int epollfd,int fd,bool one_shot);
extern void remove(int epollfd,int fd);
extern void modfd(int epollfd,int fd,int ev);
int main(int argc,char*argv[]){
    if(argc<=1){
        printf("请按照如下格式运行：%s port_number\n",basename(argv[0]));
        exit(-1);
    }
    int port=atoi(argv[1]);//获取端口号
    addsig(SIGPIPE,SIG_IGN);//添加SIGPIPE信号
    threadpool<http_conn>*pool=NULL;
    try{
        pool=new threadpool<http_conn>;//创建线程池，任务类型为http_conn
    }catch(...){
        exit(-1);
    }
    http_conn * users=new http_conn[MAX_FD];//这个数组中保存了所有的客户端信息
    //创建一个监听socket
    int listenfd=socket(PF_INET,SOCK_STREAM,0);
    //设置端口复用，即便当前端口正在处于time_wait状态，也可以立即使用
    int reuse=1;
    setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    //bind
    struct sockaddr_in address;
    address.sin_family=AF_INET;
    address.sin_port=htons(port);//命令行写入的端口号
    address.sin_addr.s_addr=INADDR_ANY;//IP地址
    bind(listenfd,(struct sockaddr*)&address,sizeof(address));
    //listen
    listen(listenfd,5);//当监听队列的长度大于5这个上限时，客户端的连接请求将不再被受理，而是会收到ECONNEREFUSED的错误信息
    //创建epoll
    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd=epoll_create(5);//创建一个epoll对象
    //将listenfd添加到epoll中,准备监听
    addfd(epollfd,listenfd,false);
    http_conn::m_epollfd=epollfd;//设置连接类的epoll对象（此时该对象只监听listenfd这一个文件描述符）
    while(true){
        int num=epoll_wait(epollfd,events,MAX_EVENT_NUMBER,-1);
        if((num<0)&&(errno!=EINTR)){
            printf("epoll failure\n");
            break;
        }
        for(int i=0;i<num;i++){
            int sockfd=events[i].data.fd;
            if(sockfd==listenfd){
                struct sockaddr_in client_address;//等待回填的客户端地址
                socklen_t client_addrlen=sizeof(client_address);
                int connfd=accept(listenfd,(struct sockaddr*)&client_address,&client_addrlen);
                if(http_conn::m_user_count>=MAX_FD){
                    //目前链接数已满
                    close(connfd);
                    continue;
                }
                //将新的客户数据放入数组中
                users[connfd].init(connfd,client_address);
            }
            //异常情况，关闭连接
            else if(events[i].events&(EPOLLRDHUP|EPOLLHUP|EPOLLERR)){
                users[sockfd].close_conn();
            }
            else if(events[i].events& EPOLLIN){
                if(users[sockfd].read()){
                    pool->append(users+sockfd);
                }
                else{
                    users[sockfd].close_conn();
                }
            }
            else if(events[i].events & EPOLLOUT){
                if(!users[sockfd].write()){
                    users[sockfd].close_conn();
                }

            }
        }
    }
    return 0;
}