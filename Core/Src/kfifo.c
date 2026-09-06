#include "kfifo.h"
#include <string.h>

void Kfifo_Init(Kfifo_t *fifo, void *buffer, size_t element_size, size_t capacity)
{
  fifo->buffer = (uint8_t *)buffer;
  fifo->element_size = element_size;
  fifo->capacity = capacity;
  fifo->read_index = 0U;
  fifo->write_index = 0U;
  fifo->count = 0U;
}

bool Kfifo_Push(Kfifo_t *fifo, const void *element)
{
  if (fifo->count == fifo->capacity)
  {
    return false;
  }
  memcpy(&fifo->buffer[fifo->write_index * fifo->element_size], element, fifo->element_size);
  fifo->write_index = (fifo->write_index + 1U) % fifo->capacity;
  fifo->count++;
  return true;
}

bool Kfifo_PushOverwrite(Kfifo_t *fifo, const void *element)
{
  if (fifo->count == fifo->capacity)
  {
    fifo->read_index = (fifo->read_index + 1U) % fifo->capacity;
    fifo->count--;
  }
  return Kfifo_Push(fifo, element);
}

bool Kfifo_Pop(Kfifo_t *fifo, void *element)
{
  if (fifo->count == 0U)
  {
    return false;
  }
  memcpy(element, &fifo->buffer[fifo->read_index * fifo->element_size], fifo->element_size);
  fifo->read_index = (fifo->read_index + 1U) % fifo->capacity;
  fifo->count--;
  return true;
}

size_t Kfifo_Count(const Kfifo_t *fifo)
{
  return fifo->count;
}
