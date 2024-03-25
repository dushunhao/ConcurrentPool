#include "ThreadCache.h"
#include "CentralCache.h"

void* ThreadCache::FetchFromCentralCache(size_t index, size_t size)
{
	// ����ʼ���������㷨
	// �ʼ����һ����central cacheҪһ������Ҫ̫�࣬��ΪҪ̫���˿����ò���
	// ���������size��С�ڴ�������ôbatchNum�ͻ᲻��������ֱ������
	// sizeԽ��һ����central cacheҪ��batchNum��ԽС
	// sizeԽС��һ����central cacheҪ��batchNum��Խ��
	size_t batchNum = min(_freeLists[index].MaxSize(), SizeClass::NumMoveSize(size));// ͨ����Ҫ��::������windows.h����min��
	if (_freeLists[index].MaxSize() == batchNum)
	{
		_freeLists[index].MaxSize() += 1;
	}

	void* start = nullptr;
	void* end = nullptr;
	size_t actualNum = CentralCache::GetInStance()->FetchRangeObj(start, end, batchNum, size);
	assert(actualNum > 0);

	if (actualNum == 1)
	{
		assert(start == end);
		return start;
	}
	else
	{
		_freeLists[index].PushRange(NextObj(start), end, actualNum - 1);
		return start;
	}
}

void* ThreadCache::Allocate(size_t size)
{
	assert(size < MAX_BYTES); // ������ֽ�һ��ҪС�����Ŀ������ֽ�
	size_t alignSize = SizeClass::RoundUp(size);
	size_t index = SizeClass::Index(size);

	if (!_freeLists[index].Empty())
	{
		return _freeLists[index].Pop();
	}
	else
	{
		return FetchFromCentralCache(index, alignSize);
	}
}

void ThreadCache::Deallocate(void* ptr, size_t size)
{
	assert(ptr);
	assert(size <= MAX_BYTES);

	// �ҳ�ӳ�����������Ͱ������������
	size_t index = SizeClass::Index(size);
	_freeLists[index].Push(ptr);
	 
	// ... 
	// �������ȴ���һ������������ڴ�ʱ�Ϳ�ʼ��һ��list��central cache
	if (_freeLists[index].Size() >= _freeLists[index].MaxSize())
	{
		ListTooLong(_freeLists[index], size);
	}

}

// �ͷŶ���ʱ���������ʱ�������ڴ�ص����Ļ���
void ThreadCache::ListTooLong(FreeList& list, size_t size)
{
	void* start = nullptr;
	void* end = nullptr;
	list.PopRange(start, end, list.MaxSize());

	CentralCache::GetInStance()->ReleaseListToSpans(start, size);
}