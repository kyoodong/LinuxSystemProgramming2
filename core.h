#ifndef H_CORE
#define H_CORE 1

#define TRASH "trash"
#define TRASH_FILES "trash/files"
#define TRASH_INFO "trash/info"
#define BASE_DIR "dir"
#define MAX_TRASH_INFO_SIZE (1024 * 2)
#define TIME_FORMAT "%Y-%m-%d %H:%M:%S"

int deleteFile(const char *filename, const char *endDate, const char *endTime, int iOption, int rOption); 

int init();
int sendToTrash(const char *filepath);
int deleteFile(const char *filepath, const char *endDate, const char *endTime, int iOption, int rOption);
int writeLog(const char* filepath);
#endif
