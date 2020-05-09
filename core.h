#ifndef H_CORE
#define H_CORE 1
#include <dirent.h>

#define TRASH "trash"
#define TRASH_FILES "trash/files"
#define TRASH_INFO "trash/info"
#define MAX_TRASH_INFO_SIZE (1024 * 2)
#define TIME_FORMAT "%Y-%m-%d %H:%M:%S"
#define TIME_FORMAT_LEN 19
#define DELETE_INTERVAL 1 
#define TAB_SIZE 21
#define BUF_LEN 1024
#define DIRECTORY "dir"
#define REL_DIRECTORY "./dir"

struct deletion_node {
	char filepath[BUF_LEN];
	time_t endTime;
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
