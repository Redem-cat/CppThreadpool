#include<iostream>
#include"threadpool.h"
#include<thread>
#include<chrono>
using uLong = unsigned long long;

class MyTask : public Task {
	public:
	MyTask(int begin,int end)
			:begin_(begin),end_(end) {}
	Any run() {
		std::cout << "tid:" << std::this_thread::get_id() << " is running" << std::endl;
		int sum = 0;
		for (int i = begin_; i <= end_; i++) {
			sum += i;
			std::cout << "tid:" << std::this_thread::get_id() << " i=" << i << std::endl;
			return sum;
		}
	}
	private:
		int begin_;
		int end_;
};

void getChar() {
	std::cout << "press any key to continue..." << std::endl;
	std::cin.get();
}
int main() {
	{
	ThreadPool pool;
	pool.setMode(PoolMode::MODE_CACHED);
	pool.start(4);


	Result res1 = pool.subMitTask(std::make_shared<MyTask>(1, 10000000));
	Result res2 = pool.subMitTask(std::make_shared<MyTask>(100001, 20000000));
	Result res3 = pool.subMitTask(std::make_shared<MyTask>(200001, 30000000));
	pool.subMitTask(std::make_shared<MyTask>(300001, 40000000));
	pool.subMitTask(std::make_shared<MyTask>(400001, 50000000));
	pool.subMitTask(std::make_shared<MyTask>(500001, 60000000));

	uLong sum1 = res1.get().cast_<uLong>();  //转换为uLong类型
	uLong sum2 = res2.get().cast_<uLong>();
	uLong sum3 = res3.get().cast_<uLong>();

	std::cout << (sum1 + sum2 + sum3) << std::endl;
}
	getChar();
}