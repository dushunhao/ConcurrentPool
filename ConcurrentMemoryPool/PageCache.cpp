#include "PageCache.h"

PageCache PageCache::_sInst;

// ��ȡһ��Kҳ��span
Span* PageCache::NewSpan(size_t k)
{
	assert(k > 0);

	if (k > NPAGES - 1)
	{
		void* ptr = SystemAlloc(k);
		//Span* span = new Span;
		Span* span = _spanPool.New();
		span->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
		span->_n = k;

		//_idSpanMap[span->_pageId] = span;
		_idSpanMap.set(span->_pageId, span);

		return span;
	}

	// �ȼ���k��Ͱ������û��span
	if (!_spanLists[k].Empty())	
	{
		Span* kSpan = _spanLists[k].PopFront();

		// ����id��span��ӳ��	������central cache����С���ڴ�ʱ������ÿһ����Ӧ��span
		for (PAGE_ID i = 0; i < kSpan->_n; i++)
		{
			//_idSpanMap[kSpan->_pageId + i] = kSpan;
			_idSpanMap.set(kSpan->_pageId + i, kSpan);
		}

		return kSpan;
	}

	// ���һ�º����Ͱ������û��span������п��԰����������з�
	for (size_t i = k + 1; i < NPAGES; ++i)
	{
		if (!_spanLists[i].Empty())
		{
			Span* nSpan = _spanLists[i].PopFront();
			//Span* kSpan = new Span;
			Span* kSpan = _spanPool.New();

			// ��nSpan��ͷ����һ��kҳ����
			// kҳspan����
			// nSpan�ٹҵ���Ӧӳ���λ��
			kSpan->_pageId = nSpan->_pageId;
			kSpan->_n = k;

			nSpan->_pageId += k;
			nSpan->_n -= k;

			_spanLists[nSpan->_n].PushFront(nSpan);	

			// ֻ��Ҫ�洢nSpan����βҳ�Ÿ�nSpanӳ�䣬����page cache�����ڴ�
			// ���кϲ�����
			//_idSpanMap[nSpan->_pageId] = nSpan;
			//_idSpanMap[nSpan->_pageId + nSpan->_n - 1] = nSpan;
			_idSpanMap.set(nSpan->_pageId, nSpan);
			_idSpanMap.set(nSpan->_pageId + nSpan->_n - 1, nSpan);

			// ����id��span��ӳ��	������central cache����С���ڴ�ʱ������ÿһ����Ӧ��span
			for (PAGE_ID i = 0; i < kSpan->_n; i++)
			{
				_idSpanMap.set(kSpan->_pageId + i, kSpan);
			}

			auto ret = kSpan;
			//Span* span = MapObjectToSpan((void*)(kSpan->_pageId << 13));

			return kSpan;
		}
	}

	// �ߵ����λ�þ�˵������û�д�ҳ��span��
	// ��ʱ��ȥ�Ҷ�Ҫһ��128ҳ��span
	Span* bigSpan = _spanPool.New();
	void* ptr = SystemAlloc(NPAGES - 1);
	bigSpan->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
	bigSpan->_n = NPAGES - 1;

	_spanLists[bigSpan->_n].PushFront(bigSpan);

	return NewSpan(k);
}

// ��ȡ�Ӷ���span��ӳ��
Span* PageCache::MapObjectToSpan(void* obj)
{
	PAGE_ID id = ((PAGE_ID)obj >> PAGE_SHIFT);

	//std::unique_lock<std::mutex> lock(_pageMtx);

	//auto ret = _idSpanMap.find(id);
	//if (ret != _idSpanMap.end())
	//{
	//	return ret->second;
	//}
	//else
	//{
	//	assert(false);
	//	return nullptr;
	//}

	// �����߳��ڶ�д�Ĺ����п��ܻ�ı�����ṹ������¶�ȡ����
	// mapʹ�õĺ���������ĵ�ʱ����ܵ���������ת
	// ��ϣ�����������ݵ�ʱ��������⣬�ɱ���û�и��Ƹ��±�����ȡ�Ļ���ԭ���ı������������ΪʲôҪ����

	// ����ṹ�����ڶ�д�Ĺ��̾ͺܿ�
	// ����ӳ��֮ǰ�ͻ���ڴ��ȿ��ã������ǲ��뻹��ɾ��������ı�����ṹ
	// ��д�Ƿ����
	// ֻҪ��NewSpan��ReleaseSpanToPageCache�����������л�ȥд�룬�����ڽ��뺯��֮ǰ���Ǽ�������
	// ��ʵ������Ҳ�ǿ��Եģ���Ϊ�����Ƕ�д�������ͬһ��ҳ����
	auto ret = (Span*)_idSpanMap.get(id);
	assert(ret != nullptr);
	return ret;
}

// �ͷſռ�span�ص�PageCache���ϲ����ڵ�span
void PageCache::ReleaseSpanToPageCache(Span* span)
{
	if (span->_n > NPAGES - 1) // ����128page�����Բ�����pagecacheҪ�ģ�Ҫֱ���ͷ�
	{
		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		SystemFree(ptr);
		//delete span;
		_spanPool.Delete(span);

		return;
	}

	// ��spanǰ���ҳ�����Խ��кϲ��������ڴ���Ƭ����
	while (1)
	{
		PAGE_ID prevId = span->_pageId - 1; // ǰһ��
		//auto ret = _idSpanMap.find(prevId);
		//// ǰ���ҳ��û�У����ϲ�
		//if (ret == _idSpanMap.end())
		//{
		//	break;
		//}
		
		// ǰ���ҳ��û�У����ϲ�
		auto ret = (Span*)_idSpanMap.get(prevId);
		if (ret == nullptr)
		{
			break;
		}
		
		// ǰ�����ڵ�span��ʹ��
		Span* prevSpan = ret;
		if (prevSpan->_isUse == true)
		{
			break;
		}
		// �ϲ�����128ҳ��span
		if (prevSpan->_n+span->_n > NPAGES - 1)
		{
			break;
		}

		span->_pageId = prevSpan->_pageId;
		span->_n += prevSpan->_n;

		_spanLists[prevSpan->_n].Erase(prevSpan);
		_spanPool.Delete(prevSpan);
	}

	// ���ϲ�
	while (1)
	{
		PAGE_ID nextId = span->_pageId + span->_n;

		//auto ret = _idSpanMap.find(nextId);
		//if (ret == _idSpanMap.end())
		//{
		//	break;
		//}

		auto ret = (Span*)_idSpanMap.get(nextId);
		if (ret == nullptr)
		{
			break;
		}

		Span* nextSpan = ret;
		if (nextSpan->_isUse == true)
		{
			break;
		}
		if (nextSpan->_n + span->_n > NPAGES - 1)
		{
			break;
		}

		span->_n += nextSpan->_n;
		_spanLists[nextSpan->_n].Erase(nextSpan);
		_spanPool.Delete(nextSpan);
	}
	
	_spanLists[span->_n].PushFront(span);
	span->_isUse = false;
	//_idSpanMap[span->_pageId] = span;
	//_idSpanMap[span->_pageId + span->_n - 1] = span;
	_idSpanMap.set(span->_pageId, span);
	_idSpanMap.set(span->_pageId + span->_n - 1, span);
}