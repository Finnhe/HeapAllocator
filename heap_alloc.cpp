#include "data_types.h"
#include "heap_alloc.h"
#include "numeric_tools.h"
using namespace shark;

HeapAllocator* HeapAllocator::allocator = NULL;
const uint32 HeapAllocator::DEBUG_EXTRA_INFO_SIZE = HeapAllocator::s_preBufferSize + HeapAllocator::s_postBufferSize + HeapAllocator::s_blockHeadSize;
sPreBufferData* HeapAllocator::sTopMemoryBlock = 0;
uint32 HeapAllocator::sAllocAccount = 0;
uint32 HeapAllocator::sReleaseAccount = 0;
uint32 HeapAllocator::sTotalBytesRequested = 0;
uint32 HeapAllocator::sTotalBytesInUse = 0;
uint32 HeapAllocator::sMaximumBytesRequested = 0;
uint32 HeapAllocator::sMaxinumBytesInUse = 0;

HeapAllocator::page* HeapAllocator::bucket::get_free_page() {
	if (!mPageList.empty()) {
		page* p = &mPageList.front();
		if (p->mFreeList)
			return p;
	}
	return NULL;
}

void* HeapAllocator::bucket::alloc(page* p) { 	
	assert(p && p->mFreeList);
	p->inc_ref();
	free_link* free = p->mFreeList;
	free_link* next = free->mNext;
	p->mFreeList = next;
	//如果当前page已经没有空闲块，就移动到列表的最后
	if (!next) {
		p->unlink();
		mPageList.push_back(p);
	}
	return (void*)free;
}

void HeapAllocator::bucket::free(page* p, void* ptr) {
	free_link* free = p->mFreeList;
	free_link* lnk = (free_link*)ptr;
	lnk->mNext = free;
	p->mFreeList = lnk;
	p->dec_ref();
	//如果空闲块只有一个了，就移动到列表的最前端,保证每个page都能够全部使用到
	if (!free) {
		p->unlink();
		mPageList.push_front(p);
	}
}

bool HeapAllocator::ptr_in_bucket(void* ptr) const {
	bool result = false;
	page* p = ptr_get_page(ptr);
	unsigned bi = p->bucket_index();
	if (bi < NUM_BUCKETS) {
		result = p->check_marker(mBuckets[bi].marker());
		#ifdef MULTITHREADED
		ScopeLock lock(mBuckets[bi].get_lock());
		#endif
		const page* pe = mBuckets[bi].page_list_end();
		const page* pb = mBuckets[bi].page_list_begin();
		for (; pb != pe && pb != p; pb = pb->next()) {}
		assert(result == (pb == p));
	}
	return result;
}

void* HeapAllocator::bucket_system_alloc()
{
	void* ptr = system_alloc(PAGE_SIZE);
	if (ptr) {
		//分配出来的page的基地址，必须跟PAGE_SIZE保持对齐
		assert(((size_t)ptr & (PAGE_SIZE-1)) == 0);
		#ifdef MULTITHREADED
		ScopeLock lock(mTreeMutex);
		#endif
	}
	return ptr;
}


void HeapAllocator::bucket_system_free(void* ptr) {
	assert(ptr);
	system_free(ptr);
	#ifdef MULTITHREADED
	ScopeLock lock(mTreeMutex);
	#endif
}

HeapAllocator::page* HeapAllocator::bucket_grow(size_t elemSize, unsigned marker) {
	//保证不会超过page的最大容量
	assert((PAGE_SIZE-sizeof(page))/elemSize <= MAX_UINT16);
	void* mem = bucket_system_alloc();
	if (mem) {
		size_t i = 0;
		// n代表所需用到的真是内存
		size_t n = ((PAGE_SIZE-sizeof(page))/elemSize)*elemSize;
		// 将新的page切分成多个大小为elemSize的块
		for (; i < n-elemSize; i += elemSize)
			((free_link*)((char*)mem + i))->mNext = (free_link*)((char*)mem + i + elemSize);
		((free_link*)((char*)mem + i))->mNext = NULL;
		assert(i + elemSize + sizeof(page) <= PAGE_SIZE);
		page* p = ptr_get_page(mem);
		new (p) page((free_link*)mem, (unsigned short)elemSize, marker);
		return p;
	}
	return NULL;
}

