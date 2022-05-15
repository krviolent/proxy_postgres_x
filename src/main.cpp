#include "parse_postgres_request.h"

// connection list: flag and sockfd
std::map<int,int> connections;

int  listen_sock = -1;
int message_send_to_server = FALSE;
int message_recv_from_server = FALSE;
int server_sock = -1;
int last_client = -1;

struct sess_t {
	int sock_c;
	int sock_s;
} sess;

struct sess_t *s = (sess_t *)malloc(MAX_SESSIONS*sizeof(sess_t));

// temp buf for server packets
int buffer_len = -1;
char tmp_buf[BUF_SIZE];

typedef struct conn {
	struct sockaddr_in local;
	struct sockaddr_in remote;
	int sock;
} conn_t;

typedef struct session {
	conn_t conn_pc; // proxy-client
	conn_t conn_ps; // proxy-server
	char host[MAX_HOST_LEN];
	char *request;
	size_t request_len;
} session_t;

typedef struct poll_data {
	int fd;
	int flag;
	session_t *session;
} poll_data_t;

void raw_print(char buffer[BUF_SIZE], int len){
	int i = 0;
	printf("raw: ");
	// print received hex data
	for (i = 0; i < len; i++) 
		printf("%02x ", buffer[i] );
	printf("\n\ntext: ");
	for (i=0; i<len; i++)
		printf("%c", buffer[i]);
	printf("\n\n");
}

// check file by name
int check_if_file_exist(const char *filename) {
	FILE *myfile;
	if (fopen(filename, "r")  == NULL) {
			if (debug_flag == 1)
				printf("query.txt not exist\n");
			myfile = fopen("query.txt", "w");
			if (!myfile) {
				perror("fopen() failed");
				return -1;
			} else {
				return 0;
			}
	}
}

int set_nonblocking(int sock){
	int on = 1, rc = -1;
	//return ioctl(sock, FIONBIO, (char *)&on);
	return fcntl(sock,F_SETFL, fcntl(sock, F_GETFD,0) | O_NONBLOCK);
}

// connect to server
int connect_socket(const char *ip_addr, int port) {
	if (debug_flag == 1)
		printf("Trying to connect to server\n");
	int len, rc, on = 1;
	int sock = -1, new_sd = -1;
	struct sockaddr_in   addr;
	/* Create an AF_INET stream socket to receive incoming      */
	/* connections on                                            */
	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		perror("socket() failed");
		exit(-1);
	}

	/* Allow socket descriptor to be reuseable                   */
	rc = setsockopt(sock, SOL_SOCKET,  SO_REUSEADDR, (char *)&on, sizeof(on));
	if (rc < 0)
	{
		perror("setsockopt() failed");
		close(sock);
		exit(-1);
	}	
	if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(int)) <0){
		perror("setsockopt(TCP_NODELAY) failed");
		close(sock);
		exit(-1);
	}

	/* Set socket to be nonblocking. All of the sockets for      */
	/* the incoming connections will also be nonblocking since   */
	/* they will inherit that state from the listening socket.   */
	rc = set_nonblocking(sock);
	if (rc < 0) {
		perror("set_nonblocking() failed");
		close(sock);
		exit(-1);
	}
	/* Bind the socket                                           */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	inet_aton(ip_addr, &addr.sin_addr);
	addr.sin_port = htons(port);
	rc = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
	if (rc < 0 && errno != EINPROGRESS) {
		perror("connect() failed");
		close(sock);
		exit(-1);
	}
	return sock;
}

