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