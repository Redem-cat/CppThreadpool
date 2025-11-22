#ifndef THREADPOOL_H
#define THREADPOOL_H
//头文件守卫：每个头文件都应包含防止重复包含的机制
//头文件和源文件一般命名需要保持一致性
//头文件主要用于声明，作为接口提供给其他文件使用

#include<iostream>
#include<vector>
#include<queue>
#include<atomic>
#include<memory>
#include<mutex>
#include<condition_variable>
#include<functional>
#include<thread>
#include<chrono>
#include<unordered_map>

class Any{
public:
	Any() = default;
	~Any() = default;
	Any(const Any&) = delete;
	Any& operator=(const Any&) = delete;
	Any(Any&&) = default;
	Any& operator=(Any&&) = default;
	//模板不能将声明和实现分离,需要放在头文件中
	template<typename T>
	Any(T data) {
		base_ = std::make_shared<Derive<T>>(data);
	}

	//该方法将Any对象中存储的data转化为指定类型T并返回
	template<typename T>
	T cast_() {
		//将一个基类指针转换为派生类指针
		Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
		if (pd == nullptr) {
			throw"type is incompitable!";
		}
		return pd->data_;
	}
private:
	class Base {
	public:
		virtual ~Base() {}
	//当通过基类指针（如 Base*）删除派生类对象时，如果基类的析构函数不是虚函数，
	// 会导致 未定义行为（通常是派生类部分未被正确释放，引发内存泄漏）。
	};
	//派生类类型
	template<typename T>
	class Derive : public Base {
	public:
		Derive(T data) :data_(data) {}
		T data_;
	};
private:
	//定义一个Base类的指针
	std::unique_ptr<Base> base_;
};

//实现一个信号量类
class Semaphore {
public:
	Semaphore(int limit = 0)
		:resLimit_(limit) 
	{}
	~Semaphore() = default;
	void wait() {
		//获取一个信号量资源
	std::unique_lock<std::mutex> lock(mtx_);
	//等待信号量有资源,没有资源则会阻塞等待
	cond_.wait(lock, [&]()->bool {return resLimit_ > 0; });
	--resLimit_;
	}
	void post() {
		//释放一个信号量资源
	std::unique_lock<std::mutex> lock(mtx_);
		++resLimit_;
		cond_.notify_one();
	}
private:
	int resLimit_; //资源数量
	std::mutex mtx_;
	std::condition_variable cond_;
};

class Task;//前向声明

//通过Semaphore实现线程间的通信以接收线程返回值result
class Result {
public:
	Result(std::shared_ptr<Task>task, bool isValid=true);
	~Result() = default;
	//setVal方法,获取任务返回值
	void setVal(Any any);
	//get方法,用户调用这个方法获取任务的返回值
	Any get();
private:
	//存储任意类型的返回值Any作为成员变量
	Any any_;
	Semaphore sem_;//信号量对象
	std::shared_ptr<Task> task_;//指向对应获取返回值的任务对象
	std::atomic_bool isValid_;//结果是否有效
};

//任务抽象基类
//用户可以自定义任意任务类型，从Task继承，重写run方法，实现自定义任务处理
class Task
{
public:
	Task();
	~Task() = default;
	void exec();
	void setResult(Result* res);
	virtual Any run() = 0;
private:
	Result* result_;
};

enum class PoolMode
{
	MODE_FIXED, //固定数量的线程
	MODE_CACHED,//线程数量可动态增长
};

//线程类型
class Thread
{
public:
	using ThreadFunc = std::function<void()>; //线程函数类型别名

	Thread(ThreadFunc func);

	~Thread();//线程析构函数
	void start(); //启动线程
	int getId();//获取线程ID
private:
	ThreadFunc func_; //线程函数
	static int generateId_; //静态成员变量，用于生成唯一线程ID
	int threadId_; //线程ID
};

/*
example:
ThreadPool pool;
pool.start(4);
class MyTask : public Task {
public:
	void run() override {
		// Task implementation
	}
};
pool.subMitTask(std::make_shared<MyTask>());
*/

//线程池类型
class ThreadPool
{
public:
	ThreadPool();
	~ThreadPool();

	void setMode(PoolMode mode); 

	void setTaskQueMaxThreshold(int threshold); 

	void setThreadSizeThreshold(int threshold);

	Result subMitTask(std::shared_ptr<Task> sp);//提交任务

	void start(int initThreadSize=4); //启动线程池

	//= delete 是C++11引入的新特性，称为 “删除函数”。它明确地告诉编译器和程序员：“这个函数被有意地、彻底地删除了，不允许使用”。
	
	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;//明确禁止线程池对象被复制和赋值

private:

	void threadFunc(int threadId); //线程处理函数,每个线程在创建时都会运行该函数
	//这里也可以选择在thread类中实现线程处理函数,然后使用友元的方式访问
	bool checkRunningState();
private:
	std::unordered_map<int, std::unique_ptr <Thread>> threads_; 

	//线程池中的线程集合，用后缀下划线表示成员变量,下划线在前面的一般是C++库的变量
	size_t initThreadSize_; 
	int threadSizeThreshold_;
	std::atomic_int curThreadSize_;
	std::atomic_int idleThreadSize_; //空闲线程数量

	std::queue<std::shared_ptr<Task>> taskQue_; 
	std::atomic_int taskSize_;
	int TaskQueMaxThreshold_;

	std::mutex taskQueMtx_; //任务队列互斥锁，保证任务队列的线程安全
	std::condition_variable notEmpty_; //任务队列不为空条件变量
	std::condition_variable notFull_; //任务队列不为满条件变量
	std::condition_variable exitCond_;
	//条件变量：一个线程等待某个条件成立，而另一个线程则在条件成立时通知它
	//条件变量通常与互斥锁一起使用，以避免竞态条件

	PoolMode poolMode_;//线程池模式
	std::atomic_bool isPoolRunning_;//标识线程池是否正在运行,如果正在运行将不允许改变模式
	//使用atomic_bool避免竞态条件
	

};

#endif

