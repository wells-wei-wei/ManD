#ifndef _CLIENT_H_
#define _CLIENT_H_

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
#include <memory>

//定义需要用到的线程安全容器
namespace thread_safe_utils{
    
    //线程安全队列
    template <class T>
    class queue {
    protected:
        // Data
        std::queue<T> _queue;
        typename std::queue<T>::size_type _size_max;
    
        // Thread gubbins
        std::mutex _mutex;
        std::condition_variable _fullQue;
        std::condition_variable _empty;
    
        // Exit
        // 原子操作
        std::atomic_bool _quit; //{ false };
        std::atomic_bool _finished; // { false };
    
    public:
        queue(const size_t size_max) :_size_max(size_max) {
            _quit = ATOMIC_VAR_INIT(false);
            _finished = ATOMIC_VAR_INIT(false);
        }
        queue() :_size_max(500) {
            _quit = ATOMIC_VAR_INIT(false);
            _finished = ATOMIC_VAR_INIT(false);
        }
    
        bool push(T& data)
        {
            std::unique_lock<std::mutex> lock(_mutex);
            while (!_quit && !_finished)
            {
                if (_queue.size() < _size_max)
                {
                    _queue.push(std::move(data));
                    //_queue.push(data);
                    _empty.notify_all();
                    return true;
                }
                else
                {
                    // wait的时候自动释放锁，如果wait到了会获取锁
                    _fullQue.wait(lock);
                }
            }
    
            return false;
        }
    
    
        bool pop(T &data)
        {
            std::unique_lock<std::mutex> lock(_mutex);
            while (!_quit)
            {
                if (!_queue.empty())
                {
                    //data = std::move(_queue.front());
                    data = _queue.front();
                    _queue.pop();
    
                    _fullQue.notify_all();
                    return true;
                }
                else if (_queue.empty() && _finished)
                {
                    return false;
                }
                else
                {
                    _empty.wait(lock);
                }
            }
            return false;
        }
    
        // The queue has finished accepting input
        void finished()
        {
            _finished = true;
            _empty.notify_all();
        }
    
        void quit()
        {
            _quit = true;
            _empty.notify_all();
            _fullQue.notify_all();
        }
    
        int length()
        {
            return static_cast<int>(_queue.size());
        }
    };
    
    //线程安全字典
    template<typename TKey, typename TValue>
    class map
    {
    public:
        map() 
        {
        }

        virtual ~map() 
        { 
            std::lock_guard<std::mutex> locker(m_mutexMap);
            m_map.clear(); 
        }

        bool insert(const TKey &key, const TValue &value, bool cover = false)
        {
            std::lock_guard<std::mutex> locker(m_mutexMap);

            auto find = m_map.find(key);
            if (find != m_map.end() && cover)
            {
                m_map.erase(find);
            }

            auto result = m_map.insert(std::pair<TKey, TValue>(key, value));
            return result.second;
        }

        void remove(const TKey &key)
        {
            std::lock_guard<std::mutex> locker(m_mutexMap);

            auto find = m_map.find(key);
            if (find != m_map.end())
            {
                m_map.erase(find);
            }
        }

        bool lookup(const TKey &key, TValue &value)
        {
            std::lock_guard<std::mutex> locker(m_mutexMap);

            auto find = m_map.find(key);
            if (find != m_map.end())
            {
                value = (*find).second;
                return true;
            }
            else
            {
                return false;
            }
        }

        int size()
        {
            std::lock_guard<std::mutex> locker(m_mutexMap);
            return m_map.size();
        }

    public:
        std::mutex m_mutexMap;
        std::map<TKey, TValue> m_map;
    };
}

class client{
public:
    //连接daemon
    void connect(std::string name){
        std::string task_id = "MD"+strRand(4);
        std::string msg = task_id+"#00#"+name;
        char buf[1024];
        memset(buf,'\0',sizeof(buf));
        msg.copy(buf, msg.size(), 0);
        sendto(client_sock, buf, BUFF_LEN, 0, (struct sockaddr*)&daemon_addr, sizeof(daemon_addr));

        std::string resonse_from_daemon;
        while(1){
            if(_map_receive_sys_msg.lookup(task_id, resonse_from_daemon)){
                if(resonse_from_daemon == "连接成功") return;
            }
        }
    }

