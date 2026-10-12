#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include "tcp_server.h"

// gcc -Ilib/tcp/include lib/tcp/src/tcp_server.c src/server.c

typedef struct Header{
    uint8_t type; //message type
    uint32_t filepath_len; //length of filepath string in big endian
    char dest_filepath[256]; //name of file in little endian
    uint64_t file_size; //size of file in big endian

}file_header;

void header_initialization(char* data){

}

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
            char *data;
            file_header *header;
            int bytes_rcv;
            close(sockfd);

                //TODO: codice per gestire il file

            receive_data(new_sockfd,&data,sizeof(file_header));

            printf("bytes: %d raw: %s\n",bytes_rcv,data);
            
            //header initialization
            header=malloc(sizeof(header));
            if(header == NULL){
                printf("[ERROR] Memory failure\n");
                return -1;
            }
            memcpy(header,data,sizeof(file_header));

            close(new_sockfd);
            free(data);
            exit(0);
        }else{
            //dad process
            printf("[DEBUG] dad socket: %d\n",sockfd);
            close(new_sockfd);
        }
    }
}