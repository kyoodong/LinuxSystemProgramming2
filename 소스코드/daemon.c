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


/**
  파일 트리 순회하는 함수
  @param rootFile 순회할 트리 root 노드, 최초 사용 시에만 사용하고, 이후에는 NULL 이 들어올 시 이 전 root 를 이어서 사용
  @return 순회 중인 트리에서 노드를 하나씩 리턴, 더 이상 방문할 노드가 없다면 NULL
  */
struct file* traversal(struct file *rootFile) {
	static struct file *file;
	struct file *ret;

	// rootFile 에 값이 들어온 경우에는
	if (rootFile != NULL) {
		// 초기화
		file = rootFile;

		// 가장 depth 가 깊은 곳을 찾아 리턴함
		while (file->first_child != NULL)
			file = file->first_child;

		// 그리고 file 은 다음번에 리턴될 곳을 미리 가리킴
		ret = file;
		if (ret == rootFile)
			return NULL;

		if (file->next_sibling != NULL) {
			file = file->next_sibling;

			while (file->first_child != NULL)
				file = file->first_child;
		}
		else
			file = file->parent;
		return ret;
	}

	if (file == NULL || file == &root) {
		return NULL;
	}

	// 현재 가리키는 곳을 리턴하고
	// 다음 번에 리턴될 곳을 미리 찾아 가리킴
	ret = file;
	if (file->next_sibling != NULL) {
		file = file->next_sibling;

		while (file->first_child != NULL)
			file = file->first_child;
	}
	else
		file = file->parent;

	return ret;
}

/**
  파일 리스트에 노드를 추가하는 함수
  @param parent 추가될 노드의 부모 노드
  @param file 추가될 노드
  */
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

/**
  파일과 그 자식 파일들까지 재귀적으로 없애주는 함수
  @param file 삭제할 파일
  디렉토리라면 재귀적으로 하위 파일들까지 리스트에서 제거하고, 파일일 경우 그냥 제거
  */
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

/**
  파일 리스트에서 파일을 삭제해주는 함수
  @param file 삭제할 파일
  */
void delete(struct file *file) {
	if (file == NULL || file->parent == NULL)
		return;

	if (file->parent->first_child == file) {
		file->parent->first_child = file->next_sibling;
	}

	if (file->next_sibling != NULL) {
		file->next_sibling->prev_sibling = file->prev_sibling;
	}

	if (file->prev_sibling != NULL)
		file->prev_sibling->next_sibling = file->next_sibling;

	deleteChildren(file);
}

/**
  파일 경로로 노드를 검색해주는 함수
  @param parent 노드를 갖고 있는 부모 노드
  @param filepath 검색 키워드
  @return 있는 경우 노드 포인터, 없는 경우 NULL
  */
struct file* find(const struct file *parent, const char *filepath) {
	struct file *file = parent->first_child;
	
	while (file != NULL) {
		if (!strcmp(file->filepath, filepath))
			return file;

		file = file->next_sibling;
	}

	return NULL;
}

/**
  daemon 프로세스로 만들어주는 함수
  */
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

/**
  scandir 에서 . .. 디렉토리를 걸러주는 필터 함수
  */
int ignoreParentAndSelfDirFilter(const struct dirent *dir) {
	if (!strcmp(dir->d_name, ".") || !strcmp(dir->d_name, ".."))
		return 0;
	return 1;
}

/**
  log.txt 파일에 로그를 작성하는 함수
  @param state 어떤 작업을 했는지. CREATE, MODIFY, DELETE
  @param filepath 어떤 파일에 작업이 이루어졌는지
  */
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
	
	// 파일 명 추출
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

	// 파일 경로에 '/' 는 '_'로 치환
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

/**
  지정 디렉토리를 관찰하는 함수
  @param parent 현재 관찰 중인 파일의 부모 파일노드, 새로운 파일이 생성됨을 감지하면 해당 parent 노드 밑에 자식 노드로 추가됨
  @param filepath 현재 관찰 중인 파일의 경로
  */