void* HeapAllocator::bucket_alloc(size_t size) {
	assert(size <= MAX_SMALL_ALLOCATION);
	unsigned bi = bucket_spacing_function(size);
	assert(bi < NUM_BUCKETS);
	#ifdef MULTITHREADED
	ScopeLock lock(mBuckets[bi].get_lock());
	#endif
	page* p = mBuckets[bi].get_free_page();
	if (!p) {
		size_t bsize = bucket_spacing_function_inverse(bi);
		p = bucket_grow(bsize, mBuckets[bi].marker());
		if (!p)
			return NULL;
		mBuckets[bi].add_free_page(p);
	}
	assert(p->elem_size() >= size);
	return mBuckets[bi].alloc(p);
}

void* HeapAllocator::bucket_alloc_direct(unsigned bi) {
	assert(bi < NUM_BUCKETS);
	#ifdef MULTITHREADED
	ScopeLock lock(mBuckets[bi].get_lock());
	#endif
	page* p = mBuckets[bi].get_free_page();
	if (!p) {
		size_t bsize = bucket_spacing_function_inverse(bi);
		p = bucket_grow(bsize, mBuckets[bi].marker());
		if (!p)
			return NULL;
		mBuckets[bi].add_free_page(p);
	}
	return mBuckets[bi].alloc(p);
}

void* HeapAllocator::bucket_realloc(void* ptr, size_t size) {
	page* p = ptr_get_page(ptr);
	size_t elemSize = p->elem_size();
	if (size <= elemSize)
		return ptr;		
	void* newPtr = bucket_alloc(size);
	if (!newPtr)
		return NULL;
	memcpy(newPtr, ptr, elemSize);
	bucket_free(ptr);
	return newPtr;
}

void HeapAllocator::bucket_free(void* ptr) {
	page* p = ptr_get_page(ptr);
	unsigned bi = p->bucket_index();
	assert(bi < NUM_BUCKETS);
	#ifdef MULTITHREADED
	ScopeLock lock(mBuckets[bi].get_lock());
	#endif
	mBuckets[bi].free(p, ptr);
}

void HeapAllocator::bucket_free_direct(void* ptr, unsigned bi) {
	assert(bi < NUM_BUCKETS);
	page* p = ptr_get_page(ptr);
	assert(bi == p->bucket_index());
	#ifdef MULTITHREADED
	ScopeLock lock(mBuckets[bi].get_lock());
	#endif
	mBuckets[bi].free(p, ptr);
}

//释放掉所有未使用的page
void HeapAllocator::bucket_purge() {
	for (unsigned i = 0; i < NUM_BUCKETS; i++) {
		#ifdef MULTITHREADED
		ScopeLock lock(mBuckets[i].get_lock());
		#endif
		page *pageEnd = mBuckets[i].page_list_end();
		for (page* p = mBuckets[i].page_list_begin(); p != pageEnd; ) {
			if (p->mFreeList == NULL) 
				break;
			page* next = p->next();
			if (p->empty()) {
				assert(p->mFreeList);
				p->unlink();
				void* memAddr = align_down((char*)p, PAGE_SIZE);
				bucket_system_free(memAddr);
			}
			p = next;
		}
	}
}

void HeapAllocator::split_block(block_header* bl, size_t size)
{
	assert(size + sizeof(block_header) + sizeof(free_node) <= bl->size());
	block_header* newBl = (block_header*)((char*)bl + size + sizeof(block_header));
	newBl->link_after(bl);
	newBl->set_unused();
}

HeapAllocator::block_header* HeapAllocator::shift_block(block_header* bl, size_t offs) {
	assert(offs > 0);
	block_header* prev = bl->prev();
	bl->unlink();
	bl = (block_header*)((char*)bl + offs);
	bl->link_after(prev);
	bl->set_unused();
	return bl;
}

HeapAllocator::block_header* HeapAllocator::coalesce_block(block_header* bl) {
	assert(!bl->used());
	block_header* next = bl->next();
	if (!next->used()) {
		tree_detach(next);
		next->unlink();
	}
	block_header* prev = bl->prev();
	if (!prev->used()) {
		tree_detach(prev);
		bl->unlink();
		bl = prev;
	}
	return bl;
}

void* HeapAllocator::tree_system_alloc(size_t size) {
	// 确保size是PAGE_SIZE的倍数
	assert(size/PAGE_SIZE*PAGE_SIZE == size);
	void* ptr = system_alloc(size);
	return ptr;
}

void HeapAllocator::tree_system_free(void* ptr, size_t size) {
	assert(ptr);
	assert(size/PAGE_SIZE*PAGE_SIZE == size);
	system_free(ptr);
}

