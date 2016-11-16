#include <sys/types.h>

#ifndef CAPSULE_H
#define CAPSULE_H

#define O_LEN 16
#define M_LEN 1024

typedef enum {PING, CLOSE, SIZE_DESCRIPTOR, MESSAGE} Signal;

// Main capsule
typedef struct M_Capsule {
  char origin[O_LEN];
  Signal signal;
  char content[M_LEN];
} m_capsule;

// Size capsule
typedef struct S_Capsule {
  char origin[O_LEN];
  Signal signal;
  ssize_t siz;
} s_capsule;

// Ping capsule
typedef struct P_Capsule {
  char origin[O_LEN];
  Signal signal;
  int num;
} p_capsule;


// Use for calculation of the size of 'm_capsule' and 's_capsule'
typedef struct REF_Capsule {
  char origin[O_LEN];
  Signal signal;
  //char content[1];
} ref_capsule;



#endif

