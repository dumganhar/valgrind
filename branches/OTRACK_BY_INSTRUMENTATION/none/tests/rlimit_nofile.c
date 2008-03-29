#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <unistd.h>

int main(int argc, char **argv)
{
   struct rlimit oldrlim;
   struct rlimit newrlim;
   int fd;
   close(4); /* seems to be inherited from parent on SuSE 10.1 amd64 */
   if (getrlimit(RLIMIT_NOFILE, &oldrlim) < 0)
   {
      perror("getrlimit");
      exit(1);
   }

   newrlim.rlim_cur = oldrlim.rlim_cur / 2;
   newrlim.rlim_max = oldrlim.rlim_max;
     
   if (setrlimit(RLIMIT_NOFILE, &newrlim) < 0)
   {
      perror("setrlimit");
      exit(1);
   }
     
   if (getrlimit(RLIMIT_NOFILE, &newrlim) < 0)
   {
      perror("getrlimit");
      exit(1);
   }

   if (newrlim.rlim_cur != oldrlim.rlim_cur / 2)
   {
      fprintf(stderr, "rlim_cur is %lu (should be %lu)\n",
              newrlim.rlim_cur, oldrlim.rlim_cur / 2);
   }

   if (newrlim.rlim_max != oldrlim.rlim_max)
   {
      fprintf(stderr, "rlim_max is %lu (should be %lu)\n",
              newrlim.rlim_max, oldrlim.rlim_max);
   }

   newrlim.rlim_cur -= 3; /* allow for stdin, stdout and stderr */

   while (newrlim.rlim_cur-- > 0)
   {
      if (open("/dev/null", O_RDONLY) < 0)
      {
         perror("open");
      }
   }

   if ((fd = open("/dev/null", O_RDONLY)) >= 0)
   {
      fprintf(stderr, "open succeeded with fd %d - it should have failed!\n", fd);
   }
   else if (errno != EMFILE)
   {
      perror("open");
   }
   
   exit(0);
}
