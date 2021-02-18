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
#include <future>
#include <iostream>
#include <ctime>

struct msg_struct{
    std::string task_id;
    std::string task_type;
    std::string msg_0;
    std::string msg_1;
};

class MessageManager{
//成员变量
public:
    typedef  void(*process_func)(msg_struct, sockaddr_in, int); // 定义消息处理函数的别名，有别名才能放在map中
    std::map<std::string, process_func>_map_msg_handler;//记录消息类型及其对应消息处理函数的map
    inline static std::map<std::string, std::string> _map_name_ip;//记录每个客户端名字及其IP地址的map
    inline static std::map<std::string, std::string> _map_groupname_groupip;//记录每个组名及其组播IP地址的map
    inline static std::map<std::string, std::vector<std::string>> _map_groupip_groupmem;//记录每个组播IP地址及其成员ip地址的map
    inline static const  std::string _groupip_ori = "239.0.0.";//基本的组播ip地址
    inline static int _groupip_last = 2;//准备往基本的组播ip地址后面接的数字

    static const int _client_sock_port = 8080;//客户端默认端口

    int _sys_sock;//用于系统消息通信的socket（同时也用于单播）
    int ret;

//消息处理函数
private:
    //连接daemon
    static void process_00(msg_struct msg, sockaddr_in _client_addr, int _sys_sock){
        _map_name_ip[msg.msg_0]=inet_ntoa(_client_addr.sin_addr);
        std::string resbonse = msg.task_id+"#01#连接成功";
        char return_buf[100];
        memset(return_buf,'\0',sizeof(return_buf));
        resbonse.copy(return_buf, resbonse.size(), 0);
        _client_addr.sin_port = htons(_client_sock_port);
        sendto(_sys_sock, return_buf, 100, 0, (struct sockaddr*)&_client_addr, sizeof(_client_addr));

        std::cout<<"任务id： "<<msg.task_id<<" ip："<<inet_ntoa(_client_addr.sin_addr)<<"已连接成功:"<<std::endl;
    }
    //断开daemon
    static void process_10(msg_struct msg, sockaddr_in _client_addr, int _sys_sock){
        if(_map_name_ip.count(msg.msg_0) == 0){
            std::string resbonse = msg.task_id+"#11#此IP尚未连接";
            char return_buf[100];
            memset(return_buf,'\0',sizeof(return_buf));
            resbonse.copy(return_buf, resbonse.size(), 0);
            _client_addr.sin_port = htons(_client_sock_port);
            sendto(_sys_sock, return_buf, 100, 0, (struct sockaddr*)&_client_addr, sizeof(_client_addr));
            return;
        }
        _map_name_ip.erase(msg.msg_0);
        std::string resbonse = msg.task_id+"#11#已断开连接";
        char return_buf[100];
        memset(return_buf,'\0',sizeof(return_buf));
        resbonse.copy(return_buf, resbonse.size(), 0);
        _client_addr.sin_port = htons(_client_sock_port);
        sendto(_sys_sock, return_buf, 100, 0, (struct sockaddr*)&_client_addr, sizeof(_client_addr));

        std::cout<<"任务id： "<<msg.task_id<<" ip："<<inet_ntoa(_client_addr.sin_addr)<<"已断开连接:"<<std::endl;
    }
    //加入组
    static void process_20(msg_struct msg, sockaddr_in _client_addr, int _sys_sock){
        if (_map_groupname_groupip.count(msg.msg_0) == 0){//这个组名不存在时
            std::string new_group_ip = _groupip_ori+std::to_string(_groupip_last);
            _groupip_last++;
            _map_groupname_groupip[msg.msg_0]=new_group_ip;
        }
        _map_groupip_groupmem[_map_groupname_groupip[msg.msg_0]].push_back(inet_ntoa(_client_addr.sin_addr));
        
        std::string resbonse = msg.task_id+"#21#"+_map_groupname_groupip[msg.msg_0];
        char return_buf[100];
        memset(return_buf,'\0',sizeof(return_buf));
        resbonse.copy(return_buf, resbonse.size(), 0);
        _client_addr.sin_port = htons(_client_sock_port);
        sendto(_sys_sock, return_buf, 100, 0, (struct sockaddr*)&_client_addr, sizeof(_client_addr));

        std::cout<<"任务id： "<<msg.task_id<<" ip："<<inet_ntoa(_client_addr.sin_addr)<<"已加入组:"<<msg.msg_0<<" 本组组播ip："<<_map_groupname_groupip[msg.msg_0]<<std::endl;
    }
    //退出组
    static void process_30(msg_struct msg, sockaddr_in _client_addr, int _sys_sock){
        if(_map_groupname_groupip.count(msg.msg_0) == 0){//这个组名不存在时
            std::string resbonse = msg.task_id+"#31#组名不存在";
            char return_buf[100];
            memset(return_buf,'\0',sizeof(return_buf));
            resbonse.copy(return_buf, resbonse.size(), 0);
            _client_addr.sin_port = htons(_client_sock_port);
            sendto(_sys_sock, return_buf, 100, 0, (struct sockaddr*)&_client_addr, sizeof(_client_addr));
            std::cout<<resbonse<<std::endl;
            return;
        }
        
        std::vector<std::string>::iterator it;
        bool if_find=false;
        for(it=_map_groupip_groupmem[_map_groupname_groupip[msg.msg_0]].begin();it!=_map_groupip_groupmem[_map_groupname_groupip[msg.msg_0]].end();++it){
            if(*it == inet_ntoa(_client_addr.sin_addr)) it=_map_groupip_groupmem[_map_groupname_groupip[msg.msg_0]].erase(it);
            
            if_find=true;
            std::string resbonse = msg.task_id+"#31#已退出";
            char return_buf[100];
            memset(return_buf,'\0',sizeof(return_buf));
            resbonse.copy(return_buf, resbonse.size(), 0);
            _client_addr.sin_port = htons(_client_sock_port);
            sendto(_sys_sock, return_buf, 100, 0, (struct sockaddr*)&_client_addr, sizeof(_client_addr));
            std::cout<<"任务id： "<<msg.task_id<<" ip："<<inet_ntoa(_client_addr.sin_addr)<<"已退出组:"<<msg.msg_0<<" 本组组播ip："<<_map_groupname_groupip[msg.msg_0]<<std::endl;
            break;
        }
        if(!if_find){
            std::string resbonse = msg.task_id+"#31#组内没有当前IP";
            char return_buf[100];
            memset(return_buf,'\0',sizeof(return_buf));
            resbonse.copy(return_buf, resbonse.size(), 0);
            _client_addr.sin_port = htons(_client_sock_port);
            sendto(_sys_sock, return_buf, 100, 0, (struct sockaddr*)&_client_addr, sizeof(_client_addr));
            std::cout<<resbonse<<std::endl;
        }
    }
    //单播
    static void process_40(msg_struct msg, sockaddr_in _client_addr, int _sys_sock){
        if(_map_name_ip.count(msg.msg_0) == 0){//此时IP没有接入
            std::string resbonse = msg.task_id+"#43#此IP尚未接入";
            char return_buf[100];
            memset(return_buf,'\0',sizeof(return_buf));
            resbonse.copy(return_buf, resbonse.size(), 0);
            _client_addr.sin_port = htons(_client_sock_port);
            sendto(_sys_sock, return_buf, 100, 0, (struct sockaddr*)&_client_addr, sizeof(_client_addr));
            return;
        }

        struct sockaddr_in temp_addr;
        std::string resbonse = msg.task_id+"#41#"+msg.msg_1;
        char return_buf[1024];
        memset(return_buf,'\0',sizeof(return_buf));
        resbonse.copy(return_buf, resbonse.size(), 0);

        memset(&temp_addr, 0, sizeof(temp_addr));
        temp_addr.sin_family = AF_INET;
        temp_addr.sin_addr.s_addr = inet_addr(_map_name_ip[msg.msg_0].data());
        //temp_addr.sin_addr.s_addr = htonl(INADDR_ANY);  //注意网络序转换
        temp_addr.sin_port = htons(_client_sock_port);  //注意网络序转换
        sendto(_sys_sock, return_buf, 1024, 0, (struct sockaddr*)&temp_addr, sizeof(temp_addr));

        std::cout<<"任务id： "<<msg.task_id<<" 源ip："<<inet_ntoa(_client_addr.sin_addr)<<"已经向ip:"<<_map_name_ip[msg.msg_0]<<" 发出单播消息："<<resbonse<<std::endl;
    }
    //组播
    static void process_50(msg_struct msg, sockaddr_in _client_addr, int _sys_sock){
        if(_map_groupname_groupip.count(msg.msg_0) == 0){//这个组名不存在时
            std::string resbonse = msg.task_id+"#53#此组不存在";
            char return_buf[100];
            memset(return_buf,'\0',sizeof(return_buf));
            resbonse.copy(return_buf, resbonse.size(), 0);
            _client_addr.sin_port = htons(_client_sock_port);
            sendto(_sys_sock, return_buf, 100, 0, (struct sockaddr*)&_client_addr, sizeof(_client_addr));
            std::cout<<resbonse<<std::endl;
            return;
        }
        std::string groupmsg_ip(_map_groupname_groupip[msg.msg_0]);
        std::cout<<groupmsg_ip<<std::endl;

        std::string txt = msg.task_id+"#51#"+msg.msg_1;

        send_groupmsg(_map_groupname_groupip[msg.msg_0], txt);
        std::cout<<"任务id： "<<msg.task_id<<" 源ip："<<inet_ntoa(_client_addr.sin_addr)<<"已经向组:"<<msg.msg_0<<"（组播ip为："<<_map_groupname_groupip[msg.msg_0]<<"）发出组播消息："<<txt<<std::endl;
    }
    //发送组播消息
    static int send_groupmsg(std::string groupmsg_ip, std::string msg){
        int _groupmsg_sock;//用于组播通信的socket
        struct ip_mreq _groupmsg_dst_ip;//设定组播ip
        struct sockaddr_in _groupmsg_sock_addr;//设定组播ip和端口

        //为socket设置组播发送方式
        memset(&_groupmsg_dst_ip, 0, sizeof(_groupmsg_dst_ip));
        inet_pton(AF_INET, groupmsg_ip.data(), &_groupmsg_dst_ip.imr_multiaddr);//设置组播目标IP
        inet_pton(AF_INET, "0.0.0.0", &_groupmsg_dst_ip.imr_interface);//使用的本机IP，0.0.0.0表示本机上的所有IP均可使用此服务
        
        setsockopt(_groupmsg_sock, IPPROTO_IP, IP_MULTICAST_IF, &_groupmsg_dst_ip, sizeof(_groupmsg_dst_ip));// 设置发送数据包是组播方式发送

        //构建发送目标结构体
        memset(&_groupmsg_sock_addr, 0, sizeof(_groupmsg_sock_addr));
        _groupmsg_sock_addr.sin_family		= AF_INET;
        _groupmsg_sock_addr.sin_port		= htons(_client_sock_port);		// 目标端口
        inet_pton(AF_INET, groupmsg_ip.data(), &_groupmsg_sock_addr.sin_addr.s_addr);// 目标的组地址

        char buf[1024];
        msg.copy(buf, msg.size(), 0);
        *(buf + msg.size()) = '\0';

        sendto(_groupmsg_sock, buf, strlen(buf), 0, (struct sockaddr*)& _groupmsg_sock_addr, sizeof( _groupmsg_sock_addr));

        return 1;
    }

public:
    MessageManager(){
        register_msg_handler("00", process_00);
        register_msg_handler("10", process_10);
        register_msg_handler("20", process_20);
        register_msg_handler("30", process_30);
        register_msg_handler("40", process_40);
        register_msg_handler("50", process_50);
    };
    void register_msg_handler(std::string task_type, process_func func){
        _map_msg_handler[task_type]=func;
    }
    void on_message(msg_struct msg, sockaddr_in &_client_addr, int _sys_sock){
        (_map_msg_handler[msg.task_type])(msg, _client_addr, _sys_sock);
    }
};

