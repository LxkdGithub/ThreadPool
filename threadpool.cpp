#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <assert.h>
#include <string.h>
#include <errno.h>

#include <chrono>
#include <thread>
#include <future>
#include <iostream>
#include <queue>
#include <functional>
using namespace std;

#define DEFAULT_TIME 10       /*默认时间10s*/
#define MIN_WAIT_TASK_NUM 10  /*当任务数超过了它，就该添加新线程了*/
#define DEFAULT_THREAD_NUM 10 /*每次创建或销毁的线程个数*/
#define true 1
#define false 0

#define DEBUG

/*任务*/
typedef struct
{
   void *(*function)(void *);
   void *arg;
} threadpool_task_t;

class threadpool_t
{
public:
   threadpool_t(int min_thr_num, int max_thr_num, int queue_max_size)
   {
      int flag = threadpool_create(min_thr_num, max_thr_num, queue_max_size);
      if (flag)
      {
         cout << "Create pool succeed!" << endl;
      }
      else
      {
         cout << "Create pool Failed!" << endl;
      }
   }

   ~threadpool_t()
   {
      //threadpool_destroy();
   }

   /*创建线程池*/
   int threadpool_create(int min_thr_num, int max_thr_num, int queue_max_size);
   /*释放线程池*/
   int threadpool_free();
   /*销毁线程池*/
   int threadpool_destroy();
   /*管理线程*/
   static void *admin_thread(void *);
   /*线程是否存在*/
   static int is_thread_alive(pthread_t tid);
   /*工作线程*/
   static void *threadpool_thread(void *);
   /*向线程池的任务队列中添加一个任务*/
   template <typename F, typename... Args>
   std::future<typename std::result_of<F (Args...)>::type> threadpool_add_task(F&& f, Args&&... args);

public:
   pthread_mutex_t lock;           /* 锁住整个结构体 */
   pthread_mutex_t thread_counter; /* 用于使用忙线程数时的锁 */
   pthread_cond_t queue_not_full;  /* 条件变量，任务队列不为满 */
   pthread_cond_t queue_not_empty; /* 任务队列不为空 */

   pthread_t *threads;            /* 存放线程的tid,实际上就是管理了线 数组 */
   pthread_t admin_tid;           /* 管理者线程tid */
   std::queue<std::function<void()>> task_list; /* 任务队列 */

   /*线程池信息*/
   int min_thr_num;       /* 线程池中最小线程数 */
   int max_thr_num;       /* 线程池中最大线程数 */
   int live_thr_num;      /* 线程池中存活的线程数 */
   int busy_thr_num;      /* 忙线程，正在工作的线程 */
   int wait_exit_thr_num; /* 需要销毁的线程数 */

   /* 存在的任务数 */
   int queue_max_size; /* 队列能容纳的最大任务数 */

   /*状态*/
   int shutdown; /* true为关闭 */
};

/*创建线程池*/
int threadpool_t::threadpool_create(int min_thr_num, int max_thr_num, int queue_max_size)
{
   int i;
   do
   {
      /*信息初始化*/
      this->min_thr_num = min_thr_num;
      this->max_thr_num = max_thr_num;
      this->busy_thr_num = 0;
      this->live_thr_num = min_thr_num;
      this->wait_exit_thr_num = 0;
      this->queue_max_size = queue_max_size;
      this->shutdown = false;

      /*根据最大线程数，给工作线程数组开空间，清0*/
      this->threads = (pthread_t *)malloc(sizeof(pthread_t) * max_thr_num);
      if (this->threads == NULL)
      {
         printf("malloc threads false;\n");
         break;
      }
      memset(this->threads, 0, sizeof(pthread_t) * max_thr_num);

      /*队列开空间*/
      // this->task_queue =
         //  (threadpool_task_t *)malloc(sizeof(threadpool_task_t) * queue_max_size);

      /*初始化互斥锁和条件变量*/
      if (pthread_mutex_init(&(this->lock), NULL) != 0 ||
          pthread_mutex_init(&(this->thread_counter), NULL) != 0 ||
          pthread_cond_init(&(this->queue_not_empty), NULL) != 0 ||
          pthread_cond_init(&(this->queue_not_full), NULL) != 0)
      {
         printf("init lock or cond false;\n");
         break;
      }

      /*启动min_thr_num个工作线程*/
      for (i = 0; i < this->min_thr_num; i++)
      {
         /*pool指向当前线程池*/
         pthread_create(&(this->threads[i]), NULL, threadpool_thread, (void *)this);
         #ifdef DEBUG
         printf("start thread 0x%x... \n", (unsigned int)this->threads[i]);
         #endif
      }
      /*管理者线程*/
      pthread_create(&(this->admin_tid), NULL, admin_thread, (void *)this);

      return 1;
   } while (0);

   /*释放pool的空间*/
   threadpool_free();
   return 0;
}