    //与daemon断开连接
    void disconnect(std::string name){
        std::string task_id = "MD"+strRand(4);
        std::string msg = task_id+"#10#"+name;
        char buf[1024];
        memset(buf,'\0',sizeof(buf));
        msg.copy(buf, msg.size(), 0);
        sendto(client_sock, buf, BUFF_LEN, 0, (struct sockaddr*)&daemon_addr, sizeof(daemon_addr));

        std::string resonse_from_daemon;
        while(1){
            if(_map_receive_sys_msg.lookup(task_id, resonse_from_daemon)){
                if(resonse_from_daemon == "已断开连接") return;
            }
        }
    }

    //加入组
    void join(std::string name){
        std::string task_id = "MD"+strRand(4);
        std::string msg = task_id+"#20#"+name;
        //std::cout<<msg<<std::endl;
        char buf[1024];
        memset(buf,'\0',sizeof(buf));
        msg.copy(buf, msg.size(), 0);
        //printf(buf);
        //std::cout<<""<<std::endl;
        sendto(client_sock, buf, BUFF_LEN, 0, (struct sockaddr*)&daemon_addr, sizeof(daemon_addr));

        std::string resonse_from_daemon;
        while(1){
            if(_map_receive_sys_msg.lookup(task_id, resonse_from_daemon)){
                struct ip_mreqn group;
                inet_pton(AF_INET, resonse_from_daemon.data(), &group.imr_multiaddr);
                inet_pton(AF_INET, "0.0.0.0", &group.imr_address);
                group.imr_ifindex = if_nametoindex("eth0");
                setsockopt(client_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &group, sizeof(group));

                return;
            }
        }
    }

    //退出组
    void drop(std::string name){
        std::string task_id = "MD"+strRand(4);
        std::string msg = task_id+"#30#"+name;
        char buf[1024];
        memset(buf,'\0',sizeof(buf));
        msg.copy(buf, msg.size(), 0);
        sendto(client_sock, buf, BUFF_LEN, 0, (struct sockaddr*)&daemon_addr, sizeof(daemon_addr));

        std::string resonse_from_daemon;
        while(1){
            if(_map_receive_sys_msg.lookup(task_id, resonse_from_daemon)){
                struct ip_mreqn group;
                inet_pton(AF_INET, resonse_from_daemon.data(), &group.imr_multiaddr);
                inet_pton(AF_INET, "0.0.0.0", &group.imr_address);
                group.imr_ifindex = if_nametoindex("eth0");
                setsockopt(client_sock, IPPROTO_IP, IP_DROP_MEMBERSHIP, &group, sizeof(group));
                return;
            }
        }
    }

    //单播
    void unicast(std::string dst, std::string txt){
        std::string task_id = "MD"+strRand(4);
        std::string msg = task_id+"#40#"+dst+"#"+txt;
        char buf[1024];
        memset(buf,'\0',sizeof(buf));
        msg.copy(buf, msg.size(), 0);
        sendto(client_sock, buf, BUFF_LEN, 0, (struct sockaddr*)&daemon_addr, sizeof(daemon_addr));

        std::string resonse_from_daemon;
        while(1){
            if(_map_receive_sys_msg.lookup(task_id, resonse_from_daemon)){
                if(resonse_from_daemon == "已收到") return;
            }
        }
    }

    //组播
    void multicast(std::string dst, std::string txt){
        std::string task_id = "MD"+strRand(4);
        std::string msg = task_id+"#50#"+dst+"#"+txt;
        char buf[1024];
        memset(buf,'\0',sizeof(buf));
        msg.copy(buf, msg.size(), 0);
        sendto(client_sock, buf, BUFF_LEN, 0, (struct sockaddr*)&daemon_addr, sizeof(daemon_addr));

        std::string resonse_from_daemon;
        while(1){
            if(_map_receive_sys_msg.lookup(task_id, resonse_from_daemon)){
                if(resonse_from_daemon == "已收到") return;
            }
        }
    }

    //返回现在已经收到的消息数量
    bool mail_size(int & size){
        size=_que_receive_msg.length();
        return size > 0 ? true:false;
    }

