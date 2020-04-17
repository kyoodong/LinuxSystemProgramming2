#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "prompt.h"

char command[10];
char promptBuffer[BUF_LEN];

char* commandList[6] = {
	DELETE,
	SIZE,
	RECOVER,
	TREE,
	EXIT,
	HELP
};

/**
  프롬프트 출력해주는 함수
  @return 종료 시 1, 종료 아닐 시 0
  */
int printPrompt() {
	printf("%s> ", STUDENT_ID);
	fgets(promptBuffer, sizeof(promptBuffer), stdin);
	promptBuffer[strlen(promptBuffer) - 1] = '\0';
	processCommand(promptBuffer);
}

/**
  명령어를 처리해주는 함수
  @param commandStr 명령어와 함께 입력한 문자열 한 줄
  @return 종료 시 1, 종료 아닐 시 0
  */
int processCommand(char* commandStr) {
	char* operator = strtok(commandStr, " ");

	if (operator == NULL) {
		printPrompt();
		return 0;
	}

	strcpy(command, operator);

	if (!strcmp(command, DELETE)) {
		printf("Delete\n");
	}
	else if (!strcmp(command, SIZE)) {
		printf("Size\n");
	}
	else if (!strcmp(command, RECOVER)) {
		printf("Recover\n");
	}
	else if (!strcmp(command, TREE)) {
		printf("Tree\n");
	}
	else if (!strcmp(command, EXIT)) {
		printf("Exit\n");
		return 1;
	}
	else {
		printf("Help\n");
	}

	return 0;
}

