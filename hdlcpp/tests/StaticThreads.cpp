#include <pthread.h>

static void* worker(void* argument)
{
    int& result = *static_cast<int*>(argument);
    int previous;
    result = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previous);
    if (result == 0) {
        result = pthread_setcancelstate(previous, nullptr);
    }
    return nullptr;
}

int main()
{
    int result = -1;
    pthread_t thread;
    if (pthread_create(&thread, nullptr, worker, &result) != 0) {
        return 1;
    }
    if (pthread_join(thread, nullptr) != 0) {
        return 2;
    }
    return result != 0;
}
