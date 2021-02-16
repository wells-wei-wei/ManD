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

std::map<std::string, std::string> _map_name_ip;
std::map<std::string, std::string> _map_groupname_groupip;
std::map<std::string, std::vector<std::string>> _map_groupip_groupmem;
std::string _groupip_ori = "239.0.0.";
int _groupip_last = 2;
int ret;

int _epoll_fd;

const int MAXEPOLLSIZE = 100;
const int MAXLINE = 1500;

struct epoll_event events[100];

int _sys_sock;//用于系统消息通信的socket（同时也用于单播）
int _groupmsg_sock;//用于组播通信的socket

struct sockaddr_in _sys_sock_addr;//用于系统消息通信的地址
const int _sys_sock_port = 9000;//用于系统消息通信的默认端口
const int _client_sock_port = 8080;
struct ip_mreq _groupmsg_dst_ip;
struct sockaddr_in _groupmsg_sock_addr;

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
            //std::cout<<"mapsize: "<<_map_name_ip.size()<<std::endl;
            //std::cout<<epoll_fd_num<<std::endl;
            for(int n = 0; n < epoll_fd_num; ++n){
                if (events[n].data.fd == _sys_sock){
                    //Test t(events[n].data.fd);
                    _thread_pool.commit(process_msg, _sys_sock);
                }
            }
        }
    }

    static std::vector<std::string> split(const std::string& str,const std::string& delim) { 
        std::vector<std::string> res;
        if("" == str) return  res;
        
        std::string strs = str + delim; //*****扩展字符串以方便检索最后一个分隔出的字符串
        size_t pos;
        size_t size = strs.size();
    
        for (int i = 0; i < size; ++i) {
            pos = strs.find(delim, i); //pos为分隔符第一次出现的位置，从i到pos之前的字符串是分隔出来的字符串
            if( pos < size) { //如果查找到，如果没有查找到分隔符，pos为string::npos
                std::string s = strs.substr(i, pos - i);//*****从i开始长度为pos-i的子字符串
                res.push_back(s);//两个连续空格之间切割出的字符串为空字符串，这里没有判断s是否为空，所以最后的结果中有空字符的输出，
                i = pos + delim.size() - 1;
            }
            
        }
        return res;	
    }
    static void process_msg(int sock){

        char recvbuf[1000];
        struct sockaddr_in client_addr;
        socklen_t cli_len=sizeof(client_addr);
        int ret = recvfrom(sock, recvbuf, 1000, 0, (struct sockaddr *)&client_addr, &cli_len);
        std::cout<<"socket "<<sock<<" 接收到来自:"<<inet_ntoa(client_addr.sin_addr)<<":"<<ntohs(client_addr.sin_port)<<"的消息成功:"<<recvbuf<<"，共"<<ret<<"个字节的数据"<<std::endl;
        if (ret <= 0){
            printf("接受消息出错");
            return;
        }
        /*
        消息的基本格式是MD1204#00#XXXXX
        */
        std::string recv_msg(recvbuf);
        //printf(recvbuf);
        std::vector<std::string> msg_part = split(recv_msg ,"#");
        if(msg_part.size()<3 || msg_part[0].substr(0, 2)!="MD"){
            printf("接受消息格式出错");
            return;
        }
        if(msg_part[1][0]=='0'){//连接daemon
            _map_name_ip[msg_part[2]]=inet_ntoa(client_addr.sin_addr);
            char return_buf[13] = "连接成功";
            client_addr.sin_port = htons(_client_sock_port);
            sendto(sock, return_buf, 13, 0, (struct sockaddr*)&client_addr, sizeof(client_addr));
        }
        else if(msg_part[1][0]=='1'){//断开连接daemon
            if(_map_name_ip.count(msg_part[2]) == 0){
                char return_buf[18] = "此IP尚未连接";
                client_addr.sin_port = htons(_client_sock_port);
                sendto(sock, return_buf, 18, 0, (struct sockaddr*)&client_addr, sizeof(client_addr));
            }
            _map_name_ip.erase(msg_part[2]);

            char return_buf[20] = "已断开连接";
            client_addr.sin_port = htons(_client_sock_port);
            sendto(sock, return_buf, 20, 0, (struct sockaddr*)&client_addr, sizeof(client_addr));
        }
        else if(msg_part[1][0]=='2'){//加入组
            if (_map_groupname_groupip.count(msg_part[2]) == 0){//这个组名不存在时
                std::string new_group_ip = _groupip_ori+std::to_string(_groupip_last);
                _groupip_last++;
                _map_groupname_groupip[msg_part[2]]=new_group_ip;
            }
            _map_groupip_groupmem[_map_groupname_groupip[msg_part[2]]].push_back(inet_ntoa(client_addr.sin_addr));
            
            char return_buf[100];
            _map_groupname_groupip[msg_part[2]].copy(return_buf,_map_groupname_groupip[msg_part[2]].size(),0);
            client_addr.sin_port = htons(_client_sock_port);
            sendto(sock, return_buf, 100, 0, (struct sockaddr*)&client_addr, sizeof(client_addr));
        }
        else if(msg_part[1][0]=='3'){//退出组
            if(_map_groupname_groupip.count(msg_part[2]) == 0){//这个组名不存在时
                char return_buf[50] = "组名不存在";
                client_addr.sin_port = htons(_client_sock_port);
                sendto(sock, return_buf, 50, 0, (struct sockaddr*)&client_addr, sizeof(client_addr));
            }
            
            std::vector<std::string>::iterator it;
            bool if_find=false;
            for(it=_map_groupip_groupmem[_map_groupname_groupip[msg_part[2]]].begin();it!=_map_groupip_groupmem[_map_groupname_groupip[msg_part[2]]].end();++it){
                if(*it == inet_ntoa(client_addr.sin_addr)) it=_map_groupip_groupmem[_map_groupname_groupip[msg_part[2]]].erase(it);
                
                if_find=true;
                char return_buf[10]="已退出";
                client_addr.sin_port = htons(_client_sock_port);
                sendto(sock, return_buf, 10, 0, (struct sockaddr*)&client_addr, sizeof(client_addr));
                break;
            }
            if(!if_find){
                char return_buf[50]="组内没有当前IP";
                client_addr.sin_port = htons(_client_sock_port);
                sendto(sock, return_buf, 50, 0, (struct sockaddr*)&client_addr, sizeof(client_addr));
            }
        }
        else if(msg_part[1][0]=='4'){//单播
            if(_map_name_ip.count(msg_part[2]) == 0){//此时IP没有接入
                char return_buf[50] = "此IP尚未接入";
                client_addr.sin_port = htons(_client_sock_port);
                sendto(sock, return_buf, 50, 0, (struct sockaddr*)&client_addr, sizeof(client_addr));
                return;
            }

            struct sockaddr_in temp_addr;
            char return_buf[1024];
            msg_part[3].copy(return_buf,msg_part[3].size(),0);

            memset(&temp_addr, 0, sizeof(temp_addr));
            temp_addr.sin_family = AF_INET;
            temp_addr.sin_addr.s_addr = inet_addr(_map_name_ip[msg_part[2]].data());
            //temp_addr.sin_addr.s_addr = htonl(INADDR_ANY);  //注意网络序转换
            temp_addr.sin_port = htons(_client_sock_port);  //注意网络序转换
            sendto(sock, return_buf, 50, 0, (struct sockaddr*)&temp_addr, sizeof(temp_addr));
        }
        else if(msg_part[1][0]=='5'){//组播
            if(_map_groupname_groupip.count(msg_part[2]) == 0){//这个组名不存在时
                char return_buf[50] = "此组不存在";
                client_addr.sin_port = htons(_client_sock_port);
                sendto(sock, return_buf, 50, 0, (struct sockaddr*)&client_addr, sizeof(client_addr));
                return;
            }
            std::string groupmsg_ip(_map_groupname_groupip[msg_part[2]]);
            send_groupmsg(groupmsg_ip, msg_part[3]);
        }
        return;
    }
    static int send_groupmsg(std::string groupmsg_ip, std::string msg){
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

        char buf[MAXLINE];
        msg.copy(buf, msg.size(), 0);
        *(buf + msg.size()) = '\0';

        sendto(_groupmsg_sock, buf, strlen(buf), 0, (struct sockaddr*)& _groupmsg_sock_addr, sizeof( _groupmsg_sock_addr));

        return 1;
    }
