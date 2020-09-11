/**************************************************************************
***    
*** Copyright (c) 2003 Regents of the University of Michigan,
***               Hayward H. Chan and Igor L. Markov
***
***  Contact author(s): hhchan@umich.edu, imarkov@umich.edu
***  Original Affiliation:   EECS Department, 
***                          The University of Michigan,
***                          Ann Arbor, MI 48109-2122
***
***  Permission is hereby granted, free of charge, to any person obtaining 
***  a copy of this software and associated documentation files (the
***  "Software"), to deal in the Software without restriction, including
***  without limitation 
***  the rights to use, copy, modify, merge, publish, distribute, sublicense, 
***  and/or sell copies of the Software, and to permit persons to whom the 
***  Software is furnished to do so, subject to the following conditions:
***
***  The above copyright notice and this permission notice shall be included
***  in all copies or substantial portions of the Software.
***
*** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
*** EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
*** OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. 
*** IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
*** CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
*** OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR
*** THE USE OR OTHER DEALINGS IN THE SOFTWARE.
***
***
***************************************************************************/
#ifndef STACKQUEUE_H
#define STACKQUEUE_H

#include <iostream>
using namespace std;

const int MAX_SIZE = 2001; // ONE MORE THAN max-handle size

// -----STACK AND QUEUE WITH NO ERROR HANDLING AT ALL------
template <typename T>
class OwnStack
{
public:
   OwnStack();
   int size() const;

   void push(const T& newItem);
   T pop();

   T operator[](int index) const;
   T top() const;

private:
   int next;
   T item[MAX_SIZE];
};
// --------------------------------------------------------
template <typename T>
class OwnQueue
{
public:
   OwnQueue();
   int size() const;

   void enqueue(const T& newItem);
   T dequeue();

   T operator[](int index) const;
   T front() const;
   T back() const;

private:
   int head;
   int tail;
   T item[MAX_SIZE];
};


// -----IMPLEMENTATIONS------------------------------------
// -----OwnStack CLASS MEMBER FUNCTIONS--------------------
template <typename T>
OwnStack<T>::OwnStack() : next(0)
{}
// --------------------------------------------------------
template <typename T>
int OwnStack<T>::size() const
{
   return next;
}
// --------------------------------------------------------
template <typename T>
void OwnStack<T>::push(const T& newItem)
{
   item[next++] = newItem;
}
// --------------------------------------------------------
template <typename T>
T OwnStack<T>::pop()
{
   return item[--next];
}
// --------------------------------------------------------
template <typename T>
T OwnStack<T>::operator [](int index) const
{
   return item[index];
}
// --------------------------------------------------------
template <typename T>
T OwnStack<T>::top() const
{
   return item[next-1];
}
// -----OwnQueue CLASS MEMBER FUNCTIONS--------------------
template <typename T>
OwnQueue<T>::OwnQueue() : head(0), tail(0)
{}
// --------------------------------------------------------
template <typename T>
int OwnQueue<T>::size() const
{
   return (tail >= head)? (tail-head) : (tail-head+MAX_SIZE);
}
// --------------------------------------------------------
template <typename T>
void OwnQueue<T>::enqueue(const T& newItem)
{
   item[tail++] = newItem;
   tail %= MAX_SIZE;
}
// --------------------------------------------------------
template <typename T>
T OwnQueue<T>::dequeue()
{
   T result = item[head];
   head++;
   head %= MAX_SIZE;
   return result;
}
// --------------------------------------------------------
template <typename T>
T OwnQueue<T>::operator [](int index) const
{
   index += head;
   index %= MAX_SIZE;
   return item[index];
}
// --------------------------------------------------------
template <typename T>
T OwnQueue<T>::front() const
{
   return item[head];
}
// --------------------------------------------------------
template <typename T>
T OwnQueue<T>::back() const
{
   int index = tail-1;
   if (index < 0)
      index += MAX_SIZE;
   return item[index];
}
// --------------------------------------------------------
#endif
