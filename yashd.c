/**
 * @file TCPServer-ex2.c 
 * @brief The program creates a TCP socket in
 * the inet domain and listens for connections from TCPClients, accept clients
 * into private sockets, and fork an echo process to ``serve'' the
 * client.  If [port] is not specified, the program uses any available
 * port.  
 * Run as: 
 *     TCPServer-ex2 <port>
 */
/*
NAME:        
SYNOPSIS:    TCPServer [port]

DESCRIPTION:  

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
#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <malloc.h>
#include <errno.h>
#include <semaphore.h>

#define MAX_ARGS 128
#define MAXHOSTNAME 80
#define PATHMAX 255

static char u_server_path[PATHMAX+1] = "/tmp";  /* default */
static char u_socket_path[PATHMAX+1];
static char u_log_path[PATHMAX+1];
static char u_pid_path[PATHMAX+1];

void reusePort(int sock);
void ThreadServe(void *arg);
void write_to_log(char *buff, size_t buf_size, char *h_addr, int hp);
void parseString(char * str);
void jobsMonitor();
void removeProcesses();
int getInfoPid(int pid);
int getInfoTid(pthread_t tid);

struct info_t *info_table;
int table_index = 0;
int ret;
sem_t my_sem;

struct process
{
	int pid; // process id
	int state; // 0 running, 1 stopped, 2 done
	int jobnum; // current job number
	char * text; // input text of process to execute
	struct process * next; // next job
	struct process * prev; // previous job
	int bg; // 0 no, 1 yes
};

struct process * head = NULL; // front of jobs
struct process * tail = NULL; // back of jobs

typedef struct _thread_data_t {
  	struct sockaddr_in from;
  	int psd;
} thread_data_t;

struct info_t{
    	pthread_t tid;
    	int sock;
    	int shell_pid;
    	//int pt_pipefd[2];
};

int status, shell_pid; 

/**
 * @brief  If we are waiting reading from a pipe and
 *  the interlocutor dies abruptly (say because
 *  of ^C or kill -9), then we receive a SIGPIPE
 *  signal. Here we handle that.
 */
void sig_pipe(int n) 
{
   perror("Broken pipe signal");
}


/**
 * @brief Handler for SIGCHLD signal 
 */
void sig_chld(int n)
{
  int status;

  wait(&status); /* So no zombies */
}


/**
 * @brief Initializes the current program as a daemon, by changing working 
 *  directory, umask, and eliminating control terminal,
 *  setting signal handlers, saving pid, making sure that only
 *  one daemon is running. Modified from R.Stevens.
 * @param[in] path is where the daemon eventually operates
 * @param[in] mask is the umask typically set to 0
 */
void daemon_init(const char * const path, uint mask)
{
  	pid_t pid;
  	char buff[256];
  	static FILE *log; /* for the log */
  	int fd;
  	int k;

  	/* put server in background (with init as parent) */
  	if ( ( pid = fork() ) < 0 ) 
	{
    		perror("daemon_init: cannot fork");
    		exit(0);
  	} 
	else if (pid > 0) /* The parent */
   		exit(0);

  	/* the child */

  	/* Close all file descriptors that are open */
 	for (k = getdtablesize()-1; k>0; k--)
      		close(k);

  	/* Redirecting stdin and stdout to /dev/null */
  	if ( (fd = open("/dev/null", O_RDWR)) < 0) 
	{
    		perror("Open");
    		exit(0);
  	}
  	dup2(fd, STDIN_FILENO);      /* detach stdin */
  	//dup2(fd, STDOUT_FILENO);     /* detach stdout */
  	close (fd);
  	/* From this point on printf and scanf have no effect */

  	/* Redirecting stderr to u_log_path */
  	log = fopen(u_log_path, "aw"); /* attach stderr to u_log_path */
  	fd = fileno(log); /* obtain file descriptor of the log */
  	dup2(fd, STDERR_FILENO);
  	close (fd);
  	/* From this point on printing to stderr will go to /tmp/u-echod.log */

	/* Establish handlers for signals */
	  if ( signal(SIGCHLD, sig_chld) < 0 ) {
	    perror("Signal SIGCHLD");
	    exit(1);
	  }
	  if ( signal(SIGPIPE, sig_pipe) < 0 ) {
	    perror("Signal SIGPIPE");
	    exit(1);
	  }

  	/* Change directory to specified directory */
  	chdir(path); 

  	/* Set umask to mask (usually 0) */
  	umask(mask); 
  
  	/* Detach controlling terminal by becoming session leader */
  	setsid();

  	/* Put self in a new process group */
  	pid = getpid();
  	setpgrp(); /* GPI: modified for linux */

  	/* Make sure only one server is running */
  	if ( ( k = open(u_pid_path, O_RDWR | O_CREAT, 0666) ) < 0 )
    		exit(1);
  	if ( lockf(k, F_TLOCK, 0) != 0)
    		exit(0);

  	/* Save server's pid without closing file (so lock remains)*/
  	sprintf(buff, "%6d", pid);
  	write(k, buff, strlen(buff));

  	return;
}


