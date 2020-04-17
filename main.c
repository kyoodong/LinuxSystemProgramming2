#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "prompt.h"


int main() {
	int isEnd = 0;

	while (1) {
		isEnd = printPrompt();

		if (isEnd)
			break;
	}

	// 시간 출력 해야하나
	exit(0);
}
