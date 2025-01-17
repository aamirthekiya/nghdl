/**********************************************************************************
 * <ghdlserver.c>  FOSSEE, IIT-Bombay
 **********************************************************************************
 * 08.Nov.2019 - Rahul Paknikar  - Switched to blocking sockets from non-blocking
 *								 - Close previous used socket to prevent from   
 *								   generating too many socket descriptors
 *								 - Enabled SO_REUSEPORT, SO_DONTROUTE socket options
 * 26.Sept.2019 - Rahul Paknikar - Added reading of IP from a file to 
 *                                 support multiple digital models
 *                               - On exit, the test bench removes the
 *                                 NGHDL_COMMON_IP_<ngspice_pid> file, shared by all
 *                                 nghdl digital models and is stored in /tmp
 *                                 directory. It tracks the used IPs for existing
 *                                 digital models in current simulation.
 *              				 - Writes PID file in append mode.
 * 5.July.2019 - Rahul Paknikar  - Added loop to send all port values for 
 *                                 a given event.
 *                               - Removed bug to terminate multiple testbench
 *                                 instances in ngpsice windows.
 **********************************************************************************
 **********************************************************************************
 * 24.Mar.2017 - Raj Mohan - Added signal handler for SIGUSR1, to handle an 
 *                           orphan test bench process.
 *                           The test bench will now create a PID file in
 *                           /tmp directory with the name 
 *                           NGHDL_<ngspice pid>_<test bench>_<instance_id>
 *                           This file contains the PID of the test bench .
 *                           On exit, the test bench removes this file.
 *                           The SIGUSR1 signal serves the same purpose as the 
 *                           "End" signal.
 *                         - Added syslog interface for logging.
 *                         - Enabled SO_REUSEADDR socket option.
 *                         - Added the following functions:
 *                             o create_pid_file()
 *                             o get_ngspice_pid()
 * 22.Feb.2017 - Raj Mohan - Implemented a kludge to fix a problem in the
 *                           test bench VHDL code.
 *                         - Changed sleep() to nanosleep().
 * 10.Feb.2017 - Raj Mohan - Log messages with timestamp/code clean up.
 *                           Added the following functions:
 *                             o curtim()
 *                             o print_hash_table()
 *********************************************************************************/

#include <string.h>
#include "ghdlserver.h"
#include "uthash.h"
#include <fcntl.h>
#include <stdio.h>                                                              
#include <stdlib.h>                                                             
#include <assert.h>                                                             
#include <signal.h>                                                             
#include <unistd.h>
#include <sys/types.h>                                                          
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/time.h>                                                        
#include <netinet/in.h> 
#include <netdb.h>
#include <limits.h>
#include <time.h>
#include <errno.h>
#include <dirent.h>
#include <syslog.h>

#define _XOPEN_SOURCE 500
#define MAX_NUMBER_PORT 100
#define NGSPICE "ngspice"     // 17.Mar.2017 - RM

static FILE* pid_file;
static char pid_filename[80];
static char* Out_Port_Array[MAX_NUMBER_PORT];           
static int out_port_num = 0; 
static int server_socket_id = -1;
static int sendto_sock;      // 22.Feb.2017 - RM - Kludge
static int prev_sendto_sock; // 22.Feb.2017 - RM - Kludge
static int pid_file_created; // 10.Mar.2017 - RM 

extern char* __progname;  // 26.Feb.2017 May not be portable to non-GNU systems.

void Vhpi_Exit(int sig);

struct my_struct {
    char val[1024];                  
    char key[1024];       
    UT_hash_handle hh;    //Makes this structure hashable.
};

static struct my_struct *s, *users, *tmp = NULL;


