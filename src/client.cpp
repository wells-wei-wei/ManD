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
#include <random>
#include <string>

class client{
public:
    client(std::string daemon_ip, int daemon_port):daemon_ip(daemon_ip), daemon_port(daemon_port){
        client_sock = socket(AF_INET, SOCK_DGRAM, 0);

        memset(&client_addr, 0, sizeof(client_addr));
        client_addr.sin_family = AF_INET;
        client_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        client_addr.sin_port = htons(client_port);  //注意网络序转换

        ret = bind(client_sock, (struct sockaddr*)&client_addr, sizeof(client_addr));

        memset(&daemon_addr, 0, sizeof(daemon_addr));
        daemon_addr.sin_family = AF_INET;
        daemon_addr.sin_addr.s_addr = inet_addr(daemon_ip.data());
        daemon_addr.sin_port = htons(daemon_port);  //注意网络序转换
    };
    void connect(std::string name){
        std::string task_id = "MD"+strRand(4);
        std::string msg = task_id+"#00#"+name;
        char buf[1024];
        msg.copy(buf, msg.size(), 0);
        sendto(client_sock, buf, BUFF_LEN, 0, (struct sockaddr*)&daemon_addr, sizeof(daemon_addr));
        while(1){
            memset(buf, 0, BUFF_LEN);
            struct sockaddr_in src;
            socklen_t len=sizeof(src);
            recvfrom(client_sock, buf, BUFF_LEN, 0, (struct sockaddr*)&src, &len);
            std::string recv_msg(buf);
            std::vector<std::string> msg_part = split(recv_msg ,"#");
            if(msg_part.size()<3 || msg_part[0]!=task_id || msg_part[1]!="01" || msg_part[2]!="连接成功") continue;
            return;
        }
    }

    void disconnect(std::string name){
        std::string task_id = "MD"+strRand(4);
        std::string msg = task_id+"#10#"+name;
        char buf[1024];
        msg.copy(buf, msg.size(), 0);
        sendto(client_sock, buf, BUFF_LEN, 0, (struct sockaddr*)&daemon_addr, sizeof(daemon_addr));
        while(1){
            memset(buf, 0, BUFF_LEN);
            struct sockaddr_in src;
            socklen_t len=sizeof(src);
            recvfrom(client_sock, buf, BUFF_LEN, 0, (struct sockaddr*)&src, &len);
            std::string recv_msg(buf);
            std::vector<std::string> msg_part = split(recv_msg ,"#");
            if(msg_part.size()<3 || msg_part[0]!=task_id || (msg_part[1]!="11")|| msg_part[2]!="已断开连接") continue;
            return;
        }
    }

    void join(std::string name){
        std::string task_id = "MD"+strRand(4);
        std::string msg = task_id+"#20#"+name;
        char buf[1024];
        msg.copy(buf, msg.size(), 0);
        sendto(client_sock, buf, BUFF_LEN, 0, (struct sockaddr*)&daemon_addr, sizeof(daemon_addr));
        while(1){
            memset(buf, 0, BUFF_LEN);
            struct sockaddr_in src;
            socklen_t len=sizeof(src);
            recvfrom(client_sock, buf, BUFF_LEN, 0, (struct sockaddr*)&src, &len);
            std::string recv_msg(buf);
            std::vector<std::string> msg_part = split(recv_msg ,"#");
            if(msg_part.size()<3 || msg_part[0]!=task_id || (msg_part[1]!="21")) continue;

            struct ip_mreqn group;
            inet_pton(AF_INET, msg_part[2].data(), &group.imr_multiaddr);
            inet_pton(AF_INET, "0.0.0.0", &group.imr_address);
            group.imr_ifindex = if_nametoindex("eth0");
            setsockopt(client_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &group, sizeof(group));
            return;
        }
    }

    void drop(std::string name){
        std::string task_id = "MD"+strRand(4);
        std::string msg = task_id+"#30#"+name;
        char buf[1024];
        msg.copy(buf, msg.size(), 0);
        sendto(client_sock, buf, BUFF_LEN, 0, (struct sockaddr*)&daemon_addr, sizeof(daemon_addr));
        while(1){
            memset(buf, 0, BUFF_LEN);
            struct sockaddr_in src;
            socklen_t len=sizeof(src);
            recvfrom(client_sock, buf, BUFF_LEN, 0, (struct sockaddr*)&src, &len);
            std::string recv_msg(buf);
            std::vector<std::string> msg_part = split(recv_msg ,"#");
            if(msg_part.size()<3 || msg_part[0]!=task_id || (msg_part[1]!="31")|| msg_part[2]!="已退出") continue;
            return;
        }
    }

