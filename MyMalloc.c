 #include <stdlib.h>
 #include <string.h>
 #include <stdio.h>
 #include <unistd.h>
 #include <sys/mman.h>
 #include <pthread.h>
 #include "MyMalloc.h"

 int i = 0;

 static pthread_mutex_t mutex;

 const int arenaSize = 2097152;

 void increaseMallocCalls()  { _mallocCalls++; }

 void increaseReallocCalls() { _reallocCalls++; }

 void increaseCallocCalls()  { _callocCalls++; }

 void increaseFreeCalls()    { _freeCalls++; }

 extern void atExitHandlerInC()
 {
     atExitHandler();
 }

 /*
 * Initial setup of allocator. First chunk is retrieved from the OS,
 * and the fence posts and freeList are initialized.
 */
 void initialize()
 {
     // Environment var VERBOSE prints stats at end and turns on debugging
     // Default is on
     _verbose = 1;
     const char *envverbose = getenv("MALLOCVERBOSE");
     if (envverbose && !strcmp(envverbose, "NO")) {
         _verbose = 0;
     }

     pthread_mutex_init(&mutex, NULL);
     void *_mem = getMemoryFromOS(arenaSize);
 
     // In verbose mode register also printing statistics at exit
     atexit(atExitHandlerInC);
     // establish fence posts
     ObjectHeader * fencePostHead = (ObjectHeader *)_mem;
     fencePostHead->_allocated = 1;
     fencePostHead->_objectSize = 0;

     char *temp = (char *)_mem + arenaSize - sizeof(ObjectHeader);
     ObjectHeader * fencePostFoot = (ObjectHeader *)temp;
     fencePostFoot->_allocated = 1;
     fencePostFoot->_objectSize = 0;

     // Set up the sentinel as the start of the freeList
     _freeList = &_freeListSentinel;

     // Initialize the list to point to the _mem
     temp = (char *)_mem + sizeof(ObjectHeader);
     ObjectHeader *currentHeader = (ObjectHeader *)temp;
     currentHeader->_objectSize = arenaSize - (2*sizeof(ObjectHeader)); // ~2MB
     currentHeader->_leftObjectSize = 0;
     currentHeader->_allocated = 0;
     currentHeader->_listNext = _freeList;
     currentHeader->_listPrev = _freeList;
     _freeList->_listNext = currentHeader;
     _freeList->_listPrev = currentHeader;

     // Set the start of the allocated memory
     _memStart = (char *)currentHeader;

     _initialized = 1;
 }

 /*
  * TODO: In allocateObject you will handle retrieving new memory for the malloc
  * request. The current implementation simply pulls from the OS for every
  * request.
  *
  * @param: amount of memory requested
  * @return: pointer to start of useable memory
  */
 void * allocateObject(size_t size)
 {
     // Make sure that allocator is initialized
     if (!_initialized)
         initialize();

     size_t roundedSize = (size + sizeof(ObjectHeader) + 7) & ~7;
     void *_mem = NULL;
     ObjectHeader *o = NULL;
     ObjectHeader *temp = _freeList;
     int allocd = 0;

     //Loops through the Free List, allocating in the first largest space available and spliting if necessary
     for(temp = temp->_listNext; temp != _freeList; temp = temp->_listNext) {
         if(temp->_objectSize > roundedSize + 7 + sizeof(ObjectHeader)) {
                 int sizeT = (int)(temp->_objectSize - roundedSize);
                 char* tempT = (char*) temp +  sizeT;
                 o = (ObjectHeader*) tempT;
                 o->_allocated = 1;
                 o->_objectSize = roundedSize;
                 o->_leftObjectSize = sizeT;
                 temp->_objectSize = sizeT;
                 ObjectHeader* temp2 = (ObjectHeader*)(tempT+roundedSize);
                 temp2->_leftObjectSize = roundedSize;
                 o->_listNext = NULL;
                 o->_listPrev = NULL;
                 allocd = 1;
                 break;
         }
         else if(temp->_objectSize >= roundedSize) {
                 o = temp;
                 o->_listNext->_listPrev = o->_listPrev;
                 o->_listPrev->_listNext = o->_listNext;
                 o->_allocated = 1;
                 o->_objectSize = size;
                 ObjectHeader* temp2 = (ObjectHeader*)(temp+roundedSize);
                 temp2->_leftObjectSize = roundedSize;
                 o->_listNext = NULL;
                 o->_listPrev = NULL;
                 allocd = 1;
                 break;
         }
     }

     //If no space is large enough, gets memory from the OS and adds it to the top of the heap, initializing the pointers as needed
     if(allocd == 0) {
         _mem = getMemoryFromOS(arenaSize);
         ObjectHeader* oldFenceHead = (ObjectHeader*) ((char*)_freeList->_listNext + arenaSize - sizeof(ObjectHeader));

         ObjectHeader* newFenceFoot = (ObjectHeader*) ((char*)oldFenceHead + arenaSize - sizeof(ObjectHeader));
         newFenceFoot->_allocated = 1;
         newFenceFoot->_objectSize = 0;

         ObjectHeader* firstHeader = (ObjectHeader*) ((char*)oldFenceHead + sizeof(ObjectHeader));
         firstHeader->_objectSize = arenaSize - 2*sizeof(ObjectHeader);
         firstHeader->_leftObjectSize = 0;
         firstHeader->_allocated = 0;
         firstHeader->_listPrev = _freeList;
         firstHeader->_listNext = _freeList->_listNext;
         _freeList->_listNext->_listPrev = firstHeader;
         _freeList->_listNext = firstHeader;

         if(firstHeader->_objectSize > roundedSize + 7 + sizeof(ObjectHeader)) {
                 int sizeT = (int) (firstHeader->_objectSize - roundedSize);
                 char* tempT = (char*) firstHeader + sizeT;
                 o = (ObjectHeader*) tempT;
                 o->_allocated = 1;
                 o->_objectSize = roundedSize;
                 o->_leftObjectSize = sizeT;
                 firstHeader->_objectSize = sizeT;
                 newFenceFoot->_leftObjectSize = roundedSize;
                 o->_listNext = NULL;
                 o->_listPrev = NULL;
         }
         else if(firstHeader->_objectSize >= roundedSize) {
                 o = firstHeader;
                 o->_listNext->_listPrev = o->_listPrev;
                 o->_listPrev->_listNext = o->_listNext;
                 o->_allocated = 1;
                 o->_objectSize = size;
                 //o->_leftObjectSize = ;
                 ObjectHeader* temp2 = (ObjectHeader*)(firstHeader+roundedSize);
                 temp2->_leftObjectSize = roundedSize;
                 o->_listNext = NULL;
                 o->_listPrev = NULL;
         }
    }

    //Unlocks threads
    pthread_mutex_unlock(&mutex);

     // Return a pointer to useable memory
    return (void *)((char*)o+sizeof(ObjectHeader));
 }

 /*
  * TODO: In freeObject you will implement returning memory back to the free
  * list, and coalescing the block with surrounding free blocks if possible.
  *
  * @param: pointer to the beginning of the block to be returned
  * Note: ptr points to beginning of useable memory, not the block's header
  */
 void freeObject(void *ptr)
 {
     //Sets ptr back to the header
     ptr -= sizeof(ObjectHeader);

     //Pointers necessary for freeing
     ObjectHeader* o = (ObjectHeader*) ptr;
     char* tempR = (char*) ptr + o->_objectSize;
     char* tempL = (char*) ptr - o->_leftObjectSize;
     ObjectHeader* right = (ObjectHeader*) tempR;
     ObjectHeader* left = (ObjectHeader*) tempL;
     o->_allocated = 0;

     //If both right and left are free
     if(right->_allocated == 0 && left->_allocated == 0) {
         left->_objectSize += o->_objectSize + right->_objectSize;
         ObjectHeader* temp = (ObjectHeader*)(tempR + right->_objectSize);
         temp->_leftObjectSize = left->_objectSize;
         if(right->_listNext != left) {
                 right->_listNext->_listPrev = left;
                 left->_listNext = right->_listNext;
         }
         else {
                 right->_listPrev->_listNext = left;
                 left->_listPrev = right->_listPrev;
         }
         right->_listNext = NULL;
         right->_listPrev = NULL;
     }
     //If only right is free
     else if(right->_allocated == 0) {
         o->_objectSize += right->_objectSize;
         ObjectHeader* temp = (ObjectHeader*)(tempR + right->_objectSize);
         temp->_leftObjectSize = o->_objectSize;
         o->_listNext = right->_listNext;
         o->_listPrev = right->_listPrev;
         right->_listPrev->_listNext = o;
         right->_listNext->_listPrev = o;
         right->_listNext = NULL;
         right->_listPrev = NULL;
     }
     //If only left is free
     else if(left->_allocated == 0) {
         left->_objectSize += o->_objectSize;
         right->_leftObjectSize = left->_objectSize;
     }
     //If neither are free
     else {
         _freeList->_listNext->_listPrev = o;
         o->_listPrev = _freeList;
         o->_listNext = _freeList->_listNext;
         _freeList->_listNext = o;
     }

     //Unlocks threads
     pthread_mutex_unlock(&mutex);
 }

 /*
  * Prints the current state of the heap.
  */
 void print()
 {
     printf("\n-------------------\n");

     printf("HeapSize:\t%zd bytes\n", _heapSize );
     printf("# mallocs:\t%d\n", _mallocCalls );
     printf("# reallocs:\t%d\n", _reallocCalls );
     printf("# callocs:\t%d\n", _callocCalls );
     printf("# frees:\t%d\n", _freeCalls );

     printf("\n-------------------\n");
 }

 /*
  * Prints the current state of the freeList
  */
 void print_list() {
     printf("FreeList: ");
     if (!_initialized)
         initialize();

     ObjectHeader * ptr = _freeList->_listNext;

     while (ptr != _freeList) {
         long offset = (long)ptr - (long)_memStart;
         printf("[offset:%ld,size:%zd]", offset, ptr->_objectSize);
         ptr = ptr->_listNext;
         if (ptr != NULL)
             printf("->");
     }
     printf("\n");
 }

 /*
  * This function employs the actual system call, sbrk, that retrieves memory
  * from the OS.
  *
  * @param: the chunk size that is requested from the OS
  * @return: pointer to the beginning of the chunk retrieved from the OS
  */
 void * getMemoryFromOS(size_t size)
 {
     _heapSize += size;

     // Use sbrk() to get memory from OS
     void *_mem = sbrk(size);

     // if the list hasn't been initialized, initialize memStart to mem
     if (!_initialized)
         _memStart = _mem;

    return _mem;
 }

 void atExitHandler()
{
     // Print statistics when exit
     if (_verbose)
         print();
 }

 /*
  * C interface
  */

