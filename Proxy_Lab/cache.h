#include "./csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE      1049000
#define MAX_OBJECT_SIZE     102400
#define CAHCE_OBJECT_NUM    10
#define MAXINT              0X3F3F3F3F
#define MININT              -0X3F3F3F3F

typedef struct{
    char url[MAXLINE];
    char obj[MAX_OBJECT_SIZE];
    int valid;
    long long lru;

    int reader_cnt; 
    sem_t rmutex;
    sem_t wmutex;
}cache_block;

void InitCache(void);
int ReadCache(char *url, int connfd);
void WriteCache(char *url, char *obj);

/* 信号量 */
void BeforeReadSem(int i);
void AfterReadSem(int i);
void BeforWriteSem(int i);
void AfterWriteSem(int i);