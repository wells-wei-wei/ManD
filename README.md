# ManD
## 基本介绍
本项目是一种基于UDP的单播/组播通信服务，旨在使得服务器之间信息交互或者边云协同更加方便。ManD来源于使命一词（Mandate）。
## 基本架构
本项目分为两部分。分别是守护进程和客户端。使用时首先由客户端与守护进程进行连接，之后客户端只需调用命令，即可将单播、组播等任务交予守护进程代为完成。

守护进程可以与某个客户端运行在同一台设备上，也可以单独运行在一台设备上。目前本项目仅支持局域网内的组播。

## 编译
编译daemon：
```
g++ src/daemon.cpp -o bin/daemon -std=c++1z -pthread
g++ main.cpp -I src -o bin/main -std=c++1z -pthread
```
目前项目仅支持c++17

## 使用
1. 启动守护进程
```
./bin/daemon
```
2. 在两个其他的设备（可以是两台服务器或者一台服务器一台边缘设备）上使用客户端提供的函数库进行编程
```c++
#include <client.h>
#include <memory>
#include <iostream>

int main(){
    //初始化客户端，输入守护进程的IP及端口
    std::shared_ptr<client> cli(client::get_instance("10.112.212.188", 9000));

    //与守护进程建立连接，输入当前设备的名称。之后守护进程将会记录本机IP及名称，并能够使用其他功能
    cli->connect("user1");

    //将本机加入组，G1为想要加入的组名，如果还没有当前这个组将会新建这个组。
    cli->join("G1");

    //将本机退出组G1
    //cli.drop("G1");

    //向名为user1的设备发送消息“hello”
    cli->unicast("user1","hello");

    //向组名为G1的组发送组播消息，之后这个组的每个设备都将收到这条消息
    cli->multicast("G1","hello G1");

    //mail_size函数用于表示当前设备收到了多少条别的设备发送来的消息，num即为收到的消息数量，当有消息时返回true，无消息时返回false。
    int num;
    while(!cli->mail_size(num)){} 

    //receive()将会输出目前收到的消息。每次只输出一条，按照先进先出的规则输出。
    std::cout<<cli->receive()<<std::endl;
    
}
```
