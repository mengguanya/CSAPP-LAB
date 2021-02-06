/* 
 * 1. 动态内存分配策略：空闲块分离存储(Segregated Storage)：（分离适配（Segregated fit） + 首次适配搜索(first fit search)）。分离适配可以理解为显式空闲表的推广，空闲块的组织由一条链推广到多条链，本质上只需要增加空闲块根据块大小的分类机制。分离适配由于把内存大小映射到了多条链，所以内存分配时间会降低常数倍。采取首次适配搜索策略的原因是，CSAPP(3th)P605提到“对分离空闲链表的简单的首次适配搜索，其内存利用率接近于对整个堆的最佳适配搜索的内存利用率。”
 * 2. 空闲链表组织策略：本算法根据2的幂来划分等价类大小，共计分了14个等价类，即{1}， {2}， {3，4}， {5~8}， {9~16}， {17~32}， {33~64}， {65~128}， {129~256}， {257~512}， {513~1024}， {1025~2048}， {2049~4096}， {4096~无穷}。 属于每个等价类大小的空闲块被存储在一个空闲链表中，并按照块大小的升序排序，空闲链表的表头存储在堆（heap）的最前面。
 * 3. 空闲块组织策略： 每一个空闲链表中的空闲块组织方式采取与显式空闲链表（explicit free block list)相同的策略，最小大小为4字（16bytes）,即包括头部（header）,尾部（footer）、前驱(pred), 后继（succ），头尾部内容相同，都存储着块大小和空闲位（最后3bit），前驱和后继分别存储着本等价类（本空闲链表中）的前驱和后继空闲块的指针。第一个空闲块的前驱指针指向header， 最后一个空闲块的后继指针指向NULL
 * 4. 空闲块分配策略（malloc函数）：（1） 确定请求所在的大小类，并映射到特定的空闲链表头；（2） 对空闲链表进行首次适配搜索（命中条件是空闲块大小>=请求大小），如果命中，执行步骤三，否则执行步骤四。（3）分割命中的空闲块，并把剩余的部分插入到适当的空闲链表，空闲块分配结束。（4） 继续搜索更大的大小类的空闲链表，如果命中，执行步骤三，反之执行步骤五。（5）如果在已有的空闲链表中没有命中空闲块，那么向操作系统请求额外的堆内存，从这个新分配堆内存中分配一个块，并把剩余部分插入到适当的大小类中，并执行合并策略,结束。
 * 5. 空闲块释放策略(free函数)：要释放一个空闲块，则执行合并策略，并把合并后的块放置到相应的空闲链表中。
 * 6. 空闲块重新分配策略（realloc函数）: （1） 如果原指针为NULL，则执行malloc, 结束。（2）如果大小为0，则执行free，结束。（3） 执行malloc，并把原内存数据copy到重新分配后的内存处，之后释放原指针处的块，结束。   
*/

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "mgy",
    /* First member's full name */
    "mgy",
    /* First member's email address */
    "mguanya@163.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

#define WSIZE 4             /*word size*/
#define DSIZE 8             /*Double word size*/
#define CHUNKSIZE (1 << 12) /*the page size in bytes is 4K*/
#define MINFB_SIZE 2

#define MAX(x, y) ((x) > (y) ? (x) : (y))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p) (*(unsigned int *)(p))              /*get value of 4 sizes memory */
#define PUT(p, val) (*(unsigned int *)(p) = (val)) /*put val into 4 sizes memory */

/* Read and write a pointer at address p */
#define GET_PTR(p) ((unsigned int *)(long)(GET(p))) /*get value of 4 sizes memory */
#define PUT_PTR(p, val) PUT(p, (long)val)           /*put val into 4 sizes memory */

/* Read the size and allocated fields from address p */
#define GET_SIZE(p) (GET(p) & ~0x7) /*size of block*/
#define GET_ALLOC(p) (GET(p) & 0x1) /*block is alloc?*/

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp) ((char *)(bp)-WSIZE)                        /*head of block*/
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) /*foot of block*/

#define PRED(bp) (((char *)(bp) + WSIZE)) /* 前驱块 */

/*Given free block ptr bp ,compute addredss of next ane previous free block */
#define PREV_LINKNODE_RP(bp) ((char *)(bp))         /*prev free block*/
#define NEXT_LINKNODE_RP(bp) ((char *)(bp) + WSIZE) /*next free block*/

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp)-WSIZE))) /*next block*/
#define PREV_BLKP(bp) ((char *)(bp)-GET_SIZE(((char *)(bp)-DSIZE)))   /*prev block*/

/*additional define*/
#define LISTS_NUM 14 //空闲链表的数量

void *heap_listp;

/**/
static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);

static void *remove_free_block(void *bp);
static void *add_free_block(void *bp);

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    void *first_free_block;
    /*
    printf("mm_init begin\n");
    */
    /*Create the initial empty heap*/
    if ((heap_listp = mem_sbrk((LISTS_NUM + 4) * WSIZE)) == (void *)-1)
    {
        return -1;
    }
    PUT(heap_listp + (0 + LISTS_NUM) * WSIZE, 0);              /*alignment padding*/
    PUT(heap_listp + (1 + LISTS_NUM) * WSIZE, PACK(DSIZE, 1)); /*prologue header*/
    PUT(heap_listp + (2 + LISTS_NUM) * WSIZE, PACK(DSIZE, 1)); /*prologue footer*/
    PUT(heap_listp + (3 + LISTS_NUM) * WSIZE, PACK(0, 1));     /*epilogue header*/

    for (int i = 0; i < LISTS_NUM; i++)
    {
        PUT_PTR(heap_listp + WSIZE * i, NULL);
    }
    /*
    printf("extend_heap begin\n");
*/
    /* Extend the empty heap with a free block of CHUNKSIZE bytes */
    if ((first_free_block = extend_heap(CHUNKSIZE / WSIZE)) == NULL)
    {
        return -1;
    }

    /*
    printf("free list: %p\n", heap_listp); 
    */
    return 0;
}

