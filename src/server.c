#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include "tcp_server.h"

// gcc -Ilib/tcp/include lib/tcp/src/tcp_server.c src/server.c

int main(int argc, char* argv[]){
    int sockfd, new_sockfd;
    pid_t p;

    printf("Open TCP server\n");
    sockfd=open_tcp_socket();
    printf("socket server: %d\n",sockfd);

    while(1){
        new_sockfd=accept_connection(sockfd);
        if(new_sockfd < 0){
            continue;
        }
        p=fork();
        if(p<0){
            printf("[ERROR] Failed to manage new connection\n");
            close(new_sockfd);
            continue;
        }
        if (p == 0){
            //child process
            char *header;
            int bytes_rcv;
            close(sockfd);
                //TODO: codice per gestire il file
            printf("[DEBUG] child socket: %d\n",new_sockfd);
            bytes_rcv=receive_data(new_sockfd,&header);
            printf("bytes: %d raw: %s\n",bytes_rcv,header);
            close(new_sockfd);
            exit(0);
        }else{
            //dad process
            printf("[DEBUG] dad socket: %d\n",sockfd);
            close(new_sockfd);
        }
    }
}