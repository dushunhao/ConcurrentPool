#include "PageCache.h"

PageCache PageCache::_sInst;

// 获取一个K页的span
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

	// 先检查第k个桶里面有没有span
	if (!_spanLists[k].Empty())	
	{
		Span* kSpan = _spanLists[k].PopFront();

		// 建立id和span的映射	，方便central cache回收小块内存时，查找每一个对应的span
		for (PAGE_ID i = 0; i < kSpan->_n; i++)
		{
			//_idSpanMap[kSpan->_pageId + i] = kSpan;
			_idSpanMap.set(kSpan->_pageId + i, kSpan);
		}

		return kSpan;
	}

	// 检查一下后面的桶里面有没有span，如果有可以把他它进行切分
	for (size_t i = k + 1; i < NPAGES; ++i)
	{
		if (!_spanLists[i].Empty())
		{
			Span* nSpan = _spanLists[i].PopFront();
			//Span* kSpan = new Span;
			Span* kSpan = _spanPool.New();

			// 在nSpan的头部切一个k页下来
			// k页span返回
			// nSpan再挂到对应映射的位置
			kSpan->_pageId = nSpan->_pageId;
			kSpan->_n = k;

			nSpan->_pageId += k;
			nSpan->_n -= k;

			_spanLists[nSpan->_n].PushFront(nSpan);	

			// 只需要存储nSpan的首尾页号跟nSpan映射，方便page cache回收内存
			// 进行合并查找
			//_idSpanMap[nSpan->_pageId] = nSpan;
			//_idSpanMap[nSpan->_pageId + nSpan->_n - 1] = nSpan;
			_idSpanMap.set(nSpan->_pageId, nSpan);
			_idSpanMap.set(nSpan->_pageId + nSpan->_n - 1, nSpan);

			// 建立id和span的映射	，方便central cache回收小块内存时，查找每一个对应的span
			for (PAGE_ID i = 0; i < kSpan->_n; i++)
			{
				_idSpanMap.set(kSpan->_pageId + i, kSpan);
			}

			auto ret = kSpan;
			//Span* span = MapObjectToSpan((void*)(kSpan->_pageId << 13));

			return kSpan;
		}
	}

	// 走到这个位置就说明后面没有大页的span了
	// 这时就去找堆要一个128页的span
	Span* bigSpan = _spanPool.New();
	void* ptr = SystemAlloc(NPAGES - 1);
	bigSpan->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
	bigSpan->_n = NPAGES - 1;

	_spanLists[bigSpan->_n].PushFront(bigSpan);

	return NewSpan(k);
}

// 获取从对象到span的映射
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

	// 其他线程在读写的过程中可能会改变这个结构，最后导致读取出错
	// map使用的红黑树，更改的时候可能导致树的旋转
	// 哈希表可能在扩容的时候出现问题，旧表还没有复制给新表，读取的还是原来的表，所以这就是为什么要加锁

	// 这个结构本身在读写的过程就很快
	// 它在映射之前就会把内存先开好，不管是插入还是删除都不会改变这个结构
	// 读写是分离的
	// 只要在NewSpan和ReleaseSpanToPageCache这两个函数中会去写入，但是在进入函数之前都是加了锁的
	// 其实不加锁也是可以的，因为不管是读写都不会对同一个页进行
	auto ret = (Span*)_idSpanMap.get(id);
	assert(ret != nullptr);
	return ret;
}

// 释放空间span回到PageCache，合并相邻的span
void PageCache::ReleaseSpanToPageCache(Span* span)
{
	if (span->_n > NPAGES - 1) // 大于128page，所以不是向pagecache要的，要直接释放
	{
		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		SystemFree(ptr);
		//delete span;
		_spanPool.Delete(span);

		return;
	}

	// 对span前后的页，尝试进行合并，缓解内存碎片问题
	while (1)
	{
		PAGE_ID prevId = span->_pageId - 1; // 前一个
		//auto ret = _idSpanMap.find(prevId);
		//// 前面的页号没有，不合并
		//if (ret == _idSpanMap.end())
		//{
		//	break;
		//}
		
		// 前面的页号没有，不合并
		auto ret = (Span*)_idSpanMap.get(prevId);
		if (ret == nullptr)
		{
			break;
		}
		
		// 前面相邻的span在使用
		Span* prevSpan = ret;
		if (prevSpan->_isUse == true)
		{
			break;
		}
		// 合并超过128页的span
		if (prevSpan->_n+span->_n > NPAGES - 1)
		{
			break;
		}

		span->_pageId = prevSpan->_pageId;
		span->_n += prevSpan->_n;

		_spanLists[prevSpan->_n].Erase(prevSpan);
		_spanPool.Delete(prevSpan);
	}

	// 向后合并
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