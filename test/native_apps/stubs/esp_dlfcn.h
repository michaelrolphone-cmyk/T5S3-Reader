#pragma once
#define RTLD_NOW 2
void *dlopen(const char *, int);
void *dlsym(void *, const char *);
int dlclose(void *);
const char *dlerror(void);
