#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/select.h>

#include "capsule.h"
#include "dlist.h"
#include "psutils.h"

#define PORT1 15001			// Port1
#define PORT2 15002			// Port2
#define BACKLOG 10	    // Max number of pending connections
#define MAXCON 100			// Max number of allowed connections

int setup_serv_con(int *sock_fd, int *port);
int accept_con(int *listen_sock_fd, fd_set *read_fds, dlist *cli_list,
		int nfds);
void *get_in_addr(struct sockaddr *sa);
void serv_loop(int *listen_sock_fd_ptr);
void child_proc(int *sock_fd, int pipe_fd[2]);
void free_chld(dlist_position p, dlist *l, fd_set *s);

// main
int main() {
	int listen_sock_fd = 0;
	int port = 0;

	// Open link
	port = PORT1;
	if ((setup_serv_con(&listen_sock_fd, &port)) < 0)
		exit(EXIT_FAILURE);

	// Listen
	if (listen(listen_sock_fd, BACKLOG) == -1) {
		perror("listen");
		exit(EXIT_FAILURE);
	}

	printf("server: waiting for connections...\n");

	// Main loop of server
	serv_loop(&listen_sock_fd);
	close(listen_sock_fd);
	exit(EXIT_SUCCESS);
}

// serv_loop
void serv_loop(int *listen_sock_fd_ptr) {
	int *pipe_fd;
	dlist *cli_list;
	char buf_ch;
	struct timeval read_tv, set_tv = { .tv_sec = 1, .tv_usec = 0 };
	fd_set read_fds, set_fds;
	int nact = 0, nfds = 0, nfds_tmp;
	int mfds = 0;
	dlist_position p, q;
	s_capsule s_cap;
	m_capsule m_cap;
	int num_read_bytes = 0;      // Number of read bytes
	int num_writ_bytes = 0;      // Number of written bytes

	// Define linked list of clients
	cli_list = dlist_empty();
	dlist_setMemHandler(cli_list, free);

	// Define file descriptor set and add listening socket
	FD_ZERO(&set_fds);
	FD_SET(STDIN_FILENO, &set_fds);
	FD_SET(*listen_sock_fd_ptr, &set_fds);

	nfds = *listen_sock_fd_ptr + 1;

	while(1) {

		read_fds = set_fds;
		read_tv = set_tv;
		mfds = select(nfds, &read_fds, NULL, NULL, &read_tv);
		nact = 0;

		// Exit on ESC
		if (FD_ISSET(STDIN_FILENO, &read_fds)) {
			nact++;

			// Protected getchar
			pgetc(&buf_ch);

			if (buf_ch == 27) {
				printf("ESC is pressed\n");
				m_cap.signal = CLOSE;

				/* Removing elements from 'cli_list' from beginning to end.
				 * Every iteration removes the first element.
				 * From the implementation of dlist, 'dlist_first' returns the
				 * same address even after the first value of the list is removed.
				 */
				for (p = dlist_first(cli_list);
						!dlist_isEnd(p);
						) {
					pipe_fd = dlist_inspect(p);
					// Send close signal to children
					if ( (num_writ_bytes = write(pipe_fd[1], &m_cap,
									sizeof(m_cap))) == -1 )
						perror("server write 'CLOSE' to child");
					free_chld(p, cli_list, &set_fds);
				}
				dlist_free(cli_list);
				break;
			}
		}


		// Error check
		if (mfds == -1) {
			if (strcmp(strerror(errno),"EINTR"))
				continue;
			perror("server select");
			dlist_free(cli_list);
			exit(EXIT_FAILURE);
		}


		// Restart loop if no fd is set
		if (mfds == 0)
			continue;


		// Test if listen_sock_fd is set i.e. a new client attempts to connect,
		// then accept the connection.
		if (FD_ISSET(*listen_sock_fd_ptr, &read_fds)) {
			if ( (nfds_tmp = accept_con(listen_sock_fd_ptr,
							&set_fds, cli_list, nfds)) > nfds )
				nfds = nfds_tmp;

			printf("nsdf: %d\n",nfds);
			nact++;
			continue;
		}


		// Distribute messages

		for ( p = dlist_first(cli_list);
				( dlist_isValid(p) && !dlist_isEnd(p) && (nact < mfds) );
				p=dlist_next(p)) {

			pipe_fd = dlist_inspect(p);

			// Test if child is set
			if (FD_ISSET(pipe_fd[0], &read_fds)) {
				nact++;

				q = p;

				// Read message
				if ( (num_read_bytes = read(pipe_fd[0], &s_cap,
								sizeof(s_cap))) == -1 )
					perror("server receive s_cap from child");

				switch (s_cap.signal) {

					// Free child if child is closing
					case CLOSE :
						free_chld(q, cli_list, &set_fds);
						break;

					case SIZE_DESCRIPTOR :

						// Read m_cap
						memset(&m_cap,0,sizeof(m_cap));
						if ( (num_read_bytes = read(pipe_fd[0], &m_cap,
										s_cap.siz)) == -1 )
							perror("child receive m_cap from server");


						q = dlist_moveToFront(cli_list, q);

						// Transfer message
						for (q = dlist_next(q);
								dlist_isValid(q) && !dlist_isEnd(q);
								q = dlist_next(q)) {

							pipe_fd = dlist_inspect(q);

							// Write s_cap to child
							if ( (num_writ_bytes = write(pipe_fd[1], &s_cap,
											sizeof(s_cap))) == -1 )
								perror("server send to child");

							// Write m_cap to child
							if ( (num_writ_bytes = write(pipe_fd[1], &m_cap,
											s_cap.siz)) == -1 )
								perror("server send to child");
						}
						break;

					default :
						break;
				}
			}
		}
	}
	return;
}

