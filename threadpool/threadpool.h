#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <iostream>
#include <vector>
#include <queue>
#include <atomic>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <thread>
#include <chrono>
#include <unordered_map>

// --- Any 类 (保持不变) ---
class Any {
public:
    Any() = default;
    ~Any() = default;
    Any(const Any&) = delete;
    Any& operator=(const Any&) = delete;
    Any(Any&&) = default;
    Any& operator=(Any&&) = default;

    template<typename T>
    Any(T data) : base_(std::make_unique<Derive<T>>(data)) {}

    template<typename T>
    T cast_() {
        Derive<T>* pd = dynamic_cast<Derive<T>*>(base_.get());
        if (pd == nullptr) {
            throw std::runtime_error("type is incompatible!");
        }
        return pd->data_;
    }
private:
    class Base {
    public:
        virtual ~Base() = default;
    };
    template<typename T>
    class Derive : public Base {
    public:
        Derive(T data) : data_(data) {}
        T data_;
    };
    std::unique_ptr<Base> base_;
};

// --- 信号量类 (保持不变) ---
class Semaphore {
public:
    Semaphore(int limit = 0) : resLimit_(limit) {}
    ~Semaphore() = default;

    void wait() {
        std::unique_lock<std::mutex> lock(mtx_);
        cond_.wait(lock, [&]()->bool { return resLimit_ > 0; });
        --resLimit_;
    }

    void post() {
        std::unique_lock<std::mutex> lock(mtx_);
        ++resLimit_;
        cond_.notify_one();
    }
private:
    int resLimit_;
    std::mutex mtx_;
    std::condition_variable cond_;
};

class Task;

// --- 新增：ResultState ---
// 用于在 Task 和 Result 之间共享状态
struct ResultState {
    Any any_;
    Semaphore sem_;
    std::atomic_bool isValid_{ true };
};

// --- Result 类 ---
class Result {
public:
    // Result 现在持有共享状态，而不是持有 Task
    Result(std::shared_ptr<ResultState> state, bool isValid = true);
    ~Result() = default;

    Any get();
private:
    std::shared_ptr<ResultState> state_; // 共享状态
    bool isValid_;
};

// --- Task 抽象基类 ---
class Task {
public:
    Task();
    virtual ~Task() = default;
    void exec();
    // 设置共享状态
    void setResultState(std::shared_ptr<ResultState> state);
    virtual Any run() = 0;
private:
    // Task 持有共享状态的指针
    std::shared_ptr<ResultState> state_;
};

enum class PoolMode {
    MODE_FIXED,
    MODE_CACHED,
};

// --- Thread 类 ---
class Thread {
public:
    using ThreadFunc = std::function<void(int)>; // 接收 threadId

    Thread(ThreadFunc func);
    ~Thread();
    void start();
    int getId() const;
private:
    ThreadFunc func_;
    static std::atomic_int generateId_; // 优化：改为原子变量
    int threadId_;
};

// --- ThreadPool 类 ---
class ThreadPool {
public:
    ThreadPool();
    ~ThreadPool();

    void setMode(PoolMode mode);
    void setTaskQueMaxThreshold(int threshold);
    void setThreadSizeThreshold(int threshold);

    Result subMitTask(std::shared_ptr<Task> sp);
    void start(int initThreadSize = 4);

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

private:
    void threadFunc(int threadId);
    bool checkRunningState() const; // 优化：const修饰

private:
    // 线程列表
    std::unordered_map<int, std::unique_ptr<Thread>> threads_;

    size_t initThreadSize_;
    int threadSizeThreshold_;
    std::atomic_int curThreadSize_;
    std::atomic_int idleThreadSize_;

    std::queue<std::shared_ptr<Task>> taskQue_;
    std::atomic_int taskSize_;
    int TaskQueMaxThreshold_;

    std::mutex taskQueMtx_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    std::condition_variable exitCond_;

    PoolMode poolMode_;
    std::atomic_bool isPoolRunning_;
};

#endif