static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;

    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

    /*
    printf("add free block begin\n");
    */
    add_free_block(bp);
    return coalesce(bp);
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t asize;      /* Adjusted block size */
    size_t extendsize; /* Amount to extend heap if not fit */
    char *bp;
    /* Ignore spurious requests */
    if (0 == size)
    {
        return NULL;
    }

    /* Adjust block size to include overhead and alignment reqs */
    if (size <= DSIZE)
    {
        asize = 3 * DSIZE;
    }
    else
    {
        asize = DSIZE * ((size + (DSIZE + DSIZE) + (DSIZE - 1)) / DSIZE);
    }

    /* Search the free list for a fit free block */
    if ((bp = find_fit(asize)) != NULL)
    {
        place(bp, asize);
        return bp;
    }

    /* Not fit found. Get more memory and place the block */
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
    {
        return NULL;
    }
    place(bp, asize);
    return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *bp)
{
    void *bp_new = coalesce(bp);
    add_free_block(bp_new);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;

    newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;
    copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    if (size < copySize)
        copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}

static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(HDRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc)
    {
    }

    else if (prev_alloc && !next_alloc)
    {
        /*把下一块从空闲链表中删除*/
        remove_free_block(NEXT_BLKP(bp));

        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
    }
    else if (!prev_alloc && next_alloc)
    {
        /*把前一块从空闲链表中删除*/
        remove_free_block(PREV_BLKP(bp));

        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    else
    {
        /*把下一块从空闲链表中删除*/
        remove_free_block(NEXT_BLKP(bp));

        /*把前一块从空闲链表中删除*/
        remove_free_block(PREV_BLKP(bp));

        size += GET_SIZE(HDRP(NEXT_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    return bp;
}

void *find_list(size_t asize)
{
    for (int i = 0; i < LISTS_NUM; i++)
    {
        if ((asize = asize >> 1) == 0)
        {
            return (void *)((int *)(heap_listp) + i);
        }
    }
    return (void *)((int *)(heap_listp) + LISTS_NUM - 1);
}

static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));
    if ((csize - asize) >= (3 * DSIZE))
    {
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize - asize, 0));
        PUT(FTRP(bp), PACK(csize - asize, 0));

        add_free_block(bp);
    }
    else
    {
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
}

void *find_block(size_t asize, void *ptr)
{
    void *bp = ptr;
    while (GET_PTR(bp) != NULL)
    {
        if (GET_SIZE(HDRP(bp)) >= asize)
        {
            return bp;
        }
        bp = GET_PTR(bp);
    }
    return NULL;
}

static void *find_fit(size_t asize)
{
    void *bp;
    void *list = find_list(asize);
    while (GET_PTR(list) != NULL)
    {
        if ((bp = find_block(asize, (int *)list + 1)) != NULL)
        {
            return bp;
        }
    }
    return NULL;
}

static void *remove_free_block(void *bp)
{
    if (bp == NULL)
    {
        return NULL;
    }
    void *pre = PRED(bp);
    void *next = bp;

    /*设置它前驱的后继为它的后继*/
    PUT_PTR(pre, next);

    /*如果后继是不NULL，那么就可以设置他的前驱*/
    if (next != NULL)
    {
        PUT_PTR(PRED(next), pre);
    }
    return pre;
}

static void *add_free_block(void *bp)
{
    if (bp == NULL)
    {
        return NULL;
    }
    void *next = find_list(GET_SIZE(HDRP(bp)));
    void *pre = next;
    while (GET_PTR(next) != NULL)
    {

        if (GET_SIZE(HDRP(next)) < GET_SIZE(HDRP(bp)))
        {
            /*pre <- next, next <- *(next)*/

            pre = next;
            next = GET_PTR(next);
        }
        else
        {
            /*把bp的地址写进next前面的那个可用块第一个位置，同理，把next的地址写进next的可用块第一个位置*/
            PUT_PTR(pre, bp);
            PUT_PTR(bp, next);

            PUT_PTR(PRED(bp), pre);
            PUT_PTR(PRED(next), bp);

            /*标志设置为空闲， 大小不变*/
            PUT(HDRP(bp), PACK(GET_SIZE(HDRP(bp)), 0));
            PUT(FTRP(bp), PACK(GET_SIZE(FTRP(bp)), 0));
            return pre;
        }
    }

    /*把bp的地址写进next前面的那个可用块第一个位置，同理，把next的地址写进bp的可用块第一个位置*/
    PUT_PTR(pre, bp);

    PUT_PTR(bp, next);

    PUT_PTR(PRED(bp), pre);

    /*标志设置为空闲， 大小不变*/
    PUT(HDRP(bp), PACK(GET_SIZE(HDRP(bp)), 0));
    PUT(FTRP(bp), PACK(GET_SIZE(FTRP(bp)), 0));
    return bp;
}

int mm_check(void)
{
    int *p = heap_listp;
    for (int i = 0; i < LISTS_NUM; i++)
    {
        if(p == NULL)
            continue;
        printf("list %d:\n",i);
        while(p != NULL){
            printf("%d  ", GET_SIZE(HDRP(p)));
            p = GET_PTR(p);
        }
        printf("\n");
    }
}