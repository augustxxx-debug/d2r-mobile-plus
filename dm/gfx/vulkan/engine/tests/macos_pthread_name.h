/* 在 macOS 上编 xbg_texture.c 做单测用：macOS 的 pthread_setname_np 只收一个参数（只能给自己命名），这里把两参数的调用吃掉。
 * cc ... -include tests/macos_pthread_name.h ../xbg_texture.c
 * For building xbg_texture.c in macOS unit tests: macOS's pthread_setname_np takes one argument (self only); swallow the two-argument call. */
#include <pthread.h>
#define pthread_setname_np(thread, name) ((void)(thread), (void)(name), 0)
