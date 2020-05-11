#ifndef H_CORE
#define H_CORE 1
#include <dirent.h>

#define TRASH "trash"
#define TRASH_FILES "trash/files"
#define TRASH_INFO "trash/info"
#define MAX_TRASH_INFO_SIZE (1024 * 2)
#define TIME_FORMAT "%Y-%m-%d %H:%M:%S"
#define DELETE_INTERVAL 5
#define TAB_SIZE 21
#define BUF_LEN 1024
#define DIRECTORY "dir"

struct deletion_node {
	char filepath[BUF_LEN];
	char endTime[50];
	int rOption;
	int iOption;
	struct deletion_node *next, *prev;
};

struct info_node {
	char filepath[BUF_LEN];
	char modificationTime[30];
	char deletionTime[30];
	struct info_node *next, *prev;
};

int readTrashInfo(FILE *fp, struct info_node *infoNode);
int getSize(const char *dirpath);
int removeDir(const char *filepath);
void removeInfoNodeByName(const char* name);
void removeInfoNode(struct info_node *node);

void* deleteThread();
void removeDeletionNode(struct deletion_node *node);
void insertDeletionNode(struct deletion_node *node);
int deleteFile(const char *filename, const char *endDate, const char *endTime, int iOption, int rOption); 

int init();
int sendToTrash(const char *filepath);
int deleteFile(const char *filepath, const char *endDate, const char *endTime, int iOption, int rOption);
int printSize(const char *filepath, int dOption);
int recoverFile(const char *filepath, int lOption);
void clearInfoList();
int printTree();
int filterHiddenFile(const struct dirent *dir); 
int ignoreParentAndSelfDirFilter(const struct dirent *dir);
#endif
