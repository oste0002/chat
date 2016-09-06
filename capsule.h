#include <sys/types.h>

#ifndef CAPSULE_H
#define CAPSULE_H

#define NAME_LEN 31
#define MESS_LEN 101
#define PORT_LEN 5

typedef enum {PING, CLOSE, MESSAGE} Signal;

typedef struct M_Capsule {
  char origin[NAME_LEN];
  Signal signal;
  char content[MESS_LEN];
} m_capsule;

typedef struct S_Capsule {
  char origin[NAME_LEN];
  Signal signal;
  char content[PORT_LEN];
} s_capsule;




#endif

