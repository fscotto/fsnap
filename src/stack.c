#include "stack.h"
#include <stdlib.h>

struct Node {
  void *data;
  struct Node *next;
};

struct Stack {
  size_t len;
  struct Node *head;
};

struct Stack *StackNew(void) {
  struct Stack *self = calloc(1, sizeof(*self));
  if (self == NULL)
    return NULL;
  self->head = NULL;
  self->len = 0;
  return self;
}

bool StackEmpty(const struct Stack *self) {
  if (self == NULL)
    return false;
  return self->len == 0;
}

void *StackTop(const struct Stack *self) {
  if (self == NULL)
    return NULL;
  if (self->head != NULL) {
    return self->head->data;
  }
  return NULL;
}

void *StackPop(struct Stack *self) {
  if (self == NULL || self->head == NULL)
    return NULL;
  struct Node *head = self->head;
  struct Node *next = self->head->next;
  self->head = next;
  void *data = head->data;
  free(head);
  self->len--;
  return data;
}

int StackPush(struct Stack *self, void *data) {
  if (self == NULL)
    return -1;
  struct Node *n = calloc(1, sizeof(*n));
  n->data = data;
  n->next = NULL;
  if (self->head == NULL) {
    self->head = n;
  } else {
    struct Node *head = self->head;
    n->next = head;
    self->head = n;
  }
  self->len++;
  return 0;
}

void StackRelease(struct Stack *self) {
  if (self == NULL || self->head == NULL)
    return;
  while (!StackEmpty(self)) {
    (void *)StackPop(self);
  }
  free(self);
}
