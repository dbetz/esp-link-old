#include <stdio.h>
#include <string.h>

char logbuf[8*1024];

int main(void)
{
    char buf[100];
    int i;

    strcpy(logbuf, "");

    for (;;) {
	printf("What is your name? ");
    	fflush(stdout);
	if (!gets(buf))
	    break;
	printf("Hi, %s!\n", buf);

        for (i = 0; i < strlen(buf); ++i)
            printf(" %02x", buf[i]);
        printf("\n");

	strcat(logbuf, buf);
	strcat(logbuf, "\n");
	printf("log:\n");
	puts(logbuf);
    }
    return 0;
}
