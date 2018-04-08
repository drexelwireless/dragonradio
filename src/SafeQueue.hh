#ifndef SAFEQUEUE_H_
#define SAFEQUEUE_H_

#include <condition_variable>
#include <mutex>
#include <queue>

template<typename T>
class SafeQueue {
public:
    void push(const T& val);
    void push(T&& val);
    void pop(T& val);
    bool maybePop(T& val);

private:
    std::mutex              m;
    std::condition_variable cond;
    std::queue<T>           q;
};

template<typename T>
void SafeQueue<T>::push(const T& val)
{
    std::lock_guard<std::mutex> lock(m);

    q.push(val);
    cond.notify_one();
}

template<typename T>
void SafeQueue<T>::push(T&& val)
{
    std::lock_guard<std::mutex> lock(m);

    q.push(std::move(val));
    cond.notify_one();
}

template<typename T>
void SafeQueue<T>::pop(T& val)
{
    std::unique_lock<std::mutex> lock(m);

    cond.wait(lock, [this]{ return !q.empty(); });
    val = std::move(q.front());
    q.pop();
}

template<typename T>
bool SafeQueue<T>::maybePop(T& val)
{
    std::unique_lock<std::mutex> lock(m);

    if (q.empty())
        return false;

    val = std::move(q.front());
    q.pop();

    return true;
}

#endif /* SAFEQUEUE_H_ */