/*释放线程池*/
int threadpool_t::threadpool_free()
{

   if (threads)
   {
      free(threads);
      pthread_mutex_lock(&(lock)); /*先锁住再销毁*/
      pthread_mutex_destroy(&(lock));
      pthread_mutex_lock(&(thread_counter));
      pthread_mutex_destroy(&(thread_counter));
      pthread_cond_destroy(&(queue_not_empty));
      pthread_cond_destroy(&(queue_not_full));
   }

   return 0;
}

/*销毁线程池*/
int threadpool_t::threadpool_destroy()
{
   cout << "enter function destroy" << endl;
   int i;
   shutdown = true;

   /*销毁管理者线程*/
   pthread_join(admin_tid, NULL);

   //通知所有线程去自杀(在自己领任务的过程中)
   for (i = 0; i < live_thr_num; i++)
   {
      pthread_cond_broadcast(&(queue_not_empty));
   }

   /*等待线程结束 先是pthread_exit 然后等待其结束*/
   for (i = 0; i < live_thr_num; i++)
   {
      pthread_join(threads[i], NULL);
   }

   threadpool_free();
   return 0;
}

/*管理线程*/
void *
threadpool_t::admin_thread(void *threadpool)
{
   std::this_thread::sleep_for(chrono::seconds(3));
   int i;
   threadpool_t *pool = (threadpool_t *)threadpool;
   while (!pool->shutdown)
   {
      #ifdef DEBUG
      printf("admin -----------------\n");
      #endif
      std::this_thread::sleep_for(chrono::seconds(DEFAULT_TIME)); /*隔一段时间再管理*/
      pthread_mutex_lock(&(pool->lock));                          /*加锁*/
      int queue_size = pool->task_list.size();                          /*任务数*/
      int live_thr_num = pool->live_thr_num;                      /*存活的线程数*/
      pthread_mutex_unlock(&(pool->lock));                        /*解锁*/

      pthread_mutex_lock(&(pool->thread_counter));
      int busy_thr_num = pool->busy_thr_num; /*忙线程数*/
      pthread_mutex_unlock(&(pool->thread_counter));

      #ifdef DEBUG
      printf("admin busy live -%d--%d-\n", busy_thr_num, live_thr_num);
      #endif

      /*创建新线程 实际任务数量大于 最小正在等待的任务数量，存活线程数小于最大线程数*/
      if (queue_size >= MIN_WAIT_TASK_NUM && live_thr_num <= pool->max_thr_num)
      {
         #ifdef DEBUG
         printf("admin add-----------\n");
         #endif
         pthread_mutex_lock(&(pool->lock));
         int add = 0;

         /*一次增加 DEFAULT_THREAD_NUM 个线程*/
         for (i = 0; i < pool->max_thr_num && add < DEFAULT_THREAD_NUM && pool->live_thr_num < pool->max_thr_num; i++)
         {
            if ((pool->threads[i] == 0) || (!is_thread_alive(pool->threads[i])))
            {
               pthread_create(&(pool->threads[i]), NULL, threadpool_t::threadpool_thread, (void *)pool);
               add++;
               pool->live_thr_num++;
               #ifdef DEBUG
                  printf("new thread -----------------------\n");
               #endif
            }
         }

         pthread_mutex_unlock(&(pool->lock));
      }

      /*销毁多余的线程 忙线程x2 都小于 存活线程，并且存活的大于最小线程数*/
      if ((busy_thr_num * 2) < live_thr_num && live_thr_num > pool->min_thr_num)
      {
         // printf("admin busy --%d--%d----\n", busy_thr_num, live_thr_num);
         /*一次销毁DEFAULT_THREAD_NUM个线程*/
         pthread_mutex_lock(&(pool->lock));
         pool->wait_exit_thr_num = DEFAULT_THREAD_NUM;
         pthread_mutex_unlock(&(pool->lock));

         for (i = 0; i < DEFAULT_THREAD_NUM; i++)
         {
            //通知正在处于空闲的线程，自杀
            pthread_cond_signal(&(pool->queue_not_empty));
            #ifdef DEBUG
            printf("admin cler --\n");
            #endif
         }
      }
   }

   return NULL;
}