HeapAllocator::block_header* HeapAllocator::tree_add_block(void* mem, size_t size) {
	// 创建一个假的blockheader来避免对prev()是否为NULL的检查。
	block_header* front = (block_header*)mem;
	front->prev(0);
	front->size(0);
	front->set_used();
	block_header* back = (block_header*)front->mem();
	back->prev(front);
	back->size(0);
	back->set_used();
	// 下面开始是真正的freeblock
	front = back;
	assert(front->used());
	back = (block_header*)((char*)mem + size - sizeof(block_header));
	back->size(0);
	back->set_used();
	front->set_unused();
	front->next(back);
	back->prev(front);
	front = coalesce_block(front);     
	return front;
}

HeapAllocator::block_header* HeapAllocator::tree_grow(size_t size) {
	size += 3*sizeof(block_header); //参考tree_add_block
	size = round_up(size, PAGE_SIZE);
	if (void* mem = tree_system_alloc(size))
		return tree_add_block(mem, size);
	return NULL;
}

HeapAllocator::block_header* HeapAllocator::tree_extract(size_t size) {
	// 优先检查最近使用的块
	block_header* bestBlock = mMRFreeBlock;
	if (bestBlock && bestBlock->size() >= size) {
		tree_detach(bestBlock);
		return bestBlock;
	}
	// 寻找大小适合的最小块
	free_node* bestNode = mFreeTree.lower_bound(size);
	if (bestNode == mFreeTree.end())
		return NULL;
	bestNode = bestNode->next(); // 优先使用邻居块，就避免调整树
	bestBlock = bestNode->get_block();
	tree_detach(bestBlock);
	return bestBlock;
}

HeapAllocator::block_header* HeapAllocator::tree_extract_aligned(size_t size, size_t alignment) {
	block_header* bestBlock = mMRFreeBlock;
	if (bestBlock) {
		size_t alignmentOffs = align_up((char*)bestBlock->mem(), alignment) - (char*)bestBlock->mem();
		if (bestBlock->size() >= size + alignmentOffs) {
			tree_detach(bestBlock);
			return bestBlock;
		}
	}
	size_t sizeUpper = size + alignment;
	free_node* bestNode = mFreeTree.lower_bound(size);
	free_node* lastNode = mFreeTree.upper_bound(sizeUpper);
	while (bestNode != lastNode) {
		size_t alignmentOffs = align_up((char*)bestNode, alignment) - (char*)bestNode;
		if (bestNode->get_block()->size() >= size + alignmentOffs)
			break;
		bestNode = bestNode->succ();
	}
	if (bestNode == mFreeTree.end())
		return NULL;
	if (bestNode == lastNode)
		bestNode = bestNode->next();
	bestBlock = bestNode->get_block();
	tree_detach(bestBlock);
	return bestBlock;
}

void HeapAllocator::tree_attach(block_header* bl) {
	if (mMRFreeBlock) {
		block_header* lastBl = mMRFreeBlock;
		if (lastBl->size() > MAX_SMALL_ALLOCATION) {
			mFreeTree.insert((free_node*)lastBl->mem());
		} else {
			mSmallFreeList.push_back((small_free_node*)lastBl->mem());
		}
	}
	mMRFreeBlock = bl;
}

void HeapAllocator::tree_detach(block_header* bl) {
	if (mMRFreeBlock == bl) {
		mMRFreeBlock = NULL;
		return;
	}
	if (bl->size() > MAX_SMALL_ALLOCATION) {
		mFreeTree.erase((free_node*)bl->mem());
	} else {
		mSmallFreeList.erase((small_free_node*)bl->mem());
	}
}

void* HeapAllocator::tree_alloc(size_t size) {
	#ifdef MULTITHREADED
	ScopeLock lock(mTreeMutex);
	#endif
	if (size < sizeof(free_node))
		size = sizeof(free_node);
	size = round_up(size, sizeof(block_header));
	block_header* newBl = tree_extract(size);
	if (!newBl) {
		newBl = tree_grow(size);
		if (!newBl)
			return NULL;
	}
	
	assert(newBl && newBl->size() >= size);
	if (newBl->size() >= size + sizeof(block_header) + sizeof(free_node)) {
		split_block(newBl, size);
		tree_attach(newBl->next());
	}
	newBl->set_used();
	return newBl->mem();
}

