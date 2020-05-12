#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "prompt.h"
#include "core.h"

int main() {
	int isEnd = 0;
	pid_t pid;
	struct timeval start, end;
	gettimeofday(&start, NULL);
	if ((pid = fork()) < 0) {
		fprintf(stderr, "fork error\n");
		exit(1);
	}

	if (pid == 0)
		execl("mdaemon", "mdaemon", NULL);

	init();
	//freopen("input.txt", "r", stdin);

	while (1) {
		isEnd = printPrompt();

		if (isEnd)
			break;
	}

	gettimeofday(&end, NULL);
	end.tv_sec -= start.tv_sec;
	if (end.tv_usec < start.tv_usec) {
		end.tv_sec--;
		end.tv_usec += 1000000;
	}
	end.tv_usec -= start.tv_usec;
	printf("Running time : %ld.%ld sec\n", end.tv_sec, end.tv_usec);
	exit(0);
}
