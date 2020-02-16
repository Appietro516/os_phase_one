#include <stdio.h>
#include <usloss.h>
#include <phase1.h>

int test_fork();
char buf[256];

int start1(char *arg)
{
  int status, pid1, kidpid;

  printf("start1(): started\n");
  pid1 = fork1("test_fork", test_fork, NULL, USLOSS_MIN_STACK, 3);

  printf("start1(): after fork of child %d\n", pid1);
  printf("start1(): performing join\n");

  kidpid = join(&status);
  
  return 0; /* so gcc will not complain about its absence... */
}

int test_fork()
{
  printf("Inside test_fork f ptr.");
  return 0;
} 