void* HeapAllocator::tree_alloc_aligned(size_t size, size_t alignment) {
	#ifdef MULTITHREADED
	ScopeLock lock(mTreeMutex);
	#endif
	if (size < sizeof(free_node))
		size = sizeof(free_node);
	size = round_up(size, sizeof(block_header));
	block_header* newBl = tree_extract_aligned(size, alignment);
	if (!newBl) {
		newBl = tree_grow(size + alignment);
		if (!newBl)
			return NULL;
	}
	assert(newBl && newBl->size() >= size);
	size_t alignmentOffs = align_up((char*)newBl->mem(), alignment) - (char*)newBl->mem();
	assert(newBl->size() >= size + alignmentOffs);
	if (alignmentOffs >= sizeof(block_header) + sizeof(free_node)) {
		split_block(newBl, alignmentOffs - sizeof(block_header));
		tree_attach(newBl);
		newBl = newBl->next();
	} else if (alignmentOffs > 0) {
		newBl = shift_block(newBl, alignmentOffs);
	}
	if (newBl->size() >= size + sizeof(block_header) + sizeof(free_node)) {
		split_block(newBl, size);
		tree_attach(newBl->next());
	}
	newBl->set_used();
	assert(((size_t)newBl->mem() & (alignment-1)) == 0);
	return newBl->mem();
}

void* HeapAllocator::tree_realloc(void* ptr, size_t size) {
	#ifdef MULTITHREADED
	ScopeLock lock(mTreeMutex);
	#endif
	if (size < sizeof(free_node))
		size = sizeof(free_node);
	size = round_up(size, sizeof(block_header));
	block_header* bl = ptr_get_block_header(ptr); 
	size_t blSize = bl->size();
	if (blSize >= size) {
		if (blSize >= size + sizeof(block_header) + sizeof(free_node)) {
			split_block(bl, size);
			block_header* next = bl->next();
			next = coalesce_block(next);
			tree_attach(next);
		}
		assert(bl->size() >= size);
		return ptr;
	}
	
	block_header* next = bl->next();
	size_t nextSize = next->used() ? 0 : next->size() + sizeof(block_header);
	if (blSize + nextSize >= size) {
		assert(!next->used());
		tree_detach(next);
		next->unlink();
		assert(bl->size() >= size);
		if (bl->size() >= size + sizeof(block_header) + sizeof(free_node)) {
			split_block(bl, size);
			tree_attach(bl->next());
		}
		return ptr;
	}
	
	block_header* prev = bl->prev();
	size_t prevSize = prev->used() ? 0 : prev->size() + sizeof(block_header);
	if (blSize + prevSize + nextSize >= size) {
		assert(!prev->used());
		tree_detach(prev);
		bl->unlink();
		if (!next->used()) {
			tree_detach(next);
			next->unlink();
		}
		bl = prev;
		bl->set_used();
		assert(bl->size() >= size);
		void* newPtr = bl->mem();
		
		memmove(newPtr, ptr, blSize);
		if (bl->size() >= size + sizeof(block_header) + sizeof(free_node)) {
			split_block(bl, size);
			tree_attach(bl->next());
		}
		return newPtr;
	}
	
	void* newPtr = tree_alloc(size);
	if (newPtr) {
		memcpy(newPtr, ptr, blSize);
		tree_free(ptr);
		return newPtr;
	}
	return NULL;
}

