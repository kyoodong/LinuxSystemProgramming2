#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <dirent.h>
#include <string.h>
#include <time.h>
#include <syslog.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "core.h"

#define CREATE "create"
#define MODIFY "modify"
#define DELETE "delete"
#define INTERVAL 1

struct file {
	struct stat stat;
	char filepath[BUF_LEN];
	int is_visited;
	struct file *parent, *first_child, *prev_sibling, *next_sibling;
};

struct file root;
int is_init = 1;
FILE *fp;

struct file* traversal(struct file *rootFile) {
	static struct file *file;
	struct file *ret;

	if (rootFile != NULL) {
		file = rootFile;

		while (file->first_child != NULL)
			file = file->first_child;

		ret = file;
		if (file->next_sibling != NULL)
			file = file->next_sibling;
		else
			file = file->parent;
		syslog(LOG_DEBUG, "[traversal] return root %s", ret->filepath);
		return ret;
	}

	if (file == NULL || file == &root) {
		syslog(LOG_DEBUG, "[traversal] return NULL");
		return NULL;
	}

	ret = file;
	if (file->next_sibling != NULL) {
		file = file->next_sibling;

		while (file->first_child != NULL)
			file = file->first_child;
	}
	else
		file = file->parent;

	syslog(LOG_DEBUG, "[traversal] return self %s", ret->filepath);
	return ret;
}

void insert(struct file *parent, struct file *file) {
	if (parent == NULL || file == NULL)
		return;

	struct file *tmp;

	file->parent = parent;
	file->next_sibling = NULL;
	file->first_child = NULL;
	file->prev_sibling = NULL;

	if (parent->first_child == NULL) {
		parent->first_child = file;
	} else {
		tmp = parent->first_child;

		while (tmp->next_sibling != NULL)
			tmp = tmp->next_sibling;
		tmp->next_sibling = file;
		file->prev_sibling = tmp;
	}
}

void deleteChildren(struct file *file) {
	struct file *child;

	if (file == NULL)
		return;

	child = file->first_child;
	while (child != NULL) {
		deleteChildren(child);
		child = child->next_sibling;
	}
	free(file);
}

void delete(struct file *file) {
	if (file == NULL || file->parent == NULL)
		return;

	if (file->parent->first_child == file) {
		file->parent->first_child = file->next_sibling;
	}

	else if (file->next_sibling != NULL) {
		file->next_sibling->prev_sibling = file->prev_sibling;
	}

	if (file->prev_sibling != NULL)
		file->prev_sibling->next_sibling = file->next_sibling;

	deleteChildren(file);
}

struct file* find(const struct file *parent, const char *filepath) {
	struct file *file = parent->first_child;
	
	while (file != NULL) {
		if (!strcmp(file->filepath, filepath))
			return file;

		file = file->next_sibling;
	}

	return NULL;
}

int init() {
	pid_t pid;
	int fd, maxfd;

	if ((pid = fork()) < 0) {
		fprintf(stderr, "fork error\n");
		exit(1);
	}

	if (pid != 0)
		exit(0);

	pid = getpid();
	printf("process %d running as daemon\n", pid);
	setsid();
	signal(SIGTTIN, SIG_IGN);
	signal(SIGTTOU, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);
	maxfd = getdtablesize();

	for (fd = 0; fd < maxfd; fd++)
		close(fd);

	umask(0);
	fd = open("/dev/null", O_RDWR);
	dup(0);
	dup(0);
	return 0;
}

int ignoreParentAndSelfDirFilter(const struct dirent *dir) {
	if (!strcmp(dir->d_name, ".") || !strcmp(dir->d_name, "..") ||
			!strcmp(dir->d_name, ".git") || !strcmp(dir->d_name, "trash"))
		return 0;
	return 1;
}