private:
    class ThreadPool{
        using Task = std::function<void()>;
        // 线程池
        std::vector<std::thread> pool;
        // 任务队列
        std::queue<Task> tasks;
        // 同步
        std::mutex m_lock;
        // 条件阻塞
        std::condition_variable cv_task;
        // 是否关闭提交
        std::atomic<bool> stoped;
        //空闲线程数量
        std::atomic<int>  idlThrNum;

    public:
        inline ThreadPool() :idlThrNum(20), stoped(false)
        {
            int size;
            for (size = 0; size < idlThrNum; ++size)
            {   //初始化线程数量
                pool.emplace_back(
                    [this]
                    { // 工作线程函数
                        while(!this->stoped)
                        {
                            std::function<void()> task;
                            {   // 获取一个待执行的 task
                                std::unique_lock<std::mutex> lock{ this->m_lock };// unique_lock 相比 lock_guard 的好处是：可以随时 unlock() 和 lock()
                                this->cv_task.wait(lock,
                                    [this] {
                                        return this->stoped.load() || !this->tasks.empty();
                                    }
                                ); // wait 直到有 task
                                if (this->stoped && this->tasks.empty())
                                    return;
                                task = std::move(this->tasks.front()); // 取一个 task
                                this->tasks.pop();
                            }
                            idlThrNum--;
                            task();
                            idlThrNum++;
                        }
                    }
                );
            }
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
        // 调用.get()获取返回值会等待任务执行完,获取返回值
        // 有两种方法可以实现调用类成员，
        // 一种是使用   bind： .commit(std::bind(&Dog::sayHello, &dog));
        // 一种是用 mem_fn： .commit(std::mem_fn(&Dog::sayHello), &dog)
        template<class F, class... Args>
        auto commit(F&& f, Args&&... args) ->std::future<decltype(f(args...))>
        {
            if (stoped.load())    // stop == true ??
                throw std::runtime_error("commit on ThreadPool is stopped.");

            using RetType = decltype(f(args...)); // typename std::result_of<F(Args...)>::type, 函数 f 的返回值类型
            auto task = std::make_shared<std::packaged_task<RetType()> >(
                std::bind(std::forward<F>(f), std::forward<Args>(args)...)
                );    // wtf !
            std::future<RetType> future = task->get_future();
            {    // 添加任务到队列
                std::lock_guard<std::mutex> lock{ m_lock };//对当前块的语句加锁  lock_guard 是 mutex 的 stack 封装类，构造的时候 lock()，析构的时候 unlock()
                tasks.emplace(
                    [task]()
                    { // push(Task{...})
                        (*task)();
                    }
                );
            }
            cv_task.notify_one(); // 唤醒一个线程执行

            return future;
        }

        //空闲线程数量
        int idlCount() { return idlThrNum; }
    } _thread_pool;
};

int main(){
    Daemon dae;
    dae.init();
    //dae.send_groupmsg("239.0.0.2", "hello wells");
    dae.event_loop();
}