#ifndef PFUN_H
#define PFUN_H

#include <atomic>
#include <pthread.h>

class IPFunTask
{
public:
    virtual void run() const = 0;
};

template<typename T>
class PFunTask : public IPFunTask
{
public:
    PFunTask(const T &t) :task(t) {};
    void run() const {task();}
private:
    const T &task;
};

class PFun {
public:
    PFun() :fun(nullptr), isRunning(false)
    {
        pthread_t tid;
        pthread_create(&tid, NULL, mainLoop, this);
    }

    void run(const IPFunTask *f)
    {
        fun = f;
        isRunning.store(true);
    }

    void wait() const
    {
        while (isRunning.load() == true);
    }

private:
    static void *mainLoop(void *self)
    {
        PFun *pfun = (PFun *)self;
        while(true)
        {
            while (pfun->isRunning.load() == false);
            pfun->fun->run();
            pfun->isRunning.store(false);
        }
    }

    const IPFunTask *fun;
    std::atomic<bool> isRunning;
};

#endif // PFUN_H
