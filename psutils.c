
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>

#include "psutils.h"




ssize_t pgets(char *str, ssize_t siz) {
	char buf_ch;
	int ch_i;

	// Initialize char array
	memset(str, 0, siz);

	// Read string from stdin
	fgets(str, siz, stdin);

	// Dump trailing stdin
	if (strlen(str) == strcspn(str, "\r\n"))
		while ((buf_ch = getchar()) != '\n' && buf_ch != EOF);

	// Remove '\r' and '\n'
	ch_i = strcspn(str, "\r\n");
	str[ch_i] = 0;

	return(ch_i);
}


void pgetc(char *c) {
	char buf_ch;

	// Read char from stdin
	*c = getchar();

	if ( (*c == '\n') || (*c == '\r') || (*c == EOF) )
		return;

	// Dump trailing newline
	while ((buf_ch = getchar()) != '\n' && buf_ch != EOF);

	return;
}


int pgetd(int *d) {
	char buf_ch = {0};
	char str[20] = {0};
	char int_max[20] = {0};
	int int_max_chars = 0;
	int i = 0;
	bool flush = false;
	int neg = 0;

	// Read string from stdin
	fgets(str, 20, stdin);

	// Dump trailing stdin
	if (strlen(str) == strcspn(str, "\r\n"))
		while ((buf_ch = getchar()) != '\n' && buf_ch != EOF);

	// Remove '\r' and '\n'
	str[strcspn(str, "\r\n")] = 0;

	// Determine if input is negative
	if (str[0] == '-')
		neg = 1;

	// Only catch digits
	for (i=neg;i<20;i++) {
		if ( (str[i] < '0') || (str[i] > '9') || (flush == true) ) {
			flush = true;
			str[i] = '\0';
		}
	}

	// return(1) if statement gives an overflow
	sprintf(int_max,"%d",INT_MAX);
	int_max_chars = strcspn(int_max, "\0");
	if ( (int)strcspn(str,"\0")-neg  >= int_max_chars ) {
		for (i=0;i<=int_max_chars;i++) {
			if (str[i+neg] > int_max[i])
				return(1);
			else if (str[i+neg] == int_max[i])
				continue;
			else
				break;
		}
	}

	// return(2) if first input character is invalid
	if (str[0] == '\0')
		return(2);

	// If str[0]=='-' and str[1]=='\n' we explicitly define str[1]==1
	if ( (str[0]=='-') && (str[1]=='\0') )
		str[1]='1';

	sscanf(str, "%d", d);

	return(0);
}


void vaclose(const unsigned int argc, ...) {
	va_list va;
	unsigned int i;

	va_start(va,argc);
	for (i=0;i<argc;i++) {
		close(va_arg(va,int));
	}
	va_end(va);

	return;
}


void vafree(const unsigned int argc, ...) {
	va_list va;
	unsigned int i;

	va_start(va,argc);
	for (i=0;i<argc;i++) {
		free(va_arg(va,void *));
	}
	va_end(va);

	return;
}
