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

    // 问题修复：之前的代码在循环里直接 return 了，并没有求和
    Any run() override {
        std::cout << "tid:" << std::this_thread::get_id() << " is running" << std::endl;
        uLong sum = 0; // 使用 uLong 防止溢出
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
    pool.start(2); // 初始2个线程

    // 提交任务
    Result res1 = pool.subMitTask(std::make_shared<MyTask>(1, 100000000));
    Result res2 = pool.subMitTask(std::make_shared<MyTask>(100000001, 200000000));
    Result res3 = pool.subMitTask(std::make_shared<MyTask>(200000001, 300000000));

    // 提交其他无需关注返回值的任务
    pool.subMitTask(std::make_shared<MyTask>(300000001, 400000000));
    pool.subMitTask(std::make_shared<MyTask>(400000001, 500000000));

    // 获取结果 (会阻塞直到任务完成)
    uLong sum1 = res1.get().cast_<uLong>();
    uLong sum2 = res2.get().cast_<uLong>();
    uLong sum3 = res3.get().cast_<uLong>();

    std::cout << "Result 1: " << sum1 << std::endl;
    std::cout << "Result 2: " << sum2 << std::endl;
    std::cout << "Result 3: " << sum3 << std::endl;

    // 简单的数学验证 (1 to 300000000)
    // 等差数列求和: (1 + 300000000) * 300000000 / 2

    std::cout << "Main continues..." << std::endl;

    // 让主线程等待一下，观察线程池的析构过程（可选）
    std::this_thread::sleep_for(std::chrono::seconds(2));

    return 0;
    // main结束时 pool 析构，会等待所有任务执行完毕
}