/*线程是否存活*/
int threadpool_t::is_thread_alive(pthread_t tid)
{
   int kill_rc = pthread_kill(tid, 0); //发送0号信号，测试是否存活
   if (kill_rc == ESRCH)               //线程不存在
   {
      return false;
   }
   return true;
}

/*工作线程*/
void *
threadpool_t::threadpool_thread(void *threadpool)
{
   threadpool_t *pool = (threadpool_t *)threadpool;
   threadpool_task_t task;

   while (true)
   {
      pthread_mutex_lock(&(pool->lock));

      //无任务则阻塞在 任务队列不为空 上，有任务则跳出
      while (pool->task_list.empty() && (!pool->shutdown))
      {
         #ifdef DEBUG
         printf("thread 0x%x is waiting \n", (unsigned int)pthread_self());
         #endif
         pthread_cond_wait(&(pool->queue_not_empty), &(pool->lock));
         // cout<<"here..........."<<endl;
         //判断是否需要清除线程,自杀功能
         if (pool->wait_exit_thr_num > 0)
         {
            #ifdef DEBUG
            cout << "wait_exit_thr_num is error" << endl;
            #endif
            pool->wait_exit_thr_num--;
            //判断线程池中的线程数是否大于最小线程数，是则结束当前线程
            if (pool->live_thr_num > pool->min_thr_num)
            {
               #ifdef DEBUG
               printf("thread 0x%x is exiting \n", (unsigned int)pthread_self());
               #endif
               pool->live_thr_num--;
               pthread_mutex_unlock(&(pool->lock));
               pthread_exit(NULL); //结束线程
            }
         }
      }

      //线程池开关状态
      if (pool->shutdown) //关闭线程池
      {
         pthread_mutex_unlock(&(pool->lock));
         #ifdef DEBUG
         printf("beacause shutdown...   thread 0x%x is exiting \n", (unsigned int)pthread_self());
         #endif
         pthread_exit(NULL); //线程自己结束自己
      }
      //否则该线程可以拿出任务
      std::function<void()> task = pool->task_list.front();
      pool->task_list.pop();


      //通知可以添加新任务
      pthread_cond_broadcast(&(pool->queue_not_full));

      //释放线程锁
      pthread_mutex_unlock(&(pool->lock));

      //执行刚才取出的任务
      #ifdef DEBUG
      printf("thread 0x%x start working \n", (unsigned int)pthread_self());
      #endif
      pthread_mutex_lock(&(pool->thread_counter)); //锁住忙线程变量
      pool->busy_thr_num++;
      pthread_mutex_unlock(&(pool->thread_counter));

      // (*(task.function))(task.arg); //执行任务
      task();

      //任务结束处理
      #ifdef DEBUG
      printf("thread 0x%x end working \n", (unsigned int)pthread_self());
      #endif
      pthread_mutex_lock(&(pool->thread_counter));
      pool->busy_thr_num--;
      pthread_mutex_unlock(&(pool->thread_counter));
   }

   pthread_exit(NULL);
}


template <typename F, typename... Args>
std::future<typename std::result_of<F (Args...)>::type> threadpool_t::threadpool_add_task(F&& f, Args&&... args)
{
    using retType = typename std::result_of<F(Args...)>::type;

    auto task = std::make_shared<std::packaged_task<retType(Args...)>>(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
    //q.push(task);

    pthread_mutex_lock(&(lock));

   /*如果队列满了,调用wait阻塞*/
   while ((this->task_list.size() == queue_max_size) && (!shutdown))
   {
      pthread_cond_wait(&(queue_not_full), &(lock));
   }
   /*如果线程池处于关闭状态*/
   if (shutdown)
   {
      pthread_mutex_unlock(&(lock));
      return std::future<retType>();
   }

   task_list.push([=] (){
                  if(task) (*task)(args...);
               });
   #ifdef DEBUG
   cout<<"add succeed!"<<endl;
   #endif
   auto fut = task->get_future();

   /*添加完任务后,队列就不为空了,唤醒线程池中的一个线程*/
   pthread_cond_signal(&(this->queue_not_empty));
   pthread_mutex_unlock(&(this->lock));


   return fut;

}

