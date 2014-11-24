#include "ftp.h"

#include <stdio.h>        /* printf */
#include <stdlib.h>       /* malloc */
#include <string.h>       /* strlen */
#include <strings.h>      /* bzero */
#include <unistd.h>       /* read / write */
#include <arpa/inet.h>    /* inet_addr */

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
    fprintf(stderr, "Error: socket()\n");
    return 1;
  }

  /*connect to the server*/
  if(connect(*socket_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
  {
    fprintf(stderr, "Error: connect()\n");
    return 1;
  }

  return 0;
}

int ftp_disconnect(FTP * ftp)
{
  /* Send QUIT command to end connection */
  if(ftp_write(ftp->control_socket_fd, "QUIT\n"))
  {
    fprintf(stderr, "Error: ftp_disconnect\n");
    return 1;
  }

  /* Close sockets */
  close(ftp->control_socket_fd);
  close(ftp->data_socket_fd);

  return 0;
}

int ftp_read(const int socket_fd, char * message, size_t size)
{
  /* Read message from the server */
  int res = read(socket_fd, message, size);
  if(res < 0)
  {
    fprintf(stderr, "Error: read\n");
    return 1;
  }

  return (res == 0);
}

int ftp_write(const int socket_fd, const char * message)
{
  /* Write message to the server */
  if(write(socket_fd, message, strlen(message)) < 0)
  {
    fprintf(stderr, "Error: write\n");
    return 1;
  }

  return 0;
}

int ftp_login(FTP * ftp, const char * user, const char * password)
{
    char buffer[INPUT_SIZE] = "";

    /* Send user */
    sprintf(buffer, "USER %s\n", user);
    if(ftp_write(ftp->control_socket_fd, buffer))
    {
      fprintf(stderr, "Error: ftp_write\n");
      return 1;
    }
    memset(buffer, 0, strlen(buffer));

    /* Read response for the user */
    if(ftp_read(ftp->control_socket_fd, buffer, INPUT_SIZE))
    {
      fprintf(stderr, "Error: ftp_read\n");
      return 1;
    }
    printf("%s", buffer);
    memset(buffer, 0, strlen(buffer));

    /* Send password */
    sprintf(buffer, "PASS %s\n", password);
    if(ftp_write(ftp->control_socket_fd, buffer))
    {
      fprintf(stderr, "Error: ftp_write\n");
      return 1;
    }
    memset(buffer, 0, strlen(buffer));

    /* Read response for the password */
    if(ftp_read(ftp->control_socket_fd, buffer, INPUT_SIZE))
    {
      fprintf(stderr, "Error: ftp_read\n");
      return 1;
    }
    printf("%s", buffer);
    memset(buffer, 0, strlen(buffer));

    /* Detects Invalid Password Response */
    if(buffer[0] == '5')
    {
      fprintf(stderr, "Error: wrong password\n");
      return 1;
    }

    return 0;
}

int ftp_download(FTP * ftp, const char * path)
{
  const char * offset = strrchr(path, '/'); /* Pointes to the last occorence of '/' */
  FILE *f;

  if(!offset)  /* No occorrence of '/' filename is the path itself */
  {
    f = fopen(offset, "w");
  }
  else
    f = fopen(offset + 1, "w");

  if (!f)   /* Failed to create/open file */
  {
    fprintf(stderr, "Error: fopen\n");
    return 1;
  }

  char buffer[INPUT_SIZE];

  /* Sends retr command */
  sprintf(buffer, "%s %s\n", "retr", path);
  if(ftp_write(ftp->control_socket_fd, buffer))
  {
    fprintf(stderr, "Error: ftp_write\n");
    return 1;
  }
  memset(buffer, 0, strlen(buffer));

  /* Receives File from the server and writes to the file */
  while(!ftp_read(ftp->data_socket_fd, buffer, INPUT_SIZE))
  {
    if(fwrite(buffer, strlen(buffer), 1, f) < 0)
    {
      fprintf(stderr, "Error: fwrite\n");
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

  /* Sends pasv command */
  if(ftp_write(ftp->control_socket_fd, "PASV\n"))
  {
    fprintf(stderr, "Error: ftp_write\n");
    return 1;
  }

  /* Reads response (IP + Port) */
  if(ftp_read(ftp->control_socket_fd, buffer, INPUT_SIZE))
  {
    fprintf(stderr, "Error: ftp_read\n");
    return 1;
  }
  printf("%s", buffer);

  int ip1, ip2, ip3, ip4, port1, port2;
  if(!sscanf(strchr(buffer, '('), "(%d,%d,%d,%d,%d,%d)", &ip1, &ip2, &ip3, &ip4, &port1, &port2))
  {
    fprintf(stderr, "Error: sscanf\n");
    return 1;
  }

  char ip[MAX_IP_SIZE];
  if(!sprintf(ip, "%d.%d.%d.%d", ip1, ip2, ip3, ip4))
  {
    fprintf(stderr, "Error: sprintf\n");
    return 1;
  }

  int port = port1 * 256 + port2;
  if(ftp_connect(ip, port, &ftp->data_socket_fd))
  {
    fprintf(stderr, "Error: ftp_connect\n");
    return 1;
  }

  return 0;
}