void send_answer_to_client(int sock, char buffer[BUF_SIZE], int len) {
		int rc = -1;
		if (debug_flag == 1)
		printf("Enter send_answer_to_client, sock = %d\n", sock);
		rc = send(sock, buffer, len , MSG_NOSIGNAL);
		if (rc < 0) {
			perror("[client] send() failed");
			message_recv_from_server = FALSE;
			message_send_to_server = FALSE;
			//int conn_lost = disconnect_sock(fds, poll_size, conn_state);
			//delete_connection(conn_lost, fds, poll_size);
		} else if (rc >0) {
			if (debug_flag == 1)
				printf("[client] sent %d bytes\n", rc);
			message_send_to_server = FALSE;
			message_recv_from_server = FALSE;
		}
}
int create_socket (char *ip_addr, int port) {
	int len, rc, on = 1;
	int sock = -1, new_sd = -1;
	struct sockaddr_in   addr;
	/* Create an AF_INET stream socket to receive incoming      */
	/* connections on                                            */
	sock = socket (AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		perror("socket() failed");
		exit(-1);
	}

	/* Allow socket descriptor to be reuseable                   */
	rc = setsockopt(sock, SOL_SOCKET,  SO_REUSEADDR, (char *)&on, sizeof(on));
	if (rc < 0)
	{
		perror("setsockopt() failed");
		close(sock);
		exit(-1);
	}

	/* Set socket to be nonblocking. All of the sockets for      */
	/* the incoming connections will also be nonblocking since   */
	/* they will inherit that state from the listening socket.   */
	rc = set_nonblocking(sock);
	if (rc < 0) {
		perror("set_nonblocking() failed");
		close(sock);
		exit(-1);
	}
	/* Bind the socket                                           */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	if (ip_addr == NULL)
		addr.sin_addr.s_addr = htonl (INADDR_ANY);
	else
		inet_aton(ip_addr, &addr.sin_addr);
	addr.sin_port = htons(port);
	rc = bind(sock, (struct sockaddr *)&addr, sizeof(addr));
	if (rc < 0) {
		perror("bind() failed");
		close(sock);
		exit(-1);
	}
	/* Set the listen back log                                   */
	rc = listen(sock, MAX_EVENTS);
	if (rc < 0) {
		perror("listen() failed");
		close(sock);
		exit(-1);
	}
	/*
	char str[40];
	bzero(str, 40);
	inet_ntop(AF_INET, &addr, str, sizeof(str));*/
	printf("Listening on port: %d\n", port);
	return sock;
}

int disconnect_sock(struct pollfd* fds, int i, int &conn_state){
	if (debug_flag == 1)
		printf("sock disconnected = %d, conn_stat = %d\n", fds[i].fd, conn_state);
	int conn_lost = FALSE;
	if (conn_state == TRUE) {
		close(fds[i].fd);
		fds[i].fd = -1;
		conn_lost = TRUE;
		return conn_lost;
	}
}

void delete_connection(int &delete_flag, struct pollfd *fds, int &poll_size){
    if (delete_flag) {
		delete_flag= FALSE;
		for (int i = 0; i < poll_size; i++) {
			if (fds[i].fd == -1) {
				for(int j = i; j < poll_size-1; j++) {
					fds[j].fd = fds[j+1].fd;
				}
			i--;
			poll_size--;
			}
		}
	}
}

int handle_accept_event(struct pollfd * fds, int sock, int &poll_size) {
	/* Listening descriptor is readable.                   */
	if (debug_flag == 1)
		printf("  Listening socket is readable\n");
	/* Accept all incoming connections that are            */
	/* queued up on the listening socket before we         */
	/* loop back and call poll again.                      */
	int new_sd = -1, end_server = FALSE;
	do {
		/* Accept each incoming connection. If               */
		/* accept fails with EWOULDBLOCK, then we            */
		/* have accepted all of them. Any other              */
		/* failure on accept will cause us to end the        */
		/* server.                                           */
		new_sd = accept(sock, NULL, NULL);
		if (new_sd < 0) {
			if (errno != EAGAIN || errno != EWOULDBLOCK)
			{
				perror("  accept() failed");
				end_server = TRUE;
			}
			break;
		}
		/* Add the new incoming connection to the            */
		/* pollfd structure                                  */
		if (debug_flag == 1)
			printf("  New incoming connection - %d\n", new_sd);
		s[poll_size].sock_c = new_sd;
		fds[poll_size].fd = new_sd;
		fds[poll_size].events = POLLIN; //POLLIN, POLLOUT
		poll_size++;
		connections[new_sd] = EPOLL_PROXY_CLIENT;
		last_client = new_sd;
		/* Loop back up and accept another incoming          */
		/* connection                                        */
	} while (new_sd != -1);
	return end_server;
}
 /* Receive all incoming data on this socket            */
