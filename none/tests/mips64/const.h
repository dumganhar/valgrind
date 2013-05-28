#define N 256

const int reg_val1[N] = {
   0x00000000L, 0x00000000L, 0x09823b6eL, 0x0d4326d9L,
   0x130476dcL, 0x17c56b6bL, 0x1a864db2L, 0x1e475005L,
   0x2608edb8L, 0x22c9f00fL, 0x2f8ad6d6L, 0x2b4bcb61L,
   0x350c9b64L, 0x31cd86d3L, 0x3c8ea00aL, 0x384fbdbdL,
   0x4c11db70L, 0x48d0c6c7L, 0x4593e01eL, 0x4152fda9L,
   0x5f15adacL, 0x5bd4b01bL, 0x569796c2L, 0x52568b75L,
   0x6a1936c8L, 0x6ed82b7fL, 0x639b0da6L, 0x675a1011L,
   0x791d4014L, 0x7ddc5da3L, 0x709f7b7aL, 0x745e66cdL,
   0x9823b6e0L, 0x9ce2ab57L, 0x91a18d8eL, 0x95609039L,
   0x8b27c03cL, 0x8fe6dd8bL, 0x82a5fb52L, 0x8664e6e5L,
   0xbe2b5b58L, 0xbaea46efL, 0xb7a96036L, 0xb3687d81L,
   0xad2f2d84L, 0xa9ee3033L, 0xa4ad16eaL, 0xa06c0b5dL,
   0xd4326d90L, 0xd0f37027L, 0xddb056feL, 0xd9714b49L,
   0xc7361b4cL, 0xc3f706fbL, 0xceb42022L, 0xca753d95L,
   0xf23a8028L, 0xf6fb9d9fL, 0xfbb8bb46L, 0xff79a6f1L,
   0xe13ef6f4L, 0xe5ffeb43L, 0xe8bccd9aL, 0xec7dd02dL,
   0x34867077L, 0x30476dc0L, 0x3d044b19L, 0x39c556aeL,
   0x278206abL, 0x23431b1cL, 0x2e003dc5L, 0x2ac12072L,
   0x128e9dcfL, 0x164f8078L, 0x1b0ca6a1L, 0x1fcdbb16L,
   0x018aeb13L, 0x054bf6a4L, 0x0808d07dL, 0x0cc9cdcaL,
   0x7897ab07L, 0x7c56b6b0L, 0x71159069L, 0x75d48ddeL,
   0x6b93dddbL, 0x6f52c06cL, 0x6211e6b5L, 0x66d0fb02L,
   0x5e9f46bfL, 0x5a5e5b08L, 0x571d7dd1L, 0x53dc6066L,
   0x4d9b3063L, 0x495a2dd4L, 0x44190b0dL, 0x40d816baL,
   0xaca5c697L, 0xa864db20L, 0xa527fdf9L, 0xa1e6e04eL,
   0xbfa1b04bL, 0xbb60adfcL, 0xb6238b25L, 0xb2e29692L,
   0x8aad2b2fL, 0x00000000L, 0x00000000L, 0x87ee0df6L,
   0x99a95df3L, 0x9d684044L, 0x902b669dL, 0x94ea7b2aL,
   0xe0b41de7L, 0xe4750050L, 0xe9362689L, 0xedf73b3eL,
   0xf3b06b3bL, 0xf771768cL, 0xfa325055L, 0xfef34de2L,
   0xc6bcf05fL, 0xc27dede8L, 0xcf3ecb31L, 0xcbffd686L,
   0xd5b88683L, 0xd1799b34L, 0xdc3abdedL, 0xd8fba05aL,
   0x690ce0eeL, 0x6dcdfd59L, 0x608edb80L, 0x644fc637L,
   0x7a089632L, 0x7ec98b85L, 0x738aad5cL, 0x774bb0ebL,
   0x4f040d56L, 0x4bc510e1L, 0x46863638L, 0x42472b8fL,
   0x5c007b8aL, 0x58c1663dL, 0x558240e4L, 0x51435d53L,
   0x251d3b9eL, 0x21dc2629L, 0x2c9f00f0L, 0x285e1d47L,
   0x36194d42L, 0x32d850f5L, 0x3f9b762cL, 0x3b5a6b9bL,
   0x0315d626L, 0x07d4cb91L, 0x0a97ed48L, 0x0e56f0ffL,
   0x1011a0faL, 0x14d0bd4dL, 0x19939b94L, 0x1d528623L,
   0xf12f560eL, 0xf5ee4bb9L, 0xf8ad6d60L, 0xfc6c70d7L,
   0xe22b20d2L, 0xe6ea3d65L, 0xeba91bbcL, 0xef68060bL,
   0xd727bbb6L, 0xd3e6a601L, 0xdea580d8L, 0xda649d6fL,
   0xc423cd6aL, 0x00000000L, 0xcda1f604L, 0x00000000L,
   0xbd3e8d7eL, 0xb9ff90c9L, 0xb4bcb610L, 0xb07daba7L,
   0xae3afba2L, 0xaafbe615L, 0xa7b8c0ccL, 0xa379dd7bL,
   0x9b3660c6L, 0x9ff77d71L, 0x92b45ba8L, 0x9675461fL,
   0x8832161aL, 0x8cf30badL, 0x81b02d74L, 0x857130c3L,
   0x5d8a9099L, 0x594b8d2eL, 0x5408abf7L, 0x50c9b640L,
   0x4e8ee645L, 0x4a4ffbf2L, 0x470cdd2bL, 0x43cdc09cL,
   0x7b827d21L, 0x7f436096L, 0x7200464fL, 0x76c15bf8L,
   0x68860bfdL, 0x6c47164aL, 0x61043093L, 0x65c52d24L,
   0x119b4be9L, 0x155a565eL, 0x18197087L, 0x1cd86d30L,
   0x029f3d35L, 0x065e2082L, 0x0b1d065bL, 0x0fdc1becL,
   0x3793a651L, 0x3352bbe6L, 0x3e119d3fL, 0x3ad08088L,
   0x2497d08dL, 0x2056cd3aL, 0x2d15ebe3L, 0x29d4f654L,
   0xc5a92679L, 0xc1683bceL, 0xcc2b1d17L, 0xc8ea00a0L,
   0xd6ad50a5L, 0xd26c4d12L, 0xdf2f6bcbL, 0xdbee767cL,
   0xe3a1cbc1L, 0xe760d676L, 0xea23f0afL, 0xeee2ed18L,
   0xf0a5bd1dL, 0xf464a0aaL, 0xf9278673L, 0xfde69bc4L,
   0x89b8fd09L, 0x8d79e0beL, 0x803ac667L, 0x84fbdbd0L,
   0x9abc8bd5L, 0x9e7d9662L, 0x933eb0bbL, 0x97ffad0cL,
   0xafb010b1L, 0xab710d06L, 0xa6322bdfL, 0xa2f33668L,
   0xbcb4666dL, 0xb8757bdaL, 0xb5365d03L, 0xb1f740b4L
};

