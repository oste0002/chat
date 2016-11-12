#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <signal.h>

#include "capsule.h"
#include "psutils.h"

#define ADDRESS "127.0.0.1"     // IP-address
#define PORT1 "15001"           // Port
#define PORT2 "15002"           // Port
#define MAXDATASIZE 100         // Max number of bytes to send
#define STDIN 0


int setup_talk_con(char *node, char *port, int *sock_fds);
void cli_loop(const int *sock_fds, char *nick_name);
void *get_in_addr(struct sockaddr *sa);
void client_free(int *sock_fds);

int main(int argc, char *argv[]) {
	int sock_fds[2];
	int num_recv_bytes = 0;
	char nick_name[NAME_LEN] = {0};
	char port[5] = {0};
	s_capsule s_cap;

	// Syntax
	if (argc != 1) {
		fprintf(stderr,"usage: client\n");
		exit(EXIT_FAILURE);
	}

	// Setup recv connection
	if ( (setup_talk_con(ADDRESS, PORT1, &sock_fds[0])) == -1)
		exit(EXIT_FAILURE);

	memset(&s_cap, 0, sizeof s_cap);
	if ((num_recv_bytes = recv(sock_fds[0], &s_cap, sizeof s_cap, 0)) == -1) {
		perror("receive");
		exit(EXIT_FAILURE);
	}

	// Setup send connection
	if (s_cap.signal == PING) {
		if ( (setup_talk_con(ADDRESS, s_cap.content, &sock_fds[1])) == -1) {
			fprintf(stderr,"Could not establish write connection\n");
			exit(EXIT_FAILURE);
		}
	} else {
		fprintf(stderr,"Server didn't answer to connection\n");
		exit(EXIT_FAILURE);
	}



	// Read nick name
	printf("Insert name or 'quit': ");
	pgets(nick_name, sizeof nick_name);
	if (!strcmp(nick_name, "quit"))
		exit(EXIT_SUCCESS);

	cli_loop(sock_fds, nick_name);

	// Free
	client_free(sock_fds);
	return 0;
}

void cli_loop(const int *sock_fds, char *nick_name) {
	char buf_ch, mess[MESS_LEN];
	int num_recv_bytes = 0;
	int num_sent_bytes = 0;
	m_capsule rcap, scap = {0}; // recv-, send-capsule
	fd_set read_fds, set_fds;
	struct timeval read_tv, set_tv = { .tv_sec = 0, .tv_usec = 200000 };
	int mfds = 0;

	strncpy(scap.origin, nick_name, NAME_LEN);

	FD_ZERO(&set_fds);
	FD_SET(STDIN, &set_fds);
	FD_SET(sock_fds[0], &set_fds);


	while(1) {
		// Initialize select() arguments
		read_fds = set_fds;
		read_tv = set_tv;
		mfds = select(sock_fds[0] +1, &read_fds, NULL, NULL, &read_tv);

		// Error check
		if (mfds == -1) {
			if (strcmp(strerror(errno),"EINTR"))
				continue;
			perror("client select");
			return;
		}

		if (mfds == 0)
			continue;

		if (FD_ISSET(STDIN, &read_fds)) {

			buf_ch = getchar();
			if (buf_ch == 27)
				exit(EXIT_SUCCESS);

			printf("%s: ", nick_name);
			pgets(mess, sizeof mess);

			if (!strcmp(mess, "quit")) {
				close(sock_fds[0]);
				close(sock_fds[1]);
				exit(EXIT_SUCCESS);
			}

			strncpy(scap.content, mess, sizeof mess);

			if ((num_sent_bytes = send(sock_fds[1], &scap, sizeof scap, 0)) == -1) {
				perror("send");
				exit(EXIT_FAILURE);
			}

			if (num_sent_bytes < (sizeof scap))
				fprintf(stderr,"Could not send all of the message");

			memset(scap.content, 0, sizeof scap.content);
		}

		if (FD_ISSET(sock_fds[0], &read_fds)) {

			if ((num_recv_bytes = recv(sock_fds[0], &rcap, sizeof rcap, 0)) == -1) {
				perror("receive");
				exit(EXIT_FAILURE);
			}
			if (num_recv_bytes == 0) {
				FD_CLR(sock_fds[0], &set_fds);
				close(sock_fds[0]);
				close(sock_fds[1]);
				exit(EXIT_SUCCESS);
			}

			printf("%s: %s\n", rcap.origin, rcap.content);
		}

	}
}

int setup_talk_con(char *node, char *port, int *sock_fds) {
	struct addrinfo hints, *servinfo, *p;
	int rv;
	char s[INET6_ADDRSTRLEN];

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((rv = getaddrinfo(node, port, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return -1;
	}

	// loop through all the results and connect to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((*sock_fds = socket(p->ai_family, p->ai_socktype,
						p->ai_protocol)) == -1) {
			perror("client: socket");
			continue;
		}
		if (connect(*sock_fds, p->ai_addr, p->ai_addrlen) == -1) {
			perror("client: connect");
			//close(*sock_fds);
			continue;
		}
		break;
	}

	if (p == NULL) {
		fprintf(stderr, "client: failed to connect\n");
		return -1;
	}

	inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
			s, sizeof s);
	printf("client: connecting to %s\n", s);

	freeaddrinfo(servinfo); // all done with this structure

	return 0;
}


void client_free(int *sock_fds) {
	close(sock_fds[0]);
	close(sock_fds[1]);
}

// Get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa) {
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

