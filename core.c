#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "prompt.h"
#include "core.h"

char errorString[BUF_LEN];
char cwd[BUF_LEN];

int init() {
	char *p;

	getcwd(cwd, sizeof(cwd));
}


int deleteFile(const char *filepath, const char *endDate, const char *endTime, int iOption, int rOption) {
	char relatedFilepath[BUF_LEN];

	if (filepath == NULL || strlen(filepath) == 0) {
		sprintf(errorString, "<filename> is empty");
		return -1;
	}

	sprintf(relatedFilepath, "%s/%s", BASE_DIR, filepath);

	if (access(relatedFilepath, F_OK) < 0) {
		sprintf(errorString, "%s does not exist", relatedFilepath);
		return -1;
	}

	// trash 파일이 있는지 검사하여 없으면 생성
	if (!iOption) {
		if (access(TRASH, F_OK) != 0) {
			printf("trash 디렉토리가 없습니다. 생성하겠습니다...\n");
			mkdir(TRASH, 0777);
			mkdir(TRASH_INFO, 0777);
			mkdir(TRASH_FILES, 0777);
		}
	}

	// 즉시 삭제
	if (endDate == NULL || endTime == NULL || strlen(endDate) == 0 || strlen(endTime) == 0) {
		if (iOption) {
			remove(relatedFilepath);
		} else {
			if (sendToTrash(relatedFilepath) < 0) {
				return -1;
			}
		}
	}
}

int sendToTrash(const char *filepath) {
	char *p;
	const char *filename;
	char buffer[BUF_LEN];
	FILE *fp;
	struct stat statbuf;
	time_t t;

	// 파일명
	filename = strrchr(filepath, '/');
	if (filename == NULL)
		filename = filepath;
	else
		filename++;

	// @TODO: 로그 파일 중에 파일 명이 같은게 있으면?
	sprintf(buffer, "%s/%s/%s", cwd, TRASH_INFO, filename);
	if ((fp = fopen(buffer, "w")) == NULL) {
		sprintf(errorString, "%s open error", buffer);
		return -1;
	}

	if (stat(buffer, &statbuf) < 0) {
		sprintf(errorString, "%s stat error", buffer);
		return -1;
	}

	fprintf(fp, "[Trash info]\n");
	fprintf(fp, "%s\n", realpath(filepath, buffer));

	t = time(NULL);
	fprintf(fp, "D : %s", ctime(&t));
	fprintf(fp, "M : %s", ctime(&statbuf.st_mtime));

	// 파일을 휴지통으로 이동
	sprintf(buffer, "%s/%s/%s", cwd, TRASH_FILES, filename);
	rename(filepath, buffer);

	fflush(fp);
	fclose(fp);

	return 0;
}

int writeLog(const char* filepath) {

}
