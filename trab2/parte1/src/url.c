#define _POSIX_SOURCE     /* fileno */
#include "url.h"
#include <stdio.h>        /* printf */
#include <stdlib.h>       /* malloc */
#include <string.h>       /* strlen */
#include <termios.h>      /* termios */
#include <netdb.h>        /* gethostbyname */
#include <arpa/inet.h>    /* inet_ntoa */

#define h_addr h_addr_list[0]
#define INPUT_SIZE 128    /* Size of buffers used */

int url_getInput(const char * message, char ** input, size_t * size) {
  char temp[INPUT_SIZE];
  printf("%s: ", message);

  if( fgets(temp, sizeof(temp), stdin) == NULL) {
    fprintf(stderr, "Error: fgets\n");
    return 1;
  }

  *size = strlen(temp) - 1;
  *input = (char *) malloc(*size);
  strncpy(*input, temp, *size);

  return 0;
}

int url_getPassword(char ** password, size_t * size) {
  struct termios oldFlags, newFlags;
  char temp[INPUT_SIZE];

  tcgetattr(fileno(stdin), &oldFlags); /* Saving old stdin configuration */
  newFlags = oldFlags;
  newFlags.c_lflag &= ~ECHO;			/* Disabling echo */
  newFlags.c_lflag |= ECHONL;

  if (tcsetattr(fileno(stdin), TCSANOW, &newFlags) != 0) {
    fprintf(stderr, "Error: tcsetattr\n");  /* Failed to change configuration */
    return 1;
  }

  printf("Password: ");
  if( fgets(temp, sizeof(temp), stdin) == NULL) {
    fprintf(stderr, "Error: fgets\n");
    return 1;
  }

  *size = strlen(temp) - 1;
  *password = (char *) malloc(*size);
  strncpy(*password, temp, *size);

  if (tcsetattr(fileno(stdin), TCSANOW, &oldFlags) != 0) {
    fprintf(stderr, "Error: tcsetattr\n");  /* Failed to restore configuration */
    return 1;
  }

  return 0;
}

int url_getIP(const char * host, char * ip) {
  struct hostent * h;

  if (( h = gethostbyname(host) ) == NULL) {
    fprintf(stderr, "Error: gethostbyname\n"); /* Failed to get IP */
    return 1;
  }

  strcpy(ip, inet_ntoa( *( (struct in_addr *)h->h_addr)));
  printf("Host name  : %s\n", h->h_name);
  printf("IP Address : %s\n", ip);

  return 0;
}

int url_parser(const char * path, URL * url) {
  if (strncmp(path, "ftp://", 6)) { /* Paths must begin with "ftp://" to be valid */
    fprintf(stderr, "Error: FTP URL must start with \"ftp://\"\n");
    return 1;
  }

  char * temp;
  unsigned int offset = 6;

  if(path[offset] == '[') { /* If user and password are specidied */
    unsigned int offsetBracket = offset; /* Index of the bracket (start of user) */

    while (1) {
      /* Get first occorrence of ']' after offsetBracket */
      if((temp = strchr(path + offsetBracket, ']')) <= 0)
        break;  /* user and pass are not specified */

      offsetBracket = strlen(path) - strlen(temp);  /* Index of ']' */

      if( path[offsetBracket - 1] == '@') { /* End of password field */
        if((temp = strchr(path + offset, ':')) > 0) { /* USER */
          url->userSize = strlen(path) - strlen(temp) - offset - 1;
          url->user = (char *) malloc( url->userSize);
          strncpy(url->user, path + offset + 1, url->userSize);
          offset += url->userSize + 2;  /* offset becomes the start of password */
        }

        url->passwordSize = offsetBracket - offset - 1;
        url->password = (char *) malloc( url->passwordSize);
        strncpy(url->password, path + offset, url->passwordSize);
        offset = offsetBracket + 1; /* offset becomes the start of the host */
        break;
      }
      offsetBracket++; /* So that the loop doesn't process always the same bracket */
    }
  }

  if ((temp = strchr(path + offset, '/'))) {  /* Path is specified */
    unsigned int offsetPath = strlen(path) - strlen(temp) + 1; /* Start of path (after '/') */
    url->pathSize = strlen(temp) - 1;
    if(url->pathSize > 0) {
      url->path = (char *) malloc( url->pathSize);
      strncpy(url->path, path + offsetPath, url->pathSize);
    }
  }

  url->hostSize = strlen(path) - offset - url->pathSize;

  if(url->pathSize > 0) /* If Path was specified we mustn't include '/' in the host name */
    url->hostSize--;

  if(url->hostSize > 0) {
    url->host = (char *) malloc( url->hostSize * sizeof(char) );
    strncpy(url->host, path + offset, url->hostSize);
  }

  return 0;
}

void url_clear(URL * url) {
  if (url->user)
    free(url->user);
  if (url->password)
    free(url->password);
  if (url->host)
    free(url->host);
  if (url->path)
    free(url->path);
}
