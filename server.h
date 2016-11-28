#ifndef SERVER_H
#define SERVER_H

#include <stdbool.h>

#include "capsule.h"
#include "dlist.h"
#include "hashtable.h"


#define PORT1 15001			// Port1
#define PORT2 15002			// Port2
#define BACKLOG 10	    // Max number of pending connections
#define MAXCON 100			// Max number of allowed connections

char self_path[14] = "/proc/self/exe";		// Linux specific

typedef struct {
	int id;
	int shm_id;
	char nick_name[NICK_LEN];
	dlist_position list_pos;
	unsigned int enc_key;
	int	pipe_fd_chld[2];
	int	pipe_fd_prnt[2];
	int client_sock_fd[2];
	bool is_active;
} child_data;


typedef struct {
	const int id;
	const int shm_id;
	const char nick_name[NICK_LEN];
	dlist_position list_pos;
	unsigned int enc_key;
	const int	pad[2];	// Padding
	const int	pipe_fd[2];
	const int client_sock_fd[2];
	volatile const bool is_active;
} child_data_prnt;

typedef struct {
	const int id;
	const int shm_id;
	char nick_name[NICK_LEN];
	volatile const dlist_position list_pos;
	unsigned int enc_key;
	const int	pipe_fd[2];
	const int	pad[2];	// Padding
	const int client_sock_fd[2];
	bool is_active;
} child_data_chld;

typedef struct {
	int listen_sock_fd;
	fd_set set_fds;
	dlist *chld_list;
	hashtable_t *chld_id_hash_tab;
	int nfds;
} serv_data;

int setup_serv_con(int *sock_fd, int *port);

int accept_con(int *listen_sock_fd, fd_set *read_fds,
		dlist *cli_list, hashtable_t *chld_id_hash_tab, int nfds);

void *get_in_addr(struct sockaddr *sa);

void serv_loop(int *listen_sock_fd_ptr);

void child_proc(child_data_chld *child);

/* FREE_CHLD - Frees all data associated to a child.
 *
 * l: (dlist *) - A dlist_ptr containing elements of (int)pipe[2]
 * p: (dlist_position) - Position in l
 * s: (fd_set *) - pipe[0] is removed from this set
 *
 * - Removes an (int *) from a dlist
 * - Removes pipe[0] from fd_set
 * - Closes and frees pipes
 */
void parent_call_free_chld(child_data_prnt *child, dlist *l, hashtable_t *h, fd_set *s);

void free_child(child_data_chld *child);



#endif

