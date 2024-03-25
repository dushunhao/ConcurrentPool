#pragma once

#include "Common.h"

//inline static void* SystemAlloc(size_t kpage)
//{
//#ifdef _WIN32
//	void* ptr = VirtualAlloc(0, kpage * (1 << 12), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
//#else
//	// linux��brk mmap��
//#endif
//	if (ptr == nullptr)
//		throw std::bad_alloc();
//	return ptr;
//}

//�����ڴ��
//template<size_t N>
//class ObjectPool
//{};

template<class T>
class ObjectPool
{
public:
	T* New()
	{
		T* obj = nullptr;
		// ���Ȱѻ��������ڴ������ٴ��ظ�����
		if (_freeList)
		{
			void* next = *(void**)_freeList;
			obj = (T*)_freeList;
			_freeList = next;
			return obj;
		}
		else
		{
			// ʣ����ڴ治��һ������Ĵ�Сʱ�������¿����ռ�
			if (_remainBytes < sizeof(T))
			{
				_remainBytes = 128 * 1024;
				_memory = (char*)SystemAlloc(_remainBytes >> 13);
				if (_memory == nullptr)
				{
					throw std::bad_alloc();
				}
			}

			obj = (T*)_memory;
			size_t objSize = sizeof(T) < sizeof(void*) ? sizeof(void*) : sizeof(T);
			_memory += objSize;
			_remainBytes -= objSize;
		}
		// ��λnew����ʾ����T�Ĺ��캯����ʼ��
		new(obj)T;

		return obj;
	}

	void Delete(T* obj)
	{
		//��ʾ�������������������
		obj->~T();

		//ͷ��
		*(void**)obj = _freeList;
		_freeList = obj;

	}

private:
	char* _memory = nullptr;  // ָ�����ڴ��ָ��
	size_t _remainBytes = 0;  // ����ڴ����зֹ�����ʣ����ֽ���
	void* _freeList = nullptr;// ���������������ӵ����������ͷָ��
};