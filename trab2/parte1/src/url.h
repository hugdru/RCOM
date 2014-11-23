#ifndef URL_H
#define URL_H

typedef struct {
  char * user;
  char * password;
  char * host;
  char * path;
  unsigned int userSize, passwordSize, hostSize, pathSize;
} URL;


/**
* @desc Faz parser do FTP URL path e preenche a estrutura url
* @arg const char * path : FTP URL recebido na linha de comandos "ftp://[<user>:<password>@]<host>/<url-path>"
* @arg URL * url : estrutura url que vai ser preenchida
* @return Retorna 0 em caso de sucesso, 1 em caso de erro
*/
int url_parser(const char * path, URL * url);

/**
* @desc Pede ao utilizador para introduzir a password (Desativado echo da linha de comandos)
* @arg char ** password : Apontador para a password introduzida
* @arg unsigned int * size : comprimendo da password
* @return Retorna 0 em caso de sucesso, 1 em caso de erro
*/
int url_getPassword(char ** password, unsigned int * size);

/**
* @desc Pede ao utilizador para introduzir um campo especificado por message
* @arg const char * message : Mensagem a ser mostrada ao utilizador
* @arg char ** input : Apontador para a string
* @arg unsigned int * size : comprimendo do input
* @return Retorna 0 em caso de sucesso, 1 em caso de erro
*/

int url_getInput(const char * message, char ** input, unsigned int * size);
/**
* @desc Obtem o ip do host
* @arg const char * host : host introduzido pelo utilizador
* @arg char * ip : ip do host
* @return Retorna 0 em caso de sucesso, 1 em caso de erro
*/
int url_getIP(const char * host, char * ip);


#endif
