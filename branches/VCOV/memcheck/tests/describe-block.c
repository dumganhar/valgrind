#include "../memcheck.h"

int main()
{
   char magic_foople_zone[0x1000];
   VALGRIND_CREATE_BLOCK(magic_foople_zone, 0x1000, "magic foople zone");
   VALGRIND_MAKE_MEM_NOACCESS(magic_foople_zone, 0x1000);
   magic_foople_zone[0] = 'x';
   return 0;
}
