#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "tcp_server.h"

// gcc -Ilib/tcp/include lib/tcp/src/tcp_server.c src/server.c

int main(int argc, char* argv[]){
    int sockfd, new_sockfd;
    printf("Open TCP server\n");
    sockfd=open_tcp_socket();
    printf("socket server: %d\n",sockfd);
    new_sockfd=accept_connection(sockfd);
    printf("new socket: %d\n",new_sockfd);
    close(sockfd);
    close(new_sockfd);
}