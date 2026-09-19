#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>
#include <pthread.h>
#include <ctype.h>

#include "tcp_client.h"

/*
 Protocol L7:
 ----------------------------------------------------------------|
 [1 byte ]  msg_type (0x01 = INFO_REQ, 0x02 = RES, 0x03 = CHUNK) |
 ----------------------------------------------------------------|
 [1 byte ]  chunk_id                                             |
 ----------------------------------------------------------------|
 [4 byte ]  filename length (big endian)                         |
 ----------------------------------------------------------------|
 [N byte ]  filename                                             |
 ----------------------------------------------------------------|
 [8 byte]   file size (big endian)                               |
 ----------------------------------------------------------------|
 [8 byte ]  payload size (big endian)                            |
 ----------------------------------------------------------------|
 [M byte ]  payload                                              |
 ----------------------------------------------------------------|
*/

/*
Il buffer circolare funziona semplicemente con un array statico da n elementi:
il writer scrive sulla tail, mentre il reader legge dalla head, ma entrambi (tail e head) sono dei CONTATORI (non puntatori) che si muovono nella stessa direzione
Il writer, prima di sovrascrivere una cella, controlla la head (posizione del reader), se questo non è ancora arrivato a codesta cella si deve bloccare in attesa 
che ci arrivi
*/


// gcc -Ilib/socket_client/include lib/socket_client/src/tcp_client.c src/client.c

typedef enum {
    MSG_INFO_REQ = 0x01,  // File state request (1 byte)
    //MSG_INFO_RES = 0x02,  // Server response (1 byte)
    MSG_CHUNK    = 0x03   // Send chunk of file (1 byte)
} msg_type_t;

typedef struct Header{
    uint8_t type; //message type
    uint32_t fileName_len; //length of filename in big endian
    char *fileName; //name of file in little endian
    uint64_t file_size; //size of file in big endian

}file_header;

typedef struct Node{
    uint8_t id; //chunk ID
    uint64_t payload_size; //size of payload
    unsigned char *payload; //content of payload
}file_node;

typedef struct CircularBuffer{
    int head;
    int tail;
    int count; //used for ambiguity
    int size; //size of circular buffer
    file_node buff[50];
}c_buff;

//args for buff_wrtier function
typedef struct args{
    char *client_path;
    int n_segments;
    int segment_len;
    uint64_t start_bytes;
    c_buff *c_buff;
}writer_args;

//helper function
void help(){

    printf("Chunkly is a tool for transfering files with resume functions\n\n");
    printf("Flags:\n\n");
    printf("    -i  input file to transfer\n");
    printf("    -d  hostname or ip of destination server\n");
    printf("    -o  destination path of server\n");
    printf("    -s  segments lenght (default is 1Mb)\n\n");

    printf("Examples:\n\n");
    printf("    ./chunkly -i <source_file> -d <server_address> -o <server_path>\n");

}

void print_c_buff(c_buff *buff){
    for(int i=0; i<buff->tail; i++){
        printf("Node %d:\n",i);
        printf("ID: %" PRIu8 "\n",buff->buff[i].id);
    }
}

/**
     * Get size of file in bytes from local filesystem.
     * @param path path of file
     * @return size of file or 0 if there are errors
     */
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

/**
     * Get size of file in bytes from destination server filesystem.
     * @param sockfd socket descriptor
     * @param data metadata of file
     * @return size of file
     */
uint64_t get_sv_filesize(int sockfd, file_header *data){
    uint64_t filesize;
    data->type=MSG_INFO_REQ;
    send_data(sockfd,data,sizeof(data));
    receive_data(sockfd,&filesize);
    return filesize;
}

int debug_circular_buff(){
    
}

//TODO: i dati vengono inviati in ordine
void* buff_writer(void *w_args){

    FILE *fd;
    unsigned char* payload=NULL,*data=NULL;
    c_buff *cb;
    writer_args *args;

    //init vars
    args=w_args; //args passed by thread
    cb=args->c_buff;
    
    printf("[DEBUG] client_path: %s\n",args->client_path);
    printf("[DEBUG] n_segments: %d\n",args->n_segments);
    printf("[DEBUG] segment_len: %d bytes\n",args->segment_len);

    data=malloc(args->segment_len);
    if (data == NULL){
        perror("[ERROR] Writer failed to memory allocation");
        return NULL;
    }

    fd=fopen(args->client_path,"rb");
    if(fd == NULL){
        ferror (fd);
        return NULL;
    }

    fseek(fd,args->start_bytes,SEEK_SET); //init file pointer

    for(int i=0; i< args->n_segments; i++){
        //wait for the reader
        while(cb->count == cb->size){
            usleep(1000);
        }

        cb->buff[cb->tail].payload_size=fread(data,1,args->segment_len,fd); //read segment from file
        //printf("[DEBUG] counter: %d\n",cb->count);
        //printf("[DEBUG] tail: %d\n",cb->tail);
        //printf("[DEBUG] payload size from writer: %" PRIu64 "\n",*payload_size);
        cb->buff[cb->tail].payload=malloc(cb->buff[cb->tail].payload_size);
        if(cb->buff[cb->tail].payload==NULL){
            perror("[ERROR] Writer failed to memory allocation");
            free(data);
            fclose(fd);
            return NULL;
        }

        //writer write to buffer
        cb->buff[cb->tail].id=i;
        memcpy(cb->buff[cb->tail].payload,data,cb->buff[cb->tail].payload_size);
        cb->tail = (cb->tail + 1) % cb->size; //if tail is 49 --> return 50 % 50 = 0 (so, return to start of array buffer)
        cb->count++;
    }

    free(data);
    fclose(fd);

}