class ThreadPool{
    // 线程池
    std::vector<std::thread> pool;
    // 任务队列
    std::queue<int> tasks;
    // 同步
    std::mutex m_lock;
    // 条件阻塞
    std::condition_variable cv_task;
    // 是否关闭提交
    std::atomic<bool> stoped;
    //空闲线程数量
    std::atomic<int>  idlThrNum;

public:
    inline ThreadPool() :idlThrNum(20), stoped(false){
        int size;
        for (size = 0; size < idlThrNum; ++size)
        {   //初始化线程数量
            pool.emplace_back(
                [this]
                { // 工作线程函数
                    MessageManager msg_manger;
                    while(!this->stoped)
                    {
                        {   // 获取一个待执行的 task
                            std::unique_lock<std::mutex> lock{ this->m_lock };// unique_lock 相比 lock_guard 的好处是：可以随时 unlock() 和 lock()
                            this->cv_task.wait(lock,
                                [this] {
                                    return this->stoped.load() || !this->tasks.empty();
                                }
                            ); // wait 直到有 task
                            if (this->stoped && this->tasks.empty())
                                return;
                            msg_manger._sys_sock = this->tasks.front(); // 取一个 task
                            this->tasks.pop();
                        }
                        idlThrNum--;
                        {
                            //从sock中接收消息
                            char recvbuf[1024];
                            struct sockaddr_in client_addr;
                            socklen_t cli_len=sizeof(client_addr);
                            int ret = recvfrom(msg_manger._sys_sock, recvbuf, 1000, 0, (struct sockaddr *)&client_addr, &cli_len);

                            if (ret <= 0){
                                printf("接受消息出错");
                                return;
                            }
                            std::cout<<"socket "<<msg_manger._sys_sock<<" 接收到来自:"<<inet_ntoa(client_addr.sin_addr)<<":"<<ntohs(client_addr.sin_port)<<"的消息:"<<recvbuf<<std::endl;
                            
                            /*
                            消息的基本格式是MD1204#00#XXXXX
                            */
                            std::string recv_msg(recvbuf);
                            //printf(recvbuf);
                            msg_struct msg_part = split(recv_msg ,"#");
                            if(msg_part.task_id.substr(0, 2)!="MD"){
                                printf("接受消息格式出错");
                                return;
                            }
                            msg_manger.on_message(msg_part, client_addr, msg_manger._sys_sock);
                        }
                        idlThrNum++;
                    }
                }
            );
        }
    }

