#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <dirent.h>
#include <errno.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "prompt.h"
#include "core.h"

char cwd[BUF_LEN];

// 삭제 파일 예약 리스트
struct deletion_node *deletionList;

// 
struct info_node *infoList;

pthread_mutex_t deletionThreadMutex = PTHREAD_MUTEX_INITIALIZER;

extern int requestInput;
extern char inputBuffer[BUF_LEN];
extern pthread_mutex_t inputMutex;
extern pthread_cond_t inputCond;

struct passwd *pw;
char homedir[BUF_LEN];

/**
  core.c 코드 사용 전 초기화 함수
  */
int init() {
	char *p;
	pthread_t deletingThread;
	int threadId;

	// 작업 디렉토리 얻어옴
	getcwd(cwd, sizeof(cwd));

	// 삭제 리스트 검사하는 스레드 생성
	threadId = pthread_create(&deletingThread, NULL, deleteThread, NULL);

	if (threadId < 0) {
		perror("thread create error : ");
		exit(1);
	}

	pw = getpwuid(getuid());
	strcpy(homedir, pw->pw_dir);
}

/**
  작업 스레드에서 입력을 받기 위해 prompt 사용권을 얻어오는 함수
  삭제 예약 처리 시 -r 옵션으로 삭제 여부를 다시 물을 때 사용함
  */
void getInputStream() {
	pthread_mutex_lock(&inputMutex);
	requestInput = 1;
	pthread_cond_signal(&inputCond);
	pthread_cond_wait(&inputCond, &inputMutex);
	pthread_mutex_unlock(&inputMutex);
}

/**
  작업 스레드에서 입력을 마치고 prompt 사용권을 메인 스레드에게 돌려주는 함수
  */
void releaseInputStream() {
	pthread_mutex_lock(&inputMutex);
	requestInput = 0;
	pthread_cond_signal(&inputCond);
	pthread_mutex_unlock(&inputMutex);
}

/**
  삭제 예약 파일들을 삭제 시간이 되었을 때 삭제하는 스레드
  */
void* deleteThread() {
	struct deletion_node *node, *tmp;
	struct tm *tm;
	time_t t;
	char curTime[50];

	while (1) {
		// 작업스레드의 파일 삭제 작업과 메인 스레드의 파일 삭제 작업이 동시에 이루어지면
		// 꼬일 수 있으므로 이를 방지하기 위함
		pthread_mutex_lock(&deletionThreadMutex);
		node = deletionList;

		// 현재 시간 세팅
		t = time(NULL);
		tm = localtime(&t);
		strftime(curTime, sizeof(curTime), TIME_FORMAT, tm);

		while (node != NULL) {
			// 삭제해야하는 파일 발견 시
			if (strcmp(node->endTime, curTime) < 0) {
				tmp = node;

				// 이미 삭제된 경우
				if (access(node->filepath, F_OK) != 0) {
					printf("file %s is already deleted\n", node->filepath);
					node = node->next;
					removeDeletionNode(tmp);
					continue;
				}

				// r 옵션 켜져있었으면 물어보고 삭제
				if (node->rOption) {
					int isDelete = 0;

					// y, n 둘 중 하나를 제대로 입력할 때까지 반복
					while (1) {
						printf("\nDelete %s [y/n]? ", node->filepath + strlen(cwd) + strlen(DIRECTORY) + 2);
						fflush(stdout);
						getInputStream();

						// 삭제 함
						if (!strcmp(inputBuffer, "y") || !strcmp(inputBuffer, "Y")) {
							isDelete = 1;
							break;
						}

						// 삭제 안함
						if (!strcmp(inputBuffer, "n") || !strcmp(inputBuffer, "N")) {
							printf("%s is not deleted\n", node->filepath);
							isDelete = 0;
							break;
						}
					}
					releaseInputStream();

					// 삭제하지 않는 경우 다음 노드로 이동
					if (!isDelete) {
						node = node->next;
						removeDeletionNode(tmp);
						continue;
					}
				}

				// 파일 삭제 후 삭제 리스트에서 제거
				if (node->iOption) {
					// 바로 삭제
					if (remove(node->filepath) < 0) {
						printf("[DeletionThread] Cannot delete %s\n", node->filepath);
					}	
				} else {
					// 휴지통으로 보내기
					if (sendToTrash(node->filepath) < 0) {
						fprintf(stderr, "[DeletionThread] sendToTrash error\n");
						exit(1);
					}
				}
				node = node->next;
				removeDeletionNode(tmp);
			}
			
			// 정렬된 상태라 아직 삭제시간이 안된 노드를 발견하면 바로 반복문 중단
			else {
				break;
			}
		}
		pthread_mutex_unlock(&deletionThreadMutex);
		sleep(DELETE_INTERVAL);
	}
}

