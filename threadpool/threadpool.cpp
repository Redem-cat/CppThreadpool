#include "threadpool.h"
#include <iostream>

const int TASK_MAX_THRESHOLD = 1024;
const int THREAD_MAX_THRESHOLD = 100;
const int THREAD_MAX_IDLE_TIME = 60;

// --- ThreadPool 实现 ---

ThreadPool::ThreadPool()
    : initThreadSize_(0)
    , taskSize_(0)
    , curThreadSize_(0)
    , idleThreadSize_(0)
    , threadSizeThreshold_(THREAD_MAX_THRESHOLD)
    , TaskQueMaxThreshold_(TASK_MAX_THRESHOLD)
    , poolMode_(PoolMode::MODE_FIXED)
    , isPoolRunning_(false)
{
}

ThreadPool::~ThreadPool() {
    isPoolRunning_ = false;

    // 重要：必须先通知所有线程，它们才能从 wait 中醒来并退出
    std::unique_lock<std::mutex> lock(taskQueMtx_);
    notEmpty_.notify_all();

    // 等待所有线程结束（线程在退出前会从 threads_ 中移除自己）
    exitCond_.wait(lock, [&]()->bool { return threads_.size() == 0; });
}

void ThreadPool::setMode(PoolMode mode) {
    if (checkRunningState()) return;
    poolMode_ = mode;
}

void ThreadPool::setTaskQueMaxThreshold(int threshold) {
    if (checkRunningState()) return;
    TaskQueMaxThreshold_ = threshold;
}

void ThreadPool::setThreadSizeThreshold(int threshold) {
    if (checkRunningState()) return;
    if (poolMode_ == PoolMode::MODE_CACHED)
        threadSizeThreshold_ = threshold;
}

Result ThreadPool::subMitTask(std::shared_ptr<Task> sp) {
    // 1. 创建共享状态 ResultState
    auto state = std::make_shared<ResultState>();
    // 2. 将状态告知 Task
    sp->setResultState(state);

    std::unique_lock<std::mutex> lock(taskQueMtx_);

    // 等待任务队列不满（最长阻塞1秒）
    if (!notFull_.wait_for(lock, std::chrono::seconds(1),
        [&]()->bool { return taskQue_.size() < (size_t)TaskQueMaxThreshold_; })) {
        std::cerr << "task que full, submit task fail" << std::endl;
        // 返回一个无效的 Result
        return Result(state, false);
    }

    taskQue_.emplace(sp);
    ++taskSize_;
    notEmpty_.notify_all();

    // Cached模式下根据需求创建新线程
    if (poolMode_ == PoolMode::MODE_CACHED
        && taskSize_ > idleThreadSize_
        && curThreadSize_ < threadSizeThreshold_) {

        // 创建新线程
        auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
        int threadId = ptr->getId();
        threads_.emplace(threadId, std::move(ptr));
        threads_[threadId]->start(); // 启动

        curThreadSize_++;
        idleThreadSize_++;
    }

    // 3. 返回持有相同状态的 Result
    return Result(state, true);
}

void ThreadPool::start(int initThreadSize) {
    isPoolRunning_ = true;
    initThreadSize_ = initThreadSize;
    curThreadSize_ = initThreadSize;

    // 创建线程
    for (int i = 0; i < initThreadSize_; i++) {
        auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
        int threadId = ptr->getId();
        threads_.emplace(threadId, std::move(ptr));
    }

    // 启动线程
    for (int i = 0; i < initThreadSize_; i++) {
        threads_[i]->start(); // start 调用后 threadFunc 开始运行
        idleThreadSize_++;    // 记录空闲线程
    }
}

void ThreadPool::threadFunc(int threadId) {
    auto lastTime = std::chrono::high_resolution_clock::now();

    for (;;) {
        std::shared_ptr<Task> task;
        {
            std::unique_lock<std::mutex> lock(taskQueMtx_);

            while (taskQue_.size() == 0) {
                // 如果线程池结束，回收资源并退出
                if (!isPoolRunning_) {
                    threads_.erase(threadId);
                    exitCond_.notify_all();
                    return;
                }

                if (poolMode_ == PoolMode::MODE_CACHED) {
                    // Cached模式：超时回收机制
                    if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
                        auto now = std::chrono::high_resolution_clock::now();
                        auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
                        if (dur.count() >= THREAD_MAX_IDLE_TIME && curThreadSize_ > initThreadSize_) {
                            // 回收当前线程
                            threads_.erase(threadId);
                            curThreadSize_--;
                            idleThreadSize_--;
                            return;
                        }
                    }
                }
                else {
                    // Fixed模式：一直等待直到有任务或线程池销毁
                    notEmpty_.wait(lock);
                }
            }

            // 双重检查：因为可能被 notify_all 唤醒但线程池要停止了
            if (!isPoolRunning_ && taskQue_.empty()) {
                threads_.erase(threadId);
                exitCond_.notify_all();
                return;
            }

            idleThreadSize_--;

            task = taskQue_.front();
            taskQue_.pop();
            --taskSize_;

            if (taskQue_.size() > 0) {
                notEmpty_.notify_all();
            }
            notFull_.notify_all();
        } // 释放锁

        if (task != nullptr) {
            task->exec();
        }

        idleThreadSize_++;
        lastTime = std::chrono::high_resolution_clock::now(); // 更新最后工作时间
    }
}

bool ThreadPool::checkRunningState() const {
    return isPoolRunning_;
}

// --- Thread 实现 ---

std::atomic_int Thread::generateId_ = 0; // 初始化

Thread::Thread(ThreadFunc func)
    : func_(func)
    , threadId_(generateId_++) // 原子自增
{
}

Thread::~Thread() {}

void Thread::start() {
    // 使用 lambda 捕获 threadId 传给 func_
    std::thread t(func_, threadId_);
    t.detach(); // 分离线程
}

int Thread::getId() const {
    return threadId_;
}

// --- Task 实现 ---

Task::Task() : state_(nullptr) {}

void Task::exec() {
    if (state_) {
        // 多态调用 run，并将结果存入共享状态
        Any ret = run();
        state_->any_ = std::move(ret);
        state_->sem_.post(); // 通知 Result 数据已准备好
    }
}

void Task::setResultState(std::shared_ptr<ResultState> state) {
    state_ = state;
}

// --- Result 实现 ---

Result::Result(std::shared_ptr<ResultState> state, bool isValid)
    : state_(state), isValid_(isValid) {
}

Any Result::get() {
    if (!isValid_ || !state_) {
        throw std::runtime_error("Invalid Result: Task submission failed.");
    }
    // 阻塞等待任务完成
    state_->sem_.wait();
    return std::move(state_->any_);
}