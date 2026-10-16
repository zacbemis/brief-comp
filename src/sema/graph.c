#include "sema/sema.h"

typedef struct {
  size_t function;
  SemaEdge *next;
} Frame;
size_t sema_components(SemaProgram *p) {
  size_t *index = arena_calloc(p->arena, p->count, sizeof(*index));
  size_t *low = arena_calloc(p->arena, p->count, sizeof(*low));
  unsigned char *active = arena_calloc(p->arena, p->count, 1);
  size_t *stack = arena_calloc(p->arena, p->count, sizeof(*stack));
  Frame *work = arena_calloc(p->arena, p->count, sizeof(*work));
  size_t serial = 0, components = 0, top = 0, depth = 0;
  for (size_t root = 0; root < p->count; root++) {
    if (index[root])
      continue;
    index[root] = low[root] = ++serial;
    active[root] = 1;
    stack[top++] = root;
    work[depth++] = (Frame){root, p->functions[root].edges};
    while (depth) {
      Frame *frame = &work[depth - 1];
      size_t id = frame->function;
      if (frame->next) {
        size_t child = frame->next->callee;
        frame->next = frame->next->next;
        if (!index[child]) {
          index[child] = low[child] = ++serial;
          active[child] = 1;
          stack[top++] = child;
          work[depth++] = (Frame){child, p->functions[child].edges};
        } else if (active[child] && low[id] > index[child])
          low[id] = index[child];
      } else {
        depth--;
        if (depth && low[work[depth - 1].function] > low[id])
          low[work[depth - 1].function] = low[id];
        if (low[id] == index[id]) {
          size_t child;
          do {
            child = stack[--top];
            active[child] = 0;
            p->functions[child].component = components;
          } while (child != id);
          components++;
        }
      }
    }
  }
  return components;
}