int main(int argc, char **argv ) {
    int   sd, psd;
    struct   sockaddr_in server;
    struct  hostent *hp, *gethostbyname();
    struct sockaddr_in from;
    unsigned int fromlen;
    unsigned int length;
    char ThisHost[80];
    int pn;
    //int childpid;
    uint16_t server_port = 3826;

    ret = sem_init(&my_sem, 0, 1);
    if (ret != 0)
    {
	perror("error in sem_init");
	abort();
    }

    /* Initialize path variables */
  	if (argc > 1) 
      		strncpy(u_server_path, argv[1], PATHMAX); /* use argv[1] */
  	strncat(u_server_path, "/", PATHMAX-strlen(u_server_path));
  	strncat(u_server_path, argv[0], PATHMAX-strlen(u_server_path));
  	strcpy(u_socket_path, u_server_path);
  	strcpy(u_pid_path, u_server_path);
  	strncat(u_pid_path, ".pid", PATHMAX-strlen(u_pid_path));
  	strcpy(u_log_path, u_server_path);
  	strncat(u_log_path, ".log", PATHMAX-strlen(u_log_path));

    daemon_init(u_server_path, 0); /* We stay in the u_server_path directory and file
                                   // creation is not restricted. */

    unlink(u_socket_path); /* delete the socket if already existing */
    
    /* get TCPServer1 Host information, NAME and INET ADDRESS */
    gethostname(ThisHost, MAXHOSTNAME);
    /* OR strcpy(ThisHost,"localhost"); */
    
    //printf("----TCP/Server running at host NAME: %s\n", ThisHost);
    if  ( (hp = gethostbyname(ThisHost)) == NULL ) {
      fprintf(stderr, "Can't find host %s\n", argv[1]);
      exit(-1);
    }
    bcopy ( hp->h_addr, &(server.sin_addr), hp->h_length);
    //printf("    (TCP/Server INET ADDRESS is: %s )\n", inet_ntoa(server.sin_addr));

    
    
    /** Construct name of socket */
    server.sin_family = AF_INET;
    /* OR server.sin_family = hp->h_addrtype; */
    
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    pn = htons(server_port);  
    server.sin_port =  pn;
    
    /** Create socket on which to send  and receive */
    
    sd = socket (AF_INET, SOCK_STREAM, IPPROTO_TCP); 
    /* OR sd = socket (hp->h_addrtype,SOCK_STREAM,0); */
    if (sd<0) {
	perror("opening stream socket");
	exit(-1);
    }
    /** this allow the server to re-start quickly instead of waiting
	for TIME_WAIT which can be as large as 2 minutes */
    reusePort(sd);
    if ( bind( sd, (struct sockaddr *) &server, sizeof(server) ) < 0 ) {
	close(sd);
	perror("binding name to stream socket");
	exit(-1);
    }
    
    /** get port information and  prints it out */
    length = sizeof(server);
    if ( getsockname (sd, (struct sockaddr *)&server,&length) ) {
	perror("getting socket name");
	exit(0);
    }
    //printf("Server Port is: %d\n", ntohs(server.sin_port));
    
    /** accept TCP connections from clients and fork a process to serve each */
    info_table = malloc(sizeof(struct info_t) * 500);
    listen(sd,4);
    fromlen = sizeof(from);
    for(;;){

	pthread_t thr[60];
        int i = 0;
	//int rc_temp;
	psd = accept((int)sd, (struct sockaddr *)&from, &fromlen);
	thread_data_t thr_data;
	thr_data.from = from;
	thr_data.psd = psd;
	info_table[table_index].sock = psd;
	if (pthread_create(&thr[i], NULL, (void *)ThreadServe, &thr_data) != 0) 
	{
 		fprintf(stderr, "error: pthread_create");
   	}
		
	if (i >= 50)
	{
		i = 0;
		while(i < 50)
		{
			pthread_join(thr[i++], NULL);
		}
		i = 0;
	}
    }
    return 0;
}

