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

#define ADDRESS		"31.209.47.153" // IP-address
#define PORT			15001           // Port
#define STDIN			0


int setup_talk_con(char *node, char *port, int *sock_fds);
void cli_loop(const int *sock_fds, char *nick_name);
void *get_in_addr(struct sockaddr *sa);
void client_free(int *sock_fds);

int main() {
	int sock_fds[2];
	int num_recv_bytes = 0;
	char nick_name[O_LEN] = {0};
	char port[5] = {0};
	p_capsule p_cap;

	// // Syntax
	// if (argc != 1) {
	// 	fprintf(stderr,"usage: client\n");
	// 	exit(EXIT_FAILURE);
	// }

	// Setup recv connection
	sprintf(port, "%d", PORT);
	if ( (setup_talk_con(ADDRESS, port, &sock_fds[0])) == -1)
		exit(EXIT_FAILURE);

	// Receive 'port2' which is used for communication
	if ((num_recv_bytes = recv(sock_fds[0], &p_cap, sizeof(p_cap), 0)) == -1) {
		perror("receive");
		exit(EXIT_FAILURE);
	}

	// Setup send connection
	sprintf(port, "%d", p_cap.num);
	memset(&p_cap, 0, sizeof(p_cap));

	if (p_cap.signal == PING) {
		if ( (setup_talk_con(ADDRESS, port, &sock_fds[1])) == -1) {
			fprintf(stderr,"Could not establish write connection\n");
			exit(EXIT_FAILURE);
		}
	} else {
		fprintf(stderr,"Server didn't answer to connection\n");
		exit(EXIT_FAILURE);
	}


	// Read nick name
	printf("Insert nick: ");
	while (pgets(nick_name, sizeof(nick_name)) < (unsigned int)(3*sizeof(char)))
		printf("Please use a nickname at least 3 letters long.\nInsert nick: ");

	cli_loop(sock_fds, nick_name);

	// Free
	client_free(sock_fds);
	return 0;
}

void cli_loop(const int *sock_fds, char *nick_name) {
	char buf_ch;
	int num_recv_bytes = 0;
	int num_sent_bytes = 0;
	s_capsule s_cap = {0};				// size capsule
	s_capsule	p_cap = {0};				// ping capsule
	m_capsule m_cap = {0};				// main capsule
	fd_set read_fds, set_fds;
	struct timeval read_tv, set_tv = { .tv_sec = 0, .tv_usec = 200000 };
	int mfds = 0;

	strncpy(m_cap.origin, nick_name, O_LEN);

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

		// Send message
		if (FD_ISSET(STDIN, &read_fds)) {

			// Exit on ESC
			pgetc(&buf_ch);
			if (buf_ch == 27) {
				close(sock_fds[0]);
				close(sock_fds[1]);
				exit(EXIT_SUCCESS);
			}

			// Get message
			printf("%s: ", nick_name);
			//	pgets(mess, sizeof(mess));
			//	strncpy(m_cap.content, mess, sizeof(mess));

			// TODO: Try something like
			// s_cap = { .origin = nick_name, .signal = SIZE, .content = 0}

			memset(&s_cap,0,sizeof(s_cap));
			memset(&m_cap,0,sizeof(m_cap));
			strncpy(s_cap.origin, nick_name, O_LEN * sizeof(char));
			strncpy(m_cap.origin, nick_name, O_LEN * sizeof(char));
			strncpy(p_cap.origin, nick_name, O_LEN * sizeof(char));
			s_cap.signal = SIZE;
			m_cap.signal = MESS;
			p_cap.signal = PING;


			// Store the size of the main message to s_cap.siz and
			// store the message to m_cap.siz simultaneously
			s_cap.siz = M_CAP_SIZ +	(int)pgets(m_cap.content, M_LEN*sizeof(char));


			// Send s_cap
			if ((num_sent_bytes = send(sock_fds[1], &s_cap,
							sizeof(s_cap), 0)) == -1)
				perror("send s_cap");
			if (num_sent_bytes != sizeof(s_cap))
				fprintf(stderr,"s_cap: Not all data is sent correctly!\nSent: %d\n"
						"Size: %u\n",num_sent_bytes,(unsigned int)sizeof(s_cap));


			//	// Receive p_cap
			//	if ((num_recv_bytes = recv(sock_fds[0], &p_cap,
			//					sizeof(p_cap), 0)) == -1)
			//		perror("receive p_cap");
			//	if (num_recv_bytes != sizeof(p_cap))
			//		fprintf(stderr,"p_cap: Not all data is received correctly!\nReceived: %d\n"
			//				"Size: %d\n",num_sent_bytes,sizeof(p_cap));


			// Send m_cap
			if ((num_sent_bytes = send(sock_fds[1], &m_cap,
							s_cap.siz, 0)) == -1)
				perror("send m_cap");
			if (num_sent_bytes != s_cap.siz)
				fprintf(stderr,"m_cap: Not all data is sent correctly!\nSent: %d\n"
						"Size: %u\n",num_sent_bytes,(unsigned int)s_cap.siz);


		}

		// Receive message
		if (FD_ISSET(sock_fds[0], &read_fds)) {

			memset(&s_cap,0,sizeof(s_cap));
			memset(&m_cap,0,sizeof(m_cap));

			// Receive s_cap
			if ((num_recv_bytes = recv(sock_fds[0], &s_cap,
							sizeof(s_cap), 0)) == -1)
				perror("receive");
			if (num_recv_bytes == 0) {
				FD_CLR(sock_fds[0], &set_fds);
				close(sock_fds[0]);
				close(sock_fds[1]);
				exit(EXIT_SUCCESS);
			}
			if (num_recv_bytes != sizeof(s_cap))
				fprintf(stderr,"s_cap: Not all data is received correctly!\n"
						"Received: %d\nSize: %u\n",
						num_sent_bytes,(unsigned int)sizeof(p_cap));


			//	// Send p_cap
			//	if ((num_sent_bytes = send(sock_fds[1], &p_cap,
			//					sizeof(p_cap), 0)) == -1)
			//		perror("send p_cap");
			//	if (num_sent_bytes != sizeof(p_cap))
			//		fprintf(stderr,"p_cap: Not all data is sent correctly!\nSent: %d\n"
			//				"Size: %d\n",num_sent_bytes,s_cap.siz);


			// Receive m_cap
			if ((num_recv_bytes = recv(sock_fds[0], &m_cap,
							s_cap.siz, 0)) == -1)
				perror("receive");
			if (num_recv_bytes == 0) {
				FD_CLR(sock_fds[0], &set_fds);
				close(sock_fds[0]);
				close(sock_fds[1]);
				exit(EXIT_SUCCESS);
			}
			if (num_recv_bytes != s_cap.siz)
				fprintf(stderr,"m_cap: Not all data is received correctly!\n"
						"Received: %d\nSize: %u\n"
						,num_sent_bytes,(unsigned int)s_cap.siz);

			// Print received message
			printf("%s: %s\n", m_cap.origin, m_cap.content);
		}

	}
}

int setup_talk_con(char *node, char *port, int *sock_fds) {
	struct addrinfo hints, *servinfo, *p;
	int rv;
	char s[INET6_ADDRSTRLEN];

	memset(&hints, 0, sizeof(hints));
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
			s, sizeof(s));
	//printf("client: connecting to %s\n", s);

	freeaddrinfo(servinfo);

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

