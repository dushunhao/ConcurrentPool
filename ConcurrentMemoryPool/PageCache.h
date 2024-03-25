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

	// ��ȡ�Ӷ���span��ӳ��
	Span* MapObjectToSpan(void* obj);

	// �ͷſռ�span�ص�PageCache���ϲ����ڵ�span
	void ReleaseSpanToPageCache(Span* span);

	// ��ȡһ��Kҳ��span
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