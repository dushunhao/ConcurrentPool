#include "CentralCache.h"
#include "PageCache.h"

CentralCache CentralCache::_sInst;

// 获取一个非空的span
Span* CentralCache::GetOneSpan(SpanList& list, size_t size)
{
	// 查看当前spanlist中是否有还未分配的span
	Span* it = list.Begin();
	while (it != list.End())
	{
		if (it->_freeList != nullptr)
		{
			return it;
		}
		else
		{
			it = it->_next;
		}
	}

	// 先把central cache的桶锁解开，这样如果其他线程释放内存对象回来，不会阻塞
	list._mtx.unlock();

	// 到这里说明没有空闲的span，只能找下一层的page cache要
	PageCache::GetInStance()->_pageMtx.lock();
	Span* span = PageCache::GetInStance()->NewSpan(SizeClass::NumMovePage(size));
	span->_isUse = true;
	span->_objSize = size;
	PageCache::GetInStance()->_pageMtx.unlock();
	
	// 对获取的span进行切分，不需要加锁，因为这会让其他线程访问不到这个span
	// 计算span的大块内存的起始地址和大块内存的大小(字节数)
	char* start = (char*)(span->_pageId << PAGE_SHIFT);
	size_t bytes = span->_n << PAGE_SHIFT;
	char* end = start + bytes;

	// 把大块内存切成	自由链表链接起来
	// 先切一块下来做头，再尾插
	span->_freeList = start;
	start += size;
	void* tail = span->_freeList;
	int i = 1;
	// 
	while (start < end)
	{
		++i;
		NextObj(tail) = start;
		tail = NextObj(tail);
		start += size;
	}
	// 把内存块切好之后，把尾部置为空
	NextObj(tail) = nullptr;

	// 切好span以后，需要把span挂到桶里面去的时候，再加锁
	list._mtx.lock();
	list.PushFront(span);

	return span;
}

size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size)
{
	size_t index = SizeClass::Index(size);

	_spanLists[index]._mtx.lock();
	
	Span* span = GetOneSpan(_spanLists[index], size);
	assert(span);
	assert(span->_freeList);

	// 从span中获取batchNum个对象
	// 如果不够batchNum个，有多少拿多少
	start = span->_freeList;
	end = start;
	size_t i = 0;
	size_t actualNum = 1;
	while (i < batchNum - 1 && NextObj(end) != nullptr)
	{
		end = NextObj(end);
		++i;
		++actualNum;
	}
	span->_freeList = NextObj(end);
	NextObj(end) = nullptr;
	span->_usecount += actualNum;

	_spanLists[index]._mtx.unlock();

	return actualNum;
}

// 将一定数量的对象释放到span跨度
void CentralCache::ReleaseListToSpans(void* start, size_t size)
{
	size_t index = SizeClass::Index(size);
	_spanLists[index]._mtx.lock();

	while (start)
	{
		void* next = NextObj(start);
		Span* span = PageCache::GetInStance()->MapObjectToSpan(start);
		NextObj(start) = span->_freeList; // 把每个内存块头插
		span->_freeList = start;
		span->_usecount--;
		// 说明span的切分出去的所有小块内存都回来了
		// 这个span就可以再回收给page cache，page cache可以尝试去做前后页合并
		if (span->_usecount == 0)
		{
			_spanLists[index].Erase(span); // 把span链表头删，连接好
			span->_freeList = nullptr;
			span->_next = nullptr;
			span->_prev = nullptr;

			// 释放span给page cache时，使用它的锁就可以了
			// 这时把桶锁解开，避免阻塞其他线程申请释放
			_spanLists[index]._mtx.unlock();

			PageCache::GetInStance()->_pageMtx.lock();
			PageCache::GetInStance()->ReleaseSpanToPageCache(span);
			PageCache::GetInStance()->_pageMtx.unlock();

			_spanLists[index]._mtx.lock();
		}
		start = next;
	}

	_spanLists[index]._mtx.unlock();
}