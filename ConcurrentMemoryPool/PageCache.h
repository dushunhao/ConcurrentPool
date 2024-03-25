#pragma once

#include "Common.h"
#include "Object.h"
#include "PageMap.h"

class PageCache
{
public:
	static PageCache* GetInStance()
	{
		return &_sInst;
	}

	// 获取从对象到span的映射
	Span* MapObjectToSpan(void* obj);

	// 释放空间span回到PageCache，合并相邻的span
	void ReleaseSpanToPageCache(Span* span);

	// 获取一个K页的span
	Span* NewSpan(size_t k);

	std::mutex _pageMtx;

private:
	SpanList _spanLists[NPAGES];
	ObjectPool<Span> _spanPool;

	//std::unordered_map<PAGE_ID, Span*> _idSpanMap;
#ifdef _WIN64
	TCMalloc_PageMap1<64 - PAGE_SHIFT> _idSpanMap;
#elif WIN32
	TCMalloc_PageMap1<32 - PAGE_SHIFT> _idSpanMap;
#endif

	PageCache()
	{}
	PageCache(const PageCache&) = delete;

	static PageCache _sInst;
};