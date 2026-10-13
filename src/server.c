#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include "tcp_server.h"

// gcc -Ilib/tcp/include lib/tcp/src/tcp_server.c src/server.c

typedef enum {
    MSG_INFO_REQ = 0x01,  // File state request (1 byte)
    MSG_INFO_RES = 0x02,  // Server response (1 byte)
    MSG_CHUNKS   = 0x03   // Send chunk of file (1 byte)
} msg_type_t;

typedef struct Header{
    uint8_t type; //message type
    uint32_t filepath_len; //length of filepath string in big endian
    char dest_filepath[256]; //name of file in little endian
    uint64_t file_size; //size of file in big endian
    uint64_t payload_size; //size of payload
}file_header;

void print_protocol(file_header* header){
    printf("Type: %" PRIu8 "\n",header->type);
    printf("FilePath length: %" PRIu32 "\n",header->filepath_len);
    printf("Destination Filepath: %s\n",header->dest_filepath);
    printf("FileSize: %" PRIu64 "\n",header->file_size);
    //printf("PayloadSize: %" PRIu64 "\n",header->payload_size);
}

uint64_t get_lc_filesize(char *path){
    FILE* fd;
    uint64_t filesize;
    fd = fopen (path, "rb");
    if (fd == NULL) {
        ferror (fd);
        return 0;
    }
    fseek (fd, 0, SEEK_END);
    filesize=ftell(fd);
    fclose(fd);
    return filesize;
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
            uint64_t filesize=0;
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

            if(header->type==MSG_INFO_REQ){
                filesize=get_lc_filesize(header->dest_filepath);
                send_data(new_sockfd,&filesize,sizeof(filesize));
            }else if(header->type==MSG_CHUNKS){
                
            }
            print_protocol(header);

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