/**
  휴지통 info노드를 파일명으로 삭제하는 함수
  @param name 삭제하고싶은 노드들의 파일명
  path가 아니라 name 인것에 주의
  */
void removeInfoNodeByName(const char* name) {
	struct info_node *node, *tmp;
	char *filename;

	node = infoList;
	while (node != NULL) {
		// 파일 이름 추출
		filename = strrchr(node->filepath, '/');
		if (filename == NULL)
			filename = node->filepath;
		else
			filename++;

		// 파일 이름이 지우고자 하는 name 과 같다면 삭제
		if (!strcmp(filename, name)) {
			tmp = node;
			node = node->next;
			removeInfoNode(tmp);
		} else {
			node = node->next;
		}
	}
}

/**
  특정 info 노드를 infoList로부터 삭제하는 함수
  @param node 삭제할 info 노드
  */
void removeInfoNode(struct info_node *node) {
	// 루트
	if (node->prev == NULL) {
		infoList = node->next;
		if (infoList != NULL)
			infoList->prev = NULL;
		free(node);
		return;
	}

	node->prev->next = node->next;

	if (node->next != NULL) {
		node->next->prev = node->prev;
	}
	free(node);
}

/**
  infoList 를 초기화시키는 함수
  */
void clearInfoList() {
	struct info_node *next;
	while (infoList != NULL) {
		next = infoList->next;
		free(infoList);
		infoList = next;
	}
}

/**
  infoList에 노드 하나를 추가하는 함수
  @param node 추가할 노드
  */
void insertInfoNode(struct info_node *node) {
	// 루트
	if (infoList == NULL) {
		node->prev = NULL;
		node->next = NULL;
		infoList = node;
		return;
	}

	struct info_node *tmp = infoList;

	// deletionTime (삭제시간)을 기준으로 오름차순 삽입정렬
	if (strcmp(tmp->deletionTime, node->deletionTime) > 0) {
		node->next = tmp;
		node->prev = NULL;
		tmp->prev = node;
		infoList = node;
		return;
	}

	// deletionTime (삭제시간)을 기준으로 오름차순 삽입정렬
	while (tmp->next != NULL) {
		if (strcmp(tmp->deletionTime, node->deletionTime) > 0)
			break;
		tmp = tmp->next;
	}

	if (strcmp(tmp->deletionTime, node->deletionTime) <= 0) {
		tmp->next = node;
		node->prev = tmp;
	} else {
		tmp->prev->next = node;
		node->prev = tmp->prev;
		node->next = tmp;
		tmp->prev = node;
	}
}

/**
  deletionList 에서 노드를 삭제하는 함수
  @param node 삭제할 노드
  */
void removeDeletionNode(struct deletion_node *node) {
	// 루트
	if (node->prev == NULL) {
		deletionList = node->next;
		if (deletionList != NULL)
			deletionList->prev = NULL;

		free(node);
		return;
	}

	node->prev->next = node->next;

	if (node->next != NULL) {
		node->next->prev = node->prev;
	}
	free(node);
}

/**
  deletionList 에 노드를 추가하는 함수
  @param nodw 추가할 노드
  */
void insertDeletionNode(struct deletion_node *node) {
	struct deletion_node *tmp, *prev;

	// 루트
	if (deletionList == NULL) {
		node->prev = NULL;
		node->next = NULL;
		deletionList = node;
		return;
	}

	tmp = deletionList;
	prev = NULL;

	// endTime(삭제 예정 시간)을 기준으로 오름차순 삽입 정렬
	while (strcmp(tmp->endTime, node->endTime) < 0) {
		prev = tmp;
		tmp = tmp->next;

		if (tmp == NULL)
			break;
	}

	// 맨 앞에 추가되어야 하는 경우
	if (prev == NULL) {
		node->next = deletionList;
		deletionList->prev = node;
		deletionList = node;
		return;
	}

	node->next = prev->next;
	prev->next = node;
	node->prev = prev;

	if (node->next != NULL)
		node->next->prev = node;
}

/**
  디렉토리만 scan 하는 필터
  @param info scandir 함수의 세 번째 인자에서 넘겨주는 인자
  @return 디렉토리라면 1, 아니면 0
  */
int filterOnlyDirectory(const struct dirent *info) {
	struct stat statbuf;

	if (!strcmp(info->d_name, ".") || !strcmp(info->d_name, ".."))
		return 0;

	if (stat(info->d_name, &statbuf) < 0) {
		fprintf(stderr, "%s stat error\n", info->d_name);
		return 0;
	}

	return S_ISDIR(statbuf.st_mode);
}

