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

#define PORT1 "15001"  // Port
#define PORT2 "15002"  // Port
#define BACKLOG 10     // Max number of pending connections
#define MAXCON 100     // Max number of allowed connections

int setup_serv_con(int *sock_fd, char *port);
int accept_con(char *argv[], int *listen_sock_fd, fd_set *read_fds, dlist *cli_list,
		int nfds);
void *get_in_addr(struct sockaddr *sa);
void sigchld_handler(int s);
void serv_loop(char *argv[], int *listen_sock_fd_ptr);
void child_proc(int *sock_fd, int pipe_fd[2]);
void free_chld(dlist_position p, dlist *l, fd_set *s);

int main(int argc, char *argv[]) {
	int listen_sock_fd = 0;

	// Open link
	if ((setup_serv_con(&listen_sock_fd, PORT1)) < 0)
		exit(EXIT_FAILURE);

	// Listen
	if (listen(listen_sock_fd, BACKLOG) == -1) {
		perror("listen");
		exit(EXIT_FAILURE);
	}

	printf("server: waiting for connections...\n");

	// Main loop of server
	serv_loop(argv, &listen_sock_fd);
	close(listen_sock_fd);
	exit(EXIT_SUCCESS);
}

void serv_loop(char *argv[], int *listen_sock_fd_ptr) {
	int *pipe_fd;
	dlist *cli_list;
	char buf_ch;
	struct timeval read_tv, set_tv = { .tv_sec = 1, .tv_usec = 0 };
	fd_set read_fds, set_fds;
	int nact = 0, nfds = 0, nfds_tmp;
	int mfds = 0;
	dlist_position p, q;
	m_capsule m_cap;
	int num_read_bytes = 0;      // Number of read bytes
	int num_writ_bytes = 0;      // Number of written bytes
	dlist_position test_i;

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

			//protected getchar
			pgetc(&buf_ch);

			if (buf_ch == 27) {
				printf("ESC is pressed\n");
				m_cap.signal = CLOSE;
				for (p = dlist_first(cli_list);
						dlist_isValid(p) && !dlist_isEnd(p);
						p = dlist_next(p)) {

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
			if ( (nfds_tmp = accept_con(argv, listen_sock_fd_ptr,
							&set_fds, cli_list, nfds)) > nfds )
				nfds = nfds_tmp;

			printf("Pipe address: %d\n",dlist_inspect(dlist_first(cli_list)));
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
				//	if (!dlist_isEnd(p))
				//		p = dlist_next(p);

				//	printf("\nBefore:\n");
				//	for (test_i=dlist_first(cli_list);
				//			(!dlist_isEnd(test_i));
				//			test_i=dlist_next(test_i)) {
				//		printf("%d\n",dlist_inspect(test_i));
				//	}

				// Receive message
				if ( (num_read_bytes = read(pipe_fd[0], &m_cap,
								sizeof(m_capsule))) == -1 )
					perror("server receive from child");

				// Close on signal 'CLOSE'
				if ( m_cap.signal == CLOSE ) {
					free_chld(q, cli_list, &set_fds);
					continue;
				}

				q=dlist_moveToFront(cli_list, q);

				//	printf("After:\n");
				//	for (test_i=dlist_first(cli_list);
				//			(!dlist_isEnd(test_i));
				//			test_i=dlist_next(test_i)) {
				//		printf("%d\n",dlist_inspect(test_i));
				//	}
				//	printf("\n%d\n",dlist_inspect(q));


				// Transfer message
				for (q = dlist_next(q);
						dlist_isValid(q) && !dlist_isEnd(q);
						q = dlist_next(q)) {

					pipe_fd = dlist_inspect(q);
					if ( (num_writ_bytes = write(pipe_fd[1], &m_cap,
									sizeof(m_cap))) == -1 )
						perror("server send to child");
				}
			}
		}
	}
	return;
}