/* before we loop back and call poll again.            */
int handle_poolin_event(struct pollfd *fds, int &poll_size, int i) {
		char   buffer[BUF_SIZE];
		bzero(buffer, BUF_SIZE);
	    int rc = 0, conn_state = FALSE, nread = 0;
		auto it = connections.find(fds[i].fd);
        do {
          /* Receive data on this connection until the         */
          /* recv fails with EWOULDBLOCK. If any other         */
          /* failure occurs, we will close the                 */
          /* connection.                                       */
			while((nread = recv(fds[i].fd, buffer + rc, BUF_SIZE - rc, MSG_DONTWAIT)) > 0){
				rc +=nread;
			}
			if (rc < 0) {
				printf("[recv] Error!\n");
				if (errno != EAGAIN && errno != EWOULDBLOCK) {
					perror("  recv() failed");
					conn_state = TRUE;
				}
				break;
			}
			int len = rc;
			if (rc > 0) {
				if (debug_flag == 1) {
					printf("Connection fd = %d\n",fds[i].fd);
					printf("%d bytes received from ", rc);
				}
				if (it->second == EPOLL_PROXY_CLIENT){
					if (debug_flag == 1)
						printf("this is client\n\n");
				} else if(it->second == EPOLL_PROXY_SERVER){
					if (debug_flag == 1)
						printf("this is server\n");
				}
				if (debug_flag == 1)
					raw_print(buffer, rc);
				/*************************/
				// try to parse message and write to log file
				if (commands_count > 100)
					commands_count = 0;
				try {
					message_name_map[commands_count] = check_first_byte(buffer, rc);
				}
				catch (const std::exception& e){
					printf("Catch exception:%s", e.what());
					conn_state = TRUE;
					return conn_state;
				}
				if (message_name_map[commands_count] == "Terminate") {
						//conn_state = TRUE;
						if (debug_flag == 1)
							printf("Got terminate message\n");
						//return conn_state;
				} else {
					commands_count++;
				}
			} else {
				return conn_state;
			}
			/* Check to see if the connection has been           */
			/* closed by the client                              */
			
			if (rc == 0) {
				printf("Nothing received. Closing connection...\n");
				//conn_state = TRUE;
				//return conn_state;
			}
			// send message received from Server to Client
			if (debug_flag == 1)
				printf("s[%d].sock_s = %d, s[%d].sock_c=%d\n", i, s[i].sock_s, i, s[i].sock_c);
			if (it->second == EPOLL_PROXY_SERVER && s[i].sock_s > 0) 
			{
				if (debug_flag == 1)
					printf("sending server answer to client [%d]= %d, last client = %d\n", i, s[i].sock_c, last_client);
				send_answer_to_client(s[i].sock_c, buffer, rc);
				return conn_state;
			} else if (it->second == EPOLL_PROXY_CLIENT && s[i].sock_s > 0){
				// connection to server already established
				if (debug_flag == 1)
					printf("This is client, try to send buf to server\n");
				rc = send(s[i].sock_s, buffer, len, MSG_NOSIGNAL);
				if (rc < 0) {
					perror("[server] send() failed");
					server_sock = -1;
					conn_state = TRUE;
					int conn_lost = disconnect_sock(fds, poll_size, conn_state);
					delete_connection(conn_lost, fds, poll_size);
					break;
				} else if (rc >0) {
					if (debug_flag == 1)
						printf(" sent %d bytes to server\n", rc);
					return conn_state;
				}
			}
			// connect to server in new connection
			if (s[i].sock_s == 0){
				/* need to send packet in buffer to client or server*/
				server_sock = connect_socket (LOCALHOST, SERVER_PORT);
				// add sock to connection
				connections[server_sock] = EPOLL_PROXY_SERVER;
				if (server_sock < 0) {
					perror(" connect_socket() failed");
					break;
				}
				if (debug_flag == 1)
					printf("Connected to server, fd = %d\n", server_sock);
				// try to trick
				s[poll_size].sock_s = server_sock;
				s[i].sock_s = server_sock;

				s[poll_size].sock_c = fds[i].fd;
				fds[poll_size].fd = server_sock;
				fds[poll_size].events = POLLIN; //POLLIN, POLLOUT
				poll_size++;
				if (debug_flag == 1)
					printf ("poll_size = %d\n", poll_size);
				// try to send client message to server
				rc = send(server_sock, buffer, len, MSG_NOSIGNAL);
				return conn_state;
				if (rc < 0){
					perror("[server] send() failed");
					server_sock = -1;
					conn_state = TRUE;
					int conn_lost = disconnect_sock(fds, poll_size, conn_state);
					delete_connection(conn_lost, fds, poll_size);
					break;
				} else if (rc >0) {
					if (debug_flag == 1)
						printf(" sent %d bytes to server\n", rc);
					return conn_state;
				}
			}
        } while(TRUE);
		return conn_state;
}

void print_usage(){
	printf("Usage: sudo ./proxy_postgres_x [debug_flag] [SERVER_PORT]\n\tDebug flag = 0 (Off) or 1 (On),\n\tSERVER_PORT - default 5432\n");
}

