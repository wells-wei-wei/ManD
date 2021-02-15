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

using namespace std;

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
            for(int n = 0; n < epoll_fd_num; ++n){
                if (events[n].data.fd == _sys_sock){
                    test(events[n].data.fd);
                }
            }
        }
    }

    int test(int sock){
        char recvbuf[1000];
        struct sockaddr_in client_addr;
        socklen_t cli_len=sizeof(client_addr);
        int ret = recvfrom(sock, recvbuf, 1000, 0, (struct sockaddr *)&client_addr, &cli_len);
        if (ret > 0)
        {
            printf("socket %d 接收到来自:%s:%d的消息成功:’%s’，共%d个字节的数据/n",
            sock, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), recvbuf, ret);
        }
    }

    int send_groupmsg(string groupmsg_ip, string msg){
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

    string _groupip_ori = "239.0.0.";
    string _groupip_last = "1";

    map<string, string> _map_groupname_groupip;
    map<string, vector<string>> _map_groupip_groupmem;
};