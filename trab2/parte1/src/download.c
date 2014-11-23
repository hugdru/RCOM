#include "ftp.h"
#include "url.h"
#include <stdio.h>
#include <string.h>

#define SERVER_PORT 21

/**
* @desc Imprime o modo de utilização da aplicação
* @arg char * name : nome da aplicação
*/
void print_usage(char * name)
{
    printf("\nDownloads files using the FTP application protocol\n");
    printf("Usage: %s ftp://[<user>:<password>@]<host>/<url-path>\n", name);
    printf("Usage: %s ftp://<host>/<url-path>\n", name);
    printf("Usage: %s\n", name);
    printf("Usage: %s -h  \t\tFor help\n\n", name);
}

int main(int argc, char * argv[])
{
  URL url;
  url.userSize = 0;
  url.passwordSize = 0;
  url.hostSize = 0;
  url.pathSize = 0;

  if (argc > 2)
  {
    perror("Too many arguments");
    print_usage(argv[0]);
    return 0;
  }

  else if (argc == 2) /* FTP URL especificado */
  {
    if(!strcmp(argv[1], "-h"))
    {
      print_usage(argv[0]);
      return 0;
    }
    if (url_parser(argv[1], &url))
    {
      perror("url_parser");
      return 1;
    }

  }

  if(url.hostSize <= 0)
    url_getInput("Host", &url.host, &url.hostSize);

  char ip[MAX_IP_SIZE];

  if(url_getIP(url.host, ip))
  {
    perror("url_getIP");
    return 1;
  }

  FTP ftp;
  if(ftp_connect( ip , SERVER_PORT, &ftp.control_socket_fd))
  {
    perror("ftp_connect");
    return 1;
  }

  unsigned int tries = 0;
  for(; tries < 3; tries++)
  {
    if(url.userSize <= 0 || tries > 0)
    {
      if(url_getInput("USER", &url.user, &url.userSize))
      {
        perror("url_getInput");
        return 1;
      }
    }

    if(url.passwordSize <= 0 || tries > 0)
    {
      if(url_getPassword(&url.password, &url.passwordSize))
      {
        perror("url_getPassword");
        return 1;
      }
    }

    if(!ftp_login(url.user, url.password, &ftp))
      break;
  }

  if(tries == 3)
  {
      perror("Failed to login");
      return 1;
  }

  if(ftp_pasv(&ftp))
  {
    perror("ftp_pasv");
    return 1;
  }

  if(url.pathSize <= 0)
  {
    if(url_getInput("PATH", &url.path, &url.pathSize))
    {
      perror("url_getInput");
      return 1;
    }
  }

  if(ftp_download(&ftp, url.path))
  {
    perror("ftp_download");
    return 1;
  }

  if(ftp_disconnect(&ftp))
  {
    perror("ftp_disconnect");
    return 1;
  }

  return 0;
}
