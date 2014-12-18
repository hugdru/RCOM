#include "ftp.h"
#include "url.h"
#include <stdio.h>      /* printf */
#include <string.h>     /* strcmp */

#define SERVER_PORT 21  /* Default Server Port */

/**
* Prints the usage of this application
* @arg name Application name
*/
void print_usage(char * name) {
    fprintf(stderr, "\nDownloads files using the FTP application protocol\n");
    fprintf(stderr, "Usage: %s ftp://[<user>:<password>@]<host>/<url-path>\n", name);
    fprintf(stderr, "Usage: %s ftp://<host>/<url-path>\n", name);
    fprintf(stderr, "Usage: %s ftp://<host>\n", name);
    fprintf(stderr, "Usage: %s\n", name);
    fprintf(stderr, "Usage: %s -h  \t\tFor help\n\n", name);
}

int main(int argc, char * argv[]) {
  URL url = {};

  if (argc > 2) {
    fprintf(stderr, "Error: Too many arguments\n");
    print_usage(argv[0]);
    return 0;
  }

  else if (argc == 2) { /* FTP URL was specified */
    if(!strcmp(argv[1], "-h")) {
      print_usage(argv[0]);
      return 0;
    }
    if (url_parser(argv[1], &url)) {
      fprintf(stderr, "Error: url_parser\n");
      url_clear(&url);
      return 1;
    }
  }

  if(url.hostSize <= 0) /* IF host was not specified, ask the user */
    url_getInput("Host", &url.host, &url.hostSize);

  char ip[MAX_IP_SIZE];
  if(url_getIP(url.host, ip)) { /* Get IP */
    fprintf(stderr, "Error: url_getIP\n");
    url_clear(&url);
    return 1;
  }

  FTP ftp;
  if(ftp_connect( ip , SERVER_PORT, &ftp.control_socket_fd)) {  /* Connect to the FTP server */
    fprintf(stderr, "Error: ftp_connect\n");
    url_clear(&url);
    return 1;
  }

  char temp[INPUT_SIZE];
  memset(temp, 0, INPUT_SIZE);
  if(ftp_read(ftp.control_socket_fd, temp, INPUT_SIZE)) { /* Read Server Connection Welcome */
    fprintf(stderr, "Error: ftp_read\n");
    goto disconnect;
  }
  fprintf(stderr, "%s", temp);

  unsigned int tries = 0;
  for(; tries < 3; tries++) { /* Tries to login 3 times if the user input is wrong */
    if(url.userSize <= 0 || tries > 0) {  /* If user was not specified or was incorrect ask the user */
      if(url_getInput("USER", &url.user, &url.userSize)) {
        fprintf(stderr, "Error: url_getInput\n");
        goto disconnect;
      }
    }

    if(url.passwordSize <= 0 || tries > 0) {  /* If password was not specified or was incorrect ask the user */
      if(url_getPassword(&url.password, &url.passwordSize)) {
        fprintf(stderr, "Error: url_getPassword\n");
        goto disconnect;
      }
    }

    if(!ftp_login(&ftp, url.user, url.password))  /* Failed Login */
      break;
  }

  if(tries == 3) {  /* Reached maximum tries limit 3 */
      fprintf(stderr, "Error: Failed to login\n");
      goto disconnect;
  }

  if(ftp_pasv(&ftp)) {  /* Enter Passive mode */
    fprintf(stderr, "Error: ftp_pasv\n");
    goto disconnect;
  }

  if(url.pathSize <= 0) { /* If path wasn't specified ask the user */
    if(url_getInput("PATH", &url.path, &url.pathSize)) {
      fprintf(stderr, "Error: url_getInput\n");
      goto disconnect;
    }
  }
  fprintf(stderr, "%s\n", url.path);

  if(ftp_download(&ftp, url.path)) {  /* Download the file */
    fprintf(stderr, "Error: ftp_download\n");
    goto disconnect;
  }
  else
    fprintf(stderr, "ftp_download was sucessful\n");

  disconnect:
    url_clear(&url);            /* Clear URL fields */
    if(ftp_disconnect(&ftp)) {  /* Disconnect from the server */
      fprintf(stderr, "Error: ftp_disconnect\n");
      return 1;
    }
  return 0;
}
