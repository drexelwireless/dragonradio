#ifndef SAFEQUEUE_H_
#define SAFEQUEUE_H_

#include <condition_variable>
#include <mutex>
#include <queue>

template<typename T>
class SafeQueue {
public:
    SafeQueue() : done(false) {};

    bool empty(void) const;

    void push(const T& val);
    void push(T&& val);
    void emplace(const T& val);
    void emplace(T&& val);
    void pop(T& val);

    void stop(void);

private:
    bool                    done;
    std::mutex              m;
    std::condition_variable cond;
    std::queue<T>           q;
};

template<typename T>
bool SafeQueue<T>::empty(void) const
{
    std::lock_guard<std::mutex> lock(m);

    return q.empty();
}

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
void SafeQueue<T>::emplace(const T& val)
{
    std::lock_guard<std::mutex> lock(m);

    q.emplace(val);
    cond.notify_one();
}

template<typename T>
void SafeQueue<T>::emplace(T&& val)
{
    std::lock_guard<std::mutex> lock(m);

    q.emplace(std::move(val));
    cond.notify_one();
}

template<typename T>
void SafeQueue<T>::pop(T& val)
{
    std::unique_lock<std::mutex> lock(m);

    cond.wait(lock, [this]{ return done || !q.empty(); });
    if (done)
        return;
    val = std::move(q.front());
    q.pop();
}

template<typename T>
void SafeQueue<T>::stop(void)
{
    done = true;
    cond.notify_all();
}

#endif /* SAFEQUEUE_H_ */
