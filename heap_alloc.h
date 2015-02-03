#ifndef HEAP_ALLOC_H
#define HEAP_ALLOC_H
#include <assert.h>
#include <string.h>
#include <new>
#include <malloc.h>
#include "data_types.h"
#include "intrusive_list.h"
#include "rbtree.h"
#include "ptr_bitset.h"
#include "mutex.h"

#define g_allocator shark::HeapAllocator::getInstance()
#define heap_alloc(size) 			g_allocator->alloc(size, __FILE__, __LINE__)
#define heap_alloc_align(size, align)	g_allocator->alloc(size, align, __FILE__, __LINE__)
#define heap_realloc(ptr, size) 		g_allocator->realloc(ptr, size, __FILE__, __LINE__)
#define heap_realloc_align(ptr, size, align) g_allocator->realloc(ptr, size, align, __FILE__, __LINE__)
#define heap_free(ptr)				{if(ptr){g_allocator->free(ptr); (ptr)=0;}}
#define heap_report()				g_allocator->report()				

namespace shark
{

#ifdef _DEBUG
#define DEBUG_ALLOCATOR
#define DEBUG_MULTI_RBTREE
#endif

#define MULTITHREADED

// 系统虚拟页面大小:64KB
const size_t VIRTUAL_PAGE_SIZE_LOG2 = 16;
const size_t VIRTUAL_PAGE_SIZE  = (size_t)1 << VIRTUAL_PAGE_SIZE_LOG2;

//////////////////////////////////////////////////////////////////////////
template<class T> inline T round_down(T x, size_t a) {return x & -(int)a;}
template<class T> inline T round_up(T x, size_t a) {return (x + (a-1)) & -(int)a;}
template<class T> inline T* align_down(T* p, size_t a) {return (T*)((size_t)p & -(int)a);}
template<class T> inline T* align_up(T* p, size_t a) {return (T*)(((size_t)p + (a-1)) & -(int)a);}

inline void* system_alloc(size_t size) {
	assert(size / VIRTUAL_PAGE_SIZE * VIRTUAL_PAGE_SIZE == size);
	return memalign(VIRTUAL_PAGE_SIZE, size);
}

inline void system_free(void* addr) {
	free(addr);
}

enum ALIGNMENT
{
	ALIGN_NONE = 0,
	DEFAULT_ALIGNMENT	= 8,
	ALIGN_16			= 16,
	ALING_32			=32
};

enum MEM_CONSTANTS
{
	MAX_CALL_DEPTH		= 16,	// depth of the recorded callstack
	MAX_CLIENT_FILENAME    = 32,	// number of letters recorded for the client filename
	MAX_FILEPATH = 128,
	PATTERN_SIZE = 32,
};

enum MEM_PATTERNS
{
	PRE_PATTERN	= 0xab,	// memory pattern written ahead of allocated client space (GAIA_MEM_DEBUG only)	
	POST_PATTERN	= 0xef,	// memory pattern written after allocated client space (GAIA_MEM_DEBUG only)
};

struct sMemoryBlockHeader
{
	uint32	actualSize : 24;	// the true size of the allocation
	uint32	pointerOffset : 8;	// an offset to the top of the allocation
};

struct sPreBufferData
{
	sPreBufferData* nextHeader;			
	sPreBufferData* previousHeader;
	uint32		requestedSize;		
	uint32		userChecksum;
	uint32		fileLine;
	char			fileName[MAX_FILEPATH];
	uint8		bytePattern[PATTERN_SIZE];
	uint8   		alignment;		
	uint8		debug_source;
};

struct sPostBufferData
{
	uint8 bytePattern[PATTERN_SIZE];
};

class HeapAllocator{
	HeapAllocator();
	HeapAllocator(const HeapAllocator&);
	HeapAllocator& operator=(const HeapAllocator&);
	static HeapAllocator* allocator;
	//桶系统定义
	static const uint32 MIN_ALLOCATION_LOG2 = 3UL;
	static const uint32 MIN_ALLOCATION  = 1UL << MIN_ALLOCATION_LOG2; 
	static const uint32 MAX_SMALL_ALLOCATION_LOG2 = 8UL;
	static const uint32 MAX_SMALL_ALLOCATION  = 1UL << MAX_SMALL_ALLOCATION_LOG2;
	static const uint32 PAGE_SIZE_LOG2  = VIRTUAL_PAGE_SIZE_LOG2;
	static const uint32 PAGE_SIZE  = 1UL << PAGE_SIZE_LOG2;
	static const uint32 NUM_BUCKETS  = (MAX_SMALL_ALLOCATION / MIN_ALLOCATION);
	static const uint32 DEBUG_EXTRA_INFO_SIZE;
	
