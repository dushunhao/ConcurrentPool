#pragma once

#include "Common.h"
#include "ThreadCache.h"
#include "PageCache.h"
#include "Object.h"

static void* ConcurrentAlloc(size_t size)
{
	if (size > MAX_BYTES)
	{
		size_t alignSize = SizeClass::RoundUp(size);
		size_t kpage = alignSize >> PAGE_SHIFT;

		PageCache::GetInStance()->_pageMtx.lock();
		Span* span = PageCache::GetInStance()->NewSpan(kpage);
		span->_objSize = size;
		PageCache::GetInStance()->_pageMtx.unlock();

		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		return ptr;
	}
	// 通过TLS每个线程无锁的获取自己的专属的ThreadCache对象
	if (pTLSThreadCache == nullptr)
	{
		//pTLSThreadCache = new ThreadCache;
		static ObjectPool<ThreadCache> tcPool;
		pTLSThreadCache = tcPool.New();

	}

	return pTLSThreadCache->Allocate(size);	
}

static void ConcurrentFree(void* ptr)
{
	Span* span = PageCache::GetInStance()->MapObjectToSpan(ptr);
	size_t size = span->_objSize;

	if (size > MAX_BYTES)
	{
		PageCache::GetInStance()->_pageMtx.lock();
		PageCache::GetInStance()->ReleaseSpanToPageCache(span);
		PageCache::GetInStance()->_pageMtx.unlock();
	}
	else
	{
		assert(pTLSThreadCache);
		pTLSThreadCache->Deallocate(ptr, size);
	}
}