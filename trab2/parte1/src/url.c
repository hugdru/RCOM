#define _POSIX_SOURCE
#include "url.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <netdb.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>

#define h_addr h_addr_list[0]





int url_getInput(const char * message, char ** input, unsigned int * size)
{
  char temp[64];

  printf("%s: ", message);
  if( fgets(temp, sizeof(temp), stdin) == NULL)
  {
    return 1;
  }

  *size = strlen(temp) - 1;
  *input = (char *) malloc(*size);
  strncpy(*input, temp, *size);

  return 0;
}

int url_getPassword(char ** password, unsigned int * size)
{
  struct termios oldFlags, newFlags;
  char temp[64];

  tcgetattr(fileno(stdin), &oldFlags); /* Saving old config */
  newFlags = oldFlags;
  newFlags.c_lflag &= ~ECHO;			/* Disabling echo */
  newFlags.c_lflag |= ECHONL;

  if (tcsetattr(fileno(stdin), TCSANOW, &newFlags) != 0) {
    perror("tcsetattr");	/* Failed to change config */
    return 1;
  }

  printf("Password: ");
  if( fgets(temp, sizeof(temp), stdin) == NULL)
  {
    return 1;
  }

  *size = strlen(temp) - 1;
  *password = (char *) malloc(*size);
  strncpy(*password, temp, *size);

  if (tcsetattr(fileno(stdin), TCSANOW, &oldFlags) != 0) {
      perror("tcsetattr");	/* Failed to restore config */
      return 1;
  }

  return 0;
}

int url_getIP(const char * host, char * ip)
{
  struct hostent * h;

  if (( h = gethostbyname(host) ) == NULL) {
    perror("gethostbyname");
    return 1;
  }

  strcpy(ip, inet_ntoa( *( (struct in_addr *)h->h_addr)));
  printf("Host name  : %s\n", h->h_name);
  printf("IP Address : %s\n", ip);

  return 0;
}

int url_parser(const char * path, URL * url)
{
  char * temp;
  unsigned int offset = 6;

  if (strncmp(path, "ftp://", 6)) /* Se o path não começa por ftp:// não é válido */
  {
    printf("Error: FTP URL não começa por \"ftp://\"\n");
    return 1;
  }

  if(path[offset] == '[') /* Pode ter user e pass especificados */
  {
    unsigned int offsetBracket = offset;

    while (1)
    {
      if((temp = strchr(path + offsetBracket, ']')) <= 0) /* user e pass não especificados */
        break;

      offsetBracket = strlen(path) - strlen(temp);

      if( path[offsetBracket - 1] == '@') /* Fim da pass */
      {
        if((temp = strchr(path + offset, ':')) > 0) /* USER */
        {
          url->userSize = strlen(path) - strlen(temp) - offset - 1;
          url->user = (char *) malloc( url->userSize);
          strncpy(url->user, path + offset + 1, url->userSize);
          offset += url->userSize + 2;
        }

        url->passwordSize = offsetBracket - offset - 1;
        url->password = (char *) malloc( url->passwordSize);
        strncpy(url->password, path + offset, url->passwordSize);
        offset = offsetBracket + 1;
        break;
      }
      offsetBracket++;
    }
  }

  if ((temp = strchr(path + offset, '/'))) /* Tem path */
  {
    unsigned int offsetPath = strlen(path) - strlen(temp) + 1;
    url->pathSize = strlen(temp) - 1;
    if(url->pathSize > 0)
    {
      url->path = (char *) malloc( url->pathSize);
      strncpy(url->path, path + offsetPath, url->pathSize);
    }
  }

  url->hostSize = strlen(path) - offset - url->pathSize;

  if(url->pathSize > 0) /* Ter em consideração '/' */
    url->hostSize--;

  if(url->hostSize > 0)
  {
    url->host = (char *) malloc( url->hostSize * sizeof(char) );
    strncpy(url->host, path + offset, url->hostSize);
  }

  return 0;
}