	static inline bool is_small_allocation(size_t s) {
		return s + DEBUG_EXTRA_INFO_SIZE <= MAX_SMALL_ALLOCATION;
	}
	static inline size_t clamp_small_allocation(size_t s) {
		return (s + DEBUG_EXTRA_INFO_SIZE < MIN_ALLOCATION) ? MIN_ALLOCATION - DEBUG_EXTRA_INFO_SIZE : s;
	}
	static inline unsigned bucket_spacing_function(size_t size) { 
		// 根据需要的块大小，计算出所在的桶序号
		return (unsigned)((size + (MIN_ALLOCATION-1)) >> MIN_ALLOCATION_LOG2) - 1;
	}
	static inline unsigned bucket_spacing_function_aligned(size_t size) { 
		// 根据需要的块大小，计算出所在的桶序号(向下圆整)
		return (unsigned)(size >> MIN_ALLOCATION_LOG2) - 1;
	}
	static inline size_t bucket_spacing_function_inverse(unsigned index) { 
		//根据桶序号来推算桶中数据块的大小
		return (size_t)(index + 1) << MIN_ALLOCATION_LOG2;
	}
	static sMemoryBlockHeader* getMemoryBlockHeader(void* pClientMem)
	{
		#ifdef DEBUG_ALLOCATOR
		return (sMemoryBlockHeader*)((char*)pClientMem - s_preBufferSize - s_blockHeadSize);
		#else
		return (sMemoryBlockHeader*)pClientMem;
		#endif
	}
	static sPreBufferData* getPreBufferData(void* pClientMem)
	{
		#ifdef DEBUG_ALLOCATOR
		char* header = (char*)getMemoryBlockHeader(pClientMem);
		return (sPreBufferData*)(header + s_blockHeadSize);
		#else
		return (sPreBufferData*)pClientMem;
		#endif
	}
	
	/* 
	 * 这里的bucket分配模型分为三个层次
	 * 最外层是bucket结构。bucket挂接在
	 * 第二层是page结构。
	 * 最内层是多个固定大小的内存块。
	 */
	struct free_link {
		free_link* mNext;
	};
	struct page : intrusive_list<page>::node {
		page(free_link* freeList, size_t elemSize, unsigned marker) 
			: mFreeList(freeList), mBucketIndex((unsigned short)bucket_spacing_function_aligned(elemSize)), mUseCount(0) {
			mMarker = marker ^ (unsigned)((size_t)this); 
		}
		free_link* mFreeList;
		unsigned short mBucketIndex;
		unsigned short mUseCount;
		unsigned mMarker;
		size_t elem_size() const {return bucket_spacing_function_inverse(mBucketIndex);}
		unsigned bucket_index() const {return mBucketIndex;}
		size_t count() const {return mUseCount;}
		bool empty() const {return mUseCount == 0;}
		void inc_ref() {mUseCount++;}
		void dec_ref() {assert(mUseCount > 0); mUseCount--;}
		bool check_marker(unsigned marker) const {return mMarker == (marker ^ (unsigned)((size_t)this));}
	};
	typedef intrusive_list<page> page_list;//页面列表，将会被放置到对应的桶中
	// 根据内存块中任意地址，可以找到保存page头部信息的位置
	static inline page* ptr_get_page(void* ptr) {
		/*
		 * 注意这里的page信息是保存在分配的page内存块的最后面
		 * 所以定位page指针的位置的时候，先将指针移动到内存块的最后位置，
		 * 然后再回移sizeof(page)大小的偏移
		 */
		return (page*)(align_down((char*)ptr, PAGE_SIZE) + (PAGE_SIZE - sizeof(page)));
	}
	/*
	 * 桶结构
	 */
	class bucket {
		bucket(const bucket&);
		bucket& operator=(const bucket&);
		
