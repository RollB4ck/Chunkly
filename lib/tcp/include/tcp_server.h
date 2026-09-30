#ifndef TCP_SERVER_H
#define TCP_SERVER_H

int open_tcp_socket();
int accept_connection(int sockfd);
int receive_data(int sockfd, char **out_buff);
int send_data(int sockfd, const void* buff, size_t buff_len);

#endif