void ThreadServe(void *arg) {
    thread_data_t *data = (thread_data_t *)arg;
    info_table[table_index].tid = pthread_self();
    char buf[512];
    int rc;
    struct sockaddr_in from = data->from;
    int psd = data->psd;
    int childpid;
    struct  hostent *hp, *gethostbyname();
    pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    //char *buf_copy;
    //char **args;

    //printf("Serving %s:%d\n", inet_ntoa(from.sin_addr),
	//   ntohs(from.sin_port));
    if ((hp = gethostbyaddr((char *)&from.sin_addr.s_addr,
			    sizeof(from.sin_addr.s_addr),AF_INET)) == NULL)
	fprintf(stderr, "Can't find host %s\n", inet_ntoa(from.sin_addr));
    else
	//printf("(Name is : %s)\n", hp->h_name);

    childpid = fork();
    if ( childpid == 0) 
    {
	dup2(psd, STDOUT_FILENO);
        table_index++; 
        /**  get data from  client and send it back */
        for(;;){
		//printf("\n...server is waiting...\n");
		printf("\n#");
	    	fflush(stdout);
		if( (rc=recv(psd, &buf, sizeof(buf), 0)) < 0){
	    		perror("receiving stream  message");
	    		exit(-1);
		}
		info_table[table_index].shell_pid = childpid;  
		if (rc > 0){
			pthread_mutex_lock(&lock);
			buf[rc] = '\0';
	    		//buf[rc-1]='\0';
			/* ADD BACK IN LATER */
			//write_to_log(buf, rc, inet_ntoa(from.sin_addr), ntohs(from.sin_port));

	    		//printf("Received: %s\n", buf);
	    		//printf("From TCP/Client: %s:%d\n", inet_ntoa(from.sin_addr),
		   	//	ntohs(from.sin_port));
	    		//printf("(Name is : %s)\n", hp->h_name);

			shell_pid = info_table[getInfoTid(pthread_self())].shell_pid;
			setpgid(info_table[getInfoTid(pthread_self())].shell_pid, info_table[getInfoTid(pthread_self())].shell_pid);
			tcsetpgrp(0, info_table[getInfoTid(pthread_self())].shell_pid); 
			
			if(strstr(buf, "CTL ") != NULL)
			{
				size_t length = strlen("CTL ");
				char* inString_cpy[1+strlen(buf+length)];
				memmove(inString_cpy, buf+length, 1+strlen(buf+length));
			
				//head->pid = info_table[getInfoTid(pthread_self())].shell_pid;
				if (strcmp((const char*)inString_cpy, "c\n") == 0)
				{
					kill(head->pid, SIGTERM);
					if(tail->prev == NULL)
					{
						head = NULL;
						tail = NULL;
					}
					else
					{
						struct process * temp = tail->prev;
						temp->next = NULL;
						tail = temp;
					}
					//break;

				}
				else if (strcmp((const char*)inString_cpy, "z\n") == 0)
				{
					head->state = 1;
					kill(head->pid, SIGTSTP);
					//break;
				}
				else if (strcmp((const char*)inString_cpy, "d\n") == 0)
				{
					struct process * current = head;
					while (current != NULL)
					{
						kill(-1*current->pid, SIGINT);
						current = current->next;
						current->prev = NULL;
					}
					//close(psd);
					break;
				}
			}
			if(strstr(buf, "CMD ") != NULL)
			{
				size_t length = strlen("CMD ");
				char* inString_cpy[1+strlen(buf+length)];
				memmove(inString_cpy, buf+length, 1+strlen(buf+length)); 
				if (strcmp((const char*)inString_cpy, "exit\n") == 0)
				{
					//close(psd);
					break;
				}
				parseString((char *)inString_cpy);
				removeProcesses();
				jobsMonitor();
				//free(inString_cpy);
			}
			pthread_mutex_unlock(&lock);
		}
		else {
	    		//printf("TCP/Client: %s:%d\n", inet_ntoa(from.sin_addr),
		   	//	ntohs(from.sin_port));
	    		//printf("(Name is : %s)\n", hp->h_name);
	    		//printf("Disconnected..\n");
	    		close(psd);
	    		exit(0);
                }
	}
    }
    else {
        close(psd);
    }
}