/**
  파일 삭제하는 실질적인 함수
  @param filepath 삭제 할 파일 경로
  @param endDate 삭제 예정 일자, NULL 인 경우 즉시 삭제
  @param endTime 삭제 예정 시간, NULL 인 경우 즉시 삭제
  @param iOption -i 옵션 on/off 여부, 삭제 시 휴지통으로 보내지 않고 바로 지우는 옵션
  @param rOption -r 옵션 on/off 여부, 삭제 시 다시 한 번 확인하는 옵션
  @return 삭제 성공 시 0, 에러 시 -1, 없는 파일을 삭제하려고 한 경우 1
  */
static int __deleteFile(const char *filepath, const char *endDate, const char *endTime, int iOption, int rOption) {
	char buffer[BUF_LEN];
	struct deletion_node *node;
	struct tm tm;

	// 파일이 해당 서브디렉토리에 없으면 건너뜀
	if (access(filepath, F_OK) != 0) {
		return 1;
	}

	// 즉시 삭제
	if (endDate == NULL || endTime == NULL || strlen(endDate) == 0 || strlen(endTime) == 0) {
		// 다시 묻기
		if (rOption) {
			printf("Delete [y/n]? ");
			char c = getchar();
			getchar();

			if (c == 'n')
				return 0;
		}

		// 즉시 삭제
		if (iOption) {
			if (remove(filepath) < 0) {
				printf("Cannot delete %s\n", filepath);
				return 0;
			}
		}
		
		// 휴지통으로 보내기
		else {
			if (sendToTrash(filepath) < 0) {
				fprintf(stderr, "%s send to trash error\n", filepath);
				return -1;
			}
		}
		return 0;
	}

	// 지정된 시간에 삭제
	node = calloc(1, sizeof(struct deletion_node));
	node->iOption = iOption;
	node->rOption = rOption;
	sprintf(node->endTime, "%s %s", endDate, endTime);
	strcat(node->endTime, ":00");
	strcpy(node->filepath, realpath(filepath, buffer));

	if (strptime(node->endTime, TIME_FORMAT, &tm) == NULL) {
		free(node);
		printf("Invalid timeformat\n");
		return 0;
	}

	// 리스트 추가
	insertDeletionNode(node);
	return 0;
}

/**
  파일 삭제하는 함수
  @param filepath 삭제 할 파일 경로
  @param endDate 삭제 예정 일자, NULL 인 경우 즉시 삭제
  @param endTime 삭제 예정 시간, NULL 인 경우 즉시 삭제
  @param iOption -i 옵션 on/off 여부, 삭제 시 휴지통으로 보내지 않고 바로 지우는 옵션
  @param rOption -r 옵션 on/off 여부, 삭제 시 다시 한 번 확인하는 옵션
  @return 삭제 성공 시 0, 에러 시 -1, 없는 파일을 삭제하려고 한 경우 1
  */
int deleteFile(const char *filepath, const char *endDate, const char *endTime, int iOption, int rOption) {
	char absoluteFilepath[BUF_LEN];
	int status;
	char buf[BUF_LEN];

	pthread_mutex_lock(&deletionThreadMutex);
	if (filepath == NULL || strlen(filepath) == 0) {
		fprintf(stderr, "<filename> is empty\n");
		pthread_mutex_unlock(&deletionThreadMutex);
		return -1;
	}

	if (!iOption) {
		// trash 파일이 있는지 검사하여 없으면 생성
		if (access(TRASH, F_OK) != 0) {
			mkdir(TRASH, 0777);
			mkdir(TRASH_INFO, 0777);
			mkdir(TRASH_FILES, 0777);
		}
	}

	chdir(DIRECTORY);

	// 절대경로
	if (*filepath == '/' || !strncmp(filepath, "~/", 2)) {
		if (!strncmp(filepath, "~/", 2)) {
			sprintf(buf, "%s/%s", homedir, filepath + 2);
		} else {
			strcpy(buf, filepath);
		}

		// 지정 디렉토리 이외의 디렉토리에서 삭제를 요구할 경우 에러
		sprintf(absoluteFilepath, "%s/%s", cwd, DIRECTORY);
		if (strstr(buf, absoluteFilepath) == NULL) {
			printf("Only file which be in the <%s>.\n", absoluteFilepath);
			chdir("../");
			pthread_mutex_unlock(&deletionThreadMutex);
			return 2;
		}
		strcpy(absoluteFilepath, buf);
	}
	// 상대경로
	else {
		realpath(filepath, absoluteFilepath);
	}

	// 삭제
	status = __deleteFile(absoluteFilepath, endDate, endTime, iOption, rOption);
	if (status < 0) {
		fprintf(stderr, "%s delete file error\n", filepath);
	} else if (status == 1) {
		printf("file %s does not exist\n", filepath);
	}

	chdir("../");
	pthread_mutex_unlock(&deletionThreadMutex);
	return status;
}