unsigned long long reg_val2[N];

void init_reg_val2()
{
   unsigned long c = 19650218UL;
   int i;
   reg_val2[0]= c & 0xffffffffUL;
   for (i = 1; i < N; i++) {
         reg_val2[i] = (1812433253UL * (reg_val2[i - 1] ^
                        (reg_val2[i - 1] >> 30)) + i);
   }
}

unsigned long long reg_val_zero[N];

void init_reg_val_zero()
{
   int i;
   for (i = 0; i < N; i++) {
      reg_val_zero[i] = 0;
   }
}

/* Floating point const. */
#define MAX_ARR 24
#define NaN 0.0/0.0

const double fr_d[] = {
   -34785666666.475, 356047.56,           -1.0,       23.04,
   1752,             0.0024575,           0.00000001, -248562.76,
   1384.6,           -7.2945676,          1000000000, -5786.47,
   -347856.475,      356047.56,           -1.0,       23.04,
   0,                45655555555.2489562, 3,          -1,
   -45786.476,       4566666.2489562,     34.00046,   45786.476,
};

const double fs_d[] = {
   0,           456.2489562, 3,          -1,
   1384.6,      -7.2945676,  1000000000, -5786.47,
   1752,        0.0024575,   0.00000001, -248562.76,
   -45786.476,  456.2489562, 34.00046,   45786.476,
   1752065,     107,         -45667.24,  -7.2945676,
   -347856.475, 356047.56,   -1.0,       23.04
};

const double ft_d[] = {
   -45786.476,  456.2489562, 34.00046,   45786.476,
   1752065,     107,         -45667.24,  -7.2945676,
   -347856.475, 356047.56,   -1.0,       23.04,
   0,           456.2489562, 3,          -1,
   1384.6,      -7.2945676,  1000000000, -5786.47,
   1752,        0.0024575,   0.00000001, -248562.76
};

const float fr_f[] = {
   -347856.475, 356047.56,   -1.0,       23.04,
   1752,        0.0024575,   0.00000001, -248562.76,
   1384.6,      -7.2945676,  1000000000, -5786.47,
   -347856.475, 356047.56,   -1.0,       23.04,
   0,           456.2489562, 3,          -1,
   -45786.476,  456.2489562, 34.00046,   45786.476
};

const float fs_f[] = {
   0,           456.2489562, 3,          -1,
   1384.6,      -7.2945676,  1000000000, -5786.47,
   1752,        0.0024575,   0.1234,     -248562.76,
   -45786.476,  456.2489562, 34.00046,   45786.476,
   1752065,     107,         -45667.24,  -7.2945676,
   -347856.475, 356047.56,   -1.0,       23.04
};

const float ft_f[] = {
   -45786.476,  456.2489562, 34.00046,   45786.476,
   1752065,     107,         -45667.24,  -7.2945676,
   -347856.475, 356047.56,   -1.0,       23.04,
   0,           456.2489562, 3,          -1,
   1384.6,      -7.2945676,  1000000000, -5786.47,
   1752,        0.0024575,   0.00000001, -248562.76
};

const int fs_w[] = {
   0,          456,        3,          -1,
   0xffffffff, 356,        1000000000, -5786,
   1752,       24575,      10,         -248562,
   -45786,     456,        34,         45786,
   1752065,    107,        -45667,     -7,
   -347856,    0x80000000, 0xfffffff,  23,
};

const long fs_l[] = {
   18,         25,         3,          -1,
   0xffffffff, 356,        1000000,    -5786,
   -1,         24575,      10,         -125458,
   -486,       456,        34,         45786,
   0,          1700000,   -45667,     -7,
   -347856,    0x80000000, 0xfffffff,  23,
};