int getInfoPid(int pid)
{
	for(int i = 0; i < table_index + 1; i++)
	{
		if(info_table[i].shell_pid == pid)
		{
			return i;
		}
	}
	return 0;
}

int getInfoTid(pthread_t tid)
{
	for(int i = 0; i < table_index + 1; i++)
	{
		if(pthread_equal(info_table[i].tid, tid) != 0)
		{
			return i;
		}
	}
	return 0;
}

void reusePort(int s)
{
    int one=1;
    
    if ( setsockopt(s,SOL_SOCKET,SO_REUSEADDR,(char *) &one,sizeof(one)) == -1 )
	{
	    fprintf(stderr, "error in setsockopt,SO_REUSEPORT \n");
	    exit(-1);
	}
}    

void write_to_log(char *buff, size_t buf_size, char *h_addr, int hp)
{
	//FILE *log;
	time_t cur_time;
	char *c_time;

	cur_time = time(NULL);
	if (cur_time == ((time_t)-1))
	{
		fprintf(stderr, "couldn't get current time\n");
		return;
	}	

	/* convert to local time */
	c_time = ctime(&cur_time);
	size_t str_size = strlen(c_time);
	c_time[strlen(c_time)-1] = '\0';
	char yashd_str[] = " yashd[";
	size_t yashd_size = strlen(yashd_str);
	size_t h_addr_size = strlen(h_addr);
	size_t port_size = sizeof(hp);
	size_t final_size = str_size + yashd_size + h_addr_size + 5 + buf_size + port_size;
	char *final = calloc(final_size, sizeof(char));
	char port[port_size];
	sprintf(port, "%d", hp);

	strcat(final, c_time);
	strcat(final, yashd_str);
	strcat(final, h_addr);
	strcat(final, ":");
	strcat(final, port);
	strcat(final, "] ");
	strcat(final, buff);

	while (ret != 0)
	{
		ret = sem_wait(&my_sem);
		if (ret != 0)
		{
			if (errno != EINVAL)
			{
				perror("error in sem_wait");
				pthread_exit(NULL);
			}
		}
	}
	
	dprintf(2, "%s\n", final);
	
/*	log = fopen(u_log_path, "aw");
	if(log)
	{
		fwrite(final, final_size, 1, log);
	}
	fclose(log);
*/
        ret = sem_post(&my_sem);
	if (ret != 0)
	{
		perror("error in sem_post");
		pthread_exit(NULL);
	} 
	free(final);
}

/*********************************** YASH.C PROJECT 1 **********************************************/

/* add process to jobs */
void addProcess(char * args, int pid1, int pid2, int bg)
{
	struct process *proc = malloc(sizeof(struct process));
	proc->text = args;
	proc->state = 0;
	proc->bg = bg; // 1 is bg
	
	proc->pid = pid1;
	setpgid(pid1, pid1);

	if(pid2 == -1)
	{
		setpgid(pid2, pid1);
	}
	
	if (head == NULL)
	{
		proc->jobnum = 1;
		proc->next = NULL;
		proc->prev = NULL;
		head = proc;
		tail = proc;
	}	
	else
	{
		proc->prev = tail;
		proc->next = NULL;
		tail->next = proc;
		proc->jobnum = tail->jobnum + 1;
		tail = proc;
	}
}