int setup_serv_con(int *sock_fd, char *port) {
	struct addrinfo hints, *servinfo, *p;
	int rv;
	int yes=1;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	if ((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
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

int accept_con(char *argv[], int *listen_sock_fd, fd_set *set_fds,
		dlist *cli_list, int nfds) {

	int client_sock_fd[2], listen_sock_fd2 = 0;
	int *pipe_fd, pipe_fd1[2], pipe_fd2[2];
	char s[INET6_ADDRSTRLEN];
	//char STDBUF[BUFSIZ];
	struct sockaddr_storage their_addr;
	int num_writ_bytes = 0;
	socklen_t sin_size = sizeof their_addr;
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
	if ((setup_serv_con(&listen_sock_fd2, PORT2)) < 0)
		exit(EXIT_FAILURE);

	// Start to listen for read connection
	if (listen(listen_sock_fd2, BACKLOG) == -1) {
		perror("listen receive socket");
		exit(EXIT_FAILURE);
	}

	// Tell client to establish the read connection on PORT2
	memset(&s_cap,0,sizeof(s_capsule));
	s_cap.signal = PING;
	strcpy(s_cap.content, PORT2);
	if ( (num_writ_bytes = send(client_sock_fd[1], &s_cap,
					sizeof(s_capsule), 0)) == -1 )
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
	//
	//  Now fork and set up pipes between mother and child

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
	//sa.sa_handler = sigchld_handler;
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

		execvp(argv[1], argv + 1);
		perror("execvp");
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
	printf("Pipe address: %d\n",pipe_fd);
	close(pipe_fd1[0]);
	close(pipe_fd2[1]);

	FD_SET(pipe_fd[0], set_fds);
	dlist_insert(dlist_first(cli_list), pipe_fd);
	if (pipe_fd[0] >= nfds)
		nfds = pipe_fd[0] + 1;

	printf("Pipe address: %d\n",dlist_inspect(dlist_first(cli_list)));
	printf("parent: pipes are linked\n");

	return nfds;
}

void child_proc(int *client_sock_fd, int pipe_fd[2]) {
	struct timeval read_tv, set_tv = { .tv_sec = 0, .tv_usec = 50000 };
	fd_set read_fds, set_fds;
	int nfds = 0;				// Largest file descriptor + 1
	volatile int mfds = 0;      // Number of modified file descriptors
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

		// Transfer data from server to client
		if (FD_ISSET(pipe_fd[0], &read_fds)) {

			// Receive
			if ( (num_read_bytes = read(pipe_fd[0], &m_cap,
							sizeof(m_cap))) == -1 )
				perror("child receive from server");

			// Exit client if parent process is closing
			if ( m_cap.signal == CLOSE )
				return;

			// Send
			if ( (num_writ_bytes = send(client_sock_fd[1], &m_cap,
							sizeof(m_cap), 0)) == -1 )
				perror("child send to client");

			// Error check
			if (num_read_bytes != num_writ_bytes) {
				fprintf(stderr,"Child process could not transfer all "
						"data correctly to client\n"
						"Read: %d\nWrite: %d\n", num_read_bytes, num_writ_bytes);
				return;
			}
		}

		// Transfer data from client to server
		if (FD_ISSET(client_sock_fd[0], &read_fds)) {

			// Receive
			if ( (num_read_bytes = recv(client_sock_fd[0], &m_cap,
							sizeof(m_cap), 0)) == -1)
				perror("child receive from client");

			if (num_read_bytes == 0)
				return;

			printf("%s: %s\n", m_cap.origin, m_cap.content);

			// Write
			if ( (num_writ_bytes = write(pipe_fd[1], &m_cap,
							sizeof(m_cap))) == -1 )
				perror("child write to server");

			// Error check
			if (num_read_bytes != num_writ_bytes) {
				fprintf(stderr,"Child process could not transfer all "
						"data correctly to server\n"
						"Read: %d\nWrit: %d\n", num_read_bytes, num_writ_bytes);
				return;
			}

			if ( m_cap.signal == CLOSE )
				return;
		}
	}
}












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

// Get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

// Zombie reaper
void sigchld_handler(int s) {
	int saved_errno = errno;
	while(waitpid(-1, NULL, WNOHANG) > 0);
	errno = saved_errno;
}