    msg_struct split(const std::string& str,const std::string& delim) { 
        msg_struct res;
        res.task_id="";
        res.msg_0="";
        res.msg_1="";
        res.task_type="";

        if("" == str) return  res;
        
        std::string strs = str + delim; //*****扩展字符串以方便检索最后一个分隔出的字符串
        size_t pos;
        size_t size = strs.size();
        std::vector<std::string> msg_part;
        for (int i = 0; i < size; ++i) {
            pos = strs.find(delim, i); //pos为分隔符第一次出现的位置，从i到pos之前的字符串是分隔出来的字符串
            if( pos < size) { //如果查找到，如果没有查找到分隔符，pos为string::npos
                std::string s = strs.substr(i, pos - i);//*****从i开始长度为pos-i的子字符串
                msg_part.push_back(s);//两个连续空格之间切割出的字符串为空字符串，这里没有判断s是否为空，所以最后的结果中有空字符的输出，
                i = pos + delim.size() - 1;
            }
        }

        res.task_id=msg_part[0];
        res.task_type=msg_part[1];
        res.msg_0=msg_part[2];
        if(msg_part.size()>3) res.msg_1=msg_part[3];

        return res;	
    }
    //ThreadPool() :idlThrNum{20},stoped{ false }{};
    inline ~ThreadPool()
    {
        stoped.store(true);
        cv_task.notify_all(); // 唤醒所有线程执行
        for (std::thread& thread : pool) {
            //thread.detach(); // 让线程“自生自灭”
            if(thread.joinable())
                thread.join(); // 等待任务结束， 前提：线程一定会执行完
        }
    }

public:
    // 提交一个任务