void* HeapAllocator::tree_realloc_aligned(void* ptr, size_t size, size_t alignment) {
	assert(((size_t)ptr & (alignment-1)) == 0);
	#ifdef MULTITHREADED
	ScopeLock lock(mTreeMutex);
	#endif
	if (size < sizeof(free_node))
		size = sizeof(free_node);
	size = round_up(size, sizeof(block_header));
	block_header* bl = ptr_get_block_header(ptr);
	size_t blSize = bl->size();
	if (blSize >= size) {
		if (blSize >= size + sizeof(block_header) + sizeof(free_node)) {
			split_block(bl, size);
			block_header* next = bl->next();
			next = coalesce_block(next);
			tree_attach(next);
		}
		assert(bl->size() >= size);
		return ptr;
	}
	block_header* next = bl->next();
	size_t nextSize = next->used() ? 0 : next->size() + sizeof(block_header);
	if (blSize + nextSize >= size) {
		assert(!next->used());
		tree_detach(next);
		next->unlink();
		assert(bl->size() >= size);
		if (bl->size() >= size + sizeof(block_header) + sizeof(free_node)) {
			split_block(bl, size);
			tree_attach(bl->next());
		}
		return ptr;
	}
	block_header* prev = bl->prev();
	size_t prevSize = prev->used() ? 0 : prev->size() + sizeof(block_header);
	size_t alignmentOffs = prev->used() ? 0 : align_up((char*)prev->mem(), alignment) - (char*)prev->mem();
	if (blSize + prevSize + nextSize >= size + alignmentOffs) {
		assert(!prev->used());
		tree_detach(prev);
		bl->unlink();
		if (!next->used()) {
			tree_detach(next);
			next->unlink();
		}
		if (alignmentOffs >= sizeof(block_header) + sizeof(free_node)) {
			split_block(prev, alignmentOffs - sizeof(block_header));
			tree_attach(prev);
			prev = prev->next();
		} else if (alignmentOffs > 0) {
			prev = shift_block(prev, alignmentOffs);
		}
		bl = prev;
		bl->set_used();
		assert(bl->size() >= size && ((size_t)bl->mem() & (alignment-1)) == 0);
		void* newPtr = bl->mem();
		memmove(newPtr, ptr, blSize - DEBUG_EXTRA_INFO_SIZE);
		if (bl->size() >= size + sizeof(block_header) + sizeof(free_node)) {
			split_block(bl, size);
			tree_attach(bl->next());
		}
		return newPtr;
	}
	void* newPtr = tree_alloc_aligned(size, alignment);
	if (newPtr) {
		memcpy(newPtr, ptr, blSize - DEBUG_EXTRA_INFO_SIZE);
		tree_free(ptr);
		return newPtr;
	}
	return NULL;
}

size_t HeapAllocator::tree_resize(void* ptr, size_t size) {
	#ifdef MULTITHREADED
	ScopeLock lock(mTreeMutex);
	#endif
	if (size < sizeof(free_node))
		size = sizeof(free_node);
	size = round_up(size, sizeof(block_header));
	block_header* bl = ptr_get_block_header(ptr); 
	size_t blSize = bl->size();
	if (blSize >= size) {
		if (blSize >= size + sizeof(block_header) + sizeof(free_node)) {
			split_block(bl, size);
			block_header* next = bl->next();
			next = coalesce_block(next);
			tree_attach(next);
		}
		assert(bl->size() >= size);
		return bl->size();
	}
	block_header* next = bl->next();
	if (!next->used() && blSize + next->size() + sizeof(block_header) >= size) {
		tree_detach(next);
		next->unlink();
		if (bl->size() >= size + sizeof(block_header) + sizeof(free_node)) {
			split_block(bl, size);
			tree_attach(bl->next());
		}
		assert(bl->size() >= size);
	}
	return bl->size();
}

void HeapAllocator::tree_free(void* ptr) {
	#ifdef MULTITHREADED
	ScopeLock lock(mTreeMutex);
	#endif
	block_header* bl = ptr_get_block_header(ptr);
	bl->set_unused();
	bl = coalesce_block(bl);
	tree_attach(bl);
}

void HeapAllocator::tree_purge_block(block_header* bl) {
	assert(!bl->used());
	assert(bl->prev() && bl->prev()->used());
	assert(bl->next() && bl->next()->used());
	if (bl->prev()->prev() == NULL && bl->next()->size() == 0) {
		tree_detach(bl);
		char* memStart = (char*)bl->prev();
		char* memEnd = (char*)bl->mem() + bl->size() + sizeof(block_header);
		void* mem = memStart;
		size_t size = memEnd - memStart;
		assert(((size_t)mem & (PAGE_SIZE-1)) == 0);
		assert((size & (PAGE_SIZE-1)) == 0);
		tree_system_free(mem, size);
	}
}

void HeapAllocator::tree_purge() {
	#ifdef MULTITHREADED
	ScopeLock lock(mTreeMutex);
	#endif
	
	tree_attach(NULL);
	size_t pageSize = PAGE_SIZE-3*sizeof(block_header)-sizeof(free_node);
	free_node* node = mFreeTree.lower_bound(pageSize);
	free_node* end = mFreeTree.end();
	while (node != end) {
		block_header* cur = node->get_block();
		node = node->succ();
		tree_purge_block(cur);
	}
	tree_attach(NULL);
}

HeapAllocator::HeapAllocator() : mMRFreeBlock(NULL)
{
}

