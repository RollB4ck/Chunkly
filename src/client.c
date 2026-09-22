/* We want POSIX.1-2008 + XSI, i.e. SuSv4, features */
#define _XOPEN_SOURCE 700

/* Added on 2017-06-25:
   If the C library can support 64-bit file sizes
   and offsets, using the standard names,
   these defines tell the C library to do so. */
#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64 

#define _GNU_SOURCE
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>
#include <pthread.h>
#include <ctype.h>
#include <ftw.h>
#include <errno.h>

#include "tcp_client.h"

//max 15 sub-directories for directory transfers
#ifndef USE_FDS
#define USE_FDS 15
#endif
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

// gcc -Ilib/socket_client/include lib/socket_client/src/tcp_client.c src/client.c

typedef enum {
    MSG_INFO_REQ = 0x01,  // File state request (1 byte)
    MSG_INFO_RES = 0x02,  // Server response (1 byte)
    MSG_CHUNKS    = 0x03   // Send chunk of file (1 byte)
} msg_type_t;

typedef struct Header{
    uint8_t type; //message type
    uint32_t filepath_len; //length of filepath string in big endian
    const char *dest_filepath; //name of file in little endian
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
    const char *client_path;
    int n_segments;
    int segment_len;
    uint64_t start_bytes;
    c_buff *c_buff;
}writer_args;

//used to transfer context in nftw() POSIX function
typedef struct {
    int sockfd;
    int segment_len;
    const char* server_path;

}transfer_context_t;

static transfer_context_t ctx; //

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; //mute exclusion for buffer threading
static pthread_cond_t cond_not_full = PTHREAD_COND_INITIALIZER; // used for synchronization between threads
static pthread_cond_t cond_not_empty = PTHREAD_COND_INITIALIZER; // used for synchronization between threads

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

//TODO: remove chunkid from protocol

/**
     * Writer of Circular Buffer. It writes payload from file to buffer
     * @param w_args writer arguments (see args struct)
     */
void* buff_writer(void *w_args){

    FILE *fd;
    unsigned char *temp_payload=NULL,*data=NULL;
    c_buff *cb;
    writer_args *args;
    int bytes_read=0;

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

        bytes_read=fread(data,1,args->segment_len,fd); //read segment from file
        temp_payload=malloc(bytes_read); //allocation of memory for payload
        memcpy(temp_payload,data,bytes_read); //insert segment to payload
        
        if(temp_payload==NULL){
            perror("[ERROR] Writer failed to memory allocation");
            free(data);
            fclose(fd);
            return NULL;
        }

        pthread_mutex_lock(&mutex); //lock for mute exclusion
        while(cb->count == cb->size){
            pthread_cond_wait(&cond_not_full, &mutex); //waiting reader
        }

        //writer write to buffer
        cb->buff[cb->tail].payload_size = bytes_read;
        cb->buff[cb->tail].payload = temp_payload;

        cb->tail = (cb->tail + 1) % cb->size; //if tail is 49 --> return 50 % 50 = 0 (so, return to start of array buffer)
        cb->count++;
        pthread_cond_signal(&cond_not_empty); //signal for reader wake up
        pthread_mutex_unlock(&mutex);
    }
    free(data);
    fclose(fd);
}

/**
     * Reader of Circular Buffer. It reads payload from buffer and send it to destination server
     * @param sockfd socket descriptor
     * @param n_segments number of total segments to send
     * @param segment_len length of single segment
     * @param cb circular buffer object
     */
int buff_reader(int sockfd,int n_segments,int segment_len,c_buff *cb){

    // init vars
    unsigned char* payload=NULL;
    uint64_t payload_size=0;
    int bytes_sent=0;
    
    for(int i=0; i<n_segments; i++){

        pthread_mutex_lock(&mutex); //lock for mute exclusion
        while(cb->count == 0){
            pthread_cond_wait(&cond_not_empty, &mutex); //waiting writer
        }

        payload_size=cb->buff[cb->head].payload_size;
        payload = cb->buff[cb->head].payload;
        cb->head = (cb->head + 1) % cb->size; //if head is 49 --> return 50 % 50 = 0 (so, return to start of array buffer)
        cb->count--;
        pthread_cond_signal(&cond_not_full); //signal for writer wake up
        pthread_mutex_unlock(&mutex);

        //reader send file to tcp server
        bytes_sent=send_data(sockfd,payload,payload_size);
        if(bytes_sent!=payload_size){
            printf("[ERROR] TCP data corruption! (%d bytes sent)\n",bytes_sent);
            return 0;
        }
        free(payload);
    }
    return 1;
}

/**
     * Circular Buffer for Chunkly core. It reads, partitions and sends segmented file from clt to srv
     * @param segment_len length of segment
     * @param filesize size of clt file
     * @param start_bytes size of srv file (so the start point of buff_writer)
     */
int circular_buffer(const char* client_path,int sockfd,int segment_len,uint64_t filesize,uint64_t start_bytes){

    int n_segments=1;
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

    if (pthread_create(&writer,NULL,buff_writer,&args) != 0){
        printf("[ERROR] Cannot create writer thread\n");
        return 0;
    }
    //buff_writer(&args);
    buff_reader(sockfd,n_segments,segment_len,&c_buff);

    //print_c_buff(&c_buff);
    pthread_join(writer, NULL);
    return 0;
}

//TODO: aggiungere server_path + filepath in dest_filepath
int get_entry( const char *filepath, const struct stat *info,
                const int typeflag, struct FTW *pathinfo){

    /*const char *const filename = filepath + pathinfo->base; //basename*/
    const double bytes = (double)info->st_size; /* Not exact if large! */
    char base_name[512];
    uint64_t srv_filesize=0; //filesize returned by server if file is already present
    file_header header;

    if (typeflag == FTW_F){
        printf(" %s...", filepath);

        //build header
        header.type=MSG_CHUNKS;
        header.dest_filepath=malloc(strlen(ctx.server_path) + strlen(filepath) + 1);
        strcpy(header.dest_filepath,ctx.server_path);
        strcat(header.dest_filepath,filepath);
        header.dest_filepath=filepath;
        header.filepath_len=strlen(filepath);
        header.file_size=bytes;

        //get server file size (if present)
        //srv_filesize=get_sv_filesize(sockfd,&header);

        //circular buffer core
        circular_buffer(filepath,ctx.sockfd,ctx.segment_len,header.file_size,srv_filesize);
    }
    return 0;
}

int main(int argc, char* argv[]){
    //network vars
    int sockfd;

    //file management vars
    file_node file;
    int segment_len=1*1000000; //length of segments in bytes (default: 1Mb)

//flags definition
    //flag vars
    char *hostname = NULL; //destionation server
    const char *server_path; // destionation path of server
    const char *client_path; // file
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

    //Add final slash to path if not present
    /*size_t len = strlen(server_path);
    if (len > 0 && server_path[len - 1] != '/') {
        snprintf(server_path+len, 2, "/");
    }*/

    //Open TCP socket
    sockfd=open_tcp_socket(hostname);

    //set context for nftw
    ctx.segment_len=segment_len;
    ctx.sockfd=sockfd;
    ctx.server_path=server_path;

    /* Invalid directory path? */
    if (client_path == NULL || *client_path == '\0'){
        printf("[ERROR] invalid client path\n");
        close(sockfd);
        return 0;
    }
    //add error for invalid path
    if (nftw(client_path, get_entry, USE_FDS, FTW_PHYS) != 0){
        fprintf(stderr, "[ERROR] invalid client path");
    }
    
    close(sockfd);

    return 0;
    
}