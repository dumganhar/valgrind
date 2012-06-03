
/* The following tests appear not to be accepted by the assembler.
      VCVTPD2PS_128 (memory form)
*/

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <malloc.h>

typedef  unsigned char           UChar;
typedef  unsigned int            UInt;
typedef  unsigned long int       UWord;
typedef  unsigned long long int  ULong;

#define IS_32_ALIGNED(_ptr) (0 == (0x1F & (UWord)(_ptr)))

typedef  union { UChar u8[32];  UInt u32[8];  }  YMM;

typedef  struct {  YMM a1; YMM a2; YMM a3; YMM a4; ULong u64; }  Block;

void showYMM ( YMM* vec )
{
   int i;
   assert(IS_32_ALIGNED(vec));
   for (i = 31; i >= 0; i--) {
      printf("%02x", (UInt)vec->u8[i]);
      if (i > 0 && 0 == ((i+0) & 7)) printf(".");
   }
}

void showBlock ( char* msg, Block* block )
{
   printf("  %s\n", msg);
   printf("    "); showYMM(&block->a1); printf("\n");
   printf("    "); showYMM(&block->a2); printf("\n");
   printf("    "); showYMM(&block->a3); printf("\n");
   printf("    "); showYMM(&block->a4); printf("\n");
   printf("    %016llx\n", block->u64);
}

UChar randUChar ( void )
{
   static UInt seed = 80021;
   seed = 1103515245 * seed + 12345;
   return (seed >> 17) & 0xFF;
}

void randBlock ( Block* b )
{
   int i;
   UChar* p = (UChar*)b;
   for (i = 0; i < sizeof(Block); i++)
      p[i] = randUChar();
}


/* Generate a function test_NAME, that tests the given insn, in both
   its mem and reg forms.  The reg form of the insn may mention, as
   operands only %ymm6, %ymm7, %ymm8, %ymm9 and %r14.  The mem form of
   the insn may mention as operands only (%rax), %ymm7, %ymm8, %ymm9
   and %r14.  It's OK for the insn to clobber ymm0, as this is needed
   for testing PCMPxSTRx. */

