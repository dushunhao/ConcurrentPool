#include "CentralCache.h"
#include "PageCache.h"

CentralCache CentralCache::_sInst;

// ��ȡһ���ǿյ�span
Span* CentralCache::GetOneSpan(SpanList& list, size_t size)
{
	// �鿴��ǰspanlist���Ƿ��л�δ�����span
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

	// �Ȱ�central cache��Ͱ���⿪��������������߳��ͷ��ڴ�����������������
	list._mtx.unlock();

	// ������˵��û�п��е�span��ֻ������һ���page cacheҪ
	PageCache::GetInStance()->_pageMtx.lock();
	Span* span = PageCache::GetInStance()->NewSpan(SizeClass::NumMovePage(size));
	span->_isUse = true;
	span->_objSize = size;
	PageCache::GetInStance()->_pageMtx.unlock();
	
	// �Ի�ȡ��span�����з֣�����Ҫ��������Ϊ����������̷߳��ʲ������span
	// ����span�Ĵ���ڴ����ʼ��ַ�ʹ���ڴ�Ĵ�С(�ֽ���)
	char* start = (char*)(span->_pageId << PAGE_SHIFT);
	size_t bytes = span->_n << PAGE_SHIFT;
	char* end = start + bytes;

	// �Ѵ���ڴ��г�	����������������
	// ����һ��������ͷ����β��
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
	// ���ڴ���к�֮�󣬰�β����Ϊ��
	NextObj(tail) = nullptr;

	// �к�span�Ժ���Ҫ��span�ҵ�Ͱ����ȥ��ʱ���ټ���
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

	// ��span�л�ȡbatchNum������
	// �������batchNum�����ж����ö���
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

// ��һ�������Ķ����ͷŵ�span���
void CentralCache::ReleaseListToSpans(void* start, size_t size)
{
	size_t index = SizeClass::Index(size);
	_spanLists[index]._mtx.lock();

	while (start)
	{
		void* next = NextObj(start);
		Span* span = PageCache::GetInStance()->MapObjectToSpan(start);
		NextObj(start) = span->_freeList; // ��ÿ���ڴ��ͷ��
		span->_freeList = start;
		span->_usecount--;
		// ˵��span���зֳ�ȥ������С���ڴ涼������
		// ���span�Ϳ����ٻ��ո�page cache��page cache���Գ���ȥ��ǰ��ҳ�ϲ�
		if (span->_usecount == 0)
		{
			_spanLists[index].Erase(span); // ��span����ͷɾ�����Ӻ�
			span->_freeList = nullptr;
			span->_next = nullptr;
			span->_prev = nullptr;

			// �ͷ�span��page cacheʱ��ʹ���������Ϳ�����
			// ��ʱ��Ͱ���⿪���������������߳������ͷ�
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