// setup_serv_con
int setup_serv_con(int *sock_fd, int *port) {
	struct addrinfo hints, *servinfo, *p;
	int rv;
	int yes=1;
	char port_char[5] = {0};

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	sprintf(port_char, "%d", *port);
	if ((rv = getaddrinfo(NULL, port_char, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return -1;
	}

	// Create socket
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((*sock_fd = socket(p->ai_family, p->ai_socktype,
						p->ai_protocol)) == -1) {
			perror("server: create socket");
			continue;
		}

		// Reuse occupied sockets
		if (setsockopt(*sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes,
					sizeof(int)) == -1) {
			close(*sock_fd);
			perror("setsockopt");
			exit(EXIT_FAILURE);
		}

		// Bind socket to port
		if (bind(*sock_fd, p->ai_addr, p->ai_addrlen) == -1) {
			close(*sock_fd);
			perror("server: bind");
			continue;
		}

		break;	// Everything went well, exiting loop
	}

	// Exit on bind fail
	if (p == NULL)  {
		fprintf(stderr, "server: failed to bind\n");
		exit(EXIT_FAILURE);
	}

	freeaddrinfo(servinfo);

	return 0;
}

// accept_con
int accept_con(int *listen_sock_fd,fd_set *set_fds,dlist *cli_list,int nfds) {

	int client_sock_fd[2], listen_sock_fd2 = 0;
	int *pipe_fd, pipe_fd1[2], pipe_fd2[2];
	char s[INET6_ADDRSTRLEN];
	int port2;
	struct sockaddr_storage their_addr;
	int num_writ_bytes = 0;
	socklen_t sin_size = sizeof their_addr;
	p_capsule p_cap;
	s_capsule s_cap;
	struct sigaction sa;

	// Set up write connection
	client_sock_fd[1] = accept(*listen_sock_fd,
			(struct sockaddr *)&their_addr, &sin_size);
	if (client_sock_fd[1] == -1) {
		perror("accept send sock");
		exit(EXIT_FAILURE);
	}
	printf("parent: write connection is set\n");


	// Open new link for read connection on PORT2
	port2 = PORT2;
	if ((setup_serv_con(&listen_sock_fd2, &port2)) < 0)
		exit(EXIT_FAILURE);

	// Start to listen for read connection
	if (listen(listen_sock_fd2, BACKLOG) == -1) {
		perror("listen receive socket");
		exit(EXIT_FAILURE);
	}

	// Tell client to establish the read connection on PORT2
	memset(&p_cap,0,sizeof(p_cap));
	p_cap.signal = PING;
	p_cap.num = PORT2;
	if ( (num_writ_bytes = send(client_sock_fd[1], &p_cap,
					sizeof(p_cap), 0)) == -1 )
		perror("child send to client");


	// Set up read connection
	client_sock_fd[0] = accept(listen_sock_fd2,
			(struct sockaddr *)&their_addr, &sin_size);
	if (client_sock_fd[0] == -1) {
		perror("accept send socket");
		exit(EXIT_FAILURE);
	}


	printf("parent: read connection is set\n");

	// Close the recently opened socket
	close(listen_sock_fd2);

	// Print successful connection to stdout
	inet_ntop(their_addr.ss_family,
			get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
	printf("server: got connection from %s\n", s);

	//  Network connection is now established
	//  Now fork and set up pipes between parent and child

	// Create pipes in two directions
	if (pipe(pipe_fd1) == -1) {
		perror("pipe");
		exit(EXIT_FAILURE);
	}
	if (pipe(pipe_fd2) == -1) {
		perror("pipe");
		exit(EXIT_FAILURE);
	}
	printf("Pipes are set\n");

	// Setup zombie reaper
	sa.sa_handler = SIG_IGN;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
	}
	printf("Zombie reaper is set\n");

	/*	Child:
	 *
	 *	client_sock_fd[0]     read
	 *	client_sock_fd[1]     write
	 *	pipe_fd[0]			  read
	 *	pipe_fd[1]			  write
	 *
	 *
	 *	Parent:
	 *
	 *	pipe_fd[0]			  read
	 *	pipe_fd[1]			  write
	 */
	// Create child process
	if (!fork()) {

		close(*listen_sock_fd); // Child doesn't need this

		if ( (pipe_fd = (int *)calloc(2, sizeof(int))) == NULL ) {
			perror("calloc");
			exit(EXIT_FAILURE);
		}
		pipe_fd[0] = pipe_fd1[0];
		pipe_fd[1] = pipe_fd2[1];
		close(pipe_fd1[1]);
		close(pipe_fd2[0]);
		printf("child: pipes are linked\n");

		// Child function starts here
		child_proc(client_sock_fd, pipe_fd);

		// Clean up after returned child process
		memset(&s_cap,0,sizeof(s_capsule));
		s_cap.signal = CLOSE;
		if ( (num_writ_bytes = write(pipe_fd[1], &s_cap, sizeof(s_cap))) == -1 )
			perror("child write to server");
		close(client_sock_fd[0]);
		close(client_sock_fd[1]);
		close(pipe_fd[0]);
		close(pipe_fd[1]);
		free(pipe_fd);
		printf("connection from %s has been closed\n", s);

		_Exit(EXIT_FAILURE);
	}
	printf("parent: child process is set\n");

	close(client_sock_fd[0]);  // Parent doesn't need this
	close(client_sock_fd[1]);  // Parent doesn't need this

	if ( (pipe_fd = (int *)calloc(2, sizeof(int))) == NULL ) {
		perror("pipe allocation");
		exit(EXIT_FAILURE);
	}
	pipe_fd[0] = pipe_fd2[0];
	pipe_fd[1] = pipe_fd1[1];
	close(pipe_fd1[0]);
	close(pipe_fd2[1]);

	FD_SET(pipe_fd[0], set_fds);
	dlist_insert(dlist_first(cli_list), pipe_fd);
	if (pipe_fd[0] >= nfds)
		nfds = pipe_fd[0] + 1;

	printf("parent: pipes are linked\n");

	return nfds;
}

// child_proc
void child_proc(int *client_sock_fd, int pipe_fd[2]) {
	struct timeval read_tv, set_tv = { .tv_sec = 0, .tv_usec = 50000 };
	fd_set read_fds, set_fds;
	int nfds = 0;				// Largest file descriptor + 1
	int mfds = 0;				// Number of modified file descriptors
	s_capsule s_cap;
	m_capsule m_cap;
	int num_read_bytes = 0;     // Number of received bytes
	int num_writ_bytes = 0;     // Number of written bytes

	FD_ZERO(&set_fds);						// Create set of file descriptors
	FD_SET(client_sock_fd[0], &set_fds);    // Listen from connected client
	FD_SET(pipe_fd[0], &set_fds);			// Listen from server

	if (client_sock_fd[0] > pipe_fd[0])
		nfds = client_sock_fd[0] + 1;
	else
		nfds = pipe_fd[0] + 1;

	while(1) {
		read_fds = set_fds;
		read_tv = set_tv;

		mfds = select(nfds, &read_fds, NULL, NULL, &read_tv);

		// Error check
		if (mfds == -1) {
			if (strcmp(strerror(errno),"EINTR")) {
				fprintf(stderr,"child select: EINTR\n");
				continue;
			}
			perror("child select");
			return;
		}

		// Transfer data from SERVER to CLIENT
		if (FD_ISSET(pipe_fd[0], &read_fds)) {

			// Read s_cap
			if ( (num_read_bytes = read(pipe_fd[0], &s_cap,
							sizeof(s_cap))) == -1 )
				perror("child receive s_cap from server");
			if (num_read_bytes != sizeof(s_cap))
				fprintf(stderr,"s_cap: Not all data is read correctly!\nRead: %d\n"
						"Size: %u\n",num_read_bytes,(unsigned int)sizeof(s_cap));


			switch (s_cap.signal) {

				// Exit client if parent process is closing
				case CLOSE :
					return;

					// Redirect incoming message from SERVER
				case SIZE_DESCRIPTOR :

					memset(&m_cap,0,sizeof(m_cap));

					// Read m_cap from SERVER
					if ( (num_read_bytes = read(pipe_fd[0], &m_cap,
									s_cap.siz)) == -1 )
						perror("child receive m_cap from server");
					if (num_read_bytes != s_cap.siz)
						fprintf(stderr,"m_cap: Not all data is read correctly!\n"
								"Read: %d\nSize: %u\n",num_read_bytes,(unsigned int)s_cap.siz);

					// Send s_cap to CLIENT
					if ( (num_writ_bytes = send(client_sock_fd[1], &s_cap,
									sizeof(s_cap), 0)) == -1 )
						perror("child send s_cap to client");
					if (num_writ_bytes != sizeof(s_cap))
						fprintf(stderr,"s_cap: Not all data is sent correctly!\n"
								"Send: %d\nSize: %u\n",num_writ_bytes,(unsigned int)sizeof(s_cap));

					// Send m_cap to CLIENT
					if ( (num_writ_bytes = send(client_sock_fd[1], &m_cap,
									s_cap.siz, 0)) == -1 )
						perror("child send to client");
					if (num_writ_bytes != s_cap.siz)
						fprintf(stderr,"m_cap: Not all data is sent correctly!\n"
								"Send: %d\nSize: %u\n",num_writ_bytes,(unsigned int)s_cap.siz);

					// Error check
					if (num_read_bytes != num_writ_bytes) {
						fprintf(stderr,"Child process could not transfer all "
								"data correctly to client\n"
								"Read: %d\nWrite: %u\n", num_read_bytes,(unsigned int)num_writ_bytes);
						return;
					}
					break;

				default :
					break;
			}
		}

		// Transfer data from CLIENT to SERVER
		if (FD_ISSET(client_sock_fd[0], &read_fds)) {

			// Receive s_cap from CLIENT
			if ( (num_read_bytes = recv(client_sock_fd[0], &s_cap,
							sizeof(s_cap), 0)) == -1)
				perror("child receive s_cap from client");
			if (num_read_bytes != sizeof(s_cap))
				fprintf(stderr,"Not all data is received correctly!\nReceive: %d\n"
						"Size: %u\n",num_read_bytes,(unsigned int)sizeof(s_cap));

			// Check if connection is closed by client
			if (num_read_bytes == 0)
				return;

			switch (s_cap.signal) {

				// Exit client if parent is closing
				case CLOSE :
					if ( (num_writ_bytes = write(pipe_fd[1], &s_cap,
									sizeof(s_cap))) == -1 )
						perror("child send s_cap:CLOSE to server");
					if (num_writ_bytes != sizeof(s_cap))
						fprintf(stderr,"s_cap: Not all data is written correctly!\n"
								"Write: %d\nSize: %u\n",num_writ_bytes,(unsigned int)sizeof(s_cap));
					return;

					// Respond to ping
				case PING :
					//TODO
					break;

				case SIZE_DESCRIPTOR :

					// Read from CLIENT
					memset(&m_cap,0,sizeof(m_cap));
					if ( (num_read_bytes = recv(client_sock_fd[0], &m_cap,
									s_cap.siz, 0)) == -1 )
						perror("child receive m_cap from client");
					if (num_read_bytes != s_cap.siz)
						fprintf(stderr,"m_cap: Not all data is read correctly!\n"
								"Read: %d\nSize: %u\n",num_read_bytes,(unsigned int)s_cap.siz);

					// Write s_cap to SERVER
					if ( (num_writ_bytes = write(pipe_fd[1], &s_cap,
									sizeof(s_cap))) == -1 )
						perror("child send s_cap:MESSAGE to server");
					if (num_writ_bytes != sizeof(s_cap))
						fprintf(stderr,"s_cap: Not all data is written correctly!\n"
								"Write: %d\nSize: %u\n",num_writ_bytes,(unsigned int)sizeof(s_cap));

					// Write m_cap to SERVER
					if ( (num_writ_bytes = write(pipe_fd[1], &m_cap,
									s_cap.siz)) == -1 )
						perror("child send m_cap to server");
					if (num_writ_bytes != s_cap.siz)
						fprintf(stderr,"m_cap: Not all data is sent correctly!\n"
								"Send: %d\nSize: %u\n",num_writ_bytes,(unsigned int)s_cap.siz);

					// Error check
					if (num_read_bytes != num_writ_bytes) {
						fprintf(stderr,"Child process could not transfer all "
								"data correctly to server\n"
								"Read: %d\nWrite: %u\n", num_read_bytes,(unsigned int)num_writ_bytes);
						return;
					}

					// Print message
					printf("%s: %s\n", m_cap.origin, m_cap.content);
					break;

				default :
					break;
			}
		}
	}
}

// free_child
/* l: (dlist *) - A dlist_ptr containing elements of (int)pipe[2]
 * p: (dlist_position) - Position in l
 * s: (fd_set *) - pipe[0] is removed from this set
 *
 * - Removes an (int *) from a dlist
 * - Removes pipe[0] from fd_set
 * - Closes and frees pipes
 */
void free_chld(dlist_position p, dlist *l, fd_set *s) {
	int *pipe_fd = dlist_inspect(p);
	FD_CLR(pipe_fd[0], s);
	close(pipe_fd[0]);
	close(pipe_fd[1]);
	dlist_remove(l, p);
	return;
}

// get_in_addr
void *get_in_addr(struct sockaddr *sa) {
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

