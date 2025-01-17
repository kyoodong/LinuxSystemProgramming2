#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include "prompt.h"
#include "core.h"

char command[10];
char promptBuffer[BUF_LEN];
int requestInput;
char inputBuffer[BUF_LEN];
pthread_mutex_t inputMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t inputCond = PTHREAD_COND_INITIALIZER;

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
  @return 종료 시 1, 종료 아닐 시 0, 에러 시 -1
  */
int printPrompt() {
	char c;
	int index = 0;

	printf("%s> ", STUDENT_ID);

	while ((c = getchar()) != '\n' && c != EOF) {
		if (requestInput) {
			char t = c;

			while (requestInput) {
				pthread_mutex_lock(&inputMutex);
				int tIndex = 0;
				if (t != '\0')
					inputBuffer[tIndex++] = t;

				while ((t = getchar()) != '\n' && t != EOF) {
					inputBuffer[tIndex++] = t;
				}
				inputBuffer[tIndex] = '\0';
				t = '\0';
				pthread_cond_signal(&inputCond);
				pthread_cond_wait(&inputCond, &inputMutex);
				pthread_mutex_unlock(&inputMutex);
			}
			
			printf("%s> ", STUDENT_ID);
			for (int i = 0; i < index; i++)
				putchar(promptBuffer[i]);
			continue;
		}
		promptBuffer[index++] = c;
	}

	// @TODO: 디버깅용
	/*
	if (index == 0) {
		return -1;
	}
	*/
	promptBuffer[index] = '\0';
	// @TODO: 디버깅용
	//printf("%s\n", promptBuffer);

	int status;
	if ((status = processCommand(promptBuffer)) < 0) {
		fprintf(stderr, "processCommand error\n");
		return -1;
	}
	promptBuffer[0] = '\0';

	if (status != 0)
		return status;
	return 0;
}

/**
  명령어를 처리해주는 함수
  @param commandStr 명령어와 함께 입력한 문자열 한 줄
  @return 종료 시 1, 종료 아닐 시 0
  */
int processCommand(char *commandStr) {
	while (*commandStr == ' ')
		commandStr++;

	char operator[20];
	char *cp = commandStr;
	int index = 0;

	memset(operator, 0, sizeof(operator));
	while (*cp != ' ' && *cp != '\n' && *cp != '\0') 
		operator[index++] = *cp++;

	if (index == 0) {
		return 0;
	}

	strcpy(command, operator);

	if (!strcmp(command, DELETE)) {
		processDelete(commandStr);
	}
	else if (!strcmp(command, SIZE)) {
		processSize(commandStr);
	}
	else if (!strcmp(command, RECOVER)) {
		processRecover(commandStr);
	}
	else if (!strcmp(command, TREE)) {
		processTree(commandStr);
	}
	else if (!strcmp(command, EXIT)) {
		printf("Exit program...\n");
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

	if (argc <= 1) {
		printf("[Delete] Usage : delete <filename> <end_time> <options>\n");
		return;
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

	// 입력한 파일을 열 수 있는지 확인
	filename = argv[optind];

	// endDate 가 정상적으로 입력된 경우
	if (argc - optind == 3 && argv[optind + 1][0] != '-' && argv[optind + 2][0] != '-') {
		endDate = argv[optind + 1];
		endTime = argv[optind + 2];
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
		length = strlen(buf);
		argv[count] = calloc(1, length + 1);
		strcpy(argv[count], buf);
		count++;
		paramStr += length;

		while (*paramStr == ' ')
			paramStr++;
	}
	return count;
}

/**
  size 기능 처리 하는 함수
  @param paramStr 파라미터 문자열
  */
void processSize(char *paramStr) {
	char *filepath;
	char *argv[10];
	int argc = getArg(paramStr, argv);
	int option;
	int dOption = 0;

	if (argc == 1) {
		fprintf(stderr, "usage: size <filename> <option>\n");
		return;
	}

	while ((option = getopt(argc, argv, "d:")) != -1) {
		switch (option) {
			case 'd':
				dOption = atoi(optarg);
				break;

			case '?':
				break;
		}
	}

	if (argc <= optind) {
		printf("Please enter <filename>\n");
		for (int i = 0; i < argc; i++)
			free(argv[i]);
		optind = 0;
		return;
	}
	filepath = argv[optind];

	// 사이즈 출력
	printSize(filepath, dOption);

	for (int i = 0; i < argc; i++)
		free(argv[i]);
	optind = 0;
}

/**
  복구 기능 처리 하는 함수
  @param paramStr 파라미터 문자열
  */
void processRecover(char *paramStr) {
	char *filepath;
	char *argv[10];
	int argc = getArg(paramStr, argv);
	int option;
	int lOption = 0;

	if (argc == 1) {
		fprintf(stderr, "usage: recover <filename> <option>\n>");
		return;
	}

	while ((option = getopt(argc, argv, "l")) != -1) {
		switch (option) {
			case 'l':
				lOption = 1;
				break;

			case '?':
				break;
		}
	}

	if (argc <= optind) {
		printf("Please enter <filename>\n");

		for (int i = 0; i < argc; i++)
			free(argv[i]);
		optind = 0;
		return;
	}
	filepath = argv[optind];
	
	// 파일 복구
	recoverFile(filepath, lOption);

	for (int i = 0; i < argc; i++)
		free(argv[i]);
	optind = 0;
}

/**
  트리 기능 처리 하는 함수
  @param paramStr 파라미터 문자열
  */
void processTree(char *paramStr) {
	printTree(".");
}

/**
  도움말 기능 처리 하는 함수
  */
void processHelp() {
	printf("delete <filename> <endtime> <option>\n");
	printf("\t-i : delete file directly without backup to trash directory.\n");
	printf("\t-r : confirm before delete file when reserved endtime.\n");
	printf("size <filename> <option>\n");
	printf("\t-d <n> : print sub directory until <n> levels.\n");
	printf("recover <filename> <option>\n");
	printf("\t-l : print trash directory list before recover.\n");
	printf("tree : print directory tree\n");
	printf("exit : terminate the program\n");
	printf("help : print usage of the program\n");
}
