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
				return -1;
			}
		}
	}
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
		sprintf(errorString, "%s open error", buffer);
		return -1;
	}

	if (stat(buffer, &statbuf) < 0) {
		sprintf(errorString, "%s stat error", buffer);
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

int writeLog(const char* filepath) {

}
