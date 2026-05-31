#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/time.h>

#define SERVER_PORT 21. // Port 21 is the default FTP control port
#define BUFFER_SIZE 1024

typedef struct {
    char user[256];
    char password[256];
    char host[256];
    char path[512];
    char filename[256];
    char ip[64];
} URLInfo;

void parseURL(char *url, URLInfo *info);
void getIP(char *host, char *ip);
int createSocket(char *ip, int port);
int readResponse(int sockfd);
void sendCommand(int sockfd, char *cmd);
void enterPassiveMode(int sockfd, char *ip, int *port);




//-------------------------------------------------------------------------------------------- main function

int main(int argc, char *argv[]) {


    //Verify that the user provided exactly one argument (the FTP URL)
    if(argc != 2) {
        fprintf(stderr, "Usage: %s ftp://[user:password@]host/filepath\n", argv[0]);
        exit(1);
    }

    URLInfo info; // Structure to hold parsed URL information

    parseURL(argv[1], &info);

    //Debugging output: Print the parsed URL components
    printf("User: %s\n", info.user);
    printf("Password: %s\n", info.password);
    printf("Host: %s\n", info.host);
    printf("Path: %s\n", info.path);
    printf("Filename: %s\n", info.filename);


    //get the IP address of the host from the parsed URL information
    getIP(info.host, info.ip);


    //Debugging output: Print the resolved IP address
    printf("IP: %s\n", info.ip);

    //CREATE CONTROL CONNECTION

    int controlSocket = createSocket(info.ip, SERVER_PORT);

    readResponse(controlSocket);

    char command[512];


    //send the USER and PASS commands to authenticate with the FTP server using the credentials from the parsed URL information

    sprintf(command, "USER %s\r\n", info.user);
    sendCommand(controlSocket, command);
    readResponse(controlSocket);

    sprintf(command, "PASS %s\r\n", info.password);
    sendCommand(controlSocket, command);
    readResponse(controlSocket);

    char passiveIP[64];
    int passivePort;

    //Enter passive mode to prepare for the data connection
    enterPassiveMode(controlSocket, passiveIP, &passivePort);


    //CREATE DATA CONNECTION
    int dataSocket = createSocket(passiveIP, passivePort);

    sprintf(command, "RETR %s\r\n", info.path);
    sendCommand(controlSocket, command);
    readResponse(controlSocket);

    FILE *file = fopen(info.filename, "wb");

    if(file == NULL) {
        perror("fopen");
        exit(1);
    }

    char buffer[BUFFER_SIZE];
    int bytes;
    struct timeval start, end;

    long totalBytes = 0;

    gettimeofday(&start, NULL);

    while((bytes = read(dataSocket, buffer, BUFFER_SIZE)) > 0) {
        fwrite(buffer, 1, bytes, file);
        totalBytes += bytes;
    }
    gettimeofday(&end, NULL);

    double elapsedTime = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;

    double transferRate = totalBytes / elapsedTime;

    printf("\nTransfer statistics:\n");
    printf("Total bytes: %ld bytes\n", totalBytes);
    printf("Elapsed time: %.3f seconds\n", elapsedTime);
    printf("Average transfer rate: %.2f bytes/s\n", transferRate);

    fclose(file);

    close(dataSocket);

    readResponse(controlSocket);

    close(controlSocket);

    printf("Download completed successfully.\n");

    return 0;
}