/**
  오래된 trash info 파일을 삭제해주는 함수
  @param curSize 현재 trash/info 디렉토리 용량
  @param maxSize 허용된 최대 trash/info 디렉토리 용량 (2KB)
  @return 성공 시 0, 에러 시 -1
  */
int deleteOldTrashFile(int curSize, int maxSize) {
	char buf[BUF_LEN];
	struct dirent **fileList;
	int count;
	FILE *fp;
	struct info_node *infoNode;
	ssize_t size;
	struct stat statbuf;
	char *filename;

	sprintf(buf, "%s/%s", cwd, TRASH_INFO);

	// .  ..  디렉토리를 제외한 모든 info 파일을 스캔
	if ((count = scandir(buf, &fileList, ignoreParentAndSelfDirFilter, NULL)) < 0) {
		fprintf(stderr, "deleteOldTrashFile error\n");
		return -1;
	}

	for (int i = 0; i < count; i++) {
		sprintf(buf, "%s/%s/%s", cwd, TRASH_INFO, fileList[i]->d_name);
		if ((fp = fopen(buf, "r")) < 0) {
			fprintf(stderr, "[deleteOldTrashFile] %s fopen error\n", buf);

			for (int j = 0; j < count; j++)
				free(fileList[i]);
			free(fileList);
			clearInfoList();
			return -1;
		}

		fseek(fp, 0, SEEK_END);
		size = ftell(fp);

		fseek(fp, 0, SEEK_SET);
		fgets(buf, sizeof(buf), fp);

		// 모든 info 파일을 읽으면서 infoList 를 구축
		// insert 시 deletionTime(삭제시간)을 기준으로 오름차순 정렬되는 삽입 정렬 이용
		while (ftell(fp) < size) {
			infoNode = calloc(1, sizeof(struct info_node));
			readTrashInfo(fp, infoNode);
			insertInfoNode(infoNode);
		}

		fclose(fp);
	}

	while (curSize > maxSize) {
		printf("Current trash info size : %d\n", curSize);

		// 오래 삭제된 순으로 insert 하게 되어있어서
		// head 인 infoList가 항상 가장 오래전에 삭제된 파일임
		filename = strrchr(infoList->filepath, '/') + 1;

		// 파일명이 같은 파일들을 다 삭제
		for (int i = 1;;i++) {
			sprintf(buf, "%s/%s/%s_%d", cwd, TRASH_FILES, filename, i);

			if (access(buf, F_OK) != 0)
				break;

			// 디렉토리여도 삭제
			if (removeDir(buf) < 0) {
				for (int j = 0; j < count; j++)
					free(fileList[j]);
				free(fileList);
				clearInfoList();
				return -1;
			}
		}

		sprintf(buf, "%s/%s/%s", cwd, TRASH_INFO, filename);
		stat(buf, &statbuf);

		// 삭제된 info 파일만큼의 용량이 확보
		curSize -= statbuf.st_size;

		// 리스트에서 제거
		removeInfoNodeByName(filename);

		// info 파일도 삭제
		remove(buf);
		printf("Delete %s\n", filename);
	}
	printf("Current trash info size : %d\n", curSize);

	for (int i = 0; i < count; i++)
		free(fileList[i]);
	free(fileList);
	clearInfoList();
	return 0;
}

/**
  디렉토리도 삭제할 수 있는 함수
  @param filepath 삭제하고자 하는 파일의 경로
  @return 성공 시 0, 에러 시 -1
  파일이 일반 파일이면 바로 삭제하고, 디렉토리면 재귀적으로 하위 파일들을 모두 삭제한 뒤 삭제
  */
int removeDir(const char *filepath) {
	struct stat statbuf;
	DIR *dir;
	struct dirent *dirent;
	char buf[BUF_LEN];

	if (stat(filepath, &statbuf) < 0) {
		fprintf(stderr, "[removeDir] %s stat error\n", filepath);
		return -1;
	}

	// 디렉토리인 경우 하위 파일들을 먼저 삭제
	if (S_ISDIR(statbuf.st_mode)) {
		dir = opendir(filepath);
		while ((dirent = readdir(dir)) != NULL) {
			if (!strcmp(dirent->d_name, ".") || !strcmp(dirent->d_name, ".."))
				continue;

			sprintf(buf, "%s/%s", filepath, dirent->d_name);
			removeDir(buf);
		}
	}

	// 삭제
	remove(filepath);
	return 0;
}

