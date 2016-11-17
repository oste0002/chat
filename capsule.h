#ifndef CAPSULE_H
#define CAPSULE_H

#define O_LEN 16
#define M_LEN 1024

#define M_CAP_SIZ 18	// 2 + O_LEN

#define PING		0		/* 00 */
#define CLOS		1		/* 01 */
#define SIZE		2		/* 10 */
#define MESS		3		/* 11 */



typedef struct M_Capsule {
	unsigned int signal : 2;
	unsigned int				: 6;
	char origin[O_LEN];
	char content[M_LEN];
} __attribute__((aligned(8))) m_capsule;

typedef struct S_Capsule {
	unsigned int signal : 2;
	unsigned int				: 6;
	char origin[O_LEN];
	int32_t siz;
} __attribute__((aligned(8))) s_capsule;

typedef struct P_Capsule {
	unsigned int signal : 2;
	unsigned int				: 6;
	char origin[O_LEN];
	uint16_t num;
} __attribute__((aligned(8))) p_capsule;


#endif

