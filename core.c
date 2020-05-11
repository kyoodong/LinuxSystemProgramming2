#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <dirent.h>
#include <errno.h>
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

int init() {
	char *p;
	pthread_t deletingThread;
	int threadId;

	getcwd(cwd, sizeof(cwd));

	// 삭제 리스트 검사하는 스레드 생성
	threadId = pthread_create(&deletingThread, NULL, deleteThread, NULL);

	if (threadId < 0) {
		perror("thread create error : ");
		exit(1);
	}
}

void getInputStream() {
	pthread_mutex_lock(&inputMutex);
	requestInput = 1;
	pthread_cond_signal(&inputCond);
	pthread_cond_wait(&inputCond, &inputMutex);
	pthread_mutex_unlock(&inputMutex);
}

void releaseInputStream() {
	pthread_mutex_lock(&inputMutex);
	requestInput = 0;
	pthread_cond_signal(&inputCond);
	pthread_mutex_unlock(&inputMutex);
}

void* deleteThread() {
	struct deletion_node *node, *tmp;
	struct tm *tm;
	time_t t;
	char curTime[50];

	while (1) {
		pthread_mutex_lock(&deletionThreadMutex);
		node = deletionList;
		t = time(NULL);
		tm = localtime(&t);
		strftime(curTime, sizeof(curTime), TIME_FORMAT, tm);

		while (node != NULL) {
			// 삭제해야하는 파일 발견 시
			if (strcmp(node->endTime, curTime) < 0) {
				tmp = node;

				printf("[filename]: %s\ncurTime = %s\nendTime = %s\n", node->filepath, curTime, node->endTime);
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
					while (1) {
						printf("Delete [y/n]? ");
						fflush(stdout);
						getInputStream();
						if (!strcmp(inputBuffer, "y") || !strcmp(inputBuffer, "Y")) {
							isDelete = 1;
							break;
						}
						if (!strcmp(inputBuffer, "n") || !strcmp(inputBuffer, "N")) {
							printf("%s is not deleted\n", node->filepath);
							isDelete = 0;
							node = node->next;
							removeDeletionNode(tmp);
							break;
						}
					}
					releaseInputStream();

					if (!isDelete)
						continue;
				}

				// 파일 삭제 후 삭제 리스트에서 제거
				if (node->iOption) {
					if (remove(node->filepath) < 0) {
						printf("Cannot delete %s\n", node->filepath);
					}	
				} else {
					if (sendToTrash(node->filepath) < 0) {
						fprintf(stderr, "sendToTrash error\n");
						exit(1);
					}
				}
				node = node->next;
				removeDeletionNode(tmp);
			} else {
				break;
			}
		}
		pthread_mutex_unlock(&deletionThreadMutex);
		sleep(DELETE_INTERVAL);
	}
}

void removeInfoNodeByName(const char* name) {
	struct info_node *node, *tmp;
	char *filename;

	node = infoList;
	while (node != NULL) {
		filename = strrchr(node->filepath, '/');
		if (filename == NULL)
			filename = node->filepath;
		else
			filename++;

		if (!strcmp(filename, name)) {
			tmp = node;
			node = node->next;
			removeInfoNode(tmp);
		} else {
			node = node->next;
		}
	}
}

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

void clearInfoList() {
	struct info_node *next;
	while (infoList != NULL) {
		next = infoList->next;
		free(infoList);
		infoList = next;
	}
}

void insertInfoNode(struct info_node *node) {
	if (infoList == NULL) {
		node->prev = NULL;
		node->next = NULL;
		infoList = node;
		return;
	}

	struct info_node *tmp = infoList;
	if (strcmp(tmp->filepath, node->filepath) > 0) {
		node->next = tmp;
		node->prev = NULL;
		tmp->prev = node;
		infoList = node;
		return;
	}

	while (tmp->next != NULL) {
		if (strcmp(tmp->filepath, node->filepath) > 0)
			break;
		tmp = tmp->next;
	}

	tmp->next = node;
	node->prev = tmp;
}

void removeDeletionNode(struct deletion_node *node) {
	pthread_mutex_lock(&deletionThreadMutex);
	// 루트
	if (node->prev == NULL) {
		deletionList = node->next;
		if (deletionList != NULL)
			deletionList->prev = NULL;

		free(node);
		pthread_mutex_unlock(&deletionThreadMutex);
		return;
	}

	node->prev->next = node->next;

	if (node->next != NULL) {
		node->next->prev = node->prev;
	}
	free(node);
	pthread_mutex_unlock(&deletionThreadMutex);
}