HeapAllocator::~HeapAllocator()
{
	purge();
	#ifdef DEBUG_ALLOCATOR
	check();
	report();
	#endif
	for (unsigned i = 0; i < NUM_BUCKETS; i++)
		assert(mBuckets[i].page_list_empty());
	assert(mFreeTree.empty());
	assert(mSmallFreeList.empty());
	assert(mMRFreeBlock == NULL);
}

HeapAllocator::bucket::bucket() 
{ 	
	#if (RAND_MAX <= SHRT_MAX)
	mMarker = (rand()*(RAND_MAX+1) + rand()) ^ MARKER;
	#else
	mMarker = rand() ^ MARKER;
	#endif
}

void* HeapAllocator::alloc(size_t size, const char* filename, int linenum)
{
	if (!is_small_allocation(size)) {
		uint32 trueSize = size + DEBUG_EXTRA_INFO_SIZE;
		void* ptr = tree_alloc(trueSize);
		return debug_alloc(ptr, size,  trueSize, DEBUG_SOURCE_TREE, ALIGN_NONE, filename, linenum);
	}
	if (size == 0)
		return NULL;
	size = clamp_small_allocation(size);
	uint32 trueSize = size + DEBUG_EXTRA_INFO_SIZE;
	void* ptr = bucket_alloc_direct(bucket_spacing_function(trueSize));
	trueSize = round_up(trueSize, MIN_ALLOCATION);
	return debug_alloc(ptr, size, trueSize, DEBUG_SOURCE_BUCKETS, ALIGN_NONE, filename, linenum);
}

void* HeapAllocator::alloc(size_t size, size_t alignment, const char* filename, int linenum)
{
	assert((alignment & (alignment-1)) == 0);
	if (alignment <= DEFAULT_ALIGNMENT)
			return alloc(size);
	if (!is_small_allocation(size) || alignment > MAX_SMALL_ALLOCATION) {
		uint32 trueSize = size + DEBUG_EXTRA_INFO_SIZE;
		void* ptr = tree_alloc_aligned(trueSize, alignment);
		return debug_alloc(ptr, size, trueSize, DEBUG_SOURCE_TREE, alignment, filename, linenum);
	}
	if (size == 0)
		return NULL;
	size = clamp_small_allocation(size);
	uint32 trueSize = round_up(size + DEBUG_EXTRA_INFO_SIZE, alignment);
	void* ptr = bucket_alloc_direct(bucket_spacing_function(trueSize));
	return debug_alloc(ptr, size, trueSize, DEBUG_SOURCE_BUCKETS, alignment, filename, linenum);
}

void* HeapAllocator::calloc(size_t count, size_t size)
{
	void* p = alloc(count * size);
	if (p)
		memset(p, 0, count * size);
	return p;
}
void* HeapAllocator::realloc(void* ptr, size_t size, const char* filename, int linenum)
{
	if (ptr == NULL)
		return alloc(size);
	if (size == 0) {
		free(ptr);
		return NULL;
	}
	debug_check(ptr);
	void* pRealMem = (void*)debug_free(ptr);
	if (ptr_in_bucket(pRealMem)) {
		size = clamp_small_allocation(size);
		if (is_small_allocation(size)) {
			uint32 trueSize = size + DEBUG_EXTRA_INFO_SIZE;
			void* newPtr = bucket_realloc(pRealMem,trueSize);
			return debug_alloc(newPtr, size, trueSize, DEBUG_SOURCE_BUCKETS, ALIGN_NONE, filename, linenum);
		}
		uint32 trueSize = size + DEBUG_EXTRA_INFO_SIZE;
		void* newPtr = tree_alloc(trueSize);
		if (!newPtr)
			return NULL;
		uint32 origSize = ptr_get_page(pRealMem)->elem_size();
		memcpy(newPtr, pRealMem, origSize);
		bucket_free(pRealMem);
		return debug_alloc(newPtr, size, trueSize, DEBUG_SOURCE_TREE, ALIGN_NONE, filename, linenum);
	}
	uint32 trueSize = size + DEBUG_EXTRA_INFO_SIZE;
	void* newPtr = tree_realloc(pRealMem, trueSize);
	return debug_alloc(newPtr, size, trueSize, DEBUG_SOURCE_TREE, ALIGN_NONE, filename, linenum);
}
void* HeapAllocator::realloc(void* ptr, size_t size, size_t alignment, const char* filename, int linenum)
{
	assert((alignment & (alignment-1)) == 0);
	if (alignment <= DEFAULT_ALIGNMENT)
		return realloc(ptr, size);
	if (ptr == NULL)
		return alloc(size, alignment);
	if (size == 0) {
		free(ptr);
		return NULL;
	}
	
	void* pRealMem = (void*)debug_free(ptr);
	if ((size_t)ptr & (alignment-1)) {
		uint32 trueSize = size + DEBUG_EXTRA_INFO_SIZE;
		void* newPtr = alloc(trueSize, alignment);
		if (!newPtr)
			return NULL;
		size_t count = this->size(pRealMem);
		if (count > size)
			count = size;
		memcpy(newPtr, ptr, count);
		free(ptr);
		return newPtr;
	}
	debug_check(ptr);
	if (ptr_in_bucket(pRealMem)) {
		size = clamp_small_allocation(size);
		if (is_small_allocation(size) && alignment <= MAX_SMALL_ALLOCATION) {
			uint32 trueSize = size + DEBUG_EXTRA_INFO_SIZE;
			void* newPtr = bucket_realloc(ptr, trueSize);
			return debug_alloc(newPtr, size, trueSize, DEBUG_SOURCE_BUCKETS, alignment, filename, linenum);
		}
		uint32 trueSize = size + DEBUG_EXTRA_INFO_SIZE;
		void* newPtr = tree_alloc_aligned(trueSize, alignment);
		if (!newPtr)
			return NULL;
		memcpy(newPtr, pRealMem, ptr_get_page(pRealMem)->elem_size());
		bucket_free(pRealMem);
		return debug_alloc(newPtr, size, trueSize, DEBUG_SOURCE_TREE, alignment, filename, linenum);
	}
	uint32 trueSize = size + DEBUG_EXTRA_INFO_SIZE;
	void* newPtr = tree_realloc_aligned(ptr, trueSize, alignment);
	return debug_alloc(newPtr, size, trueSize, DEBUG_SOURCE_TREE, alignment, filename, linenum);
}

