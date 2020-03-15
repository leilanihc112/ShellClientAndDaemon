/**
 * @file TCPClient2-ex2.c 
 * @brief The program creates a stream socket
 * in the inet domain, Connect to TCPServer2, Get messages typed by a
 * user and Send them to TCPServer2 running on hostid Then it waits
 * for a reply from the TCPServer2 and show it back to the user, with
 * a message indicating if there is an error during the round trip 
 * Run as: 
 *   TCPClient-ex2 <hostname> <port>
 */
#include <stdio.h>
/* socket(), bind(), recv, send */
#include <sys/types.h>
#include <sys/socket.h> /* sockaddr_in */
#include <netinet/in.h> /* inet_addr() */
#include <arpa/inet.h>
#include <netdb.h> /* struct hostent */
#include <string.h> /* memset() */
#include <unistd.h> /* close() */
#include <stdlib.h> /* exit() */
#include <signal.h>

#define MAXHOSTNAME 80
#define BUFSIZE 1024

char buf[BUFSIZE];
char rbuf[BUFSIZE];
void GetUserInput();
void cleanup(char *buf);

int rc, cc;
int   sd;

static void c_sig_handler(int signo) 
{
    	switch(signo)
	{
		case SIGINT:
        		cleanup(buf);
        		strcpy(buf, "CTL c\n");
        		rc = strlen(buf);
        		if (write(sd, buf, rc) < 0)
            			perror("sending stream message");
        		cleanup(buf);

		case SIGTSTP:
        		cleanup(buf);
        		strcpy(buf, "CTL z\n");
        		rc = strlen(buf);
        		if (write(sd, buf, rc) < 0)
            			perror("sending stream message");
        		cleanup(buf);
    	}
}

int main(int argc, char **argv ) {
    int childpid;
    struct sockaddr_in server;
    //struct sockaddr_in client;
    struct hostent *hp, *gethostbyname();
    struct sockaddr_in from;
    struct sockaddr_in addr;
    int fromlen;
    //int length;
    char ThisHost[80];
    uint16_t server_port = 3826;
    
    
    /* get TCPClient Host information, NAME and INET ADDRESS */
    
    gethostname(ThisHost, MAXHOSTNAME);
    /* OR strcpy(ThisHost,"localhost"); */
    
    printf("----TCP/Client running at host NAME: %s\n", ThisHost);
    if  ( (hp = gethostbyname(ThisHost)) == NULL ) {
	fprintf(stderr, "Can't find host %s\n", argv[1]);
	exit(-1);
    }
    bcopy ( hp->h_addr, &(server.sin_addr), hp->h_length);
    printf("    (TCP/Client INET ADDRESS is: %s )\n", inet_ntoa(server.sin_addr));
    
    /** get TCPServer-ex2 Host information, NAME and INET ADDRESS */
    
    if  ( (hp = gethostbyname(argv[1])) == NULL ) {
	addr.sin_addr.s_addr = inet_addr(argv[1]);
	if ((hp = gethostbyaddr((char *) &addr.sin_addr.s_addr,
				sizeof(addr.sin_addr.s_addr),AF_INET)) == NULL) {
	    fprintf(stderr, "Can't find host %s\n", argv[1]);
	    exit(-1);
	}
    }
    printf("----TCP/Server running at host NAME: %s\n", hp->h_name);
    bcopy ( hp->h_addr, &(server.sin_addr), hp->h_length);
    printf("    (TCP/Server INET ADDRESS is: %s )\n", inet_ntoa(server.sin_addr));
    
    /* Construct name of socket to send to. */
    server.sin_family = AF_INET; 
    /* OR server.sin_family = hp->h_addrtype; */
    
    server.sin_port = htons(server_port);
    
    /*   Create socket on which to send  and receive */
    
    sd = socket (AF_INET,SOCK_STREAM,0); 
    /* sd = socket (hp->h_addrtype,SOCK_STREAM,0); */
    
    if (sd<0) {
	perror("opening stream socket");
	exit(-1);
    }

    /** Connect to TCPServer-ex2 */
    if ( connect(sd, (struct sockaddr *) &server, sizeof(server)) < 0 ) {
	close(sd);
	perror("connecting stream socket");
	exit(0);
    }
    fromlen = sizeof(from);
    if (getpeername(sd,(struct sockaddr *)&from,&fromlen)<0){
	perror("could't get peername\n");
	exit(1);
    }
    printf("Connected to TCPServer1: ");
    printf("%s:%d\n", inet_ntoa(from.sin_addr),
	   ntohs(from.sin_port));
    if ((hp = gethostbyaddr((char *) &from.sin_addr.s_addr,
			    sizeof(from.sin_addr.s_addr),AF_INET)) == NULL)
	fprintf(stderr, "Can't find host %s\n", inet_ntoa(from.sin_addr));
    else
	printf("(Name is : %s)\n", hp->h_name);

    if(signal(SIGINT, c_sig_handler) == SIG_ERR)
        printf("signal(SIGINT)error");
    if(signal(SIGTSTP, c_sig_handler) == SIG_ERR)
        printf("signal(SIGTSTP)error");
    
    childpid = fork();
    if (childpid == 0) {
	GetUserInput();
    }
    
    /** get data from USER, send it SERVER,
      receive it from SERVER, display it back to USER  */
    
    for(;;) {
	cleanup(rbuf);
	if( (rc=recv(sd, rbuf, sizeof(buf), 0)) < 0){
	    perror("receiving stream  message client");
	    exit(-1);
	}
	if (rc > 0){
	    rbuf[rc]='\0';
	    printf("%s\n", rbuf);
	}else {
	    printf("Disconnected..\n");
	    close (sd);
	    exit(0);
	}
	
  }
}

static void c_sig_handler_eof(int signo) 
{
	switch(signo)
	{
		case 0:	 
			cleanup(buf);
			strcpy(buf, "CTL d\n");
			rc = strlen(buf);
			if (write(sd, buf, rc) < 0)
				perror("sending stream message");
			cleanup(buf);

		case 1:
			cleanup(buf);
			strcpy(buf, "CMD exit\n");
			rc = strlen(buf);
			if (write(sd, buf, rc) < 0)
				perror("sending stream message");
			cleanup(buf);
	}
}

void cleanup(char *buf)
{
    int i;
    for(i=0; i<BUFSIZE; i++) buf[i]='\0';
}

void GetUserInput()
{
    char cmd[] = "CMD ";
    //printf("\n# ");
    //fflush(stdout);
    for(;;) 
    {
	cleanup(buf);
	rc = read(0, buf, sizeof(buf));
	if (rc == 0) 
	{
		c_sig_handler_eof(0);
		break;
	}
	buf[rc] = '\0';
	buf[rc-1] = '\0'; 
	if(strcmp(buf, "exit") == 0)
	{
		c_sig_handler_eof(1);
		break;
	}
	char* yash_buf = strdup(buf);
	strcpy(buf, cmd);
	strcat(buf, yash_buf);
	rc = strlen(buf);
	if (send(sd, buf, rc, 0) < 0)
		perror("sending stream message");
	free(yash_buf);
    }
    printf ("EOF... exit\n");
    close(sd);
    kill(getppid(), 9);
    exit (0);
}
