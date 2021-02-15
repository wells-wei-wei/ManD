#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <map>
#include <vector>
#include <fcntl.h>
#include <sys/epoll.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <queue>
#include <iostream>
#include <ctime>

class Daemon{
public:
    int init(){

        //初始化用于系统通信的socket
        _sys_sock = socket(AF_INET, SOCK_DGRAM, 0);
        if(_sys_sock < 0){
            printf("create socket fail!\n");
            return -1;
        }

        memset(&_sys_sock_addr, 0, sizeof(_sys_sock_addr));
        _sys_sock_addr.sin_family = AF_INET;
        _sys_sock_addr.sin_addr.s_addr = htonl(INADDR_ANY); //IP地址，需要进行网络序转换，INADDR_ANY：本地地址
        _sys_sock_addr.sin_port = htons(_sys_sock_port);  //端口号，需要网络序转换

        int opt=SO_REUSEADDR;
        setsockopt(_sys_sock,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));

        setnonblocking(_sys_sock);

        ret = bind(_sys_sock, (struct sockaddr*)&_sys_sock_addr, sizeof(_sys_sock_addr));

        struct epoll_event ev;
        _epoll_fd = epoll_create(MAXEPOLLSIZE);
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = _sys_sock;
        epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, _sys_sock, &ev);

        //初始化用于组播通信的socket
        _groupmsg_sock = socket(AF_INET, SOCK_DGRAM, 0);
        
        _thread_pool.start();
    }

    int setnonblocking(int sockfd){
        if (fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFD, 0)|O_NONBLOCK) == -1)
        {
            return -1;
        }
        return 0;
    }

    int event_loop(){
        while(1){
            int epoll_fd_num = epoll_wait(_epoll_fd, events, 10000, -1);
            for(int n = 0; n < epoll_fd_num; ++n){
                if (events[n].data.fd == _sys_sock){
                    Test t(events[n].data.fd);
                    _thread_pool.appendTask(t);
                }
            }
        }
    }
    class Test{
    public:
        Test(int sock):sock(sock){}
        void operator()() const
        {
            char recvbuf[1000];
            struct sockaddr_in client_addr;
            socklen_t cli_len=sizeof(client_addr);
            int ret = recvfrom(sock, recvbuf, 1000, 0, (struct sockaddr *)&client_addr, &cli_len);
            if (ret > 0)
            {
                //printf("socket %d 接收到来自:%s:%d的消息成功:’%s’，共%d个字节的数据/n",sock, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), recvbuf, ret);
                printf("消息: %s", recvbuf);
                std::thread::id tid = std::this_thread::get_id();
                std::cout << "线程id = " << tid << std::endl;
            }
        }
    private:
        int sock;
    };

    int send_groupmsg(std::string groupmsg_ip, std::string msg){
        //为socket设置组播发送方式
        memset(&_groupmsg_dst_ip, 0, sizeof(_groupmsg_dst_ip));
        inet_pton(AF_INET, groupmsg_ip.data(), &_groupmsg_dst_ip.imr_multiaddr);//设置组播目标IP
        inet_pton(AF_INET, "0.0.0.0", &_groupmsg_dst_ip.imr_interface);//使用的本机IP，0.0.0.0表示本机上的所有IP均可使用此服务
        
        setsockopt(_groupmsg_sock, IPPROTO_IP, IP_MULTICAST_IF, &_groupmsg_dst_ip, sizeof(_groupmsg_dst_ip));// 设置发送数据包是组播方式发送

        //构建发送目标结构体
        memset(&_groupmsg_sock_addr, 0, sizeof(_groupmsg_sock_addr));
        _groupmsg_sock_addr.sin_family		= AF_INET;
        _groupmsg_sock_addr.sin_port		= htons(_sys_sock_port);		// 目标端口
        inet_pton(AF_INET, groupmsg_ip.data(), &_groupmsg_sock_addr.sin_addr.s_addr);// 目标的组地址

        char buf[MAXLINE];
        msg.copy(buf, msg.size(), 0);
        *(buf + msg.size()) = '\0';

        sendto(_groupmsg_sock, buf, strlen(buf), 0, (struct sockaddr*)& _groupmsg_sock_addr, sizeof( _groupmsg_sock_addr));

        return 1;
    }
private:
    int ret;
    
    int _epoll_fd;

    const int MAXEPOLLSIZE = 100;
    const int MAXLINE = 1500;
    
    struct epoll_event events[100];

    int _sys_sock;//用于系统消息通信的socket（同时也用于单播）
    int _groupmsg_sock;//用于组播通信的socket

    struct sockaddr_in _sys_sock_addr;//用于系统消息通信的地址
    int _sys_sock_port = 9000;//用于系统消息通信的默认端口

    struct ip_mreq _groupmsg_dst_ip;
    struct sockaddr_in _groupmsg_sock_addr;

    std::string _groupip_ori = "239.0.0.";
    std::string _groupip_last = "1";

    std::map<std::string, std::string> _map_groupname_groupip;
    std::map<std::string, std::vector<std::string>> _map_groupip_groupmem;

    class ThreadPool{
    public:
        using Task = std::function<void()>;

        explicit ThreadPool(int num): _thread_num(num), _is_running(false){}

        ThreadPool(): _thread_num(20), _is_running(false){}
        ~ThreadPool()
        {
            if (_is_running)
                stop();
        }

        void start()
        {
            _is_running = true;

            // start threads
            for (int i = 0; i < _thread_num; i++)
                _threads.emplace_back(std::thread(&ThreadPool::work, this));
        }

        void stop()
        {
            {
                // stop thread pool, should notify all threads to wake
                std::unique_lock<std::mutex> lk(_mtx);
                _is_running = false;
                _cond.notify_all(); // must do this to avoid thread block
            }

            // terminate every thread job
            for (std::thread& t : _threads)
            {
                if (t.joinable())
                    t.join();
            }
        }

        void appendTask(const Task& task)
        {
            if (_is_running)
            {
                std::unique_lock<std::mutex> lk(_mtx);
                _tasks.push(task);
                _cond.notify_one(); // wake a thread to to the task
            }
        }

    private:
        void work()
        {

            // every thread will compete to pick up task from the queue to do the task
            while (_is_running)
            {                
                Task task;
                {
                    std::unique_lock<std::mutex> lk(_mtx);
                    if (!_tasks.empty())
                    {
                        // if tasks not empty, 
                        // must finish the task whether thread pool is running or not
                        task = _tasks.front();
                        _tasks.pop(); // remove the task
                    }
                    else if (_is_running && _tasks.empty())
                        _cond.wait(lk);
                }

                if (task)
                    task(); // do the task
            }

        }

    public:
        // disable copy and assign construct
        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool& other) = delete;

    private:
        std::atomic_bool _is_running; // thread pool manager status
        std::mutex _mtx;
        std::condition_variable _cond;
        int _thread_num;
        std::vector<std::thread> _threads;
        std::queue<Task> _tasks;
    } _thread_pool;
};

int main(){
    Daemon dae;
    dae.init();
    //dae.send_groupmsg("239.0.0.2", "hello wells");
    dae.event_loop();
}