/* 17.Mar.2017 - RM - Get the process id of ngspice program.*/
static int get_ngspice_pid(void)
{
    DIR* dirp;
    FILE* fp = NULL;
    struct dirent* dir_entry;
    char path[1024], rd_buff[1024];
    pid_t pid = -1;

    if ((dirp = opendir("/proc/")) == NULL)
    {
		perror("opendir /proc failed");
		exit(-1);
    }

    while ((dir_entry = readdir(dirp)) != NULL)
    {
		char* nptr;
	    int valid_num = 0;

		int tmp = strtol(dir_entry->d_name, &nptr, 10);
		if ((errno == ERANGE) && (tmp == LONG_MAX || tmp == LONG_MIN))
		{
		    perror("strtol"); // Number out of range.
		    return(-1);
		}
		if (dir_entry->d_name == nptr)
		{
		    continue; // No digits found.
		}
		if (tmp)
		{
		    sprintf(path, "/proc/%s/comm", dir_entry->d_name);
		    if ((fp = fopen(path, "r")) != NULL)
		    {
  				fscanf(fp, "%s", rd_buff);
  				if (strcmp(rd_buff, NGSPICE) == 0)
  				{
  				    pid = (pid_t)tmp;             // 5.July.2019 - RP - Kludge
  				}
		    }
		}
    }

   if (fp) fclose(fp);

   return(pid);
}


/* 23.Mar.2017 - RM - Pass the sock_port argument. We need this if a netlist
 * uses more than one instance of the same test bench, so that we can uniquely
 * identify the PID files.
 */
/* 10.Mar.2017 - RM - Create PID file for the test bench in /tmp. */
static void create_pid_file(int sock_port)
{
    pid_t my_pid = getpid();
    pid_t ngspice_pid = get_ngspice_pid();
    
    if (ngspice_pid == -1)
    {
      	fprintf(stderr, "create_pid_file() Failed to get ngspice PID");
      	syslog(LOG_ERR,  "create_pid_file() Failed to get ngspice PID");
      	exit(1);
    }

    sprintf(pid_filename, "/tmp/NGHDL_%d_%s_%d", ngspice_pid, __progname, sock_port);
    pid_file = fopen(pid_filename, "a");	// 26.Sept.2019 - RP - Open file in append mode
    
    if (pid_file)
    {
    	pid_file_created = 1;
	    fprintf(pid_file,"%d\n", my_pid);
	    fclose(pid_file);
    } else {
        perror("fopen() - PID file");
	    syslog(LOG_ERR, "create_pid_file(): Unable to open PID file in /tmp");
        exit(1);
    }
}


#ifdef DEBUG
static char* curtim(void)
{
    static char ct[50];
    struct timeval tv;
    struct tm* ptm;
    long milliseconds;
    char time_string[40];

    gettimeofday (&tv, NULL);
    ptm = localtime (&tv.tv_sec);
    strftime (time_string, sizeof (time_string), "%Y-%m-%d %H:%M:%S", ptm);
    milliseconds = tv.tv_usec / 1000;
    sprintf (ct, "%s.%03ld", time_string, milliseconds);
    return(ct);
}
#endif


#ifdef DEBUG
static void print_hash_table(void) 
{
    struct my_struct *sptr;

    for(sptr=users; sptr != NULL; sptr=sptr->hh.next)
		syslog(LOG_INFO, "Hash table:val:%s: key: %s", sptr->val, sptr->key);
}
#endif


static void parse_buffer(int sock_id, char* receive_buffer)
{
    static int rcvnum;

    syslog(LOG_INFO,"RCVD RCVN:%d from CLT:%d buffer : %s",
	   rcvnum++, sock_id,receive_buffer);

    /*Parsing buffer to store in hash table */ 
    char *rest;
    char *token;
    char *ptr1=receive_buffer;
    char *var;
    char *value;

    // Processing tokens.
    while(token = strtok_r(ptr1, ",", &rest)) 
    {
        ptr1 = rest;
        while(var=strtok_r(token, ":", &value))
        {
          	s = (struct my_struct*)malloc(sizeof(struct my_struct));
	  		strncpy(s->key, var, 64);
	  		strncpy(s->val, value, 64);
	  		HASH_ADD_STR(users, key, s );
	  		break;    
        }
    }
        
    s = (struct my_struct*)malloc(sizeof(struct my_struct));
    strncpy(s->key, "sock_id", 64);
    snprintf(s->val,64, "%d", sock_id);
    HASH_ADD_STR(users, key, s);
}


