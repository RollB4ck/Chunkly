#ifndef TCP_CLIENT_H
#define TCP_CLIENT_H

int open_tcp_socket(char* ip_addr);
int send_data(int sockfd, const void* buff, size_t buff_len);
int receive_data(int sockfd, uint64_t *buff);

#endif