    void unicast(std::string dst, std::string txt){
        std::string task_id = "MD"+strRand(4);
        std::string msg = task_id+"#40#"+dst+"#"+txt;
        char buf[1024];
        msg.copy(buf, msg.size(), 0);
        sendto(client_sock, buf, BUFF_LEN, 0, (struct sockaddr*)&daemon_addr, sizeof(daemon_addr));
        while(1){
            memset(buf, 0, BUFF_LEN);
            struct sockaddr_in src;
            socklen_t len=sizeof(src);
            recvfrom(client_sock, buf, BUFF_LEN, 0, (struct sockaddr*)&src, &len);
            std::string recv_msg(buf);
            std::vector<std::string> msg_part = split(recv_msg ,"#");
            if(msg_part.size()<3 || msg_part[0]!=task_id || (msg_part[1]!="43")|| msg_part[2]!="已收到") continue;
            return;
        }
    }

    void multicast(std::string dst, std::string txt){
        std::string task_id = "MD"+strRand(4);
        std::string msg = task_id+"#50#"+dst+"#"+txt;
        char buf[1024];
        msg.copy(buf, msg.size(), 0);
        sendto(client_sock, buf, BUFF_LEN, 0, (struct sockaddr*)&daemon_addr, sizeof(daemon_addr));
        while(1){
            memset(buf, 0, BUFF_LEN);
            struct sockaddr_in src;
            socklen_t len=sizeof(src);
            recvfrom(client_sock, buf, BUFF_LEN, 0, (struct sockaddr*)&src, &len);
            std::string recv_msg(buf);
            std::vector<std::string> msg_part = split(recv_msg ,"#");
            if(msg_part.size()<3 || msg_part[0]!=task_id || (msg_part[1]!="53")|| msg_part[2]!="已收到") continue;
            return;
        }
    }

    std::string receive(){
        while(1){
            char buf[BUFF_LEN];
            struct sockaddr_in source_addr;
            socklen_t len=sizeof(source_addr);
            int count = recvfrom(client_sock, buf, BUFF_LEN, 0, (struct sockaddr*)&source_addr, &len);  //recvfrom是拥塞函数，没有数据就一直拥塞
            if(count == -1)
            {
                printf("recieve data fail!\n");
                return;
            }
            std::string recv_msg(buf);
            std::vector<std::string> msg_part = split(recv_msg ,"#");
            if(msg_part.size()<3 || msg_part[0].substr(0, 2)!="MD" || (msg_part[1]!="41" && msg_part[1]!="51")) continue;

            memset(buf, 0, BUFF_LEN);
            std::string rebonse = msg_part[0]+"#"+msg_part[1][0]+"2#已收到";
            sendto(client_sock, buf, BUFF_LEN, 0, (struct sockaddr*)&source_addr, sizeof(source_addr));
            return msg_part[2];
        }
    }
private:
    int ret;
    std::string daemon_ip;
    int daemon_port;
    struct sockaddr_in daemon_addr;


    int client_sock;
    const int client_port = 8080;
    struct sockaddr_in client_addr;

    const int BUFF_LEN = 1024;

    std::vector<std::string> split(const std::string& str,const std::string& delim) { 
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
    std::string strRand(int length) {			// length: 产生字符串的长度
        char tmp;							// tmp: 暂存一个随机数
        std::string buffer;						// buffer: 保存返回值
        
        // 下面这两行比较重要:
        std::random_device rd;					// 产生一个 std::random_device 对象 rd
        std::default_random_engine random(rd());	// 用 rd 初始化一个随机数发生器 random
        
        for (int i = 0; i < length; i++) {
            tmp = random() % 36;	// 随机一个小于 36 的整数，0-9、A-Z 共 36 种字符
            if (tmp < 10) {			// 如果随机数小于 10，变换成一个阿拉伯数字的 ASCII
                tmp += '0';
            } else {				// 否则，变换成一个大写字母的 ASCII
                tmp -= 10;
                tmp += 'A';
            }
            buffer += tmp;
        }
        return buffer;
    }
};