#include <iostream>
#include "threadpool.h"
#include <thread>
#include <chrono>

using uLong = unsigned long long;

class MyTask : public Task {
public:
    MyTask(int begin, int end)
        : begin_(begin), end_(end) {
    }

    // Bug fix: previous code returned directly in the loop without summing
    Any run() override {
        std::cout << "tid:" << std::this_thread::get_id() << " is running" << std::endl;
        uLong sum = 0; // Use uLong to prevent overflow
        for (int i = begin_; i <= end_; i++) {
            sum += i;
        }
        // std::cout << "tid:" << std::this_thread::get_id() << " end" << std::endl;
        return sum;
    }

private:
    int begin_;
    int end_;
};

int main()
{
    ThreadPool pool;
    // 设置为 CACHED 模式，线程数根据任务量动态调整
    pool.setMode(PoolMode::MODE_CACHED);
    pool.start(2); // Initial 2 threads

    // Submit tasks
    Result res1 = pool.subMitTask(std::make_shared<MyTask>(1, 100000000));
    Result res2 = pool.subMitTask(std::make_shared<MyTask>(100000001, 200000000));
    Result res3 = pool.subMitTask(std::make_shared<MyTask>(200000001, 300000000));

    // 提交其他无需关注返回值的任务
    pool.subMitTask(std::make_shared<MyTask>(300000001, 400000000));
    pool.subMitTask(std::make_shared<MyTask>(400000001, 500000000));

    // Get results (will block until tasks complete)
    uLong sum1 = res1.get().cast_<uLong>();
    uLong sum2 = res2.get().cast_<uLong>();
    uLong sum3 = res3.get().cast_<uLong>();

    std::cout << "Result 1: " << sum1 << std::endl;
    std::cout << "Result 2: " << sum2 << std::endl;
    std::cout << "Result 3: " << sum3 << std::endl;

    // Simple mathematical verification (1 to 300000000)
    // Arithmetic sequence sum: (1 + 300000000) * 300000000 / 2

    std::cout << "Main continues..." << std::endl;

    // Let main thread wait a bit to observe thread pool destruction process (optional)
    std::this_thread::sleep_for(std::chrono::seconds(2));

    return 0;
    // When main ends, pool destructs and waits for all tasks to complete
}