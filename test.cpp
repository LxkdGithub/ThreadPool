#include <iostream>
#include <chrono>
#include <future>
#include "threadpool.cpp"
using namespace std;

int hello(int  a){

	cout<<"task hello "<<" "<<a<<" "<<endl;
	return a + 1000;	
}


void hello1(int a){
	cout<<"task hello "<<" "<<a<<" "<<endl;
}


void hello2(){
	cout<<"task hello ------------"<<endl;
}


void hello3(const char * a){
	cout<<a<<endl;
}

int main(){
	threadpool_t pool(10, 20, 10);
	int a =1, b = 2, c = 3, d = 4;
	std::this_thread::sleep_for(std::chrono::seconds(1));
//	auto fut1 = pool.threadpool_add_task(std::move(hello), 1);
//	auto fut2 = pool.threadpool_add_task(std::move(hello), 2);
//	auto fut3 = pool.threadpool_add_task(std::move(hello), 3);
//	auto fut4 = pool.threadpool_add_task(std::move(hello), 4);
//	cout<<fut1.get()<<endl;
//	cout<<fut2.get()<<endl;
//	cout<<fut3.get()<<endl;
//	cout<<fut4.get()<<endl;
//	pool.threadpool_add_task(std::move(hello1), 4);
//	pool.threadpool_add_task(std::move(hello2));
	char s1[] = "sdsdsd";
	pool.threadpool_add_task(std::move(hello3), (const char *)s1);
	pthread_join(pool.admin_tid, NULL);
	return 0;
}
