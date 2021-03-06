// @copyright 2017-2018 zzu_softboy <zzu_softboy@163.com>
//
// THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
// OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
// IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
// NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
// THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Created by softboy on 2018/03/27.


#include "gtest/gtest.h"
#include "pdk/base/os/thread/Runnable.h"
#include "pdk/base/os/thread/ThreadPool.h"
#include "pdk/base/os/thread/Semaphore.h"
#include "pdk/base/time/DateTime.h"
#include "pdk/base/time/Time.h"
#include "pdk/base/lang/String.h"
#include "pdktest/PdkTest.h"
#include "pdk/global/Random.h"
#include <mutex>
#include <iostream>

using FunctionPointer =  void (*)();
using pdk::os::thread::Runnable;
using pdk::os::thread::ThreadPool;
using pdk::os::thread::AtomicInt;
using pdk::os::thread::Semaphore;
using pdk::os::thread::Thread;
using pdk::os::thread::AtomicPointer;
using pdk::os::thread::SemaphoreReleaser;
using pdk::time::Time;

PDKTEST_DECLARE_APP_STARTUP_ARGS();

class FunctionPointerTask : public Runnable
{
public:
   FunctionPointerTask(FunctionPointer function)
      :function(function)
   {}
   
   void run()
   {
      function();
   }
private:
   FunctionPointer function;
};

Runnable *create_task(FunctionPointer pointer)
{
   return new FunctionPointerTask(pointer);
}

static std::mutex sg_funcTestMutex;

int testFunctionCount;

void sleep_test_function()
{
   pdktest::sleep(1000);
   ++testFunctionCount;
}

void empty_function()
{}

void no_sleep_test_function()
{
   ++testFunctionCount;
}

void sleep_test_function_mutex()
{
   pdktest::sleep(1000);
   sg_funcTestMutex.lock();
   ++testFunctionCount;
   sg_funcTestMutex.unlock();
}

void no_sleep_test_function_mutex()
{
   sg_funcTestMutex.lock();
   ++testFunctionCount;
   sg_funcTestMutex.unlock();
}

TEST(ThreadPoolTest, testRunFunction)
{
   PDKTEST_BEGIN_APP_CONTEXT();
   {
      ThreadPool manager;
      testFunctionCount = 0;
      manager.start(create_task(no_sleep_test_function));
   }
   EXPECT_EQ(testFunctionCount, 1);
   PDKTEST_END_APP_CONTEXT();
}

TEST(ThreadPoolTest, testCreateThreadRunFunction)
{
   PDKTEST_BEGIN_APP_CONTEXT();
   {
      ThreadPool manager;
      testFunctionCount = 0;
      manager.start(create_task(no_sleep_test_function));
   }
   EXPECT_EQ(testFunctionCount, 1);
   PDKTEST_END_APP_CONTEXT();
}

TEST(ThreadPoolTest, testRunMultiple)
{
   PDKTEST_BEGIN_APP_CONTEXT();
   const int runs = 10;
   {
      ThreadPool manager;
      testFunctionCount = 0;
      for (int i = 0; i < runs; ++i) {
         manager.start(create_task(sleep_test_function_mutex));
      }
   }
   EXPECT_EQ(testFunctionCount, runs);

   {
      ThreadPool manager;
      testFunctionCount = 0;
      for (int i = 0; i < runs; ++i) {
         manager.start(create_task(sleep_test_function_mutex));
      }
   }
   EXPECT_EQ(testFunctionCount, runs);
   {
      ThreadPool manager;
      for (int i = 0; i < 500; ++i) {
         manager.start(create_task(empty_function));
      }
   }
   PDKTEST_END_APP_CONTEXT();
}

TEST(ThreadPoolTest, testWaitcomplete)
{
   PDKTEST_BEGIN_APP_CONTEXT();
   testFunctionCount = 0;
   const int runs = 500;
   for (int i = 0; i < 500; ++i) {
      ThreadPool pool;
      pool.start(create_task(no_sleep_test_function));
   }
   EXPECT_EQ(testFunctionCount, runs);
   PDKTEST_END_APP_CONTEXT();
}

AtomicInt ran; // bool
class TestTask : public Runnable
{
public:
   void run()
   {
      ran.store(true);
   }
};

TEST(ThreadPoolTest, testRunTask)
{
   PDKTEST_BEGIN_APP_CONTEXT();
   ThreadPool manager;
   ran.store(false);
   manager.start(new TestTask());
   PDK_TRY_VERIFY(ran.load());
   PDKTEST_END_APP_CONTEXT();
}

TEST(ThreadPoolTest, testSingleton)
{
   PDKTEST_BEGIN_APP_CONTEXT();
   ran.store(false);
   ThreadPool::getGlobalInstance()->start(new TestTask());
   PDK_TRY_VERIFY(ran.load());
   PDKTEST_END_APP_CONTEXT();
}

