#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h> 
#include <arpa/inet.h>
#include <netdb.h>

#define PORT 5000
#define MAX_CONN 20

/*
Representation of sockaddr struct. Is usefull annotation for socket coding:

typedef struct sockaddr_in {
    short int sin_family; // Address family 
    unsigned short int sin_port; // port, big endian
    struct in_addr sin_addr; // ip address, big endian
    unsigned char sin_zero[8]; // used because socket udp take a buffer with specific size
}sockaddr_in;
*/

//used to transfer context in accept() socket function
typedef struct {
    struct sockaddr *addr;
    socklen_t len;
}transfer_context_t;

static transfer_context_t ctx;

/**
     * Open TCP Socket server.
     * @return socket descriptor
     */
int open_tcp_socket(){
    struct sockaddr_in addr;
    struct hostent *h;
    int sockfd;
    char *ip_addr;

    sockfd=socket(PF_INET, SOCK_STREAM, 0);
    if(sockfd < 0){
        printf("[ERROR] Socket creation failed\n");
        return -1;
    }
    addr.sin_family=AF_INET;
    addr.sin_port=htons(PORT);
    if (inet_pton(AF_INET, "0.0.0.0", &addr.sin_addr) <= 0) { //inet_pton convert ipv4/ipv6 from text to binary
        printf("[ERROR] Invalid IP \n");
        close(sockfd);
        return -1;
    }
    memset(addr.sin_zero, '\0', sizeof addr.sin_zero); // questo serve, come scritto sopra, a riempire lo spazio rimanente della struttura originale, perche 
                                                       // sockaddr_in sarebbe in realtà una struttura piu semplice da utilizzare rispetto all'originale
    //printf("%s", inet_ntoa(addr.sin_addr));

    if(bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0){
        printf("[ERROR] Bind to server port failed\n");
        return -1;
    }

    if(listen(sockfd, MAX_CONN) < 0){
        printf("[ERROR] Listen to server socket failed\n");
        return -1;
    }
    /*deactivate NAGLE
    int flag = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int));*/

    //setup context for accept() function
    ctx.addr=(struct sockaddr *)&addr;
    ctx.len=sizeof(addr);

    return sockfd;                     
}

/**
     * Accept TCP connection from host.
     * @param sockfd socket descriptor
     * @return socket descriptor for new conenction
     */
int accept_connection(int sockfd){
    int new_sockfd;
    new_sockfd=accept(sockfd,ctx.addr,&ctx.len);
    if(new_sockfd < 0){
        printf("[ERROR] Accept incoming connection failed\n");
        return -1;
    }
    return new_sockfd;
}

/**
     * Send TCP data to host.
     * @param sockfd socket descriptor
     * @param buff pointer to message
     * @param buff_len size of message
     * @return number of bytes sent
     */
int send_data(int sockfd, const void* buff, size_t buff_len){
    int bytes_sent;
    bytes_sent = send(sockfd, buff, buff_len, 0);
    if (bytes_sent < 0){
        printf("[ERROR] Send data to host failed\n");
    }else{
        return bytes_sent;
    }
    return 0;
}

/**
     * Receive TCP data from host (used for small responses returned by server, max 8 bytes).
     * @param sockfd socket descriptor
     * @param buff pointer to message
     * @return number of bytes received
     */
int receive_data(int sockfd, uint64_t *buff){
    int bytes_rcv;
    bytes_rcv = recv(sockfd, buff, 8, 0);
    if (bytes_rcv < 0){
        printf("[ERROR] Response by client failed\n");
    }else{
        return bytes_rcv;
    }
}