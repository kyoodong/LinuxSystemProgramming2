#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "prompt.h"
#include "core.h"

char cwd[BUF_LEN];
struct deletion_node *deletion_list;

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
		node = deletion_list;
		curTime = time(NULL);
		while (node != NULL) {
			// 삭제해야하는 파일 발견 시
			if (node->endTime <= curTime) {
				tmp = node;

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
		sleep(DELETE_INTERVAL);
	}
}

void removeDeletionNode(struct deletion_node *node) {
	// 루트
	if (node->prev == NULL) {
		free(node);
		deletion_list = NULL;
		return;
	}

	node->prev->next = node->next;

	if (node->next != NULL) {
		node->next->prev = node->prev;
	}
	free(node);
}

void insertDeletionNode(struct deletion_node *node) {
	struct deletion_node *tmp, *prev;

	if (deletion_list == NULL) {
		node->prev = NULL;
		node->next = NULL;
		deletion_list = node;
		return;
	}

	tmp = deletion_list;
	prev = NULL;
	while (tmp->endTime < node->endTime) {
		prev = tmp;
		tmp = tmp->next;

		if (tmp == NULL)
			break;
	}

	// 맨 앞에 추가되어야 하는 경우
	if (prev == NULL) {
		node->next = deletion_list;
		deletion_list->prev = node;
		deletion_list = node;
		return;
	}

	node->next = prev->next;
	prev->next = node;
	node->prev = prev;

	if (node->next != NULL)
		node->next->prev = node;
}

int filterOnlyDirectory(const struct dirent *info) {
	struct stat statbuf;

	if (info->d_name[0] == '.' || !strcmp(info->d_name, TRASH))
		return 0;

	if (stat(info->d_name, &statbuf) < 0) {
		fprintf(stderr, "%s stat error\n", info->d_name);
		return 0;
	}

	return S_ISDIR(statbuf.st_mode);
}


int deleteFile(const char *filepath, const char *endDate, const char *endTime, int iOption, int rOption) {
	char relatedFilepath[BUF_LEN];
	char buffer[BUF_LEN];
	struct deletion_node *node;
	struct tm tm;
	struct dirent **dirList;
	int count;

	if (filepath == NULL || strlen(filepath) == 0) {
		fprintf(stderr, "<filename> is empty");
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

	for (int i = 0; i < count; i++) {
		sprintf(relatedFilepath, "%s/%s", dirList[i]->d_name, filepath);

		if (access(relatedFilepath, F_OK) < 0)
			continue;

		// 즉시 삭제
		if (endDate == NULL || endTime == NULL || strlen(endDate) == 0 || strlen(endTime) == 0) {
			// 지정한 시간에 삭제 시 삭제 여부 재확인이
			// 그 시간이 되면 물어보라는건가?
			if (rOption) {
				printf("Delete [y/n]? ");
				char c = getchar();
	
				if (c == 'n')
					return 0;
			}

			if (iOption) {
				remove(relatedFilepath);
			} else {
				if (sendToTrash(relatedFilepath) < 0) {
					for (int j = 0; j < count; j++)
						free(dirList[i]);
					free(dirList);
					return -1;
				}
			}
	
			for (int j = 0; j < count; j++)
				free(dirList[i]);
			free(dirList);
			return 0;
		}

		// 지정된 시간에 삭제
		sprintf(buffer, "%s %s", endDate, endTime);
		strptime(buffer, TIME_FORMAT, &tm);
		node = malloc(sizeof(struct deletion_node));
		strcpy(node->filepath, realpath(relatedFilepath, buffer));
		node->iOption = iOption;
		node->rOption = rOption;
		node->endTime = mktime(&tm);
		insertDeletionNode(node);
	
		for (int j = 0; j < count; j++)
			free(dirList[i]);
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

int __printSize(const char *filepath, int curDepth, int depth) {
	char buf[BUF_LEN];
	struct dirent **dirList;
	struct stat statbuf;
	int count;

	if (curDepth == depth)
		return 0;

	if ((count = scandir(filepath, &dirList, NULL, alphasort)) < 0) {
		fprintf(stderr, "%s scandir error\n", filepath);
		return -1;
	}

	for (int i = 0; i < count; i++) {
		if (dirList[i]->d_name[0] == '.')
			continue;

		sprintf(buf, "%s/%s", filepath, dirList[i]->d_name);
		if (stat(buf, &statbuf) < 0) {
			fprintf(stderr, "%s stat error\n", buf);
			return -1;
		}

		if (S_ISDIR(statbuf.st_mode) && curDepth + 1 < depth) {
			if (__printSize(buf, curDepth + 1, depth) < 0) {
				return -1;
			}
		}
		else
			printf("%ld\t%s\n", statbuf.st_size, buf);
	}

	for (int i = 0; i < count; i++)
		free(dirList[i]);
	free(dirList);
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

	if (dOption <= 1)
		printf("%ld\t%s\n", statbuf.st_size, buf);
	else
		__printSize(buf, 0, dOption - 1);
	return 0;
}