		page_list mPageList;
		#ifdef MULTITHREADED
		static const size_t SPIN_COUNT = 256;//自旋锁的自旋次数，暂时还没有用到,如果锁成为瓶颈可以使用
		mutable MutexLock mLock;	//桶粒度的锁，只针对单独的桶
		#endif
		unsigned mMarker;
		#ifdef MULTITHREADED
		unsigned char _padding[sizeof(void*)*16 - sizeof(page_list) - sizeof(MutexLock) - sizeof(unsigned)];
		#else
		unsigned char _padding[sizeof(void*)*4 - sizeof(page_list) - sizeof(unsigned)];
		#endif
		static const unsigned MARKER = 0xdeadbeef;
	public:
		bucket();
		#ifdef MULTITHREADED
		MutexLock& get_lock() const {return mLock;}
		#endif
		unsigned marker() const {return mMarker;}
		const page* page_list_begin() const {return mPageList.begin();}
		page* page_list_begin() {return mPageList.begin();}
		const page* page_list_end() const {return mPageList.end();}
		page* page_list_end() {return mPageList.end();}
		bool page_list_empty() const {return mPageList.empty();}
		void add_free_page(page* p) {mPageList.push_front(p);}
		page* get_free_page();
		void* alloc(page* p);	
		void free(page* p, void* ptr);
	};
	void* bucket_system_alloc();
	void bucket_system_free(void* ptr);
	page* bucket_grow(size_t elemSize, unsigned marker);
	void* bucket_alloc(size_t size);
	void* bucket_alloc_direct(unsigned bi);
	void* bucket_realloc(void* ptr, size_t size);
	void bucket_free(void* ptr);
	void bucket_free_direct(void* ptr, unsigned bi);
	void bucket_purge();

	//大内存块的块头部信息
	class block_header {
		enum block_flags {BL_USED = 1};//第一位表示该内存块是否已经用了
		block_header* mPrev;
		size_t mSizeAndFlags;
		unsigned char _padding[DEFAULT_ALIGNMENT <= sizeof(block_header*) + sizeof(size_t) ? 0 : DEFAULT_ALIGNMENT - sizeof(block_header*) - sizeof(size_t)];
	public:
		typedef block_header* block_ptr;
		size_t size() const {return mSizeAndFlags & ~3;}
		block_ptr next() const {return (block_ptr)((char*)mem() + size());}
		block_ptr prev() const {return mPrev;}
		void* mem() const {return (void*)((char*)this + sizeof(block_header));}
		bool used() const {return (mSizeAndFlags & BL_USED) != 0;}
		void set_used() {mSizeAndFlags |= BL_USED;}
		void set_unused() {mSizeAndFlags &= ~BL_USED;}
		void unlink() {
			next()->prev(prev());
			prev()->next(next());
		}
		void link_after(block_ptr link) {
			prev(link);
			next(link->next());
			next()->prev(this);
			prev()->next(this);
		}
		void size(size_t size) {
			assert((size & 3) == 0);
			mSizeAndFlags = (mSizeAndFlags & 3) | size;
		}
		void next(block_ptr next) {
			assert(next >= mem());
			size((char*)next - (char*)mem());
		}
		void prev(block_ptr prev) {
			mPrev = prev;
		}
	};
	static inline block_header* ptr_get_block_header(void* ptr) {
		return (block_header*)((char*)ptr - sizeof(block_header));
	}