AtomicInt *value = nullptr;
class IntAccessor : public Runnable
{
public:
   void run()
   {
      for (int i = 0; i < 100; ++i) {
         value->ref();
         pdktest::sleep(1);
      }
   }
};

/*
    Test that the ThreadManager destructor waits until
    all threads have completed.
*/
TEST(ThreadPoolTest, testDestruction)
{
   PDKTEST_BEGIN_APP_CONTEXT();
   value = new AtomicInt;
   ThreadPool *threadManager = new ThreadPool();
   threadManager->start(new IntAccessor());
   threadManager->start(new IntAccessor());
   delete threadManager;
   EXPECT_EQ(*value, 200);
   delete value;
   value = 0;
   PDKTEST_END_APP_CONTEXT();
}

Semaphore threadRecyclingSemaphore;
Thread *recycledThread = nullptr;

class ThreadRecorderTask : public Runnable
{
public:
   void run()
   {
      recycledThread = Thread::getCurrentThread();
      threadRecyclingSemaphore.release();
   }
};

/*
    Test that the thread pool really reuses threads.
*/
TEST(ThreadPoolTest, testThreadRecycling)
{
   PDKTEST_BEGIN_APP_CONTEXT();
   ThreadPool threadPool;
   threadPool.start(new ThreadRecorderTask());
   threadRecyclingSemaphore.acquire();
   Thread *thread1 = recycledThread;
   pdktest::sleep(100);
   threadPool.start(new ThreadRecorderTask());
   threadRecyclingSemaphore.acquire();
   Thread *thread2 = recycledThread;
   EXPECT_EQ(thread1, thread2);
   pdktest::sleep(100);
   threadPool.start(new ThreadRecorderTask());
   threadRecyclingSemaphore.acquire();
   Thread *thread3 = recycledThread;
   EXPECT_EQ(thread2, thread3);
   PDKTEST_END_APP_CONTEXT();
}

class ExpiryTimeoutTask : public Runnable
{
public:
   Thread *m_thread;
   AtomicInt m_runCount;
   Semaphore m_semaphore;
   
   ExpiryTimeoutTask()
      : m_thread(nullptr),
        m_runCount(0)
   {
      setAutoDelete(false);
   }
   
   void run()
   {
      m_thread = Thread::getCurrentThread();
      m_runCount.ref();
      m_semaphore.release();
   }
};

TEST(ThreadPoolTest, testExpiryTimeout)
{
   PDKTEST_BEGIN_APP_CONTEXT();
   ExpiryTimeoutTask task;

   ThreadPool threadPool;
   threadPool.setMaxThreadCount(1);

   int expiryTimeout = threadPool.getExpiryTimeout();
   threadPool.setExpiryTimeout(1000);
   EXPECT_EQ(threadPool.getExpiryTimeout(), 1000);

   // run the task
   threadPool.start(&task);
   EXPECT_TRUE(task.m_semaphore.tryAcquire(1, 10000));
   EXPECT_EQ(task.m_runCount.load(), 1);
   EXPECT_TRUE(!task.m_thread->wait(100));
   // thread should expire
   Thread *firstThread = task.m_thread;
   EXPECT_TRUE(task.m_thread->wait(10000));

   // run task again, thread should be restarted
   threadPool.start(&task);
   EXPECT_TRUE(task.m_semaphore.tryAcquire(1, 10000));
   EXPECT_EQ(task.m_runCount.load(), 2);
   EXPECT_TRUE(!task.m_thread->wait(100));
   // thread should expire again
   EXPECT_TRUE(task.m_thread->wait(10000));

   // thread pool should have reused the expired thread (instead of
   // starting a new one)
   EXPECT_EQ(firstThread, task.m_thread);

   threadPool.setExpiryTimeout(expiryTimeout);
   EXPECT_EQ(threadPool.getExpiryTimeout(), expiryTimeout);
   PDKTEST_END_APP_CONTEXT();
}

TEST(ThreadPoolTest, testExpiryTimeoutRace)
{
   PDKTEST_BEGIN_APP_CONTEXT();
#ifdef PDK_OS_WIN
   SUCCEED("This test is unstable on Windows. See BUG-3786.");
#endif
   ExpiryTimeoutTask task;
   ThreadPool threadPool;
   threadPool.setMaxThreadCount(1);
   threadPool.setExpiryTimeout(50);
   const int numTasks = 20;
   for (int i = 0; i < numTasks; ++i) {
      threadPool.start(&task);
      Thread::msleep(50); // exactly the same as the expiry timeout
   }
   EXPECT_TRUE(task.m_semaphore.tryAcquire(numTasks, 100000000));
   EXPECT_EQ(task.m_runCount.load(), numTasks);
   EXPECT_TRUE(threadPool.waitForDone(2000));
   PDKTEST_END_APP_CONTEXT();
}

class ExceptionTask : public Runnable
{
public:
   void run()
   {
      throw new int;
   }
};

