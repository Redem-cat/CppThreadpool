
//看不懂这段代码
template<typename Func, typename... Arges>
auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func())
{
	auto RType = decltype(func(args...));
	auto task = std::make_shared<std::packaged_task<RType()>>(
		std::bind(std::forward<Func>(func), std::forward<Args>(args)...)
	);
	std::future<RType> retult= task->get_future();

	//获取锁
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	if (!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]()->bool {return taskQue_.size() < (size_t)taskQueMaxThresHold_; }))
	{
		std::cerr << "task queue full, submit task fail" << std::endl;
		auto task=std::make_shared<std::packaged_task<RType()>>(
			[]()->RType {return RType(); }
		);
		return task->get_future();
	}
	taskQue_.emplace([task]() { (*task)(); });
	taskSize_++;
	notEmpty_.notify_all();
}