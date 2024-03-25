#include "Object.h"

int main()
{
	ObjectPool<int> oj;
	oj.New();

	int* p = (int*)malloc(128);

	oj.Delete(p);


	return 0;
}