TEST(ThreadPoolTest, testExceptions)
{
   PDKTEST_BEGIN_APP_CONTEXT();
   ExceptionTask task;
   {
      ThreadPool threadPool;
      //  Uncomment this for a nice crash.
      //        threadPool.start(&task);
   }
   PDKTEST_END_APP_CONTEXT();
}

namespace {

void set_max_thread_count_data(std::list<int> &data)
{
   data.push_back(1);
   data.push_back(-1);
   data.push_back(2);
   data.push_back(-2);
   data.push_back(4);
   data.push_back(-4);
   data.push_back(0);
   data.push_back(12345);
   data.push_back(-6789);
   data.push_back(42);
   data.push_back(666);
}

} // anonymous namespace

TEST(ThreadPoolTest, testSetMaxThreadCount)
{
   PDKTEST_BEGIN_APP_CONTEXT();
   std::list<int> data;
   set_max_thread_count_data(data);
   for(const int limit: data) {
      ThreadPool *threadPool = ThreadPool::getGlobalInstance();
      int savedLimit = threadPool->getMaxThreadCount();
      // maxThreadCount() should always return the previous argument to
      // setMaxThreadCount(), regardless of input
      threadPool->setMaxThreadCount(limit);
      EXPECT_EQ(threadPool->getMaxThreadCount(), limit);
      // setting the limit on children should have no effect on the parent
      {
         ThreadPool threadPool2(threadPool);
         savedLimit = threadPool2.getMaxThreadCount();

         // maxThreadCount() should always return the previous argument to
         // setMaxThreadCount(), regardless of input
         threadPool2.setMaxThreadCount(limit);
         EXPECT_EQ(threadPool2.getMaxThreadCount(), limit);

         // the value returned from maxThreadCount() should always be valid input for setMaxThreadCount()
         threadPool2.setMaxThreadCount(savedLimit);
         EXPECT_EQ(threadPool2.getMaxThreadCount(), savedLimit);
      }
   }
   PDKTEST_END_APP_CONTEXT();
}

TEST(ThreadPoolTest, testSetMaxThreadCountStartsAndStopsThreads)
{
   PDKTEST_BEGIN_APP_CONTEXT();
   class WaitingTask : public Runnable
   {
   public:
      Semaphore waitForStarted, waitToFinish;
      WaitingTask()
      {
         setAutoDelete(false);
      }

      void run()
      {
         waitForStarted.release();
         waitToFinish.acquire();
      }
   };
   ThreadPool threadPool;
   threadPool.setMaxThreadCount(1);

   WaitingTask *task = new WaitingTask;
   threadPool.start(task);
   EXPECT_TRUE(task->waitForStarted.tryAcquire(1, 1000));
   // thread limit is 1, cannot start more tasks
   threadPool.start(task);
   EXPECT_TRUE(!task->waitForStarted.tryAcquire(1, 1000));

   // increasing the limit by 1 should start the task immediately
   threadPool.setMaxThreadCount(2);
   EXPECT_TRUE(task->waitForStarted.tryAcquire(1, 1000));

   // ... but we still cannot start more tasks
   threadPool.start(task);
   EXPECT_TRUE(!task->waitForStarted.tryAcquire(1, 1000));

   // increasing the limit should be able to start more than one at a time
   threadPool.start(task);
   threadPool.setMaxThreadCount(4);
   EXPECT_TRUE(task->waitForStarted.tryAcquire(2, 1000));

   // ... but we still cannot start more tasks
   threadPool.start(task);
   threadPool.start(task);
   EXPECT_TRUE(!task->waitForStarted.tryAcquire(2, 1000));

   // decreasing the thread limit should cause the active thread count to go down
   threadPool.setMaxThreadCount(2);
   EXPECT_EQ(threadPool.getActiveThreadCount(), 4);
   task->waitToFinish.release(2);
   pdktest::wait(1000);
   EXPECT_EQ(threadPool.getActiveThreadCount(), 2);

   // ... and we still cannot start more tasks
   threadPool.start(task);
   threadPool.start(task);
   EXPECT_TRUE(!task->waitForStarted.tryAcquire(2, 1000));

   // start all remaining tasks
   threadPool.start(task);
   threadPool.start(task);
   threadPool.start(task);
   threadPool.start(task);
   threadPool.setMaxThreadCount(8);
   EXPECT_TRUE(task->waitForStarted.tryAcquire(6, 1000));

   task->waitToFinish.release(10);
   threadPool.waitForDone();
   delete task;
   PDKTEST_END_APP_CONTEXT();
}