extern void * malloc(size_t size)
 {
    pthread_mutex_lock(&mutex);
     increaseMallocCalls();

     return allocateObject(size);
 }

 extern void free(void *ptr)
 {
     pthread_mutex_lock(&mutex);
     increaseFreeCalls();

     if (ptr == 0) {
         // No object to free
         pthread_mutex_unlock(&mutex);
         return;
     }

     freeObject(ptr);
 }

 extern void * realloc(void *ptr, size_t size)
 {
     pthread_mutex_lock(&mutex);
     increaseReallocCalls();

     // Allocate new object
     void *newptr = allocateObject(size);

     // Copy old object only if ptr != 0
     if (ptr != 0) {

         // copy only the minimum number of bytes
         ObjectHeader* hdr = (ObjectHeader *)((char *) ptr - sizeof(ObjectHeader));
         size_t sizeToCopy =  hdr->_objectSize;
         if (sizeToCopy > size)
             sizeToCopy = size;

         memcpy(newptr, ptr, sizeToCopy);

         //Free old object
         freeObject(ptr);
     }

     return newptr;
 }

 extern void * calloc(size_t nelem, size_t elsize)
 {
     pthread_mutex_lock(&mutex);
     increaseCallocCalls();

     // calloc allocates and initializes
     size_t size = nelem *elsize;

     void *ptr = allocateObject(size);

     if (ptr) {
         // No error; initialize chunk with 0s
         memset(ptr, 0, size);
     }

     return ptr;
 }

