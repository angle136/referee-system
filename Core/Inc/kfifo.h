#ifndef KFIFO_H
#define KFIFO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
  uint8_t *buffer;
  size_t element_size;
  size_t capacity;
  size_t read_index;
  size_t write_index;
  size_t count;
} Kfifo_t;

void Kfifo_Init(Kfifo_t *fifo, void *buffer, size_t element_size, size_t capacity);
bool Kfifo_Push(Kfifo_t *fifo, const void *element);
bool Kfifo_Pop(Kfifo_t *fifo, void *element);
size_t Kfifo_Count(const Kfifo_t *fifo);

#endif