//Create Server and listen for client connections.
// 26.Sept.2019 - RP - added parameter of socket ip
static int create_server(int port_number, char my_ip[], int max_connections)
{
 	int sockfd, reuse = 1;
 	struct sockaddr_in serv_addr;

 	sockfd = socket(AF_INET, SOCK_STREAM, 0);

 	if (sockfd < 0)
 	{
    	fprintf(stderr, "%s- Error: in opening socket at server \n", __progname);
    	//exit(1);
    	return -1;
 	}

	/* 20.Mar.2017 - RM - SO_REUSEADDR option. To take care of TIME_WAIT state.*/
 	int ret = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int));

 	/* 08.Nov.2019 - RP - SO_REUSEPORT and SO_DONTROUTE option.*/
 	ret += setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(int));
  	ret += setsockopt(sockfd, SOL_SOCKET, SO_DONTROUTE, &reuse, sizeof(int));
 
 	if (ret < 0)
 	{
    	syslog(LOG_ERR, "create_server:setsockopt() failed....");
    	// close(sockfd);
    	// return -1;
 	}

 	bzero((char *) &serv_addr, sizeof(serv_addr));
 	serv_addr.sin_family = AF_INET;
 	serv_addr.sin_addr.s_addr = inet_addr(my_ip);  // 26.Sept.2019 - RP - Bind to specific IP only
 	serv_addr.sin_port = htons(port_number);
     
 	if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
 	{
     	fprintf(stderr,"%s- Error: could not bind socket to port %d\n",
             __progname, port_number);
     	syslog(LOG_ERR, "Error: could not bind socket to port %d", port_number);
     	close(sockfd);
     	exit(1);
 	}

 	// Start listening on the server.
	listen(sockfd, max_connections);

	return sockfd;
}


// The server to wait (blocking) for a client connection.
static int connect_to_client(int server_fd)                                            
{                                                                               
    int ret_val = 0;
    int newsockfd = -1;
    socklen_t clilen;
    struct sockaddr_in cli_addr;
    
    clilen = sizeof(cli_addr); 

    /* 08.Nov.2019 - RP - Blocking Socket (Accept) */
    newsockfd = accept(server_fd, (struct sockaddr *) &cli_addr, &clilen);
    if (newsockfd >= 0)
    { 
	    syslog(LOG_INFO, "SRV:%d New Client Connection CLT:%d", server_fd, newsockfd);
    }        
    else
    {
        syslog(LOG_ERR,"Error: failed in accept(), socket=%d", server_fd);
	    exit(1);
    }                   

    return newsockfd;
}   


//Receive string from socket and put it inside buffer.
static void receive_string(int sock_id, char* buffer)                                   
{                                                                               
  	int nbytes = 0;

	/* 08.Nov.2019 - RP - Blocking Socket - Receive */    
    nbytes = recv(sock_id, buffer, MAX_BUF_SIZE, 0);
    if (nbytes <= 0)
    {
		perror("READ FAILURE");
        exit(1);
    }
}


static void Data_Send(int sockid)                                       
{                                                                              
  	static int trnum;
  	char* out;

  	int i;
  	char colon = ':';
  	char semicolon = ';'; 
  	int wrt_retries = 0;
  	int ret;

  	s = NULL;

  	out = calloc(1, 2048);

  	// 5.July.2019 - RP - loop to send all ports at once for an event
  	for (i=0; i<out_port_num; i++)  
  	{  
     	HASH_FIND_STR(users,Out_Port_Array[i],s);
     	if (strcmp(Out_Port_Array[i], s->key) == 0) 
     	{
      		strncat(out, s->key, strlen(s->key));
          	strncat(out, &colon, 1);
          	strncat(out, s->val, strlen(s->val));
          	strncat(out, &semicolon, 1);
      	}      
      	else                                                                        
      	{        
          	syslog(LOG_ERR,"The %s's value not found in the table.",
                 Out_Port_Array[i]);
          	free(out);
          	return;
     	}
    }

    /* 08.Nov.2019 - RP - Blocking Socket (Send) */
    if ((send(sockid, out, strlen(out), 0)) == -1)
    {
        syslog(LOG_ERR,"Failure sending to CLT:%d buffer:%s", sockid, out);
        exit(1);
    }

    syslog(LOG_INFO,"SNT:TRNUM:%d to CLT:%d buffer: %s", trnum++, sockid, out);  
    free(out);
} 


