/*
 * Copyright (c) 2001, 2002, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Driver code for whale mating problem
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

/*
 * 08 Feb 2012 : GWA : Driver code is in kern/synchprobs/driver.c. We will
 * replace that file. This file is yours to modify as you see fit.
 *
 * You should implement your solution to the whalemating problem below.
 */

// 13 Feb 2012 : GWA : Adding at the suggestion of Isaac Elbaz. These
// functions will allow you to do local initialization. They are called at
// the top of the corresponding driver code.

// Male start, female start. Male end, Female end.
// Much better than the quantum hanky-panky I had to present with the 
// bacteria.
struct semaphore *ms, *fs, *me, *fe;

void whalemating_init() {

  ms = sem_create("Male Start", 0);
  fs = sem_create("Female Start", 0);
  me = sem_create("Male End", 0);
  fe = sem_create("Female End", 0);

  return;
}

// 20 Feb 2012 : GWA : Adding at the suggestion of Nikhil Londhe. We don't
// care if your problems leak memory, but if you do, use this to clean up.

void whalemating_cleanup() {

  sem_destroy(ms);
  sem_destroy(fs);
  sem_destroy(me);
  sem_destroy(fe);

  return;
}

void
male(void *p, unsigned long which)
{
	struct semaphore * whalematingMenuSemaphore = (struct semaphore *)p;
  (void)which;
  
  male_start();

	// Implement this function
  V(ms);
  P(me);

  male_end();

  // 08 Feb 2012 : GWA : Please do not change this code. This is so that your
  // whalemating driver can return to the menu cleanly.
  V(whalematingMenuSemaphore);
  return;
}

void
female(void *p, unsigned long which)
{
	struct semaphore * whalematingMenuSemaphore = (struct semaphore *)p;
  (void)which;
  
  female_start();

	// Implement this function 
  V(fs);
  P(fe);

  female_end();
  
  // 08 Feb 2012 : GWA : Please do not change this code. This is so that your
  // whalemating driver can return to the menu cleanly.
  V(whalematingMenuSemaphore);
  return;
}

void
matchmaker(void *p, unsigned long which)
{
	struct semaphore * whalematingMenuSemaphore = (struct semaphore *)p;
  (void)which;
  
  matchmaker_start();

	// Implement this function 
  P(ms);
  P(fs);
  V(me);
  V(fe);

  matchmaker_end();
  
  // 08 Feb 2012 : GWA : Please do not change this code. This is so that your
  // whalemating driver can return to the menu cleanly.
  V(whalematingMenuSemaphore);
  return;
}

/*
 * You should implement your solution to the stoplight problem below. The
 * quadrant and direction mappings for reference: (although the problem is,
 * of course, stable under rotation)
 *
 *   | 0 |
 * --     --
 *    0 1
 * 3       1
 *    3 2
 * --     --
 *   | 2 | 
 *
 * As way to think about it, assuming cars drive on the right: a car entering
 * the intersection from direction X will enter intersection quadrant X
 * first.
 *
 * You will probably want to write some helper functions to assist
 * with the mappings. Modular arithmetic can help, e.g. a car passing
 * straight through the intersection entering from direction X will leave to
 * direction (X + 2) % 4 and pass through quadrants X and (X + 3) % 4.
 * Boo-yah.
 *
 * Your solutions below should call the inQuadrant() and leaveIntersection()
 * functions in drivers.c.
 */

// 13 Feb 2012 : GWA : Adding at the suggestion of Isaac Elbaz. These
// functions will allow you to do local initialization. They are called at
// the top of the corresponding driver code.

// Cars should drive on the left, unless you're a lefty and the config
// makes sense. :P

struct semaphore *zero, *one, *two, *three;
struct lock *flow_lock;

void stoplight_init() {

  /*zero = sem_create("Zero", 1);
  one = sem_create("One", 1);
  two = sem_create("Two", 1);
  three = sem_create("Three", 1);*/

  flow_lock = lock_create("Stoplight Flow Lock");

  return;
}

// 20 Feb 2012 : GWA : Adding at the suggestion of Nikhil Londhe. We don't
// care if your problems leak memory, but if you do, use this to clean up.

void stoplight_cleanup() {

  /*sem_destroy(zero);
  sem_destroy(one);
  sem_destroy(two);
  sem_destroy(three);*/

  lock_destroy(flow_lock);

  return;
}

struct semaphore * toSem(long num) {

  switch(num) {
    case 0:
      return zero;
    case 1:
      return one;
    case 2:
      return two;
    case 3:
      return three;
  }

  // End of non-viod function
  return zero;
}

void
gostraight(void *p, unsigned long direction)
{

  long opr, prev; 
	struct semaphore * stoplightMenuSemaphore = (struct semaphore *)p;

  // Direction Change: X, X + 3, X + 2

  opr = 0;

  if (!opr) {
    // X
    lock_acquire(flow_lock);
    opr++;
    //P(toSem(direction));
    inQuadrant(direction);
    prev = direction;
    direction = (direction + 3) % 4;
  }

  while (opr < 3) {
    opr++;
    //P(toSem(direction)); 
    inQuadrant(direction);
    //V(toSem(prev));
    prev = direction;

    if (opr == 2) {
      // X + 3
      direction = (direction + 3) % 4;
    }
     
    if (opr == 3) {
      // X + 2
      direction = (direction + 2) % 4;
    }

    //lock_release(flow_lock);
  }

  lock_release(flow_lock);

  leaveIntersection();
  
  // 08 Feb 2012 : GWA : Please do not change this code. This is so that your
  // stoplight driver can return to the menu cleanly.
  V(stoplightMenuSemaphore);
  return;
}

void
turnleft(void *p, unsigned long direction)
{
	struct semaphore * stoplightMenuSemaphore = (struct semaphore *)p;
  
  long opr, prev;

  // Direction Change: X, X + 3, X + 2, X + 1

  opr = 0;

  if (!opr) {
    lock_acquire(flow_lock);
    opr++;
    //P(toSem(direction));
    inQuadrant(direction);
    prev = direction;
    direction = (direction + 3) % 4;
//    lock_release(flow_lock);
  }

  while (opr < 4) {
    // lock_acquire(flow_lock);
    opr++;
    //P(toSem(direction));
    inQuadrant(direction);
    //V(toSem(prev));
    prev = direction;

    if (opr == 2) {
      direction = (direction + 2) % 4;
    }

    if (opr == 3) {
      direction = (direction + 1) % 4;
    }

  //  lock_release(flow_lock);
  }

  lock_release(flow_lock);

  leaveIntersection();
  
  // 08 Feb 2012 : GWA : Please do not change this code. This is so that your
  // stoplight driver can return to the menu cleanly.
  V(stoplightMenuSemaphore);
  return;
}

void
turnright(void *p, unsigned long direction)
{
	struct semaphore * stoplightMenuSemaphore = (struct semaphore *)p;

  long prev;

  lock_acquire(flow_lock);
  //P(toSem(direction));
  inQuadrant(direction);
  prev = direction;
  direction = (direction + 3) % 4;
//  lock_release(flow_lock);
  
//  lock_acquire(flow_lock);
  //P(toSem(direction));
  inQuadrant(direction);
  //V(toSem(prev));
  lock_release(flow_lock);
  
  leaveIntersection();

  // 08 Feb 2012 : GWA : Please do not change this code. This is so that your
  // stoplight driver can return to the menu cleanly.
  V(stoplightMenuSemaphore);
  return;
}
