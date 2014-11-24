#ifndef FTP_H
#define FTP_H

#include <stddef.h> /* size_t */

#define INPUT_SIZE 128  /* Size of buffer used to read input from the server */
#define MAX_IP_SIZE 16  /* Max Size of a IP string */

/**
* Struct that contained the file descriptors of the sockets used in the FTP connection
*/
typedef struct
{
  int data_socket_fd;       /* Data Socket File Descriptor */
  int control_socket_fd;    /* Control Socket File Descriptor */
} FTP;

/**
* Connects the user to the FTP server with the IP ip and Port port
* @arg ip FTP IP of the FTP server
* @arg port FTP Port of the FTP server
* @arg socket_fd Socket File Descriptor opened by the connection
* @return Returns 0 in case of success, 1 in case of error
*/
int ftp_connect(const char * ip, const int port, int * socket_fd);

/**
* Disconnects the user from the FTP server
* @arg ftp FTP Struct needed to be cleaned
* @return Returns 0 in case of success, 1 in case of error
*/
int ftp_disconnect(FTP * ftp);

/**
* Receives the message sended from the server
* @arg socket_fd Socket File Descriptor
* @arg message Message received from the server
* @arg size Size of the message received
* @return Returns 0 in case of success, 1 in case of error
*/
int ftp_read(const int socket_fd, char * message, size_t size);

/**
* Sends a message to the server
* @arg socket_fd Socket File Descriptor
* @arg message Message to be sended to the server
* @return Returns 0 in case of success, 1 in case of error
*/
int ftp_write(const int socket_fd, const char * message);

/**
* Logins the User user to the FTP server
* @arg ftp FTP Struct of the server
* @arg user Name of the user used to log in
* @arg password Password of the user used to log in
* @return Returns 0 in case of success, 1 in case of error
*/
int ftp_login(FTP * ftp, const char * user, const char * password);

/**
* Downloads the file in the Path path
* @arg ftp FTP Struct of the server
* @arg path Path of the file
* @return Returns 0 in case of success, 1 in case of error
*/
int ftp_download(FTP * ftp, const char * path);

/**
* Enters the user in passive mode and connects the other socket to the server
* @arg ftp FTP Struct of the server
* @return Returns 0 in case of success, 1 in case of error
*/
int ftp_pasv(FTP * ftp);

#endif
