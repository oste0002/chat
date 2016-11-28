#ifndef CAPSULE_H
#define CAPSULE_H

#define NICK_LEN 15
#define M_LEN 1024

#define M_CAP_SIZ 16 // 1 + NICK_LEN (must be power of 2)

/* signal */
#define PING		0		/* 00 */
#define CLOS		1		/* 01 */
#define SIZE		2		/* 10 */
#define MESS		3		/* 11 */


typedef struct M_Capsule {
	unsigned int signal : 2;
	unsigned int				: 6;
	char origin[NICK_LEN];
	char content[M_LEN];
} __attribute__((aligned(2))) m_capsule;

typedef struct S_Capsule {
	unsigned int signal : 2;
	unsigned int				: 6;
	char origin[NICK_LEN];
	int32_t siz;
} __attribute__((aligned(2))) s_capsule;

typedef struct P_Capsule {
	unsigned int signal : 2;
	unsigned int				: 6;
	char origin[NICK_LEN];
	uint16_t num;
} __attribute__((aligned(2))) p_capsule;


#endif

