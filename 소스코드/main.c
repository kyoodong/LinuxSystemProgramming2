#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "prompt.h"
#include "core.h"

int main() {
	int isEnd = 0;
	pid_t pid;

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

	// 시간 출력 해야하나
	exit(0);
}
