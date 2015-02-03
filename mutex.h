#ifndef SCOPE_LOCK_H_20141202
#define SCOPE_LOCK_H_20141202
#include <pthread.h>

namespace shark
{

class MutexLock{
	MutexLock(const MutexLock&);
	MutexLock& operator=(const MutexLock&);
public:
	MutexLock()
	{
	pthread_mutex_init(&mutex_, NULL);
	}

	~MutexLock()
	{
	pthread_mutex_destroy(&mutex_);
	}

	void lock()
	{
	pthread_mutex_lock(&mutex_);
	}

	void unlock()
	{
	pthread_mutex_unlock(&mutex_);
	}

	pthread_mutex_t* getPthreadMutex() /* non-const */
	{
	return &mutex_;
	}

private:
  	pthread_mutex_t mutex_;
};

class ScopeLock
{
public:
	explicit ScopeLock(MutexLock& mutex)
		: mutex_(mutex)
	{
		mutex_.lock();
	}

	~ScopeLock()
	{
		mutex_.unlock();
	}
private:
	ScopeLock(const ScopeLock&);
	ScopeLock& operator=(const ScopeLock&);
	MutexLock& mutex_;//don't hold the instance of MutexLock, just a reference.
};

//prevent the misusing, the following usage is fatal.
#define ScopeLock(x) error "Missing guard object name"

}

#endif