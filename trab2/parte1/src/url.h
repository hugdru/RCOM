#ifndef URL_H
#define URL_H

#include <stddef.h> /* size_t */

/**
* Struct that contains the fields parsed in a FTP URL and needed for a FTP connection
*/
typedef struct URL {
  char * user;      /* user name*/
  char * password;  /* password of the user */
  char * host;      /* FTP host name*/
  char * path;      /* Path to the file to download */
  size_t userSize, passwordSize, hostSize, pathSize; /* Size of corresponding string */
} URL;

/**
* Parsers the FTP URL and fills the URL Struct with the respective values
* @arg path FTP URL received "ftp://[<user>:<password>@]<host>/<url-path>"
* @arg url URL Struct that will be filled
* @return Returns 0 in case of success, 1 in case of error
*/
int url_parser(const char * path, URL * url);

/**
* Asks to the user to type the password (stdin echo desactivated)
* @arg password Pointer to the typed password
* @arg size size of the password
* @return Returns 0 in case of success, 1 in case of error
*/
int url_getPassword(char ** password, size_t * size);

/**
* Asks to the user to type the specified field defined by message
* @arg message Message to be displayed to the user
* @arg input Pointer to the user input
* @arg size Size of the user input
* @return Returns 0 in case of success, 1 in case of error
*/
int url_getInput(const char * message, char ** input, size_t * size);

/**
* Gets the IP of the host
* @arg host Name of host which IP the function gets
* @arg ip Host's IP
* @return Returns 0 in case of success, 1 in case of error
*/
int url_getIP(const char * host, char * ip);

/**
* Deallocates URL fields
* @arg url URL struct to clear
*/
void url_clear(URL * url);

#endif