void log_write(const char *state, const char *filepath) {
	struct tm *timeinfo;
	time_t rawtime;
	char timestr[30];
	char filename[BUF_LEN];
	char buf[BUF_LEN];
	const char *path;

	getcwd(buf, sizeof(buf));
	strcat(buf, "/");
	strcat(buf, DIRECTORY);
	
	path = strstr(filepath, buf);
	if (path == NULL) {
		path = strstr(filepath, DIRECTORY);
		if (path == NULL)
			path = filepath;
		else
			path += strlen(DIRECTORY) + 1;
	}
	else
		path += strlen(buf) + 1;
	strcpy(filename, path);

	for (char *p = filename; *p != '\0'; p++) {
		if (*p == '/')
			*p = '_';
	}

	time(&rawtime);
	timeinfo = localtime(&rawtime);
	strftime(timestr, sizeof(timestr), TIME_FORMAT, timeinfo);
	sprintf(buf, "[%s][%s_%s]\n", timestr, state, filename);
	syslog(LOG_INFO, "[log_write][%s][%d][%s_%s]\n", timestr, getpid(), state, filename);
	fprintf(fp, "%s", buf);
}

int observe(struct file *parent, const char *filepath, int depth) {
	struct stat statbuf;
	char buf[BUF_LEN];
	char timestr[30];
	char state[30];
	const char *filename;
	struct dirent **dirList;
	int count;
	struct file *file;

	file = find(parent, filepath);
	stat(filepath, &statbuf);
	syslog(LOG_DEBUG, "[observe] visit %s", filepath);

	// 없던 파일이 생긴 경우
	if (file == NULL) {
		file = malloc(sizeof(struct file));
		strcpy(file->filepath, filepath);
		file->is_visited = 1;
		file->stat = statbuf;
		insert(parent, file);

		// 초기화 과정에서는 로그를 찍지 않음
		if (!is_init) {
			syslog(LOG_INFO, "[observe] New file %s is detected", file->filepath);
			log_write(CREATE, filepath);
		}
	}

	file->is_visited = 1;

	// 디렉토리라면 하위 파일들도 관찰
	if (S_ISDIR(statbuf.st_mode)) {
		if ((count = scandir(filepath, &dirList, ignoreParentAndSelfDirFilter, NULL)) < 0) {
			syslog(LOG_ERR, "%s scandir error\n", filepath);
			return -1;
		}

		for (int i = 0; i < count; i++) {
			sprintf(buf, "%s/%s", filepath, dirList[i]->d_name);
			observe(file, buf, depth + 1);
		}

		for (int i = 0; i < count; i++)
			free(dirList[i]);
		free(dirList);
	}
	if (depth > 0 && file->stat.st_mtime != statbuf.st_mtime) {
		log_write(MODIFY, filepath);
		file->stat = statbuf;
	}

	return 0;
}

void daemon_main() {
	char buf[BUF_LEN];
	struct stat statbuf;
	struct file *file;

	root.is_visited = 1;
	strcpy(root.filepath, "root");
	openlog("[SSUMonitor]", LOG_PID, LOG_LPR);

	syslog(LOG_DEBUG, "%d\n", getpid());
	
	if ((fp = fopen("log.txt", "w+")) == NULL) {
		syslog(LOG_ERR, "log.txt open error %m\n");
		exit(1);
	}

	while (1) {
		observe(&root, DIRECTORY, 0);
		syslog(LOG_INFO, "observing is finished");

		if (!is_init) {
			syslog(LOG_DEBUG, "before traversal");
			file = traversal(&root);
 
			while (file != NULL) {
				if (!file->is_visited) {
					syslog(LOG_DEBUG, "%s is not visited", file->filepath);
					log_write(DELETE, file->filepath);
					delete(file->first_child);
					struct file *tmp = file;
					file = traversal(NULL);
					delete(tmp);
					continue;
				}
				file->is_visited = 0;
				syslog(LOG_DEBUG, "%s %d", file->filepath, file->is_visited);
				file = traversal(NULL);
			}
			syslog(LOG_INFO, "traversal is finished");
		}
		fflush(fp);
		sleep(INTERVAL);
		is_init = 0;
		syslog(LOG_DEBUG, "still running");
	}
	closelog();
	fclose(fp);

}

int main() {
	pid_t pid;

	pid = getpid();
	printf("parent process : %d\n", pid);
	printf("daemon process initialization\n");

	if (init() < 0) {
		fprintf(stderr, "init failed\n");
		exit(1);
	}
	
	daemon_main();
	
	// 종료메시지를 터미널에 log.txt 에 출력하는건가?
	exit(0);
}
