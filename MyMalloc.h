typedef struct ObjectHeader {
      size_t _objectSize;     
      int _leftObjectSize;        
      int _allocated;     
      struct ObjectHeader *_listNext;
      struct ObjectHeader *_listPrev;
  } ObjectHeader;
 
  size_t _heapSize;     
  void *_memStart;
  int _initialized;
  int _verbose;
  int _mallocCalls;
  int _freeCalls; 
  int _reallocCalls;
  int _callocCalls;
  
  ObjectHeader *_freeList;
  ObjectHeader _freeListSentinel;
  
  void initialize();
  void * allocateObject(size_t size);
  void freeObject(void *ptr);
  size_t objectSize(void *ptr);
  void atExitHandler();
  void print();
  void print_list(); 
  int selectAlloc(ObjectHeader* o, size_t roundedSize, ObjectHeader* temp);
  void * getMemoryFromOS(size_t size);
