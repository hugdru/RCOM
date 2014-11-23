#include "ftp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <arpa/inet.h>

int ftp_connect(const char * ip, const int port, int * socket_fd)
{
  struct	sockaddr_in server_addr;

  /*server address handling*/
  bzero((char*)&server_addr,sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = inet_addr( ip );	/*32 bit Internet address network byte ordered*/
  server_addr.sin_port = htons(port);		  /*server TCP port must be network byte ordered */

  /*open an TCP socket*/
  if ((*socket_fd = socket(AF_INET,SOCK_STREAM,0)) < 0) {
    perror("socket()");
    return 1;
  }

  /*connect to the server*/
  if(connect(*socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
  {
    perror("connect()");
    return 1;
  }

  char temp[INPUT_SIZE] = "";
  if(ftp_read(*socket_fd, temp, INPUT_SIZE))
  {
    perror("ftp_read");
    return 1;
  }
  printf("%s", temp);
  
  return 0;
}

int ftp_disconnect(FTP * ftp)
{
  if(ftp_write(ftp->control_socket_fd, "QUIT\n"))
  {
    perror("ftp_disconnect");
    return 1;
  }

  if (ftp->control_socket_fd) {
    ftp->control_socket_fd = 0;
    close(ftp->control_socket_fd);
  }

  if (ftp->data_socket_fd) {
    close(ftp->data_socket_fd);
    ftp->data_socket_fd = 0;
  }

  return 0;
}

int ftp_login(const char * user, const char * password, FTP * ftp)
{
    char temp[INPUT_SIZE] = "";

    sprintf(temp, "USER %s\n", user);
    if(ftp_write(ftp->control_socket_fd, temp))
    {
      perror("ftp_write");
      return 1;
    }

    if(ftp_read(ftp->control_socket_fd, temp, INPUT_SIZE))
    {
      perror("ftp_read");
      return 1;
    }
    printf("%s", temp);
    memset(temp, 0, INPUT_SIZE);

    sprintf(temp, "PASS %s\n", password);
    if(ftp_write(ftp->control_socket_fd, temp))
    {
      perror("ftp_write");
      return 1;
    }

    if(ftp_read(ftp->control_socket_fd, temp, INPUT_SIZE))
    {
      perror("ftp_read");
      return 1;
    }
    printf("%s", temp);

    if(temp[0] == '5')
    {
      perror("wrong password");
      return 1;
    }

    return 0;
}

int ftp_read(const int socket_fd, char * message, size_t size)
{
  if(read(socket_fd, message, size) <= 0)
  {
    perror("read");
    return 1;
  }

  return 0;
}

int ftp_write(const int socket_fd, const char * message)
{
  if(write(socket_fd, message, strlen(message)) < 0)
  {
    perror("write");
    return 1;
  }

  return 0;
}

int ftp_download(FTP * ftp, const char * path)
{
  FILE* f = fopen(path, "w");
  if (!f) {
    perror("fopen");
    return -1;
  }

  char buffer[INPUT_SIZE];
  int res;

  while(!ftp_read(ftp->data_socket_fd, buffer, INPUT_SIZE))
  {
    if(fwrite(buffer, strlen(buffer), 1, f) != strlen(buffer))
    {
      perror("fwrite");
      fclose(f);
      return 1;
    }
  }

  fclose(f);
  return 0;
}

int ftp_pasv(FTP * ftp)
{
  char buffer[INPUT_SIZE];

  if(ftp_write(ftp->control_socket_fd, "PASV\n"))
  {
    perror("ftp_write");
    return 1;
  }

  if(ftp_read(ftp->control_socket_fd, buffer, INPUT_SIZE))
  {
    perror("ftp_read");
    return 1;
  }

  int ip1, ip2, ip3, ip4, port1, port2;
  if(!sscanf(buffer, "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d)", &ip1, &ip2, &ip3, &ip4, &port1, &port2))
  {
    perror("sscanf");
    return 1;
  }

  char ip[MAX_IP_SIZE];
  if(!sprintf(ip, "%d.%d.%d.%d", ip1, ip2, ip3, ip4))
  {
    perror("sprintf");
    return 1;
  }

  int port = port1 * 256 + port2;
  if(ftp_connect(ip, port, &ftp->data_socket_fd))
  {
    perror("ftp_connect");
    return 1;
  }

  return 0;
}
