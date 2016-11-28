#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <limits.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <sys/time.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <signal.h>

#include <sys/shm.h>
#include <sys/ipc.h>

#include <stdbool.h>
#include <time.h>

#include "server.h"
#include "capsule.h"
#include "dlist.h"
#include "hashtable.h"
#include "psutils.h"



// main
int main(int argc, char* argv[]) {
	int listen_sock_fd = 0;
	int port = 0;

	if (argc != 1) {
		fprintf(stderr, "Syntax: server\n");
		exit(EXIT_FAILURE);
	}


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
	dlist *chld_list;
	hashtable_t *chld_hash;
	char buf_ch;
	struct timeval read_tv, set_tv = { .tv_sec = 0, .tv_usec = 500000 };
	fd_set read_fds, set_fds;
	int nact = 0, nfds = 0, nfds_tmp;
	int mfds = 0;
	dlist_position p, q;
	s_capsule s_cap;
	m_capsule m_cap;
	int num_read_bytes = 0;      // Number of read bytes
	int num_writ_bytes = 0;      // Number of written bytes
	child_data_prnt *child;


	// Define linked list of child data
	chld_list = dlist_empty();

	// Define hashtable for child data ID:s
	chld_hash = hashtable_empty(2*MAXCON, strhash, strcmp2);

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
				m_cap.signal = CLOS;

				/* Removing elements from 'chld_list' from beginning to end.
				 * Every iteration removes the first element.
				 * From the implementation of dlist, 'dlist_first' returns the
				 * same address even after the first value of the list is removed.
				 */
				for (p = dlist_first(chld_list);
						!dlist_isEnd(p);
						) {
					child = dlist_inspect(p);
					// Send close signal to children
					if ( (num_writ_bytes = write(child->pipe_fd[1], &m_cap,
									sizeof(m_cap))) == -1 )
						perror("server write 'CLOS' to child");
					parent_call_free_chld(child, chld_list, chld_hash, &set_fds);
				}
				hashtable_free(chld_hash);
				dlist_free(chld_list);
				break;
			}
		}


		// Error check
		if (mfds == -1) {
			if (strcmp(strerror(errno),"EINTR"))
				continue;
			perror("server select");
			hashtable_free(chld_hash);
			dlist_free(chld_list);
			exit(EXIT_FAILURE);
		}


		// Restart loop if no fd is set
		if (mfds == 0)
			continue;


		// Test if listen_sock_fd is set i.e. a new client attempts to connect,
		// then accept the connection.
		if (FD_ISSET(*listen_sock_fd_ptr, &read_fds)) {
			if ( (nfds_tmp = accept_con(listen_sock_fd_ptr,	&set_fds,
							chld_list, chld_hash,	nfds)) > nfds )
				nfds = nfds_tmp;

			printf("nsdf: %d\n",nfds);
			nact++;
			continue;
		}


		// Distribute messages

		for ( p = dlist_first(chld_list);
				( dlist_isValid(p) && !dlist_isEnd(p) && (nact < mfds) );
				p=dlist_next(p)) {

			child = dlist_inspect(p);

			// Test if child is set
			if (FD_ISSET(child->pipe_fd[0], &read_fds)) {
				nact++;

				q = p;

				// Read message
				if ( (num_read_bytes = read(child->pipe_fd[0], &s_cap,
								sizeof(s_cap))) == -1 )
					perror("server receive s_cap from child");

				switch (s_cap.signal) {

					// Free child if child is closing
					case CLOS :
						parent_call_free_chld(child, chld_list, chld_hash, &set_fds);
						break;

					case SIZE:

						// Continue if child is inactive
						if (child->is_active == false) {
							continue;
						}

						// Read m_cap
						memset(&m_cap,0,sizeof(m_cap));
						if ( (num_read_bytes = read(child->pipe_fd[0], &m_cap,
										s_cap.siz)) == -1 )
							perror("child receive m_cap from server");


						q = dlist_moveToFront(chld_list, q);

						// Transfer message
						for (q = dlist_next(q);
								dlist_isValid(q) && !dlist_isEnd(q);
								q = dlist_next(q)) {

							child = dlist_inspect(q);

							// Continue if child is inactive
							if (child->is_active == false)
								continue;

							// Write s_cap to child
							if ( (num_writ_bytes = write(child->pipe_fd[1], &s_cap,
											sizeof(s_cap))) == -1 )
								perror("server send to child");

							// Write m_cap to child
							if ( (num_writ_bytes = write(child->pipe_fd[1], &m_cap,
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

// accept_con
int accept_con(int *listen_sock_fd, fd_set *set_fds, dlist *chld_list,
		hashtable_t *chld_hash, int nfds) {

	int client_sock_fd[2], listen_sock_fd2 = 0;
	int *pipe_fd, pipe_fd1[2], pipe_fd2[2];
	char s[INET6_ADDRSTRLEN];
	int port2;
	struct sockaddr_storage their_addr;
	int num_writ_bytes = 0;
	socklen_t sin_size = sizeof(their_addr);
	p_capsule p_cap = {0};
	child_data *child;
	struct sigaction sa;
	fd_set read_fds;
	struct timeval read_tv = { .tv_sec = 0, .tv_usec = 0 },
								 set_tv = { .tv_sec = 1, .tv_usec = 10000 };
	int pid;
	int i;
	key_t shm_key;
	int shm_id;
	int id;

	extern char self_path[14];
	const size_t buf_size = PATH_MAX + 1;
	char buf_name[buf_size];
	memset(&buf_name,0,sizeof(buf_name));
	readlink(self_path, buf_name, PATH_MAX);


	char elf_name[strlen(buf_name)];
	strncpy(elf_name, buf_name, sizeof(elf_name));

	// Generate child ID
	srand(time(NULL));
	id = rand();
	while( hashtable_lookup(chld_hash, &id) != NULL ) {
		srand(time(NULL));
		id = rand();
	}

	printf("child-ID: %u\n",id);

	elf_name[sizeof(elf_name)] = 0;

	// Allocate child within shared memory
	if ( (shm_key = ftok(elf_name, id)) == -1 ) {
		perror("ftok");
		exit(EXIT_FAILURE);
	}
	if ( (shm_id = shmget(shm_key, sizeof(child), IPC_CREAT | 0660 )) == -1 ) {
		perror("shmget");
		exit(EXIT_FAILURE);
	}
	if ( (child = shmat(shm_id, NULL, 0)) == (void *)(-1) ) {
		perror("parent: shmat");
		exit(EXIT_FAILURE);
	}

	memset(child, 0, sizeof(child));

	// Insert child into hashtable
	hashtable_insert(chld_hash, &id, child);

	// Insert child into dlist
	child->list_pos = dlist_insert(dlist_first(chld_list), child);

	child->shm_id = shm_id;
	child->id = id;

	// Create pipes in two directions
	if (pipe(pipe_fd1) == -1) {
		perror("pipe");
		exit(EXIT_FAILURE);
	}
	if (pipe(pipe_fd2) == -1) {
		perror("pipe");
		exit(EXIT_FAILURE);
	}
	child->pipe_fd_chld[0] = pipe_fd1[0];
	child->pipe_fd_chld[1] = pipe_fd2[1];
	child->pipe_fd_prnt[0] = pipe_fd2[0];
	child->pipe_fd_prnt[1] = pipe_fd1[1];

	printf("Pipes are set\n");

	// Setup zombie reaper
	sa.sa_handler = SIG_IGN;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
	}
	printf("Zombie reaper is set\n");


	// Create child process
	if ( !(pid=fork()) ) {

		// Attach child to shared memory
		if ( (child = shmat(shm_id, NULL, 0)) == (void *)(-1) ) {
			perror("child: shmat");
			exit(EXIT_FAILURE);
		}

		// Set up write connection
		child->client_sock_fd[1] = accept(*listen_sock_fd,
				(struct sockaddr *)&their_addr, &sin_size);

		if (child->client_sock_fd[1] == -1) {
			// TODO free_child
			exit(EXIT_FAILURE);
		}

		// Open new link for receive connection on PORT2
		port2 = PORT2;
		if ((setup_serv_con(&listen_sock_fd2, &port2)) < 0) {
			// Tell parent that connection failed so that associated data can be freed
			// TODO free_child
			exit(EXIT_FAILURE);
		}

		// Start to listen for receive connection
		if (listen(listen_sock_fd2, BACKLOG) == -1) {
			perror("listen receive socket");
			// TODO free_child
			exit(EXIT_FAILURE);
		}

		// Tell client to establish the receive connection on PORT2
		memset(&p_cap,0,sizeof(p_cap));
		p_cap.signal = PING;
		p_cap.num = PORT2;
		if ( (num_writ_bytes = send(child->client_sock_fd[1], &p_cap,
						sizeof(p_cap), 0)) == -1 ) {
			// TODO free_child
			exit(EXIT_FAILURE);
		}

		// While client is thinking and packets are in the air,
		// we allocate and link the pipes of interprocess communication

		/*	Child:
		 *
		 *	client_sock_fd[0]     read
		 *	client_sock_fd[1]     write
		 *	pipe_fd[0]			  read
		 *	pipe_fd[1]			  write
		 */


		close(pipe_fd1[1]);
		close(pipe_fd2[0]);
		printf("child: pipes are linked\n");


		// Set up read connection
		child->client_sock_fd[0] = accept(listen_sock_fd2,
				(struct sockaddr *)&their_addr, &sin_size);
		if (child->client_sock_fd[0] == -1) {
			perror("accept send socket");
			// TODO free_child
			exit(EXIT_FAILURE);
		}

		printf("child: receive connection is set\n");

		// Close sockets
		close(*listen_sock_fd);
		close(listen_sock_fd2);

		// Print successful connection to stdout
		inet_ntop(their_addr.ss_family,
				get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
		printf("server: got connection from %s\n", s);


		child->is_active = true;

		//  Network connection is now established

		// Child function starts here
		child_proc((child_data_chld *)child);

		printf("connection from %s has been closed\n", s);

		_Exit(EXIT_SUCCESS);
	}

	printf("child pid: %d\n",pid);

	/*	Parent:
	 *
	 *	pipe_fd[0]			  read
	 *	pipe_fd[1]			  write
	 */
	close(child->pipe_fd_chld[0]);
	close(child->pipe_fd_chld[1]);

	FD_SET(child->pipe_fd_prnt[0], set_fds);

	if (child->pipe_fd_prnt[0] >= nfds)
		nfds = child->pipe_fd_prnt[0] + 1;

	return nfds;
}

// child_proc
void child_proc(child_data_chld *child) {
	struct timeval read_tv, set_tv = { .tv_sec = 0, .tv_usec = 50000 };
	fd_set read_fds, set_fds;
	int nfds = 0;				// Largest file descriptor + 1
	int mfds = 0;				// Number of modified file descriptors
	s_capsule s_cap;
	m_capsule m_cap;
	int32_t num_read_bytes = 0;							// Number of received bytes
	int32_t num_writ_bytes = 0;							// Number of written bytes

	FD_ZERO(&set_fds);											// Create set of file descriptors
	FD_SET(child->client_sock_fd[0], &set_fds);		// Listen from connected client
	FD_SET(child->pipe_fd[0], &set_fds);						// Listen from server

	if (child->client_sock_fd[0] > child->pipe_fd[0])
		nfds = child->client_sock_fd[0] + 1;
	else
		nfds = child->pipe_fd[0] + 1;

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
		if (FD_ISSET(child->pipe_fd[0], &read_fds)) {

			// Read s_cap
			if ( (num_read_bytes = read(child->pipe_fd[0], &s_cap,
							sizeof(s_cap))) == -1 )
				perror("child receive s_cap from server");
			if (num_read_bytes != sizeof(s_cap)) {
				fprintf(stderr,"s_cap: Not all data is read correctly!\n"
						"Read: %d\nSize: %u\n",
						num_read_bytes,(unsigned int)sizeof(s_cap));
				return;
			}


			switch (s_cap.signal) {

				// Exit client if parent process is closing
				case CLOS :
					child->is_active = false;

					printf("c1\n");
					send(child->client_sock_fd[1], &s_cap,sizeof(s_cap), 0);
					printf("c2\n");

					printf("Freeing child\n");
					free_child(child);
					return;

					// Redirect incoming message from SERVER
				case SIZE:

					memset(&m_cap,0,sizeof(m_cap));

					// Read m_cap from SERVER
					if ( (num_read_bytes = read(child->pipe_fd[0], &m_cap,
									s_cap.siz)) == -1 )
						perror("child receive m_cap from server");
					if (num_read_bytes != s_cap.siz)
						fprintf(stderr,"m_cap: Not all data is read correctly!\n"
								"Read: %d\nSize: %u\n",
								num_read_bytes,(unsigned int)s_cap.siz);

					// Send s_cap to CLIENT
					if ( (num_writ_bytes = send(child->client_sock_fd[1], &s_cap,
									sizeof(s_cap), 0)) == -1 )
						perror("child send s_cap to client");
					if (num_writ_bytes != sizeof(s_cap))
						fprintf(stderr,"s_cap: Not all data is sent correctly!\n"
								"Send: %d\nSize: %u\n",
								num_writ_bytes,(unsigned int)sizeof(s_cap));

					// Send m_cap to CLIENT
					if ( (num_writ_bytes = send(child->client_sock_fd[1], &m_cap,
									s_cap.siz, 0)) == -1 )
						perror("child send to client");
					if (num_writ_bytes != s_cap.siz)
						fprintf(stderr,"m_cap: Not all data is sent correctly!\n"
								"Send: %d\nSize: %u\n",
								num_writ_bytes,(unsigned int)s_cap.siz);

					// Error check
					if (num_read_bytes != num_writ_bytes) {
						fprintf(stderr,"Child process could not transfer all "
								"data correctly to client\n"
								"Read: %d\nWrite: %u\n",
								num_read_bytes,(unsigned int)num_writ_bytes);
						return;
					}
					break;

				default :
					break;
			}
		}

		// Transfer data from CLIENT to SERVER
		if (FD_ISSET(child->client_sock_fd[0], &read_fds)) {

			// Receive s_cap from CLIENT
			if ( (num_read_bytes = recv(child->client_sock_fd[0], &s_cap,
							sizeof(s_cap), 0)) == -1)
				perror("child receive s_cap from client");
			if (num_read_bytes != sizeof(s_cap) && num_read_bytes != 0)
				fprintf(stderr,"Not all data is received correctly!\n"
						"Receive: %d\nSize: %u\n",
						num_read_bytes,(unsigned int)sizeof(s_cap));

			// Exit if connection is closed by client
			if (num_read_bytes == 0) {
				child->is_active = false;

				// Tell parent to close up this child
				memset(&s_cap,0,sizeof(s_cap));
				s_cap.signal=CLOS;
				if ( (num_writ_bytes = write(child->pipe_fd[1], &s_cap,
								sizeof(s_cap))) == -1 )
					perror("child send s_cap:CLOS to server");

					printf("c1\n");
				// Read ack from server
				do {
					if ( (num_read_bytes = read(child->pipe_fd[0], &s_cap,
									sizeof(s_cap))) == -1 )
						perror("child receive s_cap from server");
				} while (num_read_bytes != 0); // Now server has closed pipe

					printf("c2\n");
				printf("Freeing child\n");
				free_child(child);
				return;
			}

			switch (s_cap.signal) {

				// Exit if received close from client
				case CLOS :
					child->is_active = false;

					if ( (num_writ_bytes = write(child->pipe_fd[1], &s_cap,
									sizeof(s_cap))) == -1 )
						perror("child send s_cap:CLOS to server");

					// Read ack from server
					do {
						if ( (num_read_bytes = read(child->pipe_fd[0], &s_cap,
										sizeof(s_cap))) == -1 )
							perror("child receive s_cap from server");
					} while (num_read_bytes != 0); // Now server has closed pipe

					printf("Freeing child\n");
					free_child(child);
					return;

					// Respond to ping
				case PING :
					//TODO
					break;

				case SIZE:

					// Read from CLIENT
					memset(&m_cap,0,sizeof(m_cap));
					if ( (num_read_bytes = recv(child->client_sock_fd[0], &m_cap,
									s_cap.siz, 0)) == -1 )
						perror("child receive m_cap from client");
					if (num_read_bytes != s_cap.siz)
						fprintf(stderr,"m_cap: Not all data is read correctly!\n"
								"Read: %d\nSize: %u\n",
								num_read_bytes,(unsigned int)s_cap.siz);

					// Write s_cap to SERVER
					if ( (num_writ_bytes = write(child->pipe_fd[1], &s_cap,
									sizeof(s_cap))) == -1 )
						perror("child send s_cap:MESS to server");
					if (num_writ_bytes != sizeof(s_cap))
						fprintf(stderr,"s_cap: Not all data is written correctly!\n"
								"Write: %d\nSize: %u\n",
								num_writ_bytes,(unsigned int)sizeof(s_cap));

					// Write m_cap to SERVER
					if ( (num_writ_bytes = write(child->pipe_fd[1], &m_cap,
									s_cap.siz)) == -1 )
						perror("child send m_cap to server");
					if (num_writ_bytes != s_cap.siz)
						fprintf(stderr,"m_cap: Not all data is written correctly!\n"
								"Send: %d\nSize: %u\n",
								num_writ_bytes,(unsigned int)s_cap.siz);

					// Error check
					if (num_read_bytes != num_writ_bytes) {
						fprintf(stderr,"Child process could not transfer all "
								"data correctly to server\n"
								"Read: %d\nWrite: %u\n",
								num_read_bytes,(unsigned int)num_writ_bytes);
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

	for(p = servinfo; p != NULL; p = p->ai_next) {

		// Create socket
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

		break;
	}

	// Exit on bind fail
	if (p == NULL)  {
		fprintf(stderr, "server: failed to bind\n");
		exit(EXIT_FAILURE);
	}

	freeaddrinfo(servinfo);

	return 0;
}

// parent_call_free_child
void parent_call_free_chld(child_data_prnt *child, dlist *l, hashtable_t *h, fd_set *s) {
	int pip0 = child->pipe_fd[0];
	int pip1 = child->pipe_fd[1];
	s_capsule c_cap = {0};

	printf("p1\n");

	FD_CLR(pip0, s);
	hashtable_remove(h, (ht_key_t) &child->id);
	dlist_remove(l, child->list_pos);

	printf("p2\n");

	// Tell child to close if it is still active. It is supposed to do
	// this if child is inactive because then child is already aware of
	// it being terminated.
	if (child->is_active == true){
		c_cap.signal = CLOS;
		if ( write(pip1, &c_cap, sizeof(c_cap)) == -1 )
			perror("Free child: send c_cap:CLOS to child");
	}

	printf("p3\n");

	// Detach from shared memory
	if ( shmdt(child) == -1 )
		perror("Parent shmdt");

	printf("p4\n");

	close(pip0);
	close(pip1); // Should be last in func. Tells child we are ready to free()

	printf("p5\n");
	return;
}

void free_child(child_data_chld *child) {
	int shm_id = child->shm_id;

	close(child->client_sock_fd[0]);
	close(child->client_sock_fd[1]);
	close(child->pipe_fd[0]);
	close(child->pipe_fd[1]);

	// Detach from shared memory
	if ( shmdt(child) == -1 )
		perror("Parent shmdt");

	// Destroy shared memory
	if ( shmctl(shm_id, IPC_RMID, NULL) == -1 )
		perror("shmctl");
}

// get_in_addr
void *get_in_addr(struct sockaddr *sa) {
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}