int main (int argc, char *argv[])
{
	int    len, rc, on = 1;
	int remote_sd = -1, new_sd = -1;
	int    desc_ready, end_server = FALSE, conn_lost = FALSE;
	int    conn_state, number = 0;
	int    timeout;
	struct pollfd fds[MAX_EVENTS];
	int    nfds = 1, current_size = 0, i, j;
	if (argc == 2) {
		if (argv[1][0] == 49) {
			printf("Debug mode ON\n");
			debug_flag = 1;
		}
	} else if (argc == 3) {
		if (argv[1][0] == 49) {
			printf("Debug mode ON\n");
			debug_flag = 1;
		}
		try {
			number = std::stoi(argv[2]);
		} catch (const exception &e){
			printf("Enter proper Postgres port: ");
			try {
				std::cin >> number;
				printf("\n");
			} catch (const exception &e){
				print_usage();
				exit(-1);
			}
		}
		if (number>0) {
			SERVER_PORT = number;
		}
	} else if (argc > 3) {
		print_usage();
		exit(-1);
	}
	else{
		printf("Debug mode OFF\n");
	}
	printf("Postgres server port = %d\n", SERVER_PORT);
	// check LOG file
	check_if_file_exist("query.txt");
	listen_sock =  create_socket(NULL, PROXY_PORT);
	//remote_sd =  create_socket(SERVER_PORT);
	/* Initialize the pollfd structure                           */
	memset(fds, 0 , sizeof(fds));
   /* Set up the initial listening socket                        */
	fds[0].fd = listen_sock;
	fds[0].events = POLLIN;
  
	// timeout 3 seconds. 5 minutes
	timeout = (3*60* 1000);

	/* Loop waiting for incoming connects or for incoming data   */
	/* on any of the connected sockets.                          */
	do {
		/* Call poll() and wait 3 minutes for it to complete.      */
		rc = poll(fds, nfds, timeout);
		/* Check to see if the poll call failed.                   */
		if (rc < 0) {
			perror("  poll() failed");
			break;
		}
		/* Check to see if the 3 minute time out expired.          */
		if (rc == 0) {
			printf("  poll() timed out.  End program.\n");
			break;
		}
		/* One or more descriptors are readable.  Need to          */
		/* determine which ones they are.                          */
		for (i = 0; i < nfds; i++)
		{
			/* Loop through to find the descriptors that returned    */
			/* POLLIN and determine whether it's the listening       */
			/* or the active connection.                             */
			if (fds[i].revents == 0)
				continue;
			// If revents is not POLLIN, it's an unexpected result,  log and end the server.
			/*
			if (fds[i].revents != POLLIN) {
				printf("  Error! revents = %d\n", fds[i].revents);
				end_server = TRUE;
				break;
			}*/
			if (fds[i].fd == listen_sock) {
				// accept client connection PROXY_CLIENT
				conn_state = handle_accept_event(fds, listen_sock, nfds);
				s[nfds].sock_c = fds[nfds].fd;
				continue;
			}
			/* This is not the listening socket, therefore an        */
			/* existing connection must be readable                  */
			else {
				//printf("  Descriptor %d is readable\n", fds[i].fd);
				conn_state = FALSE;
			
				if (fds[i].revents == POLLIN)
				{
					// recv request from client or response from server
					conn_state = handle_poolin_event(fds, nfds, i);
					/* If the conn_state flag was turned on, we need       */
					/* to clean up this active connection. This            */
					/* clean up process includes removing the              */
					/* descriptor.                               */
					if (conn_state == TRUE)
						conn_lost = disconnect_sock(fds, i, conn_state);
				}
			}  /* End of existing connection is readable             */
		} /* End of loop through pollable descriptors              */
		
		/* If the compress_array flag was turned on, we need       */
		/* to squeeze together the array and decrement the number  */
		/* of file descriptors. We do not need to move back the    */
		/* events and revents fields because the events will always*/
		/* be POLLIN in this case, and revents is output.          */
		if (conn_lost == TRUE) {
			delete_connection(conn_lost, fds, nfds);
			printf("number of connections : %d\n", nfds);
		}
		conn_state = FALSE;
		conn_lost = FALSE;
	} while (end_server == FALSE); /* End of serving running.    */

	// clean sockets
  for (i = 0; i < nfds; i++)	{
    if (fds[i].fd >= 0)
		close(fds[i].fd);
  }
	return 0;
}