TEST(ThreadPoolTest, testReserveThread)
{
   PDKTEST_BEGIN_APP_CONTEXT();
   std::list<int> data;
   set_max_thread_count_data(data);
   for (const int limit: data) {
      ThreadPool *threadpool = ThreadPool::getGlobalInstance();
      int savedLimit = threadpool->getMaxThreadCount();
      threadpool->setMaxThreadCount(limit);
      // reserve up to the limit
      for (int i = 0; i < limit; ++i) {
         threadpool->reserveThread();
      }
      // reserveThread() should always reserve a thread, regardless of
      // how many have been previously reserved
      threadpool->reserveThread();
      EXPECT_EQ(threadpool->getActiveThreadCount(), (limit > 0 ? limit : 0) + 1);
      threadpool->reserveThread();
      EXPECT_EQ(threadpool->getActiveThreadCount(), (limit > 0 ? limit : 0) + 2);
      // cleanup
      threadpool->releaseThread();
      threadpool->releaseThread();
      for (int i = 0; i < limit; ++i) {
         threadpool->releaseThread();
      }
      {
         ThreadPool threadpool2(threadpool);
         threadpool2.setMaxThreadCount(limit);

         // reserve up to the limit
         for (int i = 0; i < limit; ++i)
            threadpool2.reserveThread();

         // reserveThread() should always reserve a thread, regardless
         // of how many have been previously reserved
         threadpool2.reserveThread();
         EXPECT_EQ(threadpool2.getActiveThreadCount(), (limit > 0 ? limit : 0) + 1);
         threadpool2.reserveThread();
         EXPECT_EQ(threadpool2.getActiveThreadCount(), (limit > 0 ? limit : 0) + 2);

         threadpool->reserveThread();
         EXPECT_EQ(threadpool->getActiveThreadCount(), 1);
         threadpool->reserveThread();
         EXPECT_EQ(threadpool->getActiveThreadCount(), 2);

         // cleanup
         threadpool2.releaseThread();
         threadpool2.releaseThread();
         threadpool->releaseThread();
         threadpool->releaseThread();
         while (threadpool2.getActiveThreadCount() > 0) {
            threadpool2.releaseThread();
         }
      }
      // reset limit on global ThreadPool
      threadpool->setMaxThreadCount(savedLimit);
   }
   PDKTEST_END_APP_CONTEXT();
}

TEST(ThreadPoolTest, testReleaseThread)
{
   PDKTEST_BEGIN_APP_CONTEXT();
   std::list<int> data;
   set_max_thread_count_data(data);
   for (const int limit: data) {
      ThreadPool *threadpool = ThreadPool::getGlobalInstance();
      int savedLimit = threadpool->getMaxThreadCount();
      threadpool->setMaxThreadCount(limit);
      // reserve up to the limit
      for (int i = 0; i < limit; ++i) {
         threadpool->reserveThread();
      }
      // release should decrease the number of reserved threads
      int reserved = threadpool->getActiveThreadCount();
      while (reserved-- > 0) {
         threadpool->releaseThread();
         EXPECT_EQ(threadpool->getActiveThreadCount(), reserved);
      }
      EXPECT_EQ(threadpool->getActiveThreadCount(), 0);
      // releaseThread() can release more than have been reserved
      threadpool->releaseThread();
      EXPECT_EQ(threadpool->getActiveThreadCount(), -1);
      threadpool->reserveThread();
      EXPECT_EQ(threadpool->getActiveThreadCount(), 0);

      // releasing threads in children should not effect the parent
      {
         ThreadPool threadpool2(threadpool);
         threadpool2.setMaxThreadCount(limit);
         // reserve up to the limit
         for (int i = 0; i < limit; ++i) {
            threadpool2.reserveThread();
         }
         // release should decrease the number of reserved threads
         int reserved = threadpool2.getActiveThreadCount();
         while (reserved-- > 0) {
            threadpool2.releaseThread();
            EXPECT_EQ(threadpool2.getActiveThreadCount(), reserved);
            EXPECT_EQ(threadpool->getActiveThreadCount(), 0);
         }
         EXPECT_EQ(threadpool2.getActiveThreadCount(), 0);
         EXPECT_EQ(threadpool->getActiveThreadCount(), 0);
         // releaseThread() can release more than have been reserved
         threadpool2.releaseThread();
         EXPECT_EQ(threadpool2.getActiveThreadCount(), -1);
         EXPECT_EQ(threadpool->getActiveThreadCount(), 0);
         threadpool2.reserveThread();
         EXPECT_EQ(threadpool2.getActiveThreadCount(), 0);
         EXPECT_EQ(threadpool->getActiveThreadCount(), 0);
      }
      // reset limit on global ThreadPool
      threadpool->setMaxThreadCount(savedLimit);
   }
   PDKTEST_END_APP_CONTEXT();
}