/**
  파일을 휴지통에 보내는 함수
  @param filepath 휴지통에 보낼 파일 경로
  @return 성공 시 0, 에러 시 -1
  */
int sendToTrash(const char *filepath) {
	char *p;
	const char *filename;
	char buffer[BUF_LEN];
	char buffer2[BUF_LEN];
	FILE *fp;
	struct stat statbuf;
	time_t t;
	struct tm *timeinfo;
	ssize_t size;

	// 파일명
	filename = strrchr(filepath, '/');
	if (filename == NULL)
		filename = filepath;
	else
		filename++;

	// 해당 파일명을 가진 info 파일을 만듦
	sprintf(buffer, "%s/%s/%s", cwd, TRASH_INFO, filename);
	if ((fp = fopen(buffer, "a")) == NULL) {
		fprintf(stderr, "%s open error", buffer);
		return -1;
	}

	if (stat(buffer, &statbuf) < 0) {
		fprintf(stderr, "%s stat error", buffer);
		fclose(fp);
		return -1;
	}

	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	if (size == 0) {
		fprintf(fp, "[Trash info]\n");
	}

	// 파일 path 기록
	fprintf(fp, "%s\n", realpath(filepath, buffer));

	t = time(NULL);
	timeinfo = localtime(&t);
	strftime(buffer2, sizeof(buffer2), TIME_FORMAT, timeinfo);

	// 삭제 시간 기록
	fprintf(fp, "D : %s\n", buffer2);

	// trash/files 에 같은 이름의 파일이 있을 수 있으니 겹치지 않게 넘버링
	for (int i = 1;;i++) {
		sprintf(buffer, "%s/%s/%s_%d", cwd, TRASH_FILES, filename, i);
		if (access(buffer, F_OK) != 0)
			break;
	}

	timeinfo = localtime(&statbuf.st_mtime);
	strftime(buffer2, sizeof(buffer2), TIME_FORMAT, timeinfo);

	// 수정 시간 기록
	fprintf(fp, "M : %s\n", buffer2);

	// 파일을 휴지통으로 이동
	rename(filepath, buffer);

	fflush(fp);
	fclose(fp);

	sprintf(buffer, "%s/%s", cwd, TRASH_INFO);
	size = getSize(buffer);

	// 2KB 초과 시
	if (size > MAX_TRASH_INFO_SIZE) {
		deleteOldTrashFile(size, MAX_TRASH_INFO_SIZE);
	}
	return 0;
}

/**
  디렉토리의 크기를 계산해주는 함수
  @param dirpath 디렉토리의 경로
  @return 성공 시 파일 및 디렉토리의 크기, 에러 시 -1
  */
int getSize(const char *dirpath) {
	char buf[BUF_LEN];
	struct dirent **dirList = NULL;
	struct stat statbuf;
	int size = 0;
	int count;
	int tmp;

	if (stat(dirpath, &statbuf) < 0) {
		fprintf(stderr, "%s stat error\n", dirpath);
		return -1;
	}

	// 파일인 경우
	if (!S_ISDIR(statbuf.st_mode)) {
		return statbuf.st_size;
	}

	if ((count = scandir(dirpath, &dirList, ignoreParentAndSelfDirFilter, NULL)) < 0) {
		fprintf(stderr, "%s scandir error\n", dirpath);
		return -1;
	}

	for (int i = 0; i < count; i++) {
		sprintf(buf, "%s/%s", dirpath, dirList[i]->d_name);
		stat(buf, &statbuf);

		// 디렉토리라면 재귀적으로 그 크기를 구하여 size 에 더함
		if (S_ISDIR(statbuf.st_mode)) {
			if ((tmp = getSize(buf)) < 0) {
				fprintf(stderr, "%s getSize error\n", buf);
				return -1;
			}
			size += tmp;
		}
		
		// 파일이라면 바로 그 크기를 size 에 더함
		else {
			size += statbuf.st_size;
		}
	}

	for (int i = 0; i < count; i++)
		free(dirList[i]);

	if (dirList != NULL)
		free(dirList);

	return size;
}

/**
  size 명령어를 실질적으로 처리하는 함수
  @param filepath 용량을 출력할 파일의 경로
  @param curDepth 현재 파일의 깊이
  @param depth 최대 깊이
  @return 성공 시 파일 및 디렉토리의 용량, 에러 시 -1
  */
