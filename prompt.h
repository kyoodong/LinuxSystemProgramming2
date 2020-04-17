#ifndef H_PROMPT
#define H_PROMPT 1 

#define STUDENT_ID "20162489"
#define BUF_LEN 1024

// 명령어
#define DELETE "delete"
#define SIZE "size"
#define RECOVER "recover"
#define TREE "tree"
#define EXIT "exit"
#define HELP "help"


int printPrompt();
int processCommand(char *commandStr);
void processDelete(char *paramStr);
int getArg(char* paramStr, char *argv[10]);

#endif