// 26.Sept.2019 - RP - added parameter of socket ip
void Vhpi_Initialize(int sock_port, char sock_ip[])
{
    DEFAULT_SERVER_PORT = sock_port;

    signal(SIGINT,Vhpi_Exit);
    signal(SIGTERM,Vhpi_Exit);
    signal(SIGUSR1, Vhpi_Exit); //10.Mar.2017 - RM

    int try_limit = 100;

    while(try_limit > 0)
    {
      	// 26.Sept.2019 - RP
      	server_socket_id = create_server(DEFAULT_SERVER_PORT, sock_ip, DEFAULT_MAX_CONNECTIONS);

      	if(server_socket_id >= 0)
      	{
           syslog(LOG_INFO,"Started the server on port %d  SRV:%d",
		  DEFAULT_SERVER_PORT, server_socket_id);
            break;
        }
        
        syslog(LOG_ERR,"Could not start server on port %d,will try again",
                   DEFAULT_SERVER_PORT);
	    usleep(1000);
	    try_limit--;
	        
	    if(try_limit==0)
	    {
	      	syslog(LOG_ERR,
	           "Error:Tried to start server on port %d, failed..giving up.",
	                      DEFAULT_SERVER_PORT);
		    exit(1);
        }
    }
                                                                              
  	//Reading Output Port name and storing in Out_Port_Array;
    char* line = NULL;
    size_t len = 0; 
    ssize_t read;
    char *token;
    FILE *fp;
    struct timespec ts;

    fp=fopen("connection_info.txt","r");
    if (!fp)
    {
		syslog(LOG_ERR,"Vhpi_Initialize: Failed to open connection_info.txt. Exiting...");
		exit(1);
    }

    line = (char*) malloc(80);
    while ((read = getline(&line, &len, fp)) != -1)
    {
		if (strstr(line,"OUT") != NULL || strstr(line,"out") != NULL)
		{ 
		    strtok_r(line, " ",&token);
		    Out_Port_Array[out_port_num] = line;
		    out_port_num++;
		}
		line = (char*) malloc(80);
    }                     	
    fclose(fp);
    free(line);

    ts.tv_sec = 2;
    ts.tv_nsec = 0;
    nanosleep(&ts, NULL);

	// 10.Mar.2017 - RM - Create PID file for the test bench.
    create_pid_file(sock_port);
}


void Vhpi_Set_Port_Value(char *port_name,char *port_value,int port_width)
{
	s = (struct my_struct*)malloc(sizeof(struct my_struct));
	strncpy(s->key, port_name,64);
	strncpy(s->val,port_value,64);
	HASH_ADD_STR( users, key, s );
}


void Vhpi_Get_Port_Value(char* port_name,char* port_value,int port_width)
{
  	HASH_FIND_STR(users,port_name,s);
  	if(s)
  	{  
    	snprintf(port_value,sizeof(port_value),"%s",s->val);
    	HASH_DEL(users, s);
    	free(s);
    	s=NULL;
  	}
}


void Vhpi_Listen()
{
    sendto_sock = connect_to_client(server_socket_id);	// 22.Feb.2017 - RM - Kludge
	char receive_buffer[MAX_BUF_SIZE];
	receive_string(sendto_sock, receive_buffer);
    
   	syslog(LOG_INFO, "Vhpi_Listen:New socket connection CLT:%d",sendto_sock);

   	if(strcmp(receive_buffer, "END")==0) 
    {
        syslog(LOG_INFO, "RCVD:CLOSE REQUEST from CLT:%d", sendto_sock);  
    	Vhpi_Exit(0);
    }  

	parse_buffer(sendto_sock, receive_buffer);    
}


void  Vhpi_Send() 
{
// 22.Feb.2017 - RM - Kludge
    if (prev_sendto_sock != sendto_sock)
    { 
	    Data_Send(sendto_sock);

	    close(prev_sendto_sock);	// 08.Nov.2019 - RP - Close previous socket
	    prev_sendto_sock = sendto_sock;
    }
// 22.Feb.2017 End kludge 
}


void Vhpi_Exit(int sig) 
{                          
    close(server_socket_id);
    syslog(LOG_INFO, "*** Closed VHPI link. Exiting... ***");
    exit(0);
}    