TEST(ThreadPoolTest, testReserveAndStart)
{
   PDKTEST_BEGIN_APP_CONTEXT();
   class WaitingTask : public Runnable
   {
   public:
      AtomicInt m_count;
      Semaphore m_waitForStarted;
      Semaphore m_waitBeforeDone;

      WaitingTask() { setAutoDelete(false); }

      void run()
      {
         m_count.ref();
         m_waitForStarted.release();
         m_waitBeforeDone.acquire();
      }
   };
   // Set up
   ThreadPool *threadpool = ThreadPool::getGlobalInstance();
   int savedLimit = threadpool->getMaxThreadCount();
   threadpool->setMaxThreadCount(1);
   EXPECT_EQ(threadpool->getActiveThreadCount(), 0);

   // reserve
   threadpool->reserveThread();
   EXPECT_EQ(threadpool->getActiveThreadCount(), 1);

   // start a task, to get a running thread
   WaitingTask *task = new WaitingTask;
   threadpool->start(task);
   EXPECT_EQ(threadpool->getActiveThreadCount(), 2);
   task->m_waitForStarted.acquire();
   task->m_waitBeforeDone.release();
   PDK_TRY_COMPARE(task->m_count.load(), 1);
   PDK_TRY_COMPARE(threadpool->getActiveThreadCount(), 1);

   // now the thread is waiting, but tryStart() will fail since activeThreadCount() >= maxThreadCount()
   EXPECT_TRUE(!threadpool->tryStart(task));
   PDK_TRY_COMPARE(threadpool->getActiveThreadCount(), 1);

   // start() will therefore do a failing tryStart(), followed by enqueueTask()
   // which will actually wake up the waiting thread.
   threadpool->start(task);
   PDK_TRY_COMPARE(threadpool->getActiveThreadCount(), 2);
   task->m_waitForStarted.acquire();
   task->m_waitBeforeDone.release();
   PDK_TRY_COMPARE(task->m_count.load(), 2);
   PDK_TRY_COMPARE(threadpool->getActiveThreadCount(), 1);
   threadpool->releaseThread();
   PDK_TRY_COMPARE(threadpool->getActiveThreadCount(), 0);
   delete task;
   threadpool->setMaxThreadCount(savedLimit);
   PDKTEST_END_APP_CONTEXT();
}

AtomicInt count;
class CountingRunnable : public Runnable
{
public:
   void run()
   {
      count.ref();
   }
};

TEST(ThreadPoolTest, testStart)
{
   PDKTEST_BEGIN_APP_CONTEXT();
   const int runs = 1000;
   count.store(0);
   {
      ThreadPool threadPool;
      for (int i = 0; i< runs; ++i) {
         threadPool.start(new CountingRunnable());
      }
   }
   EXPECT_EQ(count.load(), runs);
   PDKTEST_END_APP_CONTEXT();
}

TEST(ThreadPoolTest, testTryStart)
{
   PDKTEST_BEGIN_APP_CONTEXT();
   class WaitingTask : public Runnable
   {
   public:
      Semaphore m_semaphore;

      WaitingTask()
      {
         setAutoDelete(false);
      }

      void run()
      {
         m_semaphore.acquire();
         count.ref();
      }
   };

   count.store(0);

   WaitingTask task;
   ThreadPool threadPool;
   for (int i = 0; i < threadPool.getMaxThreadCount(); ++i) {
      threadPool.start(&task);
   }
   EXPECT_TRUE(!threadPool.tryStart(&task));
   task.m_semaphore.release(threadPool.getMaxThreadCount());
   threadPool.waitForDone();
   EXPECT_EQ(count.load(), threadPool.getMaxThreadCount());
   PDKTEST_END_APP_CONTEXT();
}

std::mutex mutex;
AtomicInt activeThreads;
AtomicInt peakActiveThreads;

TEST(ThreadPoolTest, testTryStartPeakThreadCount)
{
   PDKTEST_BEGIN_APP_CONTEXT();
   class CounterTask : public Runnable
   {
   public:
      CounterTask() { setAutoDelete(false); }

      void run()
      {
         {
            std::lock_guard<std::mutex> lock(mutex);
            activeThreads.ref();
            peakActiveThreads.store(std::max(peakActiveThreads.load(), activeThreads.load()));
         }

         pdktest::wait(100);
         {
            std::lock_guard<std::mutex> lock(mutex);
            activeThreads.deref();
         }
      }
   };

   CounterTask task;
   ThreadPool threadPool;

   for (int i = 0; i < 20; ++i) {
      if (threadPool.tryStart(&task) == false) {
         pdktest::wait(10);
      }
   }
   EXPECT_EQ(peakActiveThreads.load(), Thread::getIdealThreadCount());

   for (int i = 0; i < 20; ++i) {
      if (threadPool.tryStart(&task) == false) {
         pdktest::wait(10);
      }
   }
   EXPECT_EQ(peakActiveThreads.load(), Thread::getIdealThreadCount());
   PDKTEST_END_APP_CONTEXT();
}