int observe(struct file *parent, const char *filepath) {
	struct stat statbuf;
	char buf[BUF_LEN];
	char timestr[30];
	char state[30];
	const char *filename;
	struct dirent **dirList;
	int count;
	struct file *file;

	// 내부 파일을 모두 스캔
	if ((count = scandir(filepath, &dirList, ignoreParentAndSelfDirFilter, NULL)) < 0) {
		syslog(LOG_ERR, "%s scandir error\n", filepath);
		return -1;
	}

	for (int i = 0; i < count; i++) {
		sprintf(buf, "%s/%s", filepath, dirList[i]->d_name);

		// 파일명으로 리스트 내 검색
		file = find(parent, buf);
		if (stat(buf, &statbuf) < 0) {
			syslog(LOG_INFO, "[observe] %s was deleted\n", buf);
			return 0;
		}
		syslog(LOG_DEBUG, "[observe] visit %s", buf);

		// 없던 파일이 생긴 경우 리스트에 노드 추가
		if (file == NULL) {
			file = malloc(sizeof(struct file));
			strcpy(file->filepath, buf);
			file->is_visited = 1;
			file->stat = statbuf;
			insert(parent, file);

			// 초기화 과정에서는 로그를 찍지 않음
			if (!is_init) {
				syslog(LOG_INFO, "[observe] New file %s is detected", file->filepath);
				log_write(CREATE, buf);
			}
		}

		// 확인 도장
		// 나중에 이 확인 도장이 없으면 파일이 삭제됐다고 판단하고 해당 노드는 삭제될것임
		file->is_visited = 1;

		// 디렉토리라면 하위 파일들도 관찰
		if (S_ISDIR(statbuf.st_mode)) {
			observe(file, buf);
		}
	}

	for (int i = 0; i < count; i++)
		free(dirList[i]);
	free(dirList);

	return 0;
}

/**
  daemon 프로세스가 메인처럼 사용하는 함수
  */
void daemon_main() {
	char buf[BUF_LEN];
	struct stat statbuf;
	struct file *file;

	root.is_visited = 1;
	strcpy(root.filepath, DIRECTORY);
	openlog("[SSUMonitor]", LOG_PID, LOG_LPR);

	syslog(LOG_DEBUG, "%d\n", getpid());
	
	if ((fp = fopen("log.txt", "a")) == NULL) {
		syslog(LOG_ERR, "log.txt open error %m\n");
		exit(1);
	}

	while (1) {
		// 먼저 실제 디스크 파일을 관찰
		observe(&root, DIRECTORY);
		syslog(LOG_INFO, "observing is finished");

		// 최초 loop이 아닌 경우
		// 최초 loop인경우에는 그냥 observe 만 하여 리스트를 구축하기만 하고, 검증 과정은 생략함
		if (!is_init) {
			syslog(LOG_DEBUG, "before traversal");

			// 리스트를 모두 순회하면서
			file = traversal(&root);
 
			while (file != NULL) {
				// 리스트 상에는 있지만 실제 디스크 순회 시 방문하지 못한 파일인 경우
				// 파일이 삭제되었음을 의미
				if (!file->is_visited) {
					syslog(LOG_DEBUG, "%s is not visited", file->filepath);
					log_write(DELETE, file->filepath);

					// 자식 노드들 먼저 삭제하고
					delete(file->first_child);
					struct file *tmp = file;

					// 다음 순회 대상 지정 후
					file = traversal(NULL);

					// 삭제
					delete(tmp);
					continue;
				}

				// 순회는 했으나 mtime 이 바뀌어서 파일이 변경되었음을 감지한 경우
				stat(file->filepath, &statbuf);
				if (file->stat.st_mtime != statbuf.st_mtime) {
					log_write(MODIFY, file->filepath);
					file->stat = statbuf;
				}
				
				// visit 초기화
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

	if (init() < 0) {
		fprintf(stderr, "init failed\n");
		exit(1);
	}
	
	daemon_main();
	
	// 종료메시지를 터미널에 log.txt 에 출력하는건가?
	exit(0);
}