static int __printSize(const char *filepath, int curDepth, int depth) {
	char buf[BUF_LEN];
	struct dirent **dirList;
	struct stat statbuf;
	int count;
	long size = 0, tmp;

	if (curDepth == depth)
		return 0;

	// .  .. 디렉토리 빼고 모든 파일 스캔 (파일 이름 정렬)
	if ((count = scandir(filepath, &dirList, ignoreParentAndSelfDirFilter, alphasort)) < 0) {
		fprintf(stderr, "%s scandir error\n", filepath);
		return -1;
	}

	// 빈 디렉토리인 경우
	if (count == 0) {
		printf("0\t%s\n", filepath);
		return 0;
	}

	for (int i = 0; i < count; i++) {
		if (dirList[i]->d_name[0] == '.')
			continue;

		sprintf(buf, "%s/%s", filepath, dirList[i]->d_name);
		if (stat(buf, &statbuf) < 0) {
			fprintf(stderr, "%s stat error\n", buf);
			for (int j = 0; j < count; j++)
				free(dirList[j]);
			free(dirList);
			return -1;
		}

		// 디렉토리인 경우
		if (S_ISDIR(statbuf.st_mode)) {
			// depth 여유가 있는 경우 하위 파일들까지 출력
			if (curDepth + 1 < depth) {
				if ((tmp = __printSize(buf, curDepth + 1, depth)) < 0) {
					return -1;
				}
				size += tmp;
			}
			
			// 최대 depth 에 다다라서 더 이상 출력할 수 없는 경우
			else {
				// 그 크기만을 구해서 출력해줌
				if ((tmp = getSize(buf)) < 0) {
					fprintf(stderr, "%s getSize error\n", buf);
					for (int j = 0; j < count; j++)
						free(dirList[j]);
					free(dirList);
					return -1;
				}

				size += tmp;
				printf("%ld\t%s\n", tmp, buf);
			}
		}

		// 파일인 경우 바로 출력
		else {
			printf("%ld\t%s\n", statbuf.st_size, buf);
			size += statbuf.st_size;
		}
	}

	for (int i = 0; i < count; i++)
		free(dirList[i]);
	free(dirList);
	return size;
}

/**
  size 명령어를 처리하는 함수
  @param filepath 용량을 출력할 파일의 경로
  @param dOption -d 옵션에 명시된 depth 값
  @return 성공 시 0, 에러 시 -1
  */
int printSize(const char *filepath, int dOption) {
	struct stat statbuf;
	char buf[BUF_LEN];

	if (stat(filepath, &statbuf) < 0) {
		printf("File %s does not exist\n", filepath);
		return 0;
	}

	// 상대경로로 만들어줌
	if (filepath[0] == '.') {
		strcpy(buf, filepath);
	}
	else {
		sprintf(buf, "./%s", filepath);
	}

	long size = getSize(buf);
	if (size < 0) {
		fprintf(stderr, "%s getSize error\n", buf);
		return -1;
	}
	printf("%ld\t%s\n", size, buf);
	
	if (dOption > 1 && S_ISDIR(statbuf.st_mode)) {
		__printSize(buf, 0, dOption - 1);
	}
	return 0;
}

/**
  휴지통 파일에서 파일명 끝에 붙는 인덱스를 알려주는 함수
  @param filepath trash/files 파일명
  @return 성공 시 인덱스, 에러 시 -1
  */
int getIndex(const char *filepath) {
	char *p;
	p = strrchr(filepath, '_');
	if (p == NULL)
		return -1;

	p++;
	return atoi(p);
}

/**
  scandir 에서 trash/files 의 파일들의 인덱스를 기준으로 오름차순 정렬해주는 함수
  */
int endIndexSort(const struct dirent **left, const struct dirent **right) {
	int leftIndex, rightIndex;
	leftIndex = getIndex((*left)->d_name);
	rightIndex = getIndex((*right)->d_name);
	return leftIndex > rightIndex;
}

/**
  trash/info 파일에서 레코드 단위로 읽어내는 함수
  @param fp trash/info 파일
  @param infoNode 읽은 정보를 저장할 노드
  @return 성공 시 0. 에러 시 -1
  */
int readTrashInfo(FILE *fp, struct info_node *infoNode) {
	char buf[BUF_LEN];

	// 파일명
	if (fgets(buf, sizeof(buf), fp) < 0)
		return -1;

	buf[strlen(buf) - 1] = '\0';
	strcpy(infoNode->filepath, buf);
		
	// 삭제 시간
	if (fgets(buf, sizeof(buf), fp) < 0)
		return -1;

	buf[strlen(buf) - 1] = '\0';
	strcpy(infoNode->deletionTime, buf + 4);

	// 수정 시간
	if (fgets(buf, sizeof(buf), fp) < 0)
		return -1;

	buf[strlen(buf) - 1] = '\0';
	strcpy(infoNode->modificationTime, buf + 4);
	return 0;
}

/**
  파일을 복구하는 함수
  @param filepath 복구할 파일 경로
  @param lOption -l 옵션 여부
  @return 성공 시 0, 에러 시 -1
  */