TEST(ThreadPoolTest, testTryStartCount)
{
   PDKTEST_BEGIN_APP_CONTEXT();
   class SleeperTask : public Runnable
   {
   public:
      SleeperTask()
      {
         setAutoDelete(false);
      }

      void run()
      {
         pdktest::wait(50);
      }
   };

   SleeperTask task;
   ThreadPool threadPool;
   const int runs = 5;
   for (int i = 0; i < runs; ++i) {
      int count = 0;
      while (threadPool.tryStart(&task)) {
         ++count;
      }
      EXPECT_EQ(count, Thread::getIdealThreadCount());
      PDK_TRY_COMPARE(threadPool.getActiveThreadCount(), 0);
   }
   PDKTEST_END_APP_CONTEXT();
}

namespace {

void priority_start_data(std::list<int> &data)
{
   data.push_back(0);
   data.push_back(1);
   data.push_back(2);
}

} // anonymous namespace

TEST(ThreadPoolTest, priorityStart)
{
   PDKTEST_BEGIN_APP_CONTEXT();
   class Holder : public Runnable
   {
   public:
      Semaphore &sem;
      Holder(Semaphore &sem) : sem(sem) {}
      void run()
      {
         sem.acquire();
      }
   };

   class Runner : public Runnable
   {
   public:
      AtomicPointer<Runnable> &ptr;
      Runner(AtomicPointer<Runnable> &ptr) : ptr(ptr)
      {}

      void run()
      {
         ptr.testAndSetRelaxed(0, this);
      }
   };

   std::list<int> data;
   priority_start_data(data);

   for (int otherCount: data) {
      Semaphore sem;
      AtomicPointer<Runnable> firstStarted;
      Runnable *expected;
      ThreadPool threadPool;
      threadPool.setMaxThreadCount(1); // start only one thread at a time

      // queue the holder first
      // We need to be sure that all threads are active when we
      // queue the two Runners
      threadPool.start(new Holder(sem));
      while (otherCount--) {
         threadPool.start(new Runner(firstStarted), 0); // priority 0
      }
      threadPool.start(expected = new Runner(firstStarted), 1); // priority 1
      sem.release();
      EXPECT_TRUE(threadPool.waitForDone());
      EXPECT_EQ(firstStarted.load(), expected);
   }
   PDKTEST_END_APP_CONTEXT();
}

TEST(ThreadPoolTest, testWaitForDone)
{
   PDKTEST_BEGIN_APP_CONTEXT();
   Time total, pass;
   total.start();

   ThreadPool threadPool;
   while (total.elapsed() < 10000) {
      int runs;
      count.store(runs = 0);
      pass.restart();
      while (pass.elapsed() < 100) {
         threadPool.start(new CountingRunnable());
         ++runs;
      }
      threadPool.waitForDone();
      EXPECT_EQ(count.load(), runs);
      count.store(runs = 0);
      pass.restart();
      while (pass.elapsed() < 100) {
         threadPool.start(new CountingRunnable());
         threadPool.start(new CountingRunnable());
         runs += 2;
      }
      threadPool.waitForDone();
      EXPECT_EQ(count.load(), runs);
   }
   PDKTEST_END_APP_CONTEXT();
}

TEST(ThreadPoolTest, testWaitForDoneTimeout)
{
   PDKTEST_BEGIN_APP_CONTEXT();
   std::mutex mutex;
   class BlockedTask : public Runnable
   {
   public:
      std::mutex &mutex;
      explicit BlockedTask(std::mutex &m) : mutex(m) {}

      void run()
      {
         mutex.lock();
         mutex.unlock();
         pdktest::sleep(50);
      }
   };

   ThreadPool threadPool;

   mutex.lock();
   threadPool.start(new BlockedTask(mutex));
   EXPECT_TRUE(!threadPool.waitForDone(100));
   mutex.unlock();
   EXPECT_TRUE(threadPool.waitForDone(400));
   PDKTEST_END_APP_CONTEXT();
}

TEST(ThreadPoolTest, testClear)
{
   PDKTEST_BEGIN_APP_CONTEXT();
   Semaphore sem(0);
   class BlockingRunnable : public Runnable
   {
   public:
      Semaphore & sem;
      BlockingRunnable(Semaphore & sem) : sem(sem){}
      void run()
      {
         sem.acquire();
         count.ref();
      }
   };

   ThreadPool threadPool;
   threadPool.setMaxThreadCount(10);
   int runs = 2 * threadPool.getMaxThreadCount();
   count.store(0);
   for (int i = 0; i <= runs; i++) {
      threadPool.start(new BlockingRunnable(sem));
   }
   threadPool.clear();
   sem.release(threadPool.getMaxThreadCount());
   threadPool.waitForDone();
   EXPECT_EQ(count.load(), threadPool.getMaxThreadCount());
   PDKTEST_END_APP_CONTEXT();
}

