#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "prompt.h"
#include "core.h"

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
	char c;
	int index = 0;

	printf("%s> ", STUDENT_ID);

	while ((c = getchar()) != '\n' && c != EOF) {
		promptBuffer[index++] = c;
	}

	// @TODO: 디버깅용
	if (index == 0) {
		sleep(10);
	}
	promptBuffer[index] = '\0';
	if (processCommand(promptBuffer) < 0) {
		fprintf(stderr, "processCommand error\n");
	}
}

/**
  명령어를 처리해주는 함수
  @param commandStr 명령어와 함께 입력한 문자열 한 줄
  @return 종료 시 1, 종료 아닐 시 0
  */
int processCommand(char *commandStr) {
	char* operator = strtok(commandStr, " ");

	if (operator == NULL) {
		printPrompt();
		return 0;
	}

	strcpy(command, operator);

	if (!strcmp(command, DELETE)) {
		processDelete(commandStr + strlen(command) + 1);
	}
	else if (!strcmp(command, SIZE)) {
		processSize(commandStr + strlen(command) + 1);
	}
	else if (!strcmp(command, RECOVER)) {
		processRecover(commandStr + strlen(command) + 1);
	}
	else if (!strcmp(command, TREE)) {
		processTree(commandStr + strlen(command) + 1);
	}
	else if (!strcmp(command, EXIT)) {
		processExit();
		return 1;
	}
	else {
		processHelp();
	}

	return 0;
}

/**
  삭제 기능 처리 하는 함수
  @param paramStr 파라미터 문자열
  */
void processDelete(char *paramStr) {
	char *filename, *endDate = NULL, *endTime = NULL;
	int iOption = 0, rOption = 0;
	char option;
	char *argv[10];
	int argc = getArg(paramStr, argv);

	if (argc < 1) {
		printf("[Delete] Usage : delete <filename> <end_time> <options>\n");
		return;
	}

	// 입력한 파일을 열 수 있는지 확인
	filename = argv[0];

	// endDate 가 정상적으로 입력된 경우
	if (argc >= 3 && argv[1][0] != '-' && argv[2][0] != '-') {
		endDate = argv[1];
		endTime = argv[2];
	}

	// 옵션 체크
	while ((option = getopt(argc, argv, "ir")) != -1) {
		switch (option) {
			case 'i':
				iOption = 1;
				break;

			case 'r':
				rOption = 1;
				break;

			case '?':
				printf("[Delete]: Unknown option %s\n", argv[optind - 1]);
				break;
		}
	}

	// delete 처리 로직
	if (deleteFile(filename, endDate, endTime, iOption, rOption) < 0) {
		fprintf(stderr, "deleteFile error\n");
	}

	for (int i = 0; i < argc; i++) {
		free(argv[i]);
	}
	optind = 0;
}

/**
  getopt 사용을 위해 아규먼트 갯수를 구하고 argv로 분리 해주는 함수
  @param paramStr 명령어 파라미터 문자열
  @param argv 파라미터를 분리하여 argv에 저장함
  @return 파라미터 갯수
  **/
int getArg(char* paramStr, char *argv[10]) {
	int count = 0;
	size_t length;
	char buf[BUF_LEN];

	while (*paramStr == ' ')
		paramStr++;

	while (*paramStr != '\0' && *paramStr != '\n') {
		sscanf(paramStr, "%s", buf);
		length = strlen(buf) + 1;
		argv[count] = malloc(length);
		strcpy(argv[count], buf);
		count++;
		paramStr += length;

		while (*paramStr == ' ')
			paramStr++;
	}
	return count;
}

void processSize(char *paramStr) {
	char *filepath;
	char *argv[10];
	int argc = getArg(paramStr, argv);
	int option;
	int dOption = 0;

	if (argc == 0) {
		fprintf(stderr, "usage: size <filename> <option>\n>");
		return;
	}

	filepath = argv[0];

	while ((option = getopt(argc, argv, "d:")) != -1) {
		switch (option) {
			case 'd':
				dOption = atoi(optarg);
				break;

			case '?':
				break;
		}
	}

	// 사이즈 출력
	printSize(filepath, dOption);

	for (int i = 0; i < argc; i++) {
		free(argv[i]);
	}
	optind = 0;
}

void processRecover(char *paramStr) {
	char *filepath;
	char *argv[10];
	int argc = getArg(paramStr, argv);
	int option;
	int lOption = 0;

	if (argc == 0) {
		fprintf(stderr, "usage: recover <filename> <option>\n>");
		return;
	}

	filepath = argv[0];

	while ((option = getopt(argc, argv, "l")) != -1) {
		switch (option) {
			case 'l':
				lOption = 1;
				break;

			case '?':
				break;
		}
	}
	
	// 파일 복구
	recoverFile(filepath, lOption);

	for (int i = 0; i < argc; i++) {
		free(argv[i]);
	}
	optind = 0;
}

void processTree(char *paramStr) {
}

void processExit() {
}

void processHelp() {
}