#define GEN_test_RandM(_name, _reg_form, _mem_form)   \
    \
    __attribute__ ((noinline)) static void test_##_name ( void )   \
    { \
       Block* b = memalign(32, sizeof(Block)); \
       randBlock(b); \
       printf("%s(reg)\n", #_name); \
       showBlock("before", b); \
       __asm__ __volatile__( \
          "vmovdqa   0(%0),%%ymm7"  "\n\t" \
          "vmovdqa  32(%0),%%ymm8"  "\n\t" \
          "vmovdqa  64(%0),%%ymm6"  "\n\t" \
          "vmovdqa  96(%0),%%ymm9"  "\n\t" \
          "movq    128(%0),%%r14"   "\n\t" \
          _reg_form   "\n\t" \
          "vmovdqa %%ymm7,  0(%0)"  "\n\t" \
          "vmovdqa %%ymm8, 32(%0)"  "\n\t" \
          "vmovdqa %%ymm6, 64(%0)"  "\n\t" \
          "vmovdqa %%ymm9, 96(%0)"  "\n\t" \
          "movq    %%r14, 128(%0)"  "\n\t" \
          : /*OUT*/  \
          : /*IN*/"r"(b) \
          : /*TRASH*/"xmm0","xmm7","xmm8","xmm6","xmm9","r14","memory","cc" \
       ); \
       showBlock("after", b); \
       randBlock(b); \
       printf("%s(mem)\n", #_name); \
       showBlock("before", b); \
       __asm__ __volatile__( \
          "leaq      0(%0),%%rax"  "\n\t" \
          "vmovdqa  32(%0),%%ymm8"  "\n\t" \
          "vmovdqa  64(%0),%%ymm7"  "\n\t" \
          "vmovdqa  96(%0),%%ymm9"  "\n\t" \
          "movq    128(%0),%%r14"   "\n\t" \
          _mem_form   "\n\t" \
          "vmovdqa %%ymm8, 32(%0)"  "\n\t" \
          "vmovdqa %%ymm7, 64(%0)"  "\n\t" \
          "vmovdqa %%ymm9, 96(%0)"  "\n\t" \
          "movq    %%r14, 128(%0)"  "\n\t" \
          : /*OUT*/  \
          : /*IN*/"r"(b) \
          : /*TRASH*/"xmm0","xmm8","xmm7","xmm9","r14","rax","memory","cc" \
       ); \
       showBlock("after", b); \
       printf("\n"); \
       free(b); \
    }

#define GEN_test_Ronly(_name, _reg_form) \
   GEN_test_RandM(_name, _reg_form, "")
#define GEN_test_Monly(_name, _mem_form) \
   GEN_test_RandM(_name, "", _mem_form)


GEN_test_RandM(VPOR_128,
               "vpor %%xmm6,  %%xmm8, %%xmm7",
               "vpor (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VPXOR_128,
               "vpxor %%xmm6,  %%xmm8, %%xmm7",
               "vpxor (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VPSUBB_128,
               "vpsubb %%xmm6,  %%xmm8, %%xmm7",
               "vpsubb (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VPSUBD_128,
               "vpsubd %%xmm6,  %%xmm8, %%xmm7",
               "vpsubd (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VPADDD_128,
               "vpaddd %%xmm6,  %%xmm8, %%xmm7",
               "vpaddd (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VPMOVZXWD_128,
               "vpmovzxwd %%xmm6,  %%xmm8",
               "vpmovzxwd (%%rax), %%xmm8")

GEN_test_RandM(VPMOVZXBW_128,
               "vpmovzxbw %%xmm6,  %%xmm8",
               "vpmovzxbw (%%rax), %%xmm8")

GEN_test_RandM(VPBLENDVB_128,
               "vpblendvb %%xmm9, %%xmm6,  %%xmm8, %%xmm7",
               "vpblendvb %%xmm9, (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VPMINSD_128,
               "vpminsd %%xmm6,  %%xmm8, %%xmm7",
               "vpminsd (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VPMAXSD_128,
               "vpmaxsd %%xmm6,  %%xmm8, %%xmm7",
               "vpmaxsd (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VANDPD_128,
               "vandpd %%xmm6,  %%xmm8, %%xmm7",
               "vandpd (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VCVTSI2SD_32,
               "vcvtsi2sdl %%r14d,  %%xmm8, %%xmm7",
               "vcvtsi2sdl (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VCVTSI2SD_64,
               "vcvtsi2sdq %%r14,   %%xmm8, %%xmm7",
               "vcvtsi2sdq (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VCVTSI2SS_64,
               "vcvtsi2ssq %%r14,   %%xmm8, %%xmm7",
               "vcvtsi2ssq (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VCVTTSD2SI_32,
               "vcvttsd2si %%xmm8,  %%r14d",
               "vcvttsd2si (%%rax), %%r14d")

GEN_test_RandM(VCVTTSD2SI_64,
               "vcvttsd2si %%xmm8,  %%r14",
               "vcvttsd2si (%%rax), %%r14")

GEN_test_RandM(VPSHUFB_128,
               "vpshufb %%xmm6,  %%xmm8, %%xmm7",
               "vpshufb (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VCMPSD_128_0x0,
               "vcmpsd $0, %%xmm6,  %%xmm8, %%xmm7",
               "vcmpsd $0, (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VCMPSD_128_0xD,
               "vcmpsd $0xd, %%xmm6,  %%xmm8, %%xmm7",
               "vcmpsd $0xd, (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VSQRTSD_128,
               "vsqrtsd %%xmm6,  %%xmm8, %%xmm7",
               "vsqrtsd (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VORPS_128,
               "vorps %%xmm6,  %%xmm8, %%xmm7",
               "vorps (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VANDNPS_128,
               "vandnps %%xmm6,  %%xmm8, %%xmm7",
               "vandnps (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VMAXSS_128,
               "vmaxss %%xmm6,  %%xmm8, %%xmm7",
               "vmaxss (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VMINSS_128,
               "vminss %%xmm6,  %%xmm8, %%xmm7",
               "vminss (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VANDPS_128,
               "vandps %%xmm6,  %%xmm8, %%xmm7",
               "vandps (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VCVTSI2SS_128,
               "vcvtsi2ssl %%r14d,  %%xmm8, %%xmm7",
               "vcvtsi2ssl (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VUNPCKLPS_128,
               "vunpcklps %%xmm6,  %%xmm8, %%xmm7",
               "vunpcklps (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VDIVSS_128,
               "vdivss %%xmm6,  %%xmm8, %%xmm7",
               "vdivss (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VADDSS_128,
               "vaddss %%xmm6,  %%xmm8, %%xmm7",
               "vaddss (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VSUBSS_128,
               "vsubss %%xmm6,  %%xmm8, %%xmm7",
               "vsubss (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VMULSS_128,
               "vmulss %%xmm6,  %%xmm8, %%xmm7",
               "vmulss (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VPUNPCKLBW_128,
               "vpunpcklbw %%xmm6,  %%xmm8, %%xmm7",
               "vpunpcklbw (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VPUNPCKHBW_128,
               "vpunpckhbw %%xmm6,  %%xmm8, %%xmm7",
               "vpunpckhbw (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VCVTTSS2SI_32,
               "vcvttss2si %%xmm8,  %%r14d",
               "vcvttss2si (%%rax), %%r14d")

GEN_test_RandM(VMOVQ_XMMorMEM64_to_XMM,
               "vmovq %%xmm7,  %%xmm8",
               "vmovq (%%rax), %%xmm8")

/* NB tests the reg form only */
GEN_test_Ronly(VMOVQ_XMM_to_IREG64,
               "vmovq %%xmm7, %%r14")

/* This insn only exists in the reg-reg-reg form. */
GEN_test_Ronly(VMOVHLPS_128,
               "vmovhlps %%xmm6, %%xmm8, %%xmm7")

GEN_test_RandM(VPABSD_128,
               "vpabsd %%xmm6,  %%xmm8",
               "vpabsd (%%rax), %%xmm8")

/* This insn only exists in the reg-reg-reg form. */
GEN_test_Ronly(VMOVLHPS_128,
               "vmovlhps %%xmm6, %%xmm8, %%xmm7")

GEN_test_Monly(VMOVNTDQ_128,
               "vmovntdq %%xmm8, (%%rax)")

GEN_test_RandM(VMOVUPS_XMM_to_XMMorMEM,
               "vmovups %%xmm8, %%xmm7",
               "vmovups %%xmm9, (%%rax)")

GEN_test_RandM(VMOVQ_IREGorMEM64_to_XMM,
               "vmovq %%r14, %%xmm7",
               "vmovq (%%rax), %%xmm9")

GEN_test_RandM(VPCMPESTRM_0x45_128,
               "vpcmpestrm $0x45, %%xmm7, %%xmm8;  movapd %%xmm0, %%xmm9",
               "vpcmpestrm $0x45, (%%rax), %%xmm8; movapd %%xmm0, %%xmm9")

/* NB tests the reg form only */
GEN_test_Ronly(VMOVD_XMM_to_IREG32,
               "vmovd %%xmm7, %%r14d")

GEN_test_RandM(VCVTSD2SS_128,
               "vcvtsd2ss %%xmm9,  %%xmm8, %%xmm7",
               "vcvtsd2ss (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VCVTSS2SD_128,
               "vcvtss2sd %%xmm9,  %%xmm8, %%xmm7",
               "vcvtss2sd (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VPACKUSWB_128,
               "vpackuswb %%xmm9,  %%xmm8, %%xmm7",
               "vpackuswb (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VCVTTSS2SI_64,
               "vcvttss2si %%xmm8,  %%r14",
               "vcvttss2si (%%rax), %%r14")

GEN_test_Ronly(VPMOVMSKB_128,
               "vpmovmskb %%xmm8, %%r14")

GEN_test_RandM(VPAND_128,
               "vpand %%xmm9,  %%xmm8, %%xmm7",
               "vpand (%%rax), %%xmm8, %%xmm7")

GEN_test_Monly(VMOVHPD_128,
               "vmovhpd %%xmm8, (%%rax)")

GEN_test_RandM(VPCMPEQB_128,
               "vpcmpeqb %%xmm9,  %%xmm8, %%xmm7",
               "vpcmpeqb (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VSHUFPS_0x39_128,
               "vshufps $0x39, %%xmm9,  %%xmm8, %%xmm7",
               "vshufps $0xC6, (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VMULPS_128,
               "vmulps %%xmm9,  %%xmm8, %%xmm7",
               "vmulps (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VSUBPS_128,
               "vsubps %%xmm9,  %%xmm8, %%xmm7",
               "vsubps (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VADDPS_128,
               "vaddps %%xmm9,  %%xmm8, %%xmm7",
               "vaddps (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VMAXPS_128,
               "vmaxps %%xmm9,  %%xmm8, %%xmm7",
               "vmaxps (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VMINPS_128,
               "vminps %%xmm9,  %%xmm8, %%xmm7",
               "vminps (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VCVTPS2DQ_128,
               "vcvtps2dq %%xmm8, %%xmm7",
               "vcvtps2dq (%%rax), %%xmm8")

GEN_test_RandM(VPSHUFLW_0x39_128,
               "vpshuflw $0x39, %%xmm9,  %%xmm7",
               "vpshuflw $0xC6, (%%rax), %%xmm8")

GEN_test_RandM(VPSHUFHW_0x39_128,
               "vpshufhw $0x39, %%xmm9,  %%xmm7",
               "vpshufhw $0xC6, (%%rax), %%xmm8")

GEN_test_RandM(VPMULLW_128,
               "vpmullw %%xmm9,  %%xmm8, %%xmm7",
               "vpmullw (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VPADDUSW_128,
               "vpaddusw %%xmm9,  %%xmm8, %%xmm7",
               "vpaddusw (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VPMULHUW_128,
               "vpmulhuw %%xmm9,  %%xmm8, %%xmm7",
               "vpmulhuw (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VPADDUSB_128,
               "vpaddusb %%xmm9,  %%xmm8, %%xmm7",
               "vpaddusb (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VPUNPCKLWD_128,
               "vpunpcklwd %%xmm6,  %%xmm8, %%xmm7",
               "vpunpcklwd (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VPUNPCKHWD_128,
               "vpunpckhwd %%xmm6,  %%xmm8, %%xmm7",
               "vpunpckhwd (%%rax), %%xmm8, %%xmm7")

GEN_test_Ronly(VPSLLD_0x05_128,
               "vpslld $0x5, %%xmm9,  %%xmm7")

GEN_test_Ronly(VPSRLD_0x05_128,
               "vpsrld $0x5, %%xmm9,  %%xmm7")

GEN_test_RandM(VPSUBUSB_128,
               "vpsubusb %%xmm9,  %%xmm8, %%xmm7",
               "vpsubusb (%%rax), %%xmm8, %%xmm7")

GEN_test_Ronly(VPSRLDQ_0x05_128,
               "vpsrldq $0x5, %%xmm9,  %%xmm7")

GEN_test_Ronly(VPSLLDQ_0x05_128,
               "vpslldq $0x5, %%xmm9,  %%xmm7")

GEN_test_RandM(VPANDN_128,
               "vpandn %%xmm9,  %%xmm8, %%xmm7",
               "vpandn (%%rax), %%xmm8, %%xmm7")

/* NB tests the mem form only */
GEN_test_Monly(VMOVD_XMM_to_MEM32,
               "vmovd %%xmm7, (%%rax)")

GEN_test_RandM(VPINSRD_128,
               "vpinsrd $0, %%r14d,  %%xmm8, %%xmm7",
               "vpinsrd $3, (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VPUNPCKLQDQ_128,
               "vpunpcklqdq %%xmm6,  %%xmm8, %%xmm7",
               "vpunpcklqdq (%%rax), %%xmm8, %%xmm7")

GEN_test_Ronly(VPSRLW_0x05_128,
               "vpsrlw $0x5, %%xmm9,  %%xmm7")

GEN_test_RandM(VPADDW_128,
               "vpaddw %%xmm6,  %%xmm8, %%xmm7",
               "vpaddw (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VPACKSSDW_128,
               "vpackssdw %%xmm9,  %%xmm8, %%xmm7",
               "vpackssdw (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VPUNPCKLDQ_128,
               "vpunpckldq %%xmm6,  %%xmm8, %%xmm7",
               "vpunpckldq (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VINSERTPS_0x39_128,
               "vinsertps $0x39, %%xmm6,  %%xmm8, %%xmm7",
               "vinsertps $0xC6, (%%rax), %%xmm8, %%xmm7")

GEN_test_Monly(VMOVSD_M64_XMM, "vmovsd (%%rax), %%xmm8")

GEN_test_Monly(VMOVSS_M64_XMM, "vmovss (%%rax), %%xmm8")

GEN_test_Monly(VMOVSD_XMM_M64, "vmovsd %%xmm8, (%%rax)")

GEN_test_Monly(VMOVSS_XMM_M32, "vmovss %%xmm8, (%%rax)")

GEN_test_RandM(VMOVUPD_GtoE_128,
               "vmovupd %%xmm9,  %%xmm6",
               "vmovupd %%xmm7, (%%rax)")

GEN_test_RandM(VMOVAPD_EtoG_128,
               "vmovapd %%xmm6,  %%xmm8",
               "vmovapd (%%rax), %%xmm9")

GEN_test_RandM(VMOVAPD_EtoG_256,
               "vmovapd %%ymm6,  %%ymm8",
               "vmovapd (%%rax), %%ymm9")

GEN_test_RandM(VMOVAPS_EtoG_128,
               "vmovaps %%xmm6,  %%xmm8",
               "vmovaps (%%rax), %%xmm9")

GEN_test_RandM(VMOVAPS_GtoE_128,
               "vmovaps %%xmm9,  %%xmm6",
               "vmovaps %%xmm7, (%%rax)")

GEN_test_RandM(VMOVAPS_GtoE_256,
               "vmovaps %%ymm9,  %%ymm6",
               "vmovaps %%ymm7, (%%rax)")

GEN_test_RandM(VMOVAPD_GtoE_128,
               "vmovapd %%xmm9,  %%xmm6",
               "vmovapd %%xmm7, (%%rax)")

GEN_test_RandM(VMOVAPD_GtoE_256,
               "vmovapd %%ymm9,  %%ymm6",
               "vmovapd %%ymm7, (%%rax)")

GEN_test_RandM(VMOVDQU_EtoG_128,
               "vmovdqu %%xmm6,  %%xmm8",
               "vmovdqu (%%rax), %%xmm9")

GEN_test_RandM(VMOVDQA_EtoG_128,
               "vmovdqa %%xmm6,  %%xmm8",
               "vmovdqa (%%rax), %%xmm9")

GEN_test_RandM(VMOVDQA_EtoG_256,
               "vmovdqa %%ymm6,  %%ymm8",
               "vmovdqa (%%rax), %%ymm9")

GEN_test_RandM(VMOVDQU_GtoE_128,
               "vmovdqu %%xmm9,  %%xmm6",
               "vmovdqu %%xmm7, (%%rax)")

GEN_test_RandM(VMOVDQA_GtoE_128,
               "vmovdqa %%xmm9,  %%xmm6",
               "vmovdqa %%xmm7, (%%rax)")

GEN_test_RandM(VMOVDQA_GtoE_256,
               "vmovdqa %%ymm9,  %%ymm6",
               "vmovdqa %%ymm7, (%%rax)")

GEN_test_Monly(VMOVQ_XMM_MEM64, "vmovq %%xmm8, (%%rax)")

GEN_test_RandM(VMOVD_IREGorMEM32_to_XMM,
               "vmovd %%r14d, %%xmm7",
               "vmovd (%%rax), %%xmm9")

GEN_test_RandM(VMOVDDUP_XMMorMEM64_to_XMM,
               "vmovddup %%xmm8,  %%xmm7",
               "vmovddup (%%rax), %%xmm9")

GEN_test_RandM(VCMPSS_128_0x0,
               "vcmpss $0, %%xmm6,  %%xmm8, %%xmm7",
               "vcmpss $0, (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VCMPSS_128_0x1,
               "vcmpss $1, %%xmm6,  %%xmm8, %%xmm7",
               "vcmpss $1, (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VCMPSS_128_0x2,
               "vcmpss $2, %%xmm6,  %%xmm8, %%xmm7",
               "vcmpss $2, (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VCMPSS_128_0x3,
               "vcmpss $3, %%xmm6,  %%xmm8, %%xmm7",
               "vcmpss $3, (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VCMPSS_128_0x4,
               "vcmpss $4, %%xmm6,  %%xmm8, %%xmm7",
               "vcmpss $4, (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VCMPSS_128_0x5,
               "vcmpss $5, %%xmm6,  %%xmm8, %%xmm7",
               "vcmpss $5, (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VCMPSS_128_0x6,
               "vcmpss $6, %%xmm6,  %%xmm8, %%xmm7",
               "vcmpss $6, (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VCMPSS_128_0x7,
               "vcmpss $7, %%xmm6,  %%xmm8, %%xmm7",
               "vcmpss $7, (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VCMPSS_128_0xA,
               "vcmpss $0xA, %%xmm6,  %%xmm8, %%xmm7",
               "vcmpss $0xA, (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VCMPSS_128_0xC,
               "vcmpss $0xC, %%xmm6,  %%xmm8, %%xmm7",
               "vcmpss $0xC, (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VCMPSS_128_0xD,
               "vcmpss $0xD, %%xmm6,  %%xmm8, %%xmm7",
               "vcmpss $0xD, (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VCMPSS_128_0xE,
               "vcmpss $0xE, %%xmm6,  %%xmm8, %%xmm7",
               "vcmpss $0xE, (%%rax), %%xmm8, %%xmm7")

// AFAICS this is a E-to-G form insn, but the assembler on Ubuntu 11.10
// refuses to accept the memory case.  Hence test only the register case.
// "none/tests/amd64/avx-1.c:527: Error: unsupported syntax for `vcvtpd2ps'"
//GEN_test_RandM(VCVTPD2PS_128,
//               "vcvtpd2ps %%xmm8,  %%xmm7",
//               "vcvtpd2ps (%%rax), %%xmm9")
GEN_test_Ronly(VCVTPD2PS_128,
               "vcvtpd2ps %%xmm8,  %%xmm7")

GEN_test_RandM(VEXTRACTF128_0x0,
               "vextractf128 $0x0, %%ymm7, %%xmm9",
               "vextractf128 $0x0, %%ymm7, (%%rax)")

GEN_test_RandM(VEXTRACTF128_0x1,
               "vextractf128 $0x1, %%ymm7, %%xmm9",
               "vextractf128 $0x1, %%ymm7, (%%rax)")

GEN_test_RandM(VINSERTF128_0x0,
               "vinsertf128 $0x0, %%xmm9,  %%ymm7, %%ymm8",
               "vinsertf128 $0x0, (%%rax), %%ymm7, %%ymm8")

GEN_test_RandM(VINSERTF128_0x1,
               "vinsertf128 $0x1, %%xmm9,  %%ymm7, %%ymm8",
               "vinsertf128 $0x1, (%%rax), %%ymm7, %%ymm8")

GEN_test_RandM(VPEXTRD_128_0x0,
               "vpextrd $0x0, %%xmm7, %%r14d",
               "vpextrd $0x0, %%xmm7, (%%rax)")

GEN_test_RandM(VPEXTRD_128_0x3,
               "vpextrd $0x3, %%xmm7, %%r14d",
               "vpextrd $0x3, %%xmm7, (%%rax)")

GEN_test_RandM(VPCMPEQD_128,
               "vpcmpeqd %%xmm6,  %%xmm8, %%xmm7",
               "vpcmpeqd (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VPSHUFD_0x39_128,
               "vpshufd $0x39, %%xmm9,  %%xmm8",
               "vpshufd $0xC6, (%%rax), %%xmm7")

GEN_test_RandM(VMAXSD_128,
               "vmaxsd %%xmm6,  %%xmm8, %%xmm7",
               "vmaxsd (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VDIVSD_128,
               "vdivsd %%xmm6,  %%xmm8, %%xmm7",
               "vdivsd (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VMINSD_128,
               "vminsd %%xmm6,  %%xmm8, %%xmm7",
               "vminsd (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VSUBSD_128,
               "vsubsd %%xmm6,  %%xmm8, %%xmm7",
               "vsubsd (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VADDSD_128,
               "vaddsd %%xmm6,  %%xmm8, %%xmm7",
               "vaddsd (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VMULSD_128,
               "vmulsd %%xmm6,  %%xmm8, %%xmm7",
               "vmulsd (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VXORPS_128,
               "vxorps %%xmm6,  %%xmm8, %%xmm7",
               "vxorps (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VXORPD_128,
               "vxorpd %%xmm6,  %%xmm8, %%xmm7",
               "vxorpd (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VORPD_128,
               "vorpd %%xmm6,  %%xmm8, %%xmm7",
               "vorpd (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VANDNPD_128,
               "vandnpd %%xmm6,  %%xmm8, %%xmm7",
               "vandnpd (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VCVTPS2PD_128,
               "vcvtps2pd %%xmm6,  %%xmm8",
               "vcvtps2pd (%%rax), %%xmm8")

GEN_test_RandM(VUCOMISD_128,
   "vucomisd %%xmm6,  %%xmm8; pushfq; popq %%r14; andq $0x8D5, %%r14",
   "vucomisd (%%rax), %%xmm8; pushfq; popq %%r14; andq $0x8D5, %%r14")

GEN_test_RandM(VUCOMISS_128,
   "vucomiss %%xmm6,  %%xmm8; pushfq; popq %%r14; andq $0x8D5, %%r14",
   "vucomiss (%%rax), %%xmm8; pushfq; popq %%r14; andq $0x8D5, %%r14")

GEN_test_RandM(VPINSRQ_128,
               "vpinsrq $0, %%r14,   %%xmm8, %%xmm7",
               "vpinsrq $1, (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VPADDQ_128,
               "vpaddq %%xmm6,  %%xmm8, %%xmm7",
               "vpaddq (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VPSUBQ_128,
               "vpsubq %%xmm6,  %%xmm8, %%xmm7",
               "vpsubq (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VPSUBW_128,
               "vpsubw %%xmm6,  %%xmm8, %%xmm7",
               "vpsubw (%%rax), %%xmm8, %%xmm7")

GEN_test_RandM(VMOVUPD_GtoE_256,
               "vmovupd %%ymm9,  %%ymm6",
               "vmovupd %%ymm7, (%%rax)")

GEN_test_RandM(VMOVUPD_EtoG_256,
               "vmovupd %%ymm6,  %%ymm9",
               "vmovupd (%%rax), %%ymm7")


/* Comment duplicated above, for convenient reference:
   Allowed operands in test insns:
     Reg form:  %ymm6,  %ymm7, %ymm8, %ymm9 and %r14.
     Mem form:  (%rax), %ymm7, %ymm8, %ymm9 and %r14.
   Imm8 etc fields are also allowed, where they make sense.
*/

int main ( void )
{
   test_VMOVUPD_EtoG_256();
   test_VMOVUPD_GtoE_256();
   test_VPSUBW_128();
   test_VPSUBQ_128();
   test_VPADDQ_128();
   test_VPINSRQ_128();
   test_VUCOMISS_128();
   test_VUCOMISD_128();
   test_VCVTPS2PD_128();
   test_VANDNPD_128();
   test_VORPD_128();
   test_VXORPD_128();
   test_VXORPS_128();
   test_VMULSD_128();
   test_VADDSD_128();
   test_VMINSD_128();
   test_VSUBSD_128();
   test_VDIVSD_128();
   test_VMAXSD_128();
   test_VPSHUFD_0x39_128();
   test_VPCMPEQD_128();
   test_VPEXTRD_128_0x3();
   test_VPEXTRD_128_0x0();
   test_VINSERTF128_0x0();
   test_VINSERTF128_0x1();
   test_VEXTRACTF128_0x0();
   test_VEXTRACTF128_0x1();
   test_VCVTPD2PS_128(); // see comment on the test
   /* Test all CMPSS variants; this code is tricky. */
   test_VCMPSS_128_0x0();
   test_VCMPSS_128_0x1();
   test_VCMPSS_128_0x2();
   test_VCMPSS_128_0x3();
   test_VCMPSS_128_0x4();
   test_VCMPSS_128_0x5();
   test_VCMPSS_128_0x6();
   test_VCMPSS_128_0x7();
   test_VCMPSS_128_0xA();
   /* no 0xB case yet observed */
   test_VCMPSS_128_0xC();
   test_VCMPSS_128_0xD();
   test_VCMPSS_128_0xE();
   test_VMOVDDUP_XMMorMEM64_to_XMM();
   test_VMOVD_IREGorMEM32_to_XMM();
   test_VMOVQ_XMM_MEM64();
   test_VMOVDQA_GtoE_256();
   test_VMOVDQA_GtoE_128();
   test_VMOVDQU_GtoE_128();
   test_VMOVDQA_EtoG_256();
   test_VMOVDQA_EtoG_128();
   test_VMOVDQU_EtoG_128();
   test_VMOVAPD_GtoE_128();
   test_VMOVAPD_GtoE_256();
   test_VMOVAPS_GtoE_128();
   test_VMOVAPS_GtoE_256();
   test_VMOVAPS_EtoG_128();
   test_VMOVAPD_EtoG_256();
   test_VMOVAPD_EtoG_128();
   test_VMOVUPD_GtoE_128();
   test_VMOVSS_XMM_M32();
   test_VMOVSD_XMM_M64();
   test_VMOVSS_M64_XMM();
   test_VMOVSD_M64_XMM();
   test_VINSERTPS_0x39_128();
   test_VPUNPCKLDQ_128();
   test_VPACKSSDW_128();
   test_VPADDW_128();
   test_VPSRLW_0x05_128();
   test_VPUNPCKLQDQ_128();
   test_VPINSRD_128();
   test_VMOVD_XMM_to_MEM32();
   test_VPANDN_128();
   test_VPSLLDQ_0x05_128();
   test_VPSRLDQ_0x05_128();
   test_VPSUBUSB_128();
   test_VPSLLD_0x05_128();
   test_VPSRLD_0x05_128();
   test_VPUNPCKLWD_128();
   test_VPUNPCKHWD_128();
   test_VPADDUSB_128();
   test_VPMULHUW_128();
   test_VPADDUSW_128();
   test_VPMULLW_128();
   test_VPSHUFHW_0x39_128();
   test_VPSHUFLW_0x39_128();
   test_VCVTPS2DQ_128();
   test_VSUBPS_128();
   test_VADDPS_128();
   test_VMULPS_128();
   test_VMAXPS_128();
   test_VMINPS_128();
   test_VSHUFPS_0x39_128();
   test_VPCMPEQB_128();
   test_VMOVHPD_128();
   test_VPAND_128();
   test_VPMOVMSKB_128();
   test_VCVTTSS2SI_64();
   test_VPACKUSWB_128();
   test_VCVTSS2SD_128();
   test_VCVTSD2SS_128();
   test_VMOVD_XMM_to_IREG32();
   test_VPCMPESTRM_0x45_128();
   test_VMOVQ_IREGorMEM64_to_XMM();
   test_VMOVUPS_XMM_to_XMMorMEM();
   test_VMOVNTDQ_128();
   test_VMOVLHPS_128();
   test_VPABSD_128();
   test_VMOVHLPS_128();
   test_VMOVQ_XMM_to_IREG64();
   test_VMOVQ_XMMorMEM64_to_XMM();
   test_VCVTTSS2SI_32();
   test_VPUNPCKLBW_128();
   test_VPUNPCKHBW_128();
   test_VMULSS_128();
   test_VSUBSS_128();
   test_VADDSS_128();
   test_VDIVSS_128();
   test_VUNPCKLPS_128();
   test_VCVTSI2SS_128();
   test_VANDPS_128();
   test_VMINSS_128();
   test_VMAXSS_128();
   test_VANDNPS_128();
   test_VORPS_128();
   test_VSQRTSD_128();
   test_VCMPSD_128_0xD();
   test_VCMPSD_128_0x0();
   test_VPSHUFB_128();
   test_VCVTTSD2SI_32();
   test_VCVTTSD2SI_64();
   test_VCVTSI2SS_64();
   test_VCVTSI2SD_64();
   test_VCVTSI2SD_32();
   test_VPOR_128();
   test_VPXOR_128();
   test_VPSUBB_128();
   test_VPSUBD_128();
   test_VPADDD_128();
   test_VPMOVZXBW_128();
   test_VPMOVZXWD_128();
   test_VPBLENDVB_128();
   test_VPMINSD_128();
   test_VPMAXSD_128();
   test_VANDPD_128();
   return 0;
}
