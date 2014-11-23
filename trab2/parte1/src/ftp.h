#ifndef FTP_H
#define FTP_H

#include <stddef.h>

#define INPUT_SIZE 128
#define MAX_IP_SIZE 16

typedef struct {
  int data_socket_fd;
  int control_socket_fd;
} FTP;

int ftp_connect(const char * ip, const int port, int * socket_fd);

int ftp_disconnect(FTP * ftp);

int ftp_read(const int socket_fd, char * message, size_t size);

int ftp_write(const int socket_fd, const char * message);

int ftp_login(const char * user, const char * password, FTP * ftp);

int ftp_download(FTP * ftp, const char * path);

int ftp_pasv(FTP * ftp);

#endif