TEST(ThreadPoolTest, testTryTake)
{
   PDKTEST_BEGIN_APP_CONTEXT();
   Semaphore sem(0);
   Semaphore startedThreads(0);

   class BlockingRunnable : public Runnable
   {
   public:
      Semaphore &m_sem;
      Semaphore &m_startedThreads;
      AtomicInt &m_dtorCounter;
      AtomicInt &m_runCounter;
      int m_dummy;

      explicit BlockingRunnable(Semaphore &s, Semaphore &started, AtomicInt &c, AtomicInt &r)
         : m_sem(s),
           m_startedThreads(started),
           m_dtorCounter(c),
           m_runCounter(r)
      {}

      ~BlockingRunnable()
      {
         m_dtorCounter.fetchAndAddRelaxed(1);
      }

      void run() override
      {
         m_startedThreads.release();
         m_runCounter.fetchAndAddRelaxed(1);
         m_sem.acquire();
         count.ref();
      }
   };

   enum {
      MaxThreadCount = 3,
      OverProvisioning = 2,
      Runs = MaxThreadCount * OverProvisioning
   };

   ThreadPool threadPool;
   threadPool.setMaxThreadCount(MaxThreadCount);
   BlockingRunnable *runnables[Runs];

   // ensure that the ThreadPool doesn't deadlock if any of the checks fail
   // and cause an early return:
   const SemaphoreReleaser semReleaser(sem, Runs);

   count.store(0);
   AtomicInt dtorCounter = 0;
   AtomicInt runCounter = 0;
   for (int i = 0; i < Runs; i++) {
      runnables[i] = new BlockingRunnable(sem, startedThreads, dtorCounter, runCounter);
      runnables[i]->setAutoDelete(i != 0 && i != Runs - 1); // one which will run and one which will not
      EXPECT_TRUE(!threadPool.tryTake(runnables[i])); // verify NOOP for jobs not in the queue
      threadPool.start(runnables[i]);
   }
   // wait for all worker threads to have started up:
   EXPECT_TRUE(startedThreads.tryAcquire(MaxThreadCount, 60*1000 /* 1min */));

   for (int i = 0; i < MaxThreadCount; ++i) {
      // check taking runnables doesn't work once they were started:
      EXPECT_TRUE(!threadPool.tryTake(runnables[i]));
   }
   for (int i = MaxThreadCount; i < Runs ; ++i) {
      EXPECT_TRUE(threadPool.tryTake(runnables[i]));
      delete runnables[i];
   }

   runnables[0]->m_dummy = 0; // valgrind will catch this if tryTake() is crazy enough to delete currently running jobs
   EXPECT_EQ(dtorCounter.load(), int(Runs - MaxThreadCount));
   sem.release(MaxThreadCount);
   threadPool.waitForDone();
   EXPECT_EQ(runCounter.load(), int(MaxThreadCount));
   EXPECT_EQ(count.load(), int(MaxThreadCount));
   EXPECT_EQ(dtorCounter.load(), int(Runs - 1));
   delete runnables[0]; // if the pool deletes them then we'll get double-free crash
   PDKTEST_END_APP_CONTEXT();
}

TEST(ThreadPoolTest, testDestroyingWaitsForTasksToFinish)
{
   PDKTEST_BEGIN_APP_CONTEXT();
   Time total, pass;
   total.start();

   while (total.elapsed() < 10000) {
      int runs;
      count.store(runs = 0);
      {
         ThreadPool threadPool;
         pass.restart();
         while (pass.elapsed() < 100) {
            threadPool.start(new CountingRunnable());
            ++runs;
         }
      }
      EXPECT_EQ(count.load(), runs);

      count.store(runs = 0);
      {
         ThreadPool threadPool;
         pass.restart();
         while (pass.elapsed() < 100) {
            threadPool.start(new CountingRunnable());
            threadPool.start(new CountingRunnable());
            runs += 2;
         }
      }
      EXPECT_EQ(count.load(), runs);
   }
   PDKTEST_END_APP_CONTEXT();
}

// Verify that ThreadPool::stackSize is used when creating
// new threads. 
TEST(ThreadPoolTest, testStackSize)
{
   PDKTEST_BEGIN_APP_CONTEXT();
   uint targetStackSize = 512 * 1024;
   uint threadStackSize = 1; // impossible value

   class StackSizeChecker : public Runnable
   {
   public:
      uint *stackSize;

      StackSizeChecker(uint *stackSize)
         :stackSize(stackSize)
      {

      }

      void run()
      {
         *stackSize = Thread::getCurrentThread()->getStackSize();
      }
   };

   ThreadPool threadPool;
   threadPool.setStackSize(targetStackSize);
   threadPool.start(new StackSizeChecker(&threadStackSize));
   EXPECT_TRUE(threadPool.waitForDone(30000)); // 30s timeout
   EXPECT_EQ(threadStackSize, targetStackSize);
   PDKTEST_END_APP_CONTEXT();
}