int buff_reader(int sockfd,int n_segments,int segment_len,c_buff *cb){

    // init vars
    int bytes_sent=0;
    
    for(int i=0; i<n_segments; i++){
        //wait for the writer
        while(cb->count == 0){
            //printf("[DEBUG] counter:%d\n",c_buff->count);
            usleep(1000);
        }
        //reader send file to tcp server
        //printf("[DEBUG] head: %d\n",cb->head);
        //printf("[DEBUG] payload size from reader: %" PRIu64 "\n",cb->buff[cb->head].payload_size);
        bytes_sent=send_data(sockfd,cb->buff[cb->head].payload,cb->buff[cb->head].payload_size);
        if(bytes_sent!=cb->buff[cb->head].payload_size){
            printf("[ERROR] TCP data corruption! (%d bytes sent)\n",bytes_sent);
            return 0;
        }
        free(cb->buff[cb->head].payload);
        //c_buff->buff[*head].payload=NULL;
        cb->head = (cb->head + 1) % cb->size; //if head is 49 --> return 50 % 50 = 0 (so, return to start of array buffer)
        cb->count--;

    }
}

/**
     * Circular Buffer for Chunkly core. It reads, partitions and sends segmented file from clt to srv
     * @param segment_len length of segment
     * @param filesize size of clt file
     * @param start_bytes size of srv file (so the start point of buff_writer)
     */
int circular_buffer(char* client_path,int sockfd,int segment_len,uint64_t filesize,uint64_t start_bytes){

    int n_segments=1;
    int status=-1;
    c_buff c_buff;
    FILE *fd;
    pthread_t writer;
    writer_args args;

    c_buff.count=0; //init counter to 0
    c_buff.head=0;
    c_buff.tail=0;
    c_buff.size=sizeof(c_buff.buff)/sizeof*(c_buff.buff);

    //calculate n_segments
    printf("[DEBUG] File size: %" PRIu64 " bytes\n",filesize);
    if(filesize>segment_len){
        n_segments=(filesize/segment_len)+1; //calculate number of total file segments
    }else{
        printf("[INFO] File size is lower than segments length configured. Skipping segmentation...\n");
    }

    //args for buff_writer function
    args.c_buff=&c_buff;
    args.client_path=client_path;
    args.n_segments=n_segments;
    args.segment_len=segment_len;
    args.start_bytes=start_bytes;


    status = pthread_create(&writer,NULL,buff_writer,&args);
    //buff_writer(&args);
    buff_reader(sockfd,n_segments,segment_len,&c_buff);

    //print_c_buff(&c_buff);
    pthread_join(writer, NULL);
    return 0;
}

int main(int argc, char* argv[]){

    //network vars
    int sockfd;

    //file management vars
    file_header header;
    file_node file;
    char base_name[512];
    int segment_len=1*1000000; //length of segments in bytes (default: 1Mb)
    uint64_t srv_filesize=0; //filesize returned by server if file is already present

//flags definition
    //flag vars
    char *hostname = NULL; //destionation server
    char *server_path = NULL; // destionation path of server
    char *client_path = NULL; // file
    int index;
    int c;

    opterr = 0;
    while ((c = getopt (argc, argv, "i:d:o:s:h")) != -1)
        switch (c)
        {
        case 'i': //input file
            client_path = optarg;
            break;
        case 'd': //destination
            hostname = optarg;
            break;
        case 'o': //server output file
            server_path = optarg;
            break;
        case 's': //segments len in byte
            segment_len = atoi(optarg);
            break; 
        case 'h':
            help();
            return 1;
            break;
        case '?':
            if (optopt == 'c')
            fprintf (stderr, "Option -%c requires an argument.\n", optopt);
            else if (isprint (optopt))
            fprintf (stderr, "Unknown option `-%c'.\n", optopt);
            else
            fprintf (stderr,
                    "Unknown option character `\\x%x'.\n",
                    optopt);
            return 1;
        default:
            abort ();
        }

    for (index = optind; index < argc; index++)
        printf ("Non-option argument %s\n", argv[index]);
//end of flags


    //path basename (eg. "/home/test/hello.txt" to "hello.txt")
    snprintf(base_name, sizeof(base_name), "%s", basename(client_path));
    //Add final slash to path if not present
    size_t len = strlen(server_path);
    if (len > 0 && server_path[len - 1] != '/') {
        snprintf(server_path+len, 2, "/");
    }

    //Open TCP socket
    sockfd=open_tcp_socket(hostname);

    //get server file size (if present)
    //srv_filesize=get_sv_filesize(sockfd,&header);

    //build protocol for segmentation
    header.type=MSG_CHUNK;
    header.fileName=base_name;
    header.fileName_len=strlen(base_name);
    header.file_size=get_lc_filesize(client_path);

    //printf("[DEBUG] filename: %s\n",header.fileName);
    //printf("[DEBUG] filename length: %" PRIu32 "\n",header.fileName_len);

    //circular buffer core
    circular_buffer(client_path,sockfd,segment_len,header.file_size,srv_filesize);

    
    if(close(sockfd)<0){
        printf("[ERROR] Failed to close connection\n");
    }

    return 0;
    
}