//计算实际需要的内存，需要减去调试信息
size_t HeapAllocator::size(void* pRealMem) const
{
	if (pRealMem == NULL)
		return 0;
	if (ptr_in_bucket(pRealMem)) 
		return ptr_get_page(pRealMem)->elem_size() - DEBUG_EXTRA_INFO_SIZE;
	return ptr_get_block_header(pRealMem)->size() - DEBUG_EXTRA_INFO_SIZE;
}

void HeapAllocator::free(void* ptr)
{
	if (ptr == NULL)
		return;
	char* realPtr = (char*)debug_free(ptr);
	if (ptr_in_bucket(realPtr))
		return bucket_free(realPtr);
	tree_free(realPtr);
}

void HeapAllocator::purge()
{
	tree_purge();
	bucket_purge();
}

void* HeapAllocator::debug_alloc(void* pRealMem, size_t size, size_t trueSize, debug_source src, uint8 align, const char* filename, int linenum)
{
	char* pClientMem = (char*)pRealMem;
	#ifdef DEBUG_ALLOCATOR
	pClientMem = (char*)pRealMem + s_blockHeadSize + s_preBufferSize;
	
	sMemoryBlockHeader* blockHeader = (sMemoryBlockHeader*)pRealMem;
	blockHeader->actualSize = trueSize;
	blockHeader->pointerOffset = pClientMem - (char*)pRealMem;

	sPreBufferData* pPreBufferData = getPreBufferData(pClientMem);
	if(pPreBufferData == sTopMemoryBlock)
		return pClientMem;
	
	pPreBufferData->nextHeader = sTopMemoryBlock;
	pPreBufferData->previousHeader = 0;
	pPreBufferData->requestedSize =  size;
	pPreBufferData->userChecksum = 0;
	pPreBufferData->fileLine = linenum;
	pPreBufferData->alignment = align;
	pPreBufferData->debug_source = src;

	if (sTopMemoryBlock)
	{
		sTopMemoryBlock->previousHeader = pPreBufferData;
	}
	sTopMemoryBlock = pPreBufferData;

	if (filename)
	{
		strncpy(pPreBufferData->fileName, filename, MAX_FILEPATH); // filename of the caller
	}
	else
	{
		strncpy(pPreBufferData->fileName, "unknown", MAX_FILEPATH); // filename of the caller
	}

	uint8* prePattern = pPreBufferData->bytePattern;
	uint8* postPattern = (uint8*)pClientMem + size;
	for (int i=0;i< PATTERN_SIZE;i++)
	{
		prePattern[i]=(char) PRE_PATTERN;
		postPattern[i]=(char) POST_PATTERN;
	}
	
	sAllocAccount++;
	sTotalBytesRequested += size;
	sTotalBytesInUse += trueSize;
	
	sMaximumBytesRequested = 
		maximum(sMaximumBytesRequested,
				sTotalBytesRequested);

	sMaxinumBytesInUse = 
		maximum(sMaxinumBytesInUse,
				sTotalBytesInUse);
	#endif
	return pClientMem;
}