TEST(ThreadPoolTest, testStressTest)
{
   PDKTEST_BEGIN_APP_CONTEXT();
   class Task : public Runnable
   {
      Semaphore m_semaphore;
   public:
      Task()
      {
         setAutoDelete(false);
      }

      void start()
      {
         ThreadPool::getGlobalInstance()->start(this);
      }

      void wait()
      {
         m_semaphore.acquire();
      }

      void run()
      {
         m_semaphore.release();
      }
   };

   Time total;
   total.start();
   while (total.elapsed() < 30000) {
      Task t;
      t.start();
      t.wait();
   }
   PDKTEST_END_APP_CONTEXT();
}

TEST(ThreadPoolTest, testTakeAllAndIncreaseMaxThreadCount)
{
   PDKTEST_BEGIN_APP_CONTEXT();
   class Task : public Runnable
   {
   public:
      Task(Semaphore *mainBarrier, Semaphore *threadBarrier)
         : m_mainBarrier(mainBarrier)
         , m_threadBarrier(threadBarrier)
      {
         setAutoDelete(false);
      }

      void run() {
         m_mainBarrier->release();
         m_threadBarrier->acquire();
      }
   private:
      Semaphore *m_mainBarrier;
      Semaphore *m_threadBarrier;
   };

   Semaphore mainBarrier;
   Semaphore taskBarrier;

   ThreadPool threadPool;
   threadPool.setMaxThreadCount(1);

   Task *task1 = new Task(&mainBarrier, &taskBarrier);
   Task *task2 = new Task(&mainBarrier, &taskBarrier);
   Task *task3 = new Task(&mainBarrier, &taskBarrier);

   threadPool.start(task1);
   threadPool.start(task2);
   threadPool.start(task3);

   mainBarrier.acquire(1);

   EXPECT_EQ(threadPool.getActiveThreadCount(), 1);

   EXPECT_TRUE(!threadPool.tryTake(task1));
   EXPECT_TRUE(threadPool.tryTake(task2));
   EXPECT_TRUE(threadPool.tryTake(task3));

   // A bad queue implementation can segfault here because two consecutive items in the queue
   // have been taken
   threadPool.setMaxThreadCount(4);

   // Even though we increase the max thread count, there should only be one job to run
   EXPECT_EQ(threadPool.getActiveThreadCount(), 1);

   // Make sure jobs 2 and 3 never started
   EXPECT_EQ(mainBarrier.available(), 0);

   taskBarrier.release(1);

   threadPool.waitForDone();

   EXPECT_EQ(threadPool.getActiveThreadCount(), 0);

   delete task1;
   delete task2;
   delete task3;
   PDKTEST_END_APP_CONTEXT();
}

TEST(ThreadPoolTest, testWaitForDoneAfterTake)
{
   PDKTEST_BEGIN_APP_CONTEXT();
   class Task : public Runnable
   {
   public:
      Task(Semaphore *mainBarrier, Semaphore *threadBarrier)
         : m_mainBarrier(mainBarrier)
         , m_threadBarrier(threadBarrier)
      {}
      
      void run()
      {
         m_mainBarrier->release();
         m_threadBarrier->acquire();
      }
      
   private:
      Semaphore *m_mainBarrier = nullptr;
      Semaphore *m_threadBarrier = nullptr;
   };
   
   int threadCount = 4;
   
   // Blocks the main thread from releasing the threadBarrier before all run() functions have started
   Semaphore mainBarrier;
   // Blocks the tasks from completing their run function
   Semaphore threadBarrier;
   
   ThreadPool manager;
   manager.setMaxThreadCount(threadCount);
   
   // Fill all the threads with runnables that wait for the threadBarrier
   for (int i = 0; i < threadCount; i++) {
      auto *task = new Task(&mainBarrier, &threadBarrier);
      manager.start(task);
   }
   
   EXPECT_TRUE(manager.getActiveThreadCount() == manager.getMaxThreadCount());
   
   // Add runnables that are immediately removed from the pool queue.
   // This sets the queue elements to nullptr in ThreadPool and we want to test that
   // the threads keep going through the queue after encountering a nullptr.
   for (int i = 0; i < threadCount; i++) {
      Runnable *runnable = create_task(empty_function);
      manager.start(runnable);
      EXPECT_TRUE(manager.tryTake(runnable));
   }
   
   // Add another runnable that will not be removed
   manager.start(create_task(empty_function));
   
   // Wait for the first runnables to start
   mainBarrier.acquire(threadCount);
   
   EXPECT_TRUE(mainBarrier.available() == 0);
   EXPECT_TRUE(threadBarrier.available() == 0);
   
   // Release runnables that are waiting and expect all runnables to complete
   threadBarrier.release(threadCount);
   
   // Using qFatal instead of EXPECT_TRUE to force exit if threads are still running after timeout.
   // Otherwise, QCoreApplication will still wait for the stale threads and never exit the test.
   if (!manager.waitForDone(5 * 60 * 1000)) {
      FAIL() << "waitForDone returned false. Aborting to stop background threads.";
   }
   PDKTEST_END_APP_CONTEXT();
}
