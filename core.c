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
struct deletion_node *deletionList;
struct info_node *infoList;
char termbuf[BUF_LEN][BUF_LEN];
int termWidth, termHeight;
pthread_mutex_t deletionThreadMutex = PTHREAD_MUTEX_INITIALIZER;

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

void* deleteThread() {
	struct deletion_node *node, *tmp;
	time_t curTime;
	while (1) {
		pthread_mutex_lock(&deletionThreadMutex);
		node = deletionList;
		curTime = time(NULL);
		while (node != NULL) {
			// 삭제해야하는 파일 발견 시
			if (node->endTime <= curTime) {
				tmp = node;

				printf("%s\ncurTime = %ld\nendTime = %ld\n", node->filepath, curTime, node->endTime);
				// 이미 삭제된 경우
				if (access(node->filepath, F_OK) != 0) {
					node = node->next;
					removeDeletionNode(tmp);
					continue;
				}

				// r 옵션 켜져있었으면 물어보고 삭제
				if (node->rOption) {
					printf("Delete [y/n]? ");
					char c = getchar();

					// 파일 삭제 안한다고하면 삭제 리스트에서만 제거
					if (c == 'n') {
						node = node->next;
						removeDeletionNode(tmp);
						continue;
					}
				}

				// 파일 삭제 후 삭제 리스트에서 제거
				if (node->iOption) {
					remove(node->filepath);
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
	infoList = NULL;
}

void insertInfoNode(struct info_node *node) {
	if (infoList == NULL) {
		node->prev = NULL;
		node->next = NULL;
		infoList = node;
		return;
	}

	node->next = infoList;
	infoList->prev = node;
	infoList = node;
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
			remove(filepath);
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
	strcat(buffer, ":00");
	strptime(buffer, TIME_FORMAT, &tm);
	node = calloc(1, sizeof(struct deletion_node));
	strcpy(node->filepath, realpath(filepath, buffer));
	node->iOption = iOption;
	node->rOption = rOption;
	node->endTime = mktime(&tm);
	insertDeletionNode(node);
	return 0;
}

int deleteFile(const char *filepath, const char *endDate, const char *endTime, int iOption, int rOption) {
	char relatedFilepath[BUF_LEN];
	struct dirent **dirList;
	int count;
	int status;

	if (filepath == NULL || strlen(filepath) == 0) {
		fprintf(stderr, "<filename> is empty\n");
		return -1;
	}

	if ((count = scandir(cwd, &dirList, filterOnlyDirectory, NULL)) < 0) {
		fprintf(stderr, "scandir error\n");
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

	status = __deleteFile(filepath, endDate, endTime, iOption, rOption);
	if (status <= 0) {
		if (status < 0)
			fprintf(stderr, "%s delete file error\n", filepath);
		for (int i = 0; i < count; i++)
			free(dirList[i]);
		free(dirList);
		return status;
	}

	for (int i = 0; i < count; i++) {
		sprintf(relatedFilepath, "%s/%s", dirList[i]->d_name, filepath);

		if (__deleteFile(relatedFilepath, endDate, endTime, iOption, rOption) == 1)
			continue;
	
		for (int j = 0; j < count; j++)
			free(dirList[j]);
		free(dirList);
		return 0;
	}

	fprintf(stderr, "%s doesn't exist\n", filepath);
	for (int i = 0; i < count; i++)
		free(dirList[i]);
	free(dirList);
	return -1;
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
	sprintf(buffer, "%s/%s/%s", cwd, TRASH_FILES, filename);

	t = time(NULL);
	timeinfo = localtime(&t);
	strftime(buffer2, sizeof(buffer2), TIME_FORMAT, timeinfo);
	fprintf(fp, "D : %s\n", buffer2);
	strcat(buffer, buffer2);

	timeinfo = localtime(&statbuf.st_mtime);
	strftime(buffer2, sizeof(buffer2), TIME_FORMAT, timeinfo);
	fprintf(fp, "M : %s\n", buffer2);

	// 파일을 휴지통으로 이동
	rename(filepath, buffer);

	fflush(fp);
	fclose(fp);

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

int deleteTimeSort(const struct dirent **left, const struct dirent **right) {
	time_t leftTime, rightTime;
	struct tm result;

	const char *leftTimeStr = (*left)->d_name + strlen((*left)->d_name) - TIME_FORMAT_LEN;
	const char *rightTimeStr = (*right)->d_name + strlen((*right)->d_name) - TIME_FORMAT_LEN;
	strptime(leftTimeStr, TIME_FORMAT, &result);
	leftTime = mktime(&result);
	strptime(rightTimeStr, TIME_FORMAT, &result);
	rightTime = mktime(&result);

	return leftTime > rightTime;
}

int recoverFile(const char *filepath, int lOption) {
	const char *filename;
	char path[BUF_LEN];
	char buf[BUF_LEN];
	FILE *fp;
	int count, index, num;
	struct info_node *infoNode;
	size_t fileSize;
	struct dirent **fileList;

	filename = strrchr(filepath, '/');
	if (filename == NULL)
		filename = filepath;

	sprintf(buf, "%s/%s/%s", cwd, TRASH_INFO, filename);
	if (access(buf, F_OK) != 0) {
		fprintf(stderr, "There is no '%s' in '%s' directory\n", filepath, TRASH);
		return -1;
	}

	if ((fp = fopen(buf, "r+")) == NULL) {
		fprintf(stderr, "%s fopen error\n", buf);
		return -1;
	}

	if (lOption) {
		sprintf(buf, "%s/%s", cwd, TRASH_FILES);
		if ((count = scandir(buf, &fileList, ignoreParentAndSelfDirFilter, deleteTimeSort)) < 0) {
			fprintf(stderr, "scandir error\n");
			return -1;
		}

		printf("Old trash files\n");
		for (int i = 0; i < count; i++) {
			char deleteTime[TIME_FORMAT_LEN + 10];
			strcpy(buf, fileList[i]->d_name);
			strcpy(deleteTime, fileList[i]->d_name + strlen(fileList[i]->d_name) - TIME_FORMAT_LEN);
			buf[strlen(buf) - TIME_FORMAT_LEN] = '\0';

			printf("%d. %s\t\t%s\n", i + 1, buf, deleteTime);
		}
		printf("\n\n");

		for (int i = 0; i < count; i++) {
			free(fileList[i]);
		}
		free(fileList);
	}


	fseek(fp, 0, SEEK_END);
	fileSize = ftell(fp);
	count = 0;

	fseek(fp, 0, SEEK_SET);
	fgets(buf, sizeof(buf), fp);

	while (ftell(fp) < fileSize) {
		infoNode = calloc(1, sizeof(struct info_node));

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

		insertInfoNode(infoNode);
		count++;
	}

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
	} else {
		infoNode = infoList;
	}

	sprintf(buf, "%s%s", filename, infoNode->deletionTime);
	*strrchr(infoNode->filepath, '/') = '\0';
	sprintf(path, "%s/%s/", cwd, TRASH_FILES);
	strcat(path, buf);
	
	strcpy(buf, infoNode->filepath);
	strcat(buf, "/");
	strcat(buf, filename);

	// 복구하고자 하는 파일이 이미 해당 디렉토리에 있는 경우 넘버링
	if (access(buf, F_OK) == 0) {
		while (1) {
			index = 1;
			strcpy(buf, infoNode->filepath);
			sprintf(buf + strlen(buf), "/%d_%s", index, filename);
			if (access(buf, F_OK) != 0)
				break;
		}
	}

	// 파일 실제 복구
	rename(path, buf);
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


int __printTree(int top, int left, int *bottom, const char *filepath) {
	struct dirent **fileList;
	struct stat statbuf;
	int count;
	char buf[BUF_LEN];
	char exp[10];
	sprintf(exp, "|%%-%ds", TAB_SIZE - 1);

	if ((count = scandir(filepath, &fileList, ignoreParentAndSelfDirFilter, alphasort)) < 0) {
		fprintf(stderr, "%s scandir error\n", filepath);
		return -1;
	}

	for (int i = 0; i < count; i++) {
		sprintf(buf, "%s/%s", filepath, fileList[i]->d_name);
		if (stat(buf, &statbuf) < 0) {
			for (int j = 0; j < count; j++)
				free(fileList[j]);
			free(fileList);
			fprintf(stderr, "%s stat error\n", buf);
			return -1;
		}

		if (termWidth < left + strlen(fileList[i]->d_name))
			termWidth = left + strlen(fileList[i]->d_name);

		sprintf(*(termbuf + *bottom) + left, exp, fileList[i]->d_name);
		//termbuf[*bottom][left + TAB_SIZE] = ' ';

		if (S_ISDIR(statbuf.st_mode)) {
			int j;
			for (j = strlen(fileList[i]->d_name) + 1; j < TAB_SIZE; j++) 
				termbuf[*bottom][left + j] = '-';
			if (__printTree(*bottom, left + TAB_SIZE, bottom, buf) == 0) {
				sprintf(termbuf[*bottom] + left + j, "[Empty dir]");
				(*bottom)++;
			}
		} else {
			(*bottom)++;
		}
		
		if (termHeight < *bottom)
			termHeight = *bottom;
	}

	for (int j = 0; j < count; j++)
		free(fileList[j]);
	free(fileList);
	return count;
}

int printTree() {
	struct dirent **dirList;
	int count;
	int bottom = 0;

	termWidth = termHeight = 0;
	for (int i = 0; i < BUF_LEN; i++)
		for (int j = 0; j < BUF_LEN; j++)
			termbuf[i][j] = ' ';

	if ((count = scandir(".", &dirList, filterOnlyDirectory, alphasort)) < 0) {
		fprintf(stderr, ". scandir error\n");
		return -1;
	}

	for (int i = 0; i < count; i++) {
		int j;
		for (j = 0; dirList[i]->d_name[j] != '\0'; j++)
			termbuf[bottom][j] = dirList[i]->d_name[j];

		for (j = strlen(dirList[i]->d_name); j < TAB_SIZE; j++) 
			termbuf[bottom][j] = '-';

		if (__printTree(bottom, TAB_SIZE, &bottom, dirList[i]->d_name) == 0) {
			sprintf(termbuf[bottom] + j, "[Empty dir]");
		}
	}

	for (int h = 0; h <= termHeight; h++) {
		for (int w = 0; w <= termWidth; w++) {
			putchar(termbuf[h][w]);
		}
		putchar('\n');
	}

	for (int i = 0; i < count; i++)
		free(dirList[i]);
	free(dirList);
	return 0;
}
