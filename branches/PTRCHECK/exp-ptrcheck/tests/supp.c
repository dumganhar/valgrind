#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>

int main(void)
{
   int   i = 11;
   char* buf = malloc(sizeof(char) * 6);
   char  c = buf[-1];                  // LoadStoreErr
   char* x = buf + (int)buf;           // ArithErr
   char* y = (char*)((int)buf * i);    // AsmErr
   write(-1, buf+3, 5);                // SysParamErr

   return x-y+c;
}
