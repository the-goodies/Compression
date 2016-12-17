#include "lib/Array.h"
#include "lib/PriorityQueue.h"
//#include "bitstream.h"
#include "compression.h"

using std::cout;
using std::endl;
using std::cin;

template <typename cmp>
bool compare(const cmp & a,const cmp & b)
{
	cout << *a << " " << *b;
	return true;
}

template <typename compareType>
void foo(bool (*cmp)(const compareType & a,const compareType & b), compareType a, compareType b)
{
	cmp(a,b);
}


#include "lib/sorting.h"

int main()
{
	int* a = new int(10);
	int* b = new int(20);
	int c = 5;
	int d = 10;
	foo(compare, a, b);
	Array<int> la{ 2, 3, 1, -12, 41, 421, 42 };
	mergeSort(la);
	cout << "array: " << la << endl;
	
	ifbitstream instream("taip.txt");
	ofbitstream outstream("out.txt");
	compress(instream, outstream);

	instream.open("out.txt");
	for (int i = 0; i < instream.size() * 8; ++i)
	{
		cout << i << ". " << instream.readBit() << endl;
	}
	
	system("pause");
}