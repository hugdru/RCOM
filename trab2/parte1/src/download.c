#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef enum 
{
	false, 
	true
} bool;

typedef struct 
{
	char * user;
	char * password;
	char * host;
	char * path;
	int userSize, passwordSize, hostSize, pathSize;
} URL;


/**
* @desc Faz parser do FTP URL path e preenche a estrutura url
* @arg const char * path : FTP URL recebido na linha de comandos "ftp://[<user>:<password>@]<host>/<url-path>"
* @arg URL * url : estrutura url que vai ser preenchida
* @return Retorna 0 em caso de sucesso, 1 em caso de erro
*/
int parseURL(const char * path, URL * url)
{
	char * temp;
	unsigned int offset = 6;

	if (strncmp(path, "ftp://", 6)) 
	{
		printf("Error: FTP URL não começa por \"ftp://\"\n");
		return 1;
	}

	if(path[offset] == '[')
	{
		bool foundEnd = false;
		unsigned int offsetBracket = offset;

		while (!foundEnd)
		{
			if((temp = strchr(path + offsetBracket, ']')) <= 0)
				break;

			offsetBracket = strlen(path) - strlen(temp);

			if( path[offsetBracket - 1] == '@')
			{
				//USER
				if((temp = strchr(path + offset, ':')) > 0)
				{
					url->userSize = strlen(path) - strlen(temp) - offset - 1; //Get size of user
					url->user = (char *) malloc( url->userSize * sizeof(char)); //Allocates user
					strncpy(url->user, path + offset + 1, url->userSize); //Copies user
					offset += url->userSize + 2;
				}
				//PASSWORD
				url->passwordSize = offsetBracket - offset - 1; //Get size of password
				url->password = (char *) malloc( url->passwordSize * sizeof(char) ); //Allocates password
				strncpy(url->password, path + offset, url->passwordSize); //Copies password
				offset = offsetBracket + 1;

				foundEnd = true;
			}
			offsetBracket++;
		}
	}

	if (temp = strchr(path + offset, '/'))
	{
		//PATH
		unsigned int offsetPath = strlen(path) - strlen(temp) + 1;
		url->pathSize = strlen(temp) - 1; //Get size of path
		if(url->pathSize > 0)
		{
			url->path = (char *) malloc( url->pathSize * sizeof(char) ); //Allocates path
			strncpy(url->path, path + offsetPath, url->pathSize); //Copies path
		}
	}
	
	url->hostSize = strlen(path) - offset - url->pathSize; //Get size of host

	if(url->pathSize > 0)
		url->hostSize--;

	if(url->hostSize > 0)
	{
		url->host = (char *) malloc( url->hostSize * sizeof(char) ); //Allocates host
		strncpy(url->host, path + offset, url->hostSize); //Copies host
	}
	
	return 0;
}

/**
* @desc Imprime o modo de utilização da aplicação
* @arg char * name : nome da aplicação
*/
void print_usage(char * name) 
{
    printf("\n\nDownloads files using the FTP protocol\n");
    printf("Usage: %s ftp://[<user>:<password>@]<host>/<url-path>\n", name);
    printf("Usage: %s ftp://<host>/<url-path>\n", name);
    printf("Usage: %s\n", name);
    printf("Usage: %s -h  \t\tFor help\n\n", name);
}

int getUser(URL * url)
{
	char temp[100];
	printf("\nUser: ");
	fgets(temp, 100, stdin);
	url->userSize = strlen(temp) - 1;
	strncpy(url->user, temp, url->userSize);
}

int getPassword(URL * url)
{
	url->password = getpass("Password: ");
	url->passwordSize = strlen(url->password);
}

int getHost(URL * url)
{
	char temp[100];
	printf("\nHost: ");
	fgets(temp, 100, stdin);
	url->hostSize = strlen(temp) - 1;
	strncpy(url->host, temp, url->hostSize);
}

int getPath(URL * url)
{
	char temp[100];
	printf("\nPath: ");
	fgets(temp, 100, stdin);
	url->pathSize = strlen(temp) - 1;
	strncpy(url->path, temp, url->pathSize);
}

int main(int argc, char * argv[])
{
	URL url;
	url.userSize = 0;
	url.passwordSize = 0;
	url.hostSize = 0;
	url.pathSize = 0;

	if (argc > 2) //Demasiados argumentos
	{
		printf("Error: Demasiados argumentos\n");
		print_usage(argv[0]);
		return 0;
	}
	else if (argc == 2) //FTP URL especificado, parser do URL
	{
		if(!strcmp(argv[1], "-h"))
		{
			print_usage(argv[0]);
			return 0;
		}
		if (parseURL(argv[1], &url) != 0)
		{
			printf("FTP URL especificado não se encontra correcto\n");
			return 1;
		}

	}

	if(url.hostSize <= 0)
		getHost(&url);

	if(url.pathSize <= 0)
		getPath(&url);

	if(url.userSize <= 0)
		getUser(&url);

	if(url.passwordSize <= 0)
		getPassword(&url);

	printf("Final\nUser %s %d\nPassword %s %d\nHost %s %d\nPath %s %d\n", url.user, url.userSize, url.password, url.passwordSize, url.host, url.hostSize, url.path, url.pathSize);
	return 0;
}