	struct small_free_node : public intrusive_list<small_free_node>::node {};
	typedef intrusive_list<small_free_node> small_free_node_list;
	struct free_node : public intrusive_multi_rbtree<free_node>::node {
		block_header* get_block() const {return (block_header*)((char*)this - sizeof(block_header));}
		bool operator<(const free_node& rhs) const {return get_block()->size() < rhs.get_block()->size();}
		bool operator>(const free_node& rhs) const {return get_block()->size() > rhs.get_block()->size();}
		bool operator<(size_t size) const {return get_block()->size() < size;}
		bool operator>(size_t size) const {return get_block()->size() > size;}
	};
	typedef intrusive_multi_rbtree<free_node> free_node_tree;

	bool ptr_in_bucket(void* ptr) const;
	void split_block(block_header* bl, size_t size);
	block_header* shift_block(block_header* bl, size_t offs);
	block_header* coalesce_block(block_header* bl);
	void* tree_system_alloc(size_t size);
	void tree_system_free(void* ptr, size_t size);
	block_header* tree_extract(size_t size);
	block_header* tree_extract_aligned(size_t size, size_t alignment);
	block_header* tree_add_block(void* mem, size_t size);
	block_header* tree_grow(size_t size);
	void tree_attach(block_header* bl);
	void tree_detach(block_header* bl);
	void tree_purge_block(block_header* bl);
	void* tree_alloc(size_t size);
	void* tree_alloc_aligned(size_t size, size_t alignment);
	void* tree_realloc(void* ptr, size_t size);
	void* tree_realloc_aligned(void* ptr, size_t size, size_t alignment);
	size_t tree_resize(void* ptr, size_t size);
	void tree_free(void* ptr);
	void tree_purge();

	enum debug_source {DEBUG_SOURCE_BUCKETS = 0, DEBUG_SOURCE_TREE = 1};
	bucket mBuckets[NUM_BUCKETS];
	block_header* mMRFreeBlock;
	free_node_tree mFreeTree;
	small_free_node_list mSmallFreeList;
	#ifdef MULTITHREADED
	MutexLock mDebugMutex;
	MutexLock mTreeMutex;
	#endif
	#ifdef DEBUG_ALLOCATOR
	static sPreBufferData* sTopMemoryBlock;
	static uint32 sAllocAccount;
	static uint32 sReleaseAccount;
	static uint32 sTotalBytesRequested;
	static uint32 sTotalBytesInUse;
	static uint32 sMaximumBytesRequested;
	static uint32 sMaxinumBytesInUse;
	static const uint32 s_preBufferSize = sizeof(sPreBufferData);
	static const uint32 s_postBufferSize = sizeof(sPostBufferData);
	static const uint32 s_blockHeadSize = sizeof(sMemoryBlockHeader);
	#else
	static const uint32 s_preBufferSize = 0;
	static const uint32 s_postBufferSize = 0;
	static const uint32 s_blockHeadSize = 0;
	#endif
	
public:
	static HeapAllocator* getInstance()
	{
		if(allocator == NULL)
			allocator = new HeapAllocator();
		return allocator;
	}
	~HeapAllocator();
	void* alloc(size_t size, const char* filename = __FILE__, int linenum = __LINE__);
	void* alloc(size_t size, size_t alignment, const char* filename = __FILE__, int linenum = __LINE__);
	void* calloc(size_t count, size_t size);
	void* realloc(void* ptr, size_t size, const char* filename = __FILE__, int linenum = __LINE__);
	void* realloc(void* ptr, size_t size, size_t alignment, const char* filename = __FILE__, int linenum = __LINE__);
	size_t size(void* ptr) const;
	void free(void* ptr);
	void purge();
	#ifdef DEBUG_ALLOCATOR
	void* debug_alloc(void* ptr, size_t size, size_t trueSize, debug_source src, uint8 align, const char* filename, int linenum);
	void debug_realloc(void* ptr);
	void* debug_free(void* ptr);
	void debug_check(void* ptr);
	void check();
	void report();
	#endif
	
};

}

#endif