void* HeapAllocator::debug_free(void* pClientMem)
{
	void* pRealPtr = pClientMem;
	#ifdef DEBUG_ALLOCATOR
	sMemoryBlockHeader* pHeader = getMemoryBlockHeader((char*)pClientMem);
	pRealPtr = (void*)pHeader;

	sPreBufferData* pPreBufferData = getPreBufferData(pClientMem);
	uint8* prePattern = pPreBufferData->bytePattern;
	uint8* postPattern = (uint8*)pPreBufferData + s_preBufferSize + pPreBufferData->requestedSize;
	for (int i=0;i<PATTERN_SIZE;++i)
	{
		assert(prePattern[i]==(uint8)PRE_PATTERN);	//memory overrun detected
		assert(postPattern[i]==(uint8)POST_PATTERN);//memory overrun detected
	}
	sReleaseAccount++;
	sTotalBytesRequested -= pPreBufferData->requestedSize;
	sTotalBytesInUse -= pHeader->actualSize;
	if (sTopMemoryBlock == pPreBufferData)
	{
		sTopMemoryBlock = sTopMemoryBlock->nextHeader;
	}
	if (pPreBufferData->nextHeader)
	{
		pPreBufferData->nextHeader->previousHeader = pPreBufferData->previousHeader;
	}
	if (pPreBufferData->previousHeader)
	{
		pPreBufferData->previousHeader->nextHeader = pPreBufferData->nextHeader;
	}
	#endif
	return pRealPtr;
}

void HeapAllocator::debug_check(void* pClientMem)
{
	#ifdef DEBUG_ALLOCATOR
	sPreBufferData* pPreBufferData = getPreBufferData((char*)pClientMem);
	uint8* prePattern = pPreBufferData->bytePattern;
	uint8* postPattern = (uint8*)pPreBufferData + s_preBufferSize + pPreBufferData->requestedSize;
	for (int i=0;i<PATTERN_SIZE;++i)
	{
		assert(prePattern[i]==(uint8)PRE_PATTERN);
		assert(postPattern[i]==(uint8)POST_PATTERN);
	}
	#endif
}

void HeapAllocator::check()
{
	#ifdef DEBUG_ALLOCATOR
	sPreBufferData* cur_buf = sTopMemoryBlock;
	while(cur_buf)
	{
		uint8* prePattern = cur_buf->bytePattern;
		uint8* postPattern = (uint8*)cur_buf + s_preBufferSize + cur_buf->requestedSize;
		for (int i=0;i<PATTERN_SIZE;++i)
		{
			assert(prePattern[i]==(uint8)PRE_PATTERN);
			assert(postPattern[i]==(uint8)POST_PATTERN);
		}
		cur_buf = cur_buf->nextHeader;
	}
	#endif
}

void HeapAllocator::report()
{
	#ifdef DEBUG_ALLOCATOR
	printf("\n*** Memory Use Statistics ***\n");
	printf("Total Allocations: %d\n", sAllocAccount);
	printf("Total Deallocations: %d\n", sReleaseAccount);
	printf("Maximum memory used (including debug info): %d\n", sMaxinumBytesInUse);
	printf("Maximum memory required: %d\n", sMaximumBytesRequested);
	printf("Current memory used (including debug info): %d\n", sTotalBytesInUse);
	printf("Current memory used by Client: %d\n", sTotalBytesRequested);
	printf("*** End Memory Use Statistics ***\n");
	printf("\n*** Memory Use Details ***\n");
	sPreBufferData* cur_buf = sTopMemoryBlock;
	int index = 0;
	while(cur_buf)
	{
		printf("\n[memory alloc info %d]\n", ++index);
		printf("filename[%s]\n", cur_buf->fileName);
		printf("line[%d]\n", cur_buf->fileLine);
		printf("request size[%d]\n", cur_buf->requestedSize);

		cur_buf = cur_buf->nextHeader;
	}
	printf("*** End Memory Use Details ***\n\n");
	#else
	printf("*** memory allocation info is not available***\n\n");
	#endif
}

