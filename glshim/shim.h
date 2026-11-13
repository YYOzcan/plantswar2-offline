#define EXPORT __attribute__((visibility("default")))
#define RTLD_NOW 2
void* dlopen(const char*, int);
void* dlsym(void*, const char*);
