#include "threadpool.h"

const int TASK_MAX_THRESHOLD = 1024;
const int THREAD_MAX_THRESHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 60; //单位为秒

//线程池构造函数
ThreadPool::ThreadPool()
	: initThreadSize_(0)
	, taskSize_(0)
	, curThreadSize_(0)
	, idleThreadSize_(0)
	, threadSizeThreshold_(THREAD_MAX_THRESHOLD)
	, TaskQueMaxThreshold_(TASK_MAX_THRESHOLD)
	, poolMode_(PoolMode::MODE_FIXED) 
	, isPoolRunning_(false)
	{}
//线程池析构函数
ThreadPool::~ThreadPool() {
	isPoolRunning_ = false;
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	exitCond_.wait(lock, [&]()->bool {return threads_.size()==0; });
} 

void ThreadPool::setMode(PoolMode mode) {
	if (checkRunningState())
		return;
	poolMode_ = mode;
}

void ThreadPool::setTaskQueMaxThreshold(int threshold) {
	if (checkRunningState())
		return;
	TaskQueMaxThreshold_ = threshold;
}

void ThreadPool::setThreadSizeThreshold(int threshold) {
	if (checkRunningState())
		return;
	if (poolMode_==PoolMode::MODE_CACHED)
	threadSizeThreshold_ = threshold; 
}

Result ThreadPool::subMitTask(std::shared_ptr<Task> sp) {
	std::unique_lock<std::mutex>lock(taskQueMtx_);
	//获取锁
	//线程的通信,等待任务队列空余,使用wait进行阻塞等待
	if (!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]()->bool { return taskQue_.size() < TaskQueMaxThreshold_; })) {
		std::cerr << "task que full, submit task fail" << std::endl;
		return Result(sp, false);
		//之所以采用这种方式而不是在Task里面写一个getRes方法是因为Result的生命周期长于Task
	}
	//如果有空余,将任务添加到任务队列中
	taskQue_.emplace(sp);
	++taskSize_;
	//同时进行notEmpty通知
	notEmpty_.notify_all();
	//cached模式,任务处理比较紧急,适合小而快的任务
	if (poolMode_ == PoolMode::MODE_CACHED && taskSize_ > idleThreadSize_ && curThreadSize_ < threadSizeThreshold_) {
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this,std::placeholders::_1));
		//threads_.emplace_back(std::move(ptr));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		threads_[threadId]->start();
		curThreadSize_++;
		idleThreadSize_++;
	}

	return Result(sp);
}

void ThreadPool::start(int initThreadSize) {
	//设置线程池启动状态
	isPoolRunning_ = true;
	//记录初始线程数量
	initThreadSize_ = initThreadSize;
	curThreadSize_ = initThreadSize;
	//创建线程对象，放入线程池中
	for (int i = 0; i < initThreadSize_; i++){
		auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));

		//emplace_back直接在容器末尾构造对象，避免了拷贝或移动操作
		//而push_back是将已有对象拷贝或移动到容器末尾
		//为了保证线程启动的公平,先全部创建线程对象，再统一启动
	}
	for (int i = 0; i < initThreadSize_; i++) {
		threads_[i]->start();
		idleThreadSize_++;
	}
}
	//线程函数的实现
void ThreadPool::threadFunc(int threadId) {
	//线程函数返回了,线程就结束了
	//std::cout << "begin threadFunc: " <<  std::this_thread::get_id() << std::endl;	
	//std::cout << "end threadFunc" << std::endl;

	auto lastTime = std::chrono::high_resolution_clock::now();

	for (;;) {
		std::shared_ptr<Task> task;
		{
			//先获取锁
			std::unique_lock<std::mutex> lock(taskQueMtx_);
			std::cout << "tid" << std::this_thread::get_id() << " try to get task..." << std::endl;
			//等待notEmpty条件变量,等待任务队列不为空
			if (poolMode_ == PoolMode::MODE_CACHED) {

				while (taskQue_.size() == 0) {
					//条件变量, 超时返回了
					if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
						auto now = std::chrono::high_resolution_clock::now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() > THREAD_MAX_IDLE_TIME && curThreadSize_ > initThreadSize_) {
							//回收当前线程,注意不能将初始线程数量以下的线程回收掉
							threads_.erase(threadId);//不要用getId,因为threadId是我们的自定义id,getId返回的是系统线程id
							//修改记录线程数量的相关变量
							//把线程对象从线程池中移除
							curThreadSize_--;
							idleThreadSize_--;
							std::cout << "threadid: " << std::this_thread::get_id() << " is over idle time, auto exit" << std::endl;
							return;
						}
					}
				}
			}
			else {
				notEmpty_.wait(lock);
			}
			if (!isPoolRunning_) {
				threads_.erase(threadId);
				std::cout << "threadid: " << std::this_thread::get_id() << " is over idle time, auto exit" << std::endl;
				exitCond_.notify_all();
				return;
			}
			idleThreadSize_--;

			notEmpty_.wait(lock,
				[&]()->bool { return !taskQue_.empty(); });
			//从任务队列中取出任务
			std::cout << "tid" << std::this_thread::get_id() << " 获取任务成功" << std::endl;
			task = taskQue_.front();
			taskQue_.pop();
			--taskSize_;
			if (taskQue_.size() > 0) {
				notEmpty_.notify_all();
			}
			//通知生产者任务队列不为满
			notFull_.notify_all();
		}//通过局部作用域的方式，及时释放锁
		if (task != nullptr) {
			//当前线程执行任务
			task->exec();//执行任务,任务执行完后会将结果存储到Result对象中
		}
		lastTime = std::chrono::high_resolution_clock::now();
		idleThreadSize_++;
	}
} 

bool ThreadPool::checkRunningState()  {
	return isPoolRunning_;
}

int Thread::generateId_ = 0;

Thread::Thread(ThreadFunc func) {
	func_ = func;	
	threadId_ = ++generateId_;
}

Thread::~Thread() {}

////线程方法的实现
void Thread::start() {
	//线程启动代码
	std::thread t(func_,threadId_);
	t.detach();
}

int Thread::getId() {
	return threadId_;
}

////Task方法的实现
Task::Task()//构造函数不需要写类型
	:result_(nullptr) {
}

void Task::exec() {
	if (result_ != nullptr)
		result_->setVal(run()); //发生了多态调用
}
void Task::setResult(Result* res) {
	result_ = res;
}
////Result方法的实现
Result::Result(std::shared_ptr<Task> task, bool isValid)
    : task_(task), isValid_(isValid) {
}
Any Result::get() {
	if (!isValid_) {
		throw std::runtime_error("Invalid Result: Task submission failed.");
	}
	//等待任务完成的信号量
	sem_.wait(); //task没有执行完之前会阻塞在这里
	return std::move(any_);
}

void Result::setVal(Any any) {
	//存储任务返回值
	this->any_ = std::move(any);//拷贝构造的消耗很大,使用移动语义提升效率
	sem_.post(); //任务执行完，发送信号量通知已经获取到结果
}