    void commit(int sock){
        if (stoped.load())    // stop == true ??
            throw std::runtime_error("commit on ThreadPool is stopped.");

        {    // 添加任务到队列
            std::lock_guard<std::mutex> lock{ m_lock };//对当前块的语句加锁  lock_guard 是 mutex 的 stack 封装类，构造的时候 lock()，析构的时候 unlock()
            tasks.emplace(sock);
        }
        cv_task.notify_one(); // 唤醒一个线程执行

        return;
    }

    //空闲线程数量
    int idlCount() { return idlThrNum; }
};

class Daemon{
public:
    Daemon(){
        //初始化用于系统通信的socket
        _sys_sock = socket(AF_INET, SOCK_DGRAM, 0);
        if(_sys_sock < 0){
            printf("create socket fail!\n");
            return;
        }

        //设置本机地址
        memset(&_sys_sock_addr, 0, sizeof(_sys_sock_addr));
        _sys_sock_addr.sin_family = AF_INET;
        _sys_sock_addr.sin_addr.s_addr = htonl(INADDR_ANY); //IP地址，需要进行网络序转换，INADDR_ANY：本地所有地址
        _sys_sock_addr.sin_port = htons(_sys_sock_port);  //端口号，需要网络序转换
        
        //设置复用及非阻塞
        int opt=SO_REUSEADDR;
        setsockopt(_sys_sock,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
        setnonblocking(_sys_sock);

        //将_sys_sock和_sys_sock_addr绑定
        int ret = bind(_sys_sock, (struct sockaddr*)&_sys_sock_addr, sizeof(_sys_sock_addr));

        //设置epoll
        struct epoll_event ev;
        _epoll_fd = epoll_create(MAXEPOLLSIZE);
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = _sys_sock;
        epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, _sys_sock, &ev);

        //初始化MessageManager的静态成员变量
    };

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
            //std::cout<<"mapsize: "<<_map_name_ip.size()<<std::endl;
            //std::cout<<epoll_fd_num<<std::endl;
            for(int n = 0; n < epoll_fd_num; ++n){
                if (events[n].data.fd == _sys_sock){
                    //Test t(events[n].data.fd);
                    _thread_pool.commit(events[n].data.fd);
                }
            }
        }
    }
private:
    int _epoll_fd;//用于守护进程io多路复用的epoll
    int _sys_sock;//用于接受消息的socket
    struct sockaddr_in _sys_sock_addr;//用于系统消息通信的地址
    const int _sys_sock_port = 9000;//用于系统消息通信的默认端口
    const int MAXEPOLLSIZE = 100;//epoll最大连接数
    struct epoll_event events[100];//记录到来的事件
    ThreadPool _thread_pool;
};

int main(){

    Daemon dae;
    //dae.send_groupmsg("239.0.0.2", "hello wells");
    dae.event_loop();
}