void removeProcesses()
{
	struct process * current = head;
	while (current != NULL)
	{
		if (current->state == 2)
		{
			// head
			if (current->prev == NULL)
			{
				if (current->bg == 1)
				{
					printf("[%d]%s %s        %s\n", current->jobnum, "+", "Done", current->text);
				}
				
				/* if only one process in list */
				if (current->next == NULL)
				{
					head = NULL;
					tail = NULL;
				}
				else
				{
					current->next->prev = NULL;
					head = current->next;
				}
			}
			else
			{
				if (current->bg == 1)
				{
					printf("[%d]%s %s        %s\n", current->jobnum, "-", "Done", current->text);
				}

				/* tail */
				if (current->next == NULL)
				{
					current->prev->next = NULL;
					tail = current->prev;
				}
				else
				{
					current->prev->next = current->next;
					current->next->prev = current->prev;
				}
			}
		}
		if (current != NULL)
			current = current->next;
	}
}


void isFileOperator(char *args[], int argnum)
{
	int fin = -1;
	int fout = -1;
	int ferr = -1;

	for (int i = 0; i < argnum - 1; i++)
	{
		if (strcmp(args[i], ">") == 0)
		{
			if (i >= argnum - 1)
			{
				continue;
			}
			
			args[i] = NULL;

			char * file = strdup(args[i+1]);
			fout = open(file, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
			
			if (fout < 0)
			{
				perror("cannot open file");
				return;
			}
			else
			{
				dup2(fout, 1);
				close(fout);
				++i;
			}
		}

		if (strcmp(args[i], "<") == 0)
		{
			if (i >= argnum - 1)
			{
				continue;
			}
			
			args[i] = NULL;

			char * file = args[i+1];
			fin = open(file, O_RDONLY);
			
			if (fin < 0)
			{
				perror("cannot find file");
				return;
			}
			else
			{
				dup2(fin, 0);
				close(fin);
				++i;
			}
		}

		if (strcmp(args[i], "2>") == 0)
		{
			if (i >= argnum - 1)
			{
				continue;
			}
			
			args[i] = NULL;

			char * file = args[i+1];
			ferr = open(file, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH);
			
			if (ferr < 0)
			{
				perror("cannot find file");
				return;
			}
			else
			{
				dup2(ferr, 2);
				close(ferr);
				++i;
			}
		}
	}
}

void pipeFunc(char *firstcmd[], int argnum1, char* secondcmd[], int argnum2, char* args, int bg)
{
	int pipefd[2];
	int pid1, pid2;
	
	pipe(pipefd);

	pid1 = fork();
	if (pid1 < 0)
	{
		perror("fork failed");
		_exit(1);
	}
	else if (pid1 == 0)
	{
		dup2(pipefd[1], 1);
		close(pipefd[0]);
		isFileOperator(&firstcmd[0], argnum1);
		execvp(firstcmd[0], firstcmd);
	}
	pid2 = fork();
	if (pid2 < 0)
	{
		perror("fork failed");
		_exit(1);
	}
	else if (pid2 == 0)
	{
		dup2(pipefd[0], 0);
		close(pipefd[1]);
		isFileOperator(&secondcmd[0], argnum2);
		execvp(secondcmd[0], secondcmd);
	}


	addProcess(args, pid1, pid2, bg);
	if (!bg)
		wait(NULL);
}


void executeCommand(char * args[], int argnum, char * str, int bg)
{
	int pid = fork();
	if (pid < 0)
	{
		perror("fork failed");
		_exit(1);
	}
	else if (pid == 0)
	{
		isFileOperator(args, argnum);
		/* If invalid command, simply ignore it and print a new line */
      		if (execvp(args[0], args) < 0)
      		{
          		//printf("\n");
			/* remove from jobs list */
			if (head->next == NULL)
				head = NULL;
			else
				tail = NULL;
      		}
		//_exit(0);
	}
	else
	{
		addProcess(str, pid, -1, bg);
		if (!bg){
			
			waitpid(tail->pid, &status, WUNTRACED | WSTOPPED);
		}
	}
}

/* background */
void sendToBack()
{
	if((head->state == 1) | (head->state == 2))
	{
		/* run job */
		if (kill(head->pid, SIGCONT) < 0)
			perror("kill SIGCONT");
		/* set state to running */
		head->state = 0;
	}
	printf("[%d]%s %s        %s\n", head->jobnum, "+", "Running", head->text);
}

/* foreground */
void bringToFront()
{
	tcsetpgrp(0, head->pid);
	if((head->state == 1) | (head->state == 2))
	{
		/* run job */
		if (-1*kill(head->pid, SIGCONT) < 0)
			perror("kill SIGCONT");
		/* set state to running */
		head->state = 0;
	}	
	head->bg = 0;
	printf("%s\n", head->text);
	//tcsetpgrp(0, shell_pid);
	tcsetpgrp(0, info_table[getInfoTid(pthread_self())].shell_pid);
	waitpid(head->pid, &status, WUNTRACED | WSTOPPED);
}

void jobsMonitor()
{
	struct process * current = head;
	while(current != NULL)
	{
		if(waitpid(current->pid, &status, WNOHANG | WUNTRACED))
		{
			current->state = 2;
		}
		current = current->next;
	}
}

void displayJobs()
{
	struct process * current = head;
	while (current != NULL)
	{
		/* done */
		if (current->state == 2)
		{
			if (current->next == NULL)
			{
				printf("[%d]%s %s        %s\n", current->jobnum, "+", "Done", current->text);
			}
			else
			{
				printf("[%d]%s %s        %s\n", current->jobnum, "-", "Done", current->text);
			}
		}
		/* stopped */
		else if (current->state == 1)
		{
			if (current->next == NULL)
			{
				printf("[%d]%s %s     %s\n", current->jobnum, "+", "Stopped", current->text);
			}
			else
			{
				printf("[%d]%s %s     %s\n", current->jobnum, "-", "Stopped", current->text);
			}
		}
		/* terminate */
		else
		{
			if (current->next == NULL)
			{
				printf("[%d]%s %s     %s\n", current->jobnum, "+", "Running", current->text);
			}
			else
			{
				printf("[%d]%s %s     %s\n", current->jobnum, "-", "Running", current->text);
			}
		}

		current = current->next;
	}
}

/* Parses command into strings */
void parseString(char * str)
{
	if (str == NULL)
		return;
	
	char *original = strdup(str);
	char *args[MAX_ARGS];
	int i = 0;
	int pipe_addr = 0;
	int bg = 0;

	args[i] = strtok(str, " ");
	if (args[i] == NULL)
		return;
	
	while(args[i] != NULL)
	{
		args[++i] = strtok(NULL, " ");
		
		/* if piping command */
		if(args[i] != NULL && strcmp(args[i], "|") == 0)
		{
			pipe_addr = i;
		}
	}
	
	args[++i] = NULL;

	/* check if last character is & */
	if(i >= 2 && args[i - 2] != NULL && strcmp(args[i - 2], "&") == 0)
	{
		bg = 1;
		args[i - 2] = NULL;
	}

	/* check if fg, bg, jobs, exit */
	if (strcmp(args[0], "fg") == 0)
	{
		bringToFront();
		return;
	}
	else if (strcmp(args[0], "bg") == 0)
	{
		sendToBack();
		return;
	}
	else if (strcmp(args[0], "jobs") == 0)
	{
		displayJobs();
		return;
	}
	else if (strcmp(args[0], "exit") == 0)
	{
		//FIXME		
		return;
	}

	/* handle piping */
	if (pipe_addr > 0)
	{
		int arg_num = i - 1;
		
		char *firstcmd[pipe_addr+1];
		for (int j = 0; j < pipe_addr; j++)
		{
			firstcmd[j] = args[j];
		}
		firstcmd[pipe_addr] = NULL;

		char *secondcmd[arg_num - pipe_addr];
		for (int j = 0; j < (arg_num - pipe_addr - 1); j++)
		{
			secondcmd[j] = args[pipe_addr + 1 + j];
		}
		secondcmd[arg_num - pipe_addr + 1] = NULL;

		pipeFunc(firstcmd, pipe_addr, secondcmd, arg_num - pipe_addr - 1, original, bg);
	}
	else
	{
		/* execute regular command */
		executeCommand(args, i, original, bg);
	}
}   
