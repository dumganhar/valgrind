/*  D1 cache simulator, generated by vg_cachegen.
 *     total size    = 65536 bytes
 *     line size     = 64 bytes
 *     associativity = 2-way associative
 *
 *  This file should be #include-d into vg_cachesim.c
 */

static char D1_desc_line[] = 
    "desc: D1 cache:         65536 B, 64 B, 2-way associative\n";

static UInt D1_tags[512][2];

static void cachesim_D1_initcache(void)
{
   UInt set, way;
   for (set = 0; set < 512; set++)
      for (way = 0; way < 2; way++)
         D1_tags[set][way] = 0;
}

static __inline__ 
void cachesim_D1_doref(Addr a, UChar size, ULong* m1, ULong *m2)
{
   register UInt set1 = ( a         >> 6) & (512-1);
   register UInt set2 = ((a + size) >> 6) & (512-1);
   register UInt tag  = a >> (6 + 9);

   if (set1 == set2) {

      if (tag == D1_tags[set1][0]) {
         return;
      }
      else if (tag == D1_tags[set1][1]) {
         D1_tags[set1][1] = D1_tags[set1][0];
         D1_tags[set1][0] = tag;
         return;
      }
      else {
         /* A miss */
         D1_tags[set1][1] = D1_tags[set1][0];
         D1_tags[set1][0] = tag;

         (*m1)++;
         cachesim_L2_doref(a, size, m2);
      }

   } else if ((set1 + 1) % 512 == set2) {

      Bool is_D1_miss = False;

      /* Block one */
      if (tag == D1_tags[set1][0]) {
      }
      else if (tag == D1_tags[set1][1]) {
         D1_tags[set1][1] = D1_tags[set1][0];
         D1_tags[set1][0] = tag;
      }
      else {
         /* A miss */
         D1_tags[set1][1] = D1_tags[set1][0];
         D1_tags[set1][0] = tag;

         is_D1_miss = True;
      }

      /* Block two */
      if (tag == D1_tags[set2][0]) {
      }
      else if (tag == D1_tags[set2][1]) {
         D1_tags[set2][1] = D1_tags[set2][0];
         D1_tags[set2][0] = tag;
      }
      else {
         /* A miss */
         D1_tags[set2][1] = D1_tags[set2][0];
         D1_tags[set2][0] = tag;

         is_D1_miss = True;
      }

      /* Miss treatment */
      if (is_D1_miss) {
         (*m1)++;
         cachesim_L2_doref(a, size, m2);
      }

   } else {
      VG_(printf)("\nERROR: Data item 0x%x of size %u bytes is in two non-adjacent\n", a, size);
      VG_(printf)("sets %d and %d.\n", set1, set2);
      VG_(panic)("D1 cache set mismatch");
   }
}