int recoverFile(const char *filepath, int lOption) {
	const char *filename;
	char buf[BUF_LEN];
	char trashFilepath[BUF_LEN];
	FILE *fp;
	int count, index, num = 1;
	struct info_node *infoNode;
	size_t fileSize;
	struct dirent **fileList;

	// 파일 이름 추출
	filename = strchr(filepath, '/');
	if (filename == NULL)
		filename = filepath;
	else
		filename++;

	// l 옵션 - 휴지통 파일 삭제 시간이 오래된 순으로 출력 후 명령어 실행
	if (lOption) {
		sprintf(buf, "%s/%s", cwd, TRASH_INFO);

		if ((count = scandir(buf, &fileList, ignoreParentAndSelfDirFilter, endIndexSort)) < 0) {
			fprintf(stderr, "scandir error\n");
			return -1;
		}

		FILE *fp;
		for (int i = 0; i < count; i++) {
			sprintf(trashFilepath, "%s/%s", buf, fileList[i]->d_name);
			if ((fp = fopen(trashFilepath, "r")) < 0) {
				fprintf(stderr, "%s fopen error\n", trashFilepath);
				clearInfoList();
				fclose(fp);
				return -1;
			}

			// 파일 크기
			fseek(fp, 0, SEEK_END);
			fileSize = ftell(fp);
			fseek(fp, 0, SEEK_SET);

			// [Trash info] 읽어냄
			char buf[BUF_LEN];
			fgets(buf, sizeof(buf), fp);

			// 파일 읽으면서 오래된 순으로 infoList 구축
			while (ftell(fp) < fileSize) {
				infoNode = calloc(1, sizeof(struct info_node));
				readTrashInfo(fp, infoNode);
				insertInfoNode(infoNode);
			}
			fclose(fp);
		}

		infoNode = infoList;
		int i = 0;

		// 오래 전에 삭제한 파일부터 출력
		while (infoNode != NULL) {
			printf("%d. %s\t\t%s\n", i + 1, strrchr(infoNode->filepath, '/') + 1, infoNode->deletionTime);
			infoNode = infoNode->next;
			i++;
		}
		printf("\n\n");
		clearInfoList();

		for (i = 0; i < count; i++) {
			free(fileList[i]);
		}
		free(fileList);
	}

	// 휴지통에 해당 파일이 있는지 확인
	// r+ 옵션은 파일이 없는 경우 에러를 발생시킴
	sprintf(buf, "%s/%s/%s", cwd, TRASH_INFO, filename);
	if ((fp = fopen(buf, "r+")) == NULL) {
		printf("There is no '%s' in '%s' directory\n", filepath, TRASH);
		return -1;
	}


	// 파일 크기
	fseek(fp, 0, SEEK_END);
	fileSize = ftell(fp);
	count = 0;

	fseek(fp, 0, SEEK_SET);

	// [Trash info] 읽어냄
	fgets(buf, sizeof(buf), fp);

	while (ftell(fp) < fileSize) {
		infoNode = calloc(1, sizeof(struct info_node));
		readTrashInfo(fp, infoNode);

		// 리스트 구축
		insertInfoNode(infoNode);
		count++;
	}

	// 복구할 수 있는 파일이 여러개인 경우 선택
	if (count > 1) {
		printf("There are multiple <%s> Choose one of them.\n", filename);
		index = 0;
		infoNode = infoList;
		while (infoNode != NULL) {
			index++;
			printf("%d. %s D : %s M : %s\n", index, filename, infoNode->deletionTime, infoNode->modificationTime);
			infoNode = infoNode->next;
		}
		printf("Choose : ");
		scanf("%d", &num);
		getchar();
		if (num > count) {
			fprintf(stderr, "Select num up to %d.\n", count);
			clearInfoList();
			return -1;
		}

		infoNode = infoList;
		for (int i = 1; i < num; i++)
			infoNode = infoNode->next;
	}
	
	// 복구할 수 있는 파일이 하나 뿐인 경우
	else {
		infoNode = infoList;
	}

	// buf = 선택한 파일의 휴지통 내 경로
	sprintf(trashFilepath, "%s/%s/%s_%d", cwd, TRASH_FILES, filename, num);

	// 복구하고자 하는 위치에 같은 이름의 파일이 있는 경우 넘버링
	if (access(infoNode->filepath, F_OK) == 0) {
		index = 1;
		while (1) {
			strcpy(buf, infoNode->filepath);
			sprintf(strrchr(buf, '/'), "/%d_%s", index, filename);
			if (access(buf, F_OK) != 0)
				break;
			index++;
		}
		strcpy(infoNode->filepath, buf);
	}

	// 파일 실제 복구
	if (rename(trashFilepath, infoNode->filepath) < 0) {
		fprintf(stderr, "recover file %s error\n", infoNode->filepath);
		clearInfoList();
		fclose(fp);
		return -1;
	}

	// 넘버링 땡겨오기
	while (1) {
		num++;
		sprintf(buf, "%s/%s/%s_%d", cwd, TRASH_FILES, filename, num);
		if (access(buf, F_OK) != 0)
			break;

		sprintf(trashFilepath, "%s/%s/%s_%d", cwd, TRASH_FILES, filename, num - 1);
		rename(buf, trashFilepath);
	}
	removeInfoNode(infoNode);

	// info 파일 수정
	count--;
	sprintf(buf, "%s/%s/%s", cwd, TRASH_INFO, filename);

	if (count == 0) {
		remove(buf);
	}
	else {
		// info 파일 내용 다 지우고
		freopen(buf, "w", fp);

		fprintf(fp, "[Trash info]\n");

		// 다시 쓰기
		infoNode = infoList;
		while (infoNode != NULL) {
			fprintf(fp, "%s\nD : %s\nM : %s\n", infoNode->filepath, infoNode->deletionTime, infoNode->modificationTime);
			infoNode = infoNode->next;
		}
	}

	fclose(fp);
	clearInfoList();
	return 0;
}

