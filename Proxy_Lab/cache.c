#include "./cache.h"

cache_block cache[CAHCE_OBJECT_NUM];

int LRU(void)
{
    int lru = MININT;
    int eviction = 0;
    for (int i = 0; i < CAHCE_OBJECT_NUM; i++)
    {
        BeforeReadSem(i);
        if (!cache[i].valid)
        {
            AfterReadSem(i);
            return i;
        }

        if (cache[i].lru > lru)
        {
            lru = cache[i].lru;
            eviction = i;
        }
        AfterReadSem(i);
    }
    return eviction;
}

void InitCache()
{
    for (int i = 0; i < CAHCE_OBJECT_NUM; i++)
    {
        cache[i].url[0] = '\0';
        cache[i].obj[0] = '\0';
        cache[i].valid = 0;
        cache[i].lru = 0;
        cache[i].reader_cnt = 0;
        Sem_init(&cache[i].rmutex, 0, 1);
        Sem_init(&cache[i].wmutex, 0, 1);
    }
}

int ReadCache(char *url, int connfd)
{
    for (int i = 0; i < CAHCE_OBJECT_NUM; i++)
    {
        BeforeReadSem(i);
        if (cache[i].valid && (!strcmp(cache[i].url, url)))
        {
            Rio_writen(connfd, cache[i].obj,strlen(cache[i].obj));
            AfterReadSem(i);
            return 1;
        }
        AfterReadSem(i);
    }
    return 0;
}

void WriteCache(char *url, char *obj)
{
    /* update lru */
    for (int i = 0; i < CAHCE_OBJECT_NUM; i++)
    {
        BeforWriteSem(i);
        cache[i].lru++;
        AfterWriteSem(i);
    }
    int index = LRU();
    BeforWriteSem(index);
    memcpy(cache[index].url, url, MAXLINE);
    memcpy(cache[index].obj, obj, MAX_OBJECT_SIZE);
    cache[index].valid = 1;
    cache[index].lru = 0;
    AfterWriteSem(index);
}

void BeforeReadSem(int i)
{
    P(&cache[i].rmutex);
    cache[i].reader_cnt++;
    if(cache[i].reader_cnt == 1){
        P(&cache[i].wmutex);
    }
    V(&cache[i].rmutex);
}

void AfterReadSem(int i)
{
    P(&cache[i].rmutex);
    cache[i].reader_cnt--;
    if(cache[i].reader_cnt == 0){
        V(&cache[i].wmutex);
    }
    V(&cache[i].rmutex);
}
void BeforWriteSem(int i)
{
    P(&cache[i].wmutex);
}
void AfterWriteSem(int i)
{
    V(&cache[i].wmutex);
}