    //返回消息
    std::string receive(){
        std::string res;
        if(_que_receive_msg.pop(res)){
            return res;
        };
        return "";
    }

    //获取本类的单例
    static client* get_instance(std::string daemon_ip, int daemon_port){
        _singleton=new client(daemon_ip, daemon_port);
        return _singleton;
    }

    //析构函数
    ~client(){
        if_receive = false;
    }
private:
    //构造函数
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

        std::thread receive_thread(receive_all);
        receive_thread.detach();
    };

    client(const client& obj)=delete;
    client& operator=(const client& obj)=delete;

    inline static thread_safe_utils::map<std::string, std::string> _map_receive_sys_msg;//用于记录所有收到的系统消息
    inline static thread_safe_utils::queue<std::string> _que_receive_msg;//用于记录所有收到的单播或者组播消息

    int ret;

    std::string daemon_ip;//需要连接的daemon的ip
    int daemon_port;//需要连接的daemon的端口
    struct sockaddr_in daemon_addr;//需要连接的daemon的地址

    inline static client* _singleton;//本类的单例

    int client_sock;//本客户端用于通信的socket
    const int client_port = 8080;//本客户端使用的端口
    struct sockaddr_in client_addr;//本客户端使用的地址

    bool if_receive = true;//标志是否接受消息，用于在本对象析构时停止接受消息线程
    const int BUFF_LEN = 1024;

    //接受消息线程，本对象建立之后独立运行，持续接受消息并存在map和queue中
    static void receive_all(){ 
        while(_singleton->if_receive){
            char buf[_singleton->BUFF_LEN];
            memset(buf,'\0',sizeof(buf));
            struct sockaddr_in source_addr;
            socklen_t len=sizeof(source_addr);
            int count = recvfrom(_singleton->client_sock, buf, _singleton->BUFF_LEN, 0, (struct sockaddr*)&source_addr, &len);  //recvfrom是拥塞函数，没有数据就一直拥塞
            if(count == -1)
            {
                printf("recieve data fail!\n");
                continue;
            }
            std::string recv_msg(buf);
            std::cout<<recv_msg<<std::endl;
            std::vector<std::string> msg_part = _singleton->split(recv_msg ,"#");
            if(msg_part.size()<3 || msg_part[0].substr(0, 2)!="MD") continue;

            if(msg_part[1]=="41" || msg_part[1]=="51"){
                _que_receive_msg.push(msg_part[2]);
                std::string resbonse = msg_part[0]+"#"+msg_part[1][0]+"2#已收到";
                std::cout<<resbonse<<std::endl;
                char return_buf[100];
                memset(return_buf,'\0',sizeof(return_buf));
                resbonse.copy(return_buf, resbonse.size(), 0);

                if(msg_part[1]=="51"){
                    memset(&source_addr, 0, sizeof(source_addr));
                    source_addr.sin_family		= AF_INET;
                    source_addr.sin_port		= htons(_singleton->daemon_port);		// 目标端口
                    inet_pton(AF_INET, _singleton->daemon_ip.data(), &source_addr.sin_addr.s_addr);// 目标的组地址
                }

                sendto(_singleton->client_sock, return_buf, 100, 0, (struct sockaddr*)&source_addr, sizeof(source_addr));
            } 
            if(msg_part[1]=="01" || msg_part[1]=="11" || msg_part[1]=="21" || msg_part[1]=="31" || msg_part[1]=="43" || msg_part[1]=="53") _map_receive_sys_msg.insert(msg_part[0], msg_part[2]);
        }
        return;
    }

    //分割字符串，用于处理接受的信息
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
    
    //生成随机字符串，用于生成任务id
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

// int main(){
//     std::shared_ptr<client> cli(client::get_instance("10.112.212.188", 9000));

//     // cli->connect("user2");

//     // int num;
//     // while(!cli->mail_size(num)){} 
//     // std::cout<<cli->receive()<<std::endl;

//     cli->join("d2");
//     int num;
//     while(!cli->mail_size(num)){} 
//     std::cout<<cli->receive()<<std::endl;

//     //cli.drop("d");
//     //cli.unicast("user1","hellop");
//     //cli->multicast("d","hellopd");

//     //std::cout<<cli.receive()<<std::endl;
//     //cli->multicast("d2","wells");
    
// }

#endif