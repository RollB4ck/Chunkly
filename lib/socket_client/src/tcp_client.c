#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define PORT 5000

/*
Representation of sockaddr struct. Is usefull annotation for socket coding:

typedef struct sockaddr_in {
    short int sin_family; // Address family 
    unsigned short int sin_port; // port, big endian
    struct in_addr sin_addr; // ip address, big endian
    unsigned char sin_zero[8]; // used because socket udp take a buffer with specific size
}sockaddr_in;
*/

/**
     * Open TCP Socket.
     * @param data ip_address or dns
     * @return socket descriptor
     */
int open_tcp_socket(char* data){
    struct sockaddr_in addr;
    struct hostent *h;
    int sockfd;
    char *ip_addr;

    //Resolve DNS if present
    if ((h=gethostbyname(data)) == NULL) { 
        herror("gethostbyname");
        return -1;
    }
    ip_addr=inet_ntoa(*((struct in_addr *)h->h_addr));


    sockfd=socket(PF_INET, SOCK_STREAM, 0);
    if(sockfd < 0){
        printf("[ERROR] Socket creation failed\n");
        return -1;
    }
    addr.sin_family=AF_INET;
    addr.sin_port=htons(PORT);
    if (inet_pton(AF_INET, ip_addr, &addr.sin_addr) <= 0) { //inet_pton convert ipv4/ipv6 from text to binary
        printf("[ERROR] Invalid IP \n");
        close(sockfd);
        return -1;
    }
    memset(addr.sin_zero, '\0', sizeof addr.sin_zero); // questo serve, come scritto sopra, a riempire lo spazio rimanente della struttura originale, perche 
                                                       // scokaddr_in sarebbe in realtà una struttura piu semplice da utilizzare rispetto all'originale
    //printf("%s", inet_ntoa(addr.sin_addr));

    if(connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0){
        printf("[ERROR] Connection to host failed\n");
        return -1;
    }
    return sockfd;                                 
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
        printf("[ERROR] Response by server failed\n");
    }else{
        return bytes_rcv;
    }
}