/**
  scandir 에서 .과 .. 파일 빼고 모든 파일을 읽어오는 필터
  */
int ignoreParentAndSelfDirFilter(const struct dirent *dir) {
	if (!strcmp(dir->d_name, ".") || !strcmp(dir->d_name, ".."))
		return 0;
	return 1;
}

/**
  트리를 그려주는 함수
  @param filepath 현재 출력해야하는 파일의 경로
  @param depth 현태 트리의 depth
  @param needIndent 왼쪽 여백을 만들어줘야하는지 여부 (디렉토리의 첫 번째 자식 파일은 indent 가 필요없음)
  @return 성공 시 0, 에러 시 -1
  */
static int __printTree(const char *filepath, int depth, int needIndent) {
	struct dirent **fileList;
	struct stat statbuf;
	int count;
	char buf[BUF_LEN];
	char filename[TAB_SIZE];
	const char *fnamep;
	char nameBlock[10];
	char emptyBlock[10];

	// 이름 블럭
	sprintf(nameBlock, "|%%-%ds", TAB_SIZE - 1);

	// 여백 블럭
	sprintf(emptyBlock, "%%-%ds", TAB_SIZE);

	// 파일 이름
	fnamep = strrchr(filepath, '/') + 1;
	if (fnamep == NULL)
		fnamep = filepath;

	strcpy(filename, fnamep);
	stat(filepath, &statbuf);

	// 파일인 경우
	if (!S_ISDIR(statbuf.st_mode)) {
		// 인덴트가 필요하다면 depth 만큼 빈 블럭 출력
		if (needIndent) {
			for (int i = 0; i < depth; i++)
				printf(emptyBlock, "");
		}
		printf(nameBlock, filename);
		printf("\n");
		return 0;
	}

	// 디렉토리인 경우
	// 디렉토리 내부 파일 구조 탐색
	if ((count = scandir(filepath, &fileList, ignoreParentAndSelfDirFilter, alphasort)) < 0) {
		fprintf(stderr, "%s scandir error\n", filepath);
		return -1;
	}

	// 빈 디렉토리가 아니라면
	// 하위 파일을 이어서 출력해야하기 때문에 {디렉토리명}----{파일명} 과 같은 포맷을 위해 -를 추가
	if (count > 0) {
		for (int i = strlen(filename); i < TAB_SIZE - 1; i++) {
			filename[i] = '-';
		}
		filename[TAB_SIZE - 1] = '\0';
	}

	// 디렉토리여도 인덴트가 필요한 경우 인덴트 출력
	if (needIndent) {
		for (int i = 0; i < depth; i++)
			printf(emptyBlock, "");
	}
	printf(nameBlock, filename);

	if (count == 0)
		printf("\n");

	// 재귀적으로 하위 파일 출력
	for (int i = 0; i < count; i++) {
		sprintf(buf, "%s/%s", filepath, fileList[i]->d_name);
		__printTree(buf, depth + 1, i > 0);
	}

	for (int j = 0; j < count; j++)
		free(fileList[j]);
	free(fileList);
	return count;
}

/**
  트리를 출력해주는 함수
  @return 성공 시 0, 에러 시 -1
  */
int printTree() {
	char buf[BUF_LEN];
	sprintf(buf, "%s/%s", cwd, DIRECTORY);
	__printTree(buf, 0, 0);
	printf("\n");
	return 0;
}