//-------------------------------------------------------------------------------------------- parseURL 
/*
Objective: Parse the given FTP URL and extract user, password, host, path, and filename information.

*/
void parseURL(char *url, URLInfo *info) {

    if(strncmp(url, "ftp://", 6) != 0) {
        fprintf(stderr, "Invalid URL\n");
        exit(1);
    }

    char temp[1024];
    strcpy(temp, url + 6);

    char *at = strchr(temp, '@');

    if(at != NULL) {

        char credentials[256];
        strncpy(credentials, temp, at - temp);
        credentials[at - temp] = '\0';

        sscanf(credentials, "%[^:]:%s", info->user, info->password);

        strcpy(temp, at + 1);
    }
    else {
        strcpy(info->user, "anonymous");
        strcpy(info->password, "anonymous");
    }

    char *slash = strchr(temp, '/');

    if(slash == NULL) {
        fprintf(stderr, "Invalid path\n");
        exit(1);
    }

    strncpy(info->host, temp, slash - temp);
    info->host[slash - temp] = '\0';

    strcpy(info->path, slash + 1);

    char *lastSlash = strrchr(info->path, '/');

    if(lastSlash != NULL)
        strcpy(info->filename, lastSlash + 1);
    else
        strcpy(info->filename, info->path);
}


//-------------------------------------------------------------------------------------------- getIP
/*
Objective: Get the IP address of the given host using DNS resolution.

*/

void getIP(char *host, char *ip) {

    struct hostent *h;

    if((h = gethostbyname(host)) == NULL) {
        herror("gethostbyname");
        exit(1);
    }

    strcpy(ip, inet_ntoa(*((struct in_addr *) h->h_addr)));
}



//-------------------------------------------------------------------------------------------- createSocket
/*
Objective: Create a socket and connect to the FTP server at the given IP and port.

*/

int createSocket(char *ip, int port) {

    int sockfd;

    struct sockaddr_in server_addr;

    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }

    bzero((char *) &server_addr, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip);

    if(connect(sockfd,
               (struct sockaddr *) &server_addr,
               sizeof(server_addr)) < 0) {
        perror("connect");
        exit(1);
    }

    return sockfd;
}



//-------------------------------------------------------------------------------------------- readResponse 
/*
Objective: Read the response from the server and return the response code.

*/

int readResponse(int sockfd) {

    char buffer[BUFFER_SIZE];
    char code[4];

    while(1) {

        bzero(buffer, BUFFER_SIZE);

        int bytes = read(sockfd, buffer, BUFFER_SIZE - 1);

        if(bytes <= 0) {
            perror("read");
            exit(1);
        }

        buffer[bytes] = '\0';

        printf("SERVER: %s", buffer);

        strncpy(code, buffer, 3);
        code[3] = '\0';

        /*
         * FTP multiline replies:
         * lines starting with "xyz-" continue
         * final line starts with "xyz "
         */

        if(strlen(buffer) >= 4 &&
           strncmp(buffer, code, 3) == 0 &&
           buffer[3] == ' ') {

            return atoi(code);
        }
    }
}



//-------------------------------------------------------------------------------------------- sendCommand
/*
Objective: Send a command to the FTP server.

*/

void sendCommand(int sockfd, char *cmd) {

    printf("CLIENT: %s", cmd);

    write(sockfd, cmd, strlen(cmd));
}




//-------------------------------------------------------------------------------------------- enterPassiveMode
/*
Objective: Enter passive mode by sending the PASV command to the server and parsing the response to get the IP and port for the data connection.

*/
void enterPassiveMode(int sockfd, char *ip, int *port) {

    char command[] = "PASV\r\n";

    sendCommand(sockfd, command);

    char buffer[BUFFER_SIZE];

    bzero(buffer, BUFFER_SIZE);

    int bytes = read(sockfd, buffer, BUFFER_SIZE - 1);

    if(bytes <= 0) {
        perror("read");
        exit(1);
    }

    buffer[bytes] = '\0';

    printf("SERVER: %s", buffer);

    int h1, h2, h3, h4, p1, p2;

    char *start = strchr(buffer, '(');

    if(start == NULL) {
        fprintf(stderr, "PASV response format error\\n");
        exit(1);
    }

    sscanf(start + 1,
       "%d,%d,%d,%d,%d,%d",
       &h1, &h2, &h3, &h4, &p1, &p2);

    sprintf(ip, "%d.%d.%d.%d", h1, h2, h3, h4);

    *port = p1 * 256 + p2;

    printf("Passive IP: %s\n", ip);
    printf("Passive Port: %d\n", *port);
}