void insertDeletionNode(struct deletion_node *node) {
	pthread_mutex_lock(&deletionThreadMutex);
	struct deletion_node *tmp, *prev;

	if (deletionList == NULL) {
		node->prev = NULL;
		node->next = NULL;
		deletionList = node;
		pthread_mutex_unlock(&deletionThreadMutex);
		return;
	}

	tmp = deletionList;
	prev = NULL;
	while (tmp->endTime < node->endTime) {
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
		pthread_mutex_unlock(&deletionThreadMutex);
		return;
	}

	node->next = prev->next;
	prev->next = node;
	node->prev = prev;

	if (node->next != NULL)
		node->next->prev = node;
	pthread_mutex_unlock(&deletionThreadMutex);
}

int filterOnlyDirectory(const struct dirent *info) {
	struct stat statbuf;

	if (!strcmp(info->d_name, ".") || !strcmp(info->d_name, "..") || !strcmp(info->d_name, TRASH) || !strcmp(info->d_name, ".git"))
		return 0;

	if (stat(info->d_name, &statbuf) < 0) {
		fprintf(stderr, "%s stat error\n", info->d_name);
		return 0;
	}

	return S_ISDIR(statbuf.st_mode);
}


int __deleteFile(const char *filepath, const char *endDate, const char *endTime, int iOption, int rOption) {
	char buffer[BUF_LEN];
	struct deletion_node *node;
	struct tm tm;

	// 파일이 해당 서브디렉토리에 없으면 건너뜀
	if (access(filepath, F_OK) != 0)
		return 1;

	// 즉시 삭제
	if (endDate == NULL || endTime == NULL || strlen(endDate) == 0 || strlen(endTime) == 0) {
		if (rOption) {
			printf("Delete [y/n]? ");
			char c = getchar();

			if (c == 'n')
				return 0;
		}

		if (iOption) {
			if (remove(filepath) < 0) {
				printf("Cannot delete %s\n", filepath);
				return 0;
			}
		} else {
			if (sendToTrash(filepath) < 0) {
				fprintf(stderr, "%s send to trash error\n", filepath);
				return -1;
			}
		}
		return 0;
	}

	// 지정된 시간에 삭제
	sprintf(buffer, "%s %s", endDate, endTime);
	node = calloc(1, sizeof(struct deletion_node));
	node->iOption = iOption;
	node->rOption = rOption;
	strcpy(node->endTime, buffer);
	strcat(node->endTime, ":00");
	strcpy(node->filepath, realpath(filepath, buffer));
	insertDeletionNode(node);
	return 0;
}

int deleteFile(const char *filepath, const char *endDate, const char *endTime, int iOption, int rOption) {
	char absoluteFilepath[BUF_LEN];
	int status;

	if (filepath == NULL || strlen(filepath) == 0) {
		fprintf(stderr, "<filename> is empty\n");
		return -1;
	}

	// trash 파일이 있는지 검사하여 없으면 생성
	if (!iOption) {
		if (access(TRASH, F_OK) != 0) {
			mkdir(TRASH, 0777);
			mkdir(TRASH_INFO, 0777);
			mkdir(TRASH_FILES, 0777);
		}
	}

	chdir(DIRECTORY);

	// 절대경로
	if (*filepath == '/') {
		// 지정 디렉토리 이외의 디렉토리에서 삭제를 요구할 경우 에러
		sprintf(absoluteFilepath, "%s/%s", cwd, DIRECTORY);
		if (strstr(filepath, absoluteFilepath) == NULL) {
			printf("Only file which be in the <%s>.\n", absoluteFilepath);
			return 2;
		}
		strcpy(absoluteFilepath, filepath);
	}
	// 상대경로
	else {
		realpath(filepath, absoluteFilepath);
	}

	// 절대경로로 입력되어 바로 삭제할 수 있는 경우
	status = __deleteFile(absoluteFilepath, endDate, endTime, iOption, rOption);
	if (status < 0) {
		fprintf(stderr, "%s delete file error\n", filepath);
	} else if (status == 1) {
		printf("file %s does not exist\n", filepath);
	}

	chdir("../");
	return status;
}

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

		for (int i = 1;;i++) {
			sprintf(buf, "%s/%s/%s_%d", cwd, TRASH_FILES, filename, i);

			if (access(buf, F_OK) != 0)
				break;

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
		curSize -= statbuf.st_size;
		removeInfoNodeByName(filename);
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

	sprintf(buffer, "%s/%s/%s", cwd, TRASH_INFO, filename);
	if ((fp = fopen(buffer, "a")) == NULL) {
		fprintf(stderr, "%s open error", buffer);
		return -1;
	}

	if (stat(buffer, &statbuf) < 0) {
		fprintf(stderr, "%s stat error", buffer);
		return -1;
	}

	fseek(fp, 0, SEEK_END);
	size = ftell(fp);
	if (size == 0) {
		fprintf(fp, "[Trash info]\n");
	}

	// buffer 에 휴지통 파일 path 기록
	fprintf(fp, "%s\n", realpath(filepath, buffer));

	t = time(NULL);
	timeinfo = localtime(&t);
	strftime(buffer2, sizeof(buffer2), TIME_FORMAT, timeinfo);
	fprintf(fp, "D : %s\n", buffer2);

	for (int i = 1;;i++) {
		sprintf(buffer, "%s/%s/%s_%d", cwd, TRASH_FILES, filename, i);
		if (access(buffer, F_OK) != 0)
			break;
	}

	timeinfo = localtime(&statbuf.st_mtime);
	strftime(buffer2, sizeof(buffer2), TIME_FORMAT, timeinfo);
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

int getSize(const char *dirpath) {
	char buf[BUF_LEN];
	struct dirent **dirList = NULL;
	struct stat statbuf;
	int size = 0;
	int count;
	int tmp;

	if ((count = scandir(dirpath, &dirList, ignoreParentAndSelfDirFilter, NULL)) < 0) {
		fprintf(stderr, "%s scandir error\n", dirpath);
		return -1;
	}

	for (int i = 0; i < count; i++) {
		sprintf(buf, "%s/%s", dirpath, dirList[i]->d_name);
		stat(buf, &statbuf);

		if (S_ISDIR(statbuf.st_mode)) {
			if ((tmp = getSize(buf)) < 0) {
				fprintf(stderr, "%s getSize error\n", buf);
				return -1;
			}
			size += tmp;
		} else {
			size += statbuf.st_size;
		}
	}

	for (int i = 0; i < count; i++)
		free(dirList[i]);

	if (dirList != NULL)
		free(dirList);

	return size;
}

int __printSize(const char *filepath, int curDepth, int depth) {
	char buf[BUF_LEN];
	struct dirent **dirList;
	struct stat statbuf;
	int count;
	long size = 0, tmp;

	if (curDepth == depth)
		return 0;

	if ((count = scandir(filepath, &dirList, ignoreParentAndSelfDirFilter, alphasort)) < 0) {
		fprintf(stderr, "%s scandir error\n", filepath);
		return -1;
	}

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
			return -1;
		}

		// 디렉토리인 경우
		if (S_ISDIR(statbuf.st_mode)) {
			if (curDepth + 1 < depth) {
				if ((tmp = __printSize(buf, curDepth + 1, depth)) < 0) {
					return -1;
				}
				size += tmp;
			} else {
				if ((tmp = getSize(buf)) < 0) {
					fprintf(stderr, "%s getSize error\n", buf);
					return -1;
				}

				size += tmp;
				printf("%ld\t%s\n", tmp, buf);
			}
		}
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

int printSize(const char *filepath, int dOption) {
	struct stat statbuf;
	char buf[BUF_LEN];

	if (stat(filepath, &statbuf) < 0) {
		fprintf(stderr, "%s stat error\n", filepath);
		return -1;
	}

	if (filepath[0] == '.') {
		strcpy(buf, filepath);
	}
	else {
		sprintf(buf, "./%s", filepath);
	}

	if (dOption <= 1) {
		long size = getSize(buf);
		if (size < 0) {
			fprintf(stderr, "%s getSize error\n", buf);
			return -1;
		}
		printf("%ld\t%s\n", size, buf);
	}
	else
		__printSize(buf, 0, dOption - 1);
	return 0;
}

int getIndex(const char *filepath) {
	char *p;
	p = strrchr(filepath, '_');
	if (p == NULL)
		return -1;

	p++;
	return atoi(p);
}

int deleteTimeSort(const struct dirent **left, const struct dirent **right) {
	int leftIndex, rightIndex;
	leftIndex = getIndex((*left)->d_name);
	rightIndex = getIndex((*right)->d_name);
	return leftIndex > rightIndex;
}

int readTrashInfo(FILE *fp, struct info_node *infoNode) {
	char buf[BUF_LEN];

	// 파일명
	fgets(buf, sizeof(buf), fp);
	buf[strlen(buf) - 1] = '\0';
	strcpy(infoNode->filepath, buf);
		
	// 삭제 시간
	fgets(buf, sizeof(buf), fp);
	buf[strlen(buf) - 1] = '\0';
	strcpy(infoNode->deletionTime, buf + 4);

	// 수정 시간
	fgets(buf, sizeof(buf), fp);
	buf[strlen(buf) - 1] = '\0';
	strcpy(infoNode->modificationTime, buf + 4);
	return 0;
}

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
	filename = strrchr(filepath, '/');
	if (filename == NULL)
		filename = filepath;

	// 휴지통에 해당 파일이 있는지 확인
	// r+ 옵션은 파일이 없는 경우 에러를 발생시킴
	sprintf(buf, "%s/%s/%s", cwd, TRASH_INFO, filename);
	if ((fp = fopen(buf, "r+")) == NULL) {
		printf("There is no '%s' in '%s' directory\n", filepath, TRASH);
		return -1;
	}

	// l 옵션 - 휴지통 파일 삭제 시간이 오래된 순으로 출력 후 명령어 실행
	if (lOption) {
		sprintf(buf, "%s/%s", cwd, TRASH_INFO);

		if ((count = scandir(buf, &fileList, ignoreParentAndSelfDirFilter, deleteTimeSort)) < 0) {
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

			while (ftell(fp) < fileSize) {
				infoNode = calloc(1, sizeof(struct info_node));
				readTrashInfo(fp, infoNode);
				insertInfoNode(infoNode);
			}
			fclose(fp);
		}

		infoNode = infoList;
		int i = 0;
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
			fprintf(stderr, "There are %d %s in %s.\n", count, filename, TRASH);
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
		// 파일 내용 다 지우고
		freopen(buf, "w", fp);

		fprintf(fp, "[Trash info]\n");

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

int ignoreParentAndSelfDirFilter(const struct dirent *dir) {
	if (!strcmp(dir->d_name, ".") || !strcmp(dir->d_name, ".."))
		return 0;
	return 1;
}


int __printTree(const char *filepath, int depth, int needIndent) {
	struct dirent **fileList;
	struct stat statbuf;
	int count;
	char buf[BUF_LEN];
	char filename[TAB_SIZE];
	const char *fnamep;
	char nameBlock[10];
	char emptyBlock[10];
	sprintf(nameBlock, "|%%-%ds", TAB_SIZE - 1);
	sprintf(emptyBlock, "%%-%ds", TAB_SIZE);

	fnamep = strrchr(filepath, '/') + 1;
	if (fnamep == NULL)
		fnamep = filepath;

	strcpy(filename, fnamep);
	stat(filepath, &statbuf);

	// 파일인 경우
	if (!S_ISDIR(statbuf.st_mode)) {
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

	if (count > 0) {
		for (int i = strlen(filename); i < TAB_SIZE - 1; i++) {
			filename[i] = '-';
		}
		filename[TAB_SIZE - 1] = '\0';
	}

	if (needIndent) {
		for (int i = 0; i < depth; i++)
			printf(emptyBlock, "");
	}
	printf(nameBlock, filename);

	for (int i = 0; i < count; i++) {
		sprintf(buf, "%s/%s", filepath, fileList[i]->d_name);
		__printTree(buf, depth + 1, i > 0);
	}

	for (int j = 0; j < count; j++)
		free(fileList[j]);
	free(fileList);
	return count;
}

int printTree() {
	char buf[BUF_LEN];
	sprintf(buf, "%s/%s", cwd, DIRECTORY);
	__printTree(buf, 0, 0);
	printf("\n");
	return 0;
}
