#ifndef STACK_H
#define STACK_H

#include <stdbool.h>

struct Stack;

struct Stack *StackNew();
bool StackEmpty(const struct Stack *);
void *StackTop(const struct Stack *);
void *StackPop(struct Stack *);
int StackPush(struct Stack *, void *);
void StackRelease(struct Stack *);

#endif
