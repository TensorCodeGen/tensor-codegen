	.text
	.file	"matmul.c"
	.globl	foo                             # -- Begin function foo
	.p2align	4, 0x90
	.type	foo,@function
foo:                                    # @foo
	.cfi_startproc
# %bb.0:
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset %rbp, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register %rbp
	pushq	%r15
	pushq	%r14
	pushq	%rbx
	subq	$40, %rsp
	.cfi_offset %rbx, -40
	.cfi_offset %r14, -32
	.cfi_offset %r15, -24
	movl	$4, %edi
	callq	malloc@PLT
	movq	%rax, %r14
	movl	$4, %edi
	callq	malloc@PLT
	movq	%rax, %r15
	movl	$8, %edi
	callq	malloc@PLT
	addq	$8, %r15
	addq	$24, %r14
	xorl	%r8d, %r8d
	xorl	%r9d, %r9d
	.p2align	4, 0x90
.LBB0_1:                                # %rows.header
                                        # =>This Loop Header: Depth=1
                                        #     Child Loop BB0_2 Depth 2
                                        #       Child Loop BB0_3 Depth 3
	movslq	%r8d, %rcx
	leaq	(%r14,%rcx,4), %rsi
	xorl	%edi, %edi
	.p2align	4, 0x90
.LBB0_2:                                # %cols.header
                                        #   Parent Loop BB0_1 Depth=1
                                        # =>  This Loop Header: Depth=2
                                        #       Child Loop BB0_3 Depth 3
	movslq	%edi, %rcx
	leaq	(%r15,%rcx,4), %rbx
	xorl	%ecx, %ecx
	.p2align	4, 0x90
.LBB0_3:                                # %inner.header
                                        #   Parent Loop BB0_1 Depth=1
                                        #     Parent Loop BB0_2 Depth=2
                                        # =>    This Inner Loop Header: Depth=3
	movq	%rcx, %rdx
	addq	$2, %rcx
	cmpl	$2, %ecx
	jne	.LBB0_3
# %bb.4:                                # %cols.latch
                                        #   in Loop: Header=BB0_2 Depth=2
	movq	-24(%rsi,%rdx,4), %xmm0         # xmm0 = mem[0],zero
	movq	-16(%rsi,%rdx,4), %xmm13        # xmm13 = mem[0],zero
	movsd	-8(%rsi,%rdx,4), %xmm1          # xmm1 = mem[0],zero
	movaps	%xmm1, -64(%rbp)                # 16-byte Spill
	pshufd	$0, %xmm0, %xmm5                # xmm5 = xmm0[0,0,0,0]
	movdqu	-8(%rbx,%rdx,8), %xmm4
	movdqu	(%rbx,%rdx,8), %xmm3
	movdqu	8(%rbx,%rdx,8), %xmm11
	movdqu	16(%rbx,%rdx,8), %xmm10
	pshufd	$245, %xmm4, %xmm15             # xmm15 = xmm4[1,1,3,3]
	movdqa	%xmm5, %xmm1
	pmuludq	%xmm15, %xmm1
	pshufd	$232, %xmm1, %xmm1              # xmm1 = xmm1[0,2,2,3]
	movdqa	%xmm5, %xmm6
	pmuludq	%xmm4, %xmm6
	pshufd	$232, %xmm6, %xmm6              # xmm6 = xmm6[0,2,2,3]
	punpckldq	%xmm1, %xmm6            # xmm6 = xmm6[0],xmm1[0],xmm6[1],xmm1[1]
	pshufd	$85, %xmm0, %xmm7               # xmm7 = xmm0[1,1,1,1]
	pshufd	$245, %xmm3, %xmm8              # xmm8 = xmm3[1,1,3,3]
	movdqa	%xmm7, %xmm0
	pmuludq	%xmm8, %xmm0
	pshufd	$232, %xmm0, %xmm0              # xmm0 = xmm0[0,2,2,3]
	movdqa	%xmm7, %xmm2
	pmuludq	%xmm3, %xmm2
	pshufd	$232, %xmm2, %xmm1              # xmm1 = xmm2[0,2,2,3]
	punpckldq	%xmm0, %xmm1            # xmm1 = xmm1[0],xmm0[0],xmm1[1],xmm0[1]
	paddd	%xmm6, %xmm1
	movdqa	%xmm1, -48(%rbp)                # 16-byte Spill
	pshufd	$245, %xmm11, %xmm14            # xmm14 = xmm11[1,1,3,3]
	movdqa	%xmm5, %xmm0
	pmuludq	%xmm14, %xmm0
	pshufd	$232, %xmm0, %xmm0              # xmm0 = xmm0[0,2,2,3]
	pmuludq	%xmm11, %xmm5
	pshufd	$232, %xmm5, %xmm2              # xmm2 = xmm5[0,2,2,3]
	punpckldq	%xmm0, %xmm2            # xmm2 = xmm2[0],xmm0[0],xmm2[1],xmm0[1]
	pshufd	$245, %xmm10, %xmm9             # xmm9 = xmm10[1,1,3,3]
	movdqa	%xmm7, %xmm5
	pmuludq	%xmm9, %xmm5
	pshufd	$232, %xmm5, %xmm5              # xmm5 = xmm5[0,2,2,3]
	pmuludq	%xmm10, %xmm7
	pshufd	$232, %xmm7, %xmm12             # xmm12 = xmm7[0,2,2,3]
	punpckldq	%xmm5, %xmm12           # xmm12 = xmm12[0],xmm5[0],xmm12[1],xmm5[1]
	paddd	%xmm2, %xmm12
	pshufd	$0, %xmm13, %xmm2               # xmm2 = xmm13[0,0,0,0]
	movdqa	%xmm2, %xmm5
	pmuludq	%xmm15, %xmm5
	pshufd	$232, %xmm5, %xmm5              # xmm5 = xmm5[0,2,2,3]
	movdqa	%xmm2, %xmm6
	pmuludq	%xmm4, %xmm6
	pshufd	$232, %xmm6, %xmm6              # xmm6 = xmm6[0,2,2,3]
	punpckldq	%xmm5, %xmm6            # xmm6 = xmm6[0],xmm5[0],xmm6[1],xmm5[1]
	pshufd	$85, %xmm13, %xmm0              # xmm0 = xmm13[1,1,1,1]
	movdqa	%xmm0, %xmm7
	pmuludq	%xmm8, %xmm7
	pshufd	$232, %xmm7, %xmm7              # xmm7 = xmm7[0,2,2,3]
	movdqa	%xmm0, %xmm5
	pmuludq	%xmm3, %xmm5
	pshufd	$232, %xmm5, %xmm13             # xmm13 = xmm5[0,2,2,3]
	punpckldq	%xmm7, %xmm13           # xmm13 = xmm13[0],xmm7[0],xmm13[1],xmm7[1]
	paddd	%xmm6, %xmm13
	movdqa	%xmm2, %xmm5
	pmuludq	%xmm14, %xmm5
	pshufd	$232, %xmm5, %xmm5              # xmm5 = xmm5[0,2,2,3]
	pmuludq	%xmm11, %xmm2
	pshufd	$232, %xmm2, %xmm2              # xmm2 = xmm2[0,2,2,3]
	punpckldq	%xmm5, %xmm2            # xmm2 = xmm2[0],xmm5[0],xmm2[1],xmm5[1]
	movdqa	%xmm0, %xmm5
	pmuludq	%xmm9, %xmm5
	pshufd	$232, %xmm5, %xmm6              # xmm6 = xmm5[0,2,2,3]
	pmuludq	%xmm10, %xmm0
	pshufd	$232, %xmm0, %xmm5              # xmm5 = xmm0[0,2,2,3]
	punpckldq	%xmm6, %xmm5            # xmm5 = xmm5[0],xmm6[0],xmm5[1],xmm6[1]
	paddd	%xmm2, %xmm5
	movdqa	-64(%rbp), %xmm1                # 16-byte Reload
	pshufd	$0, %xmm1, %xmm0                # xmm0 = xmm1[0,0,0,0]
	movdqa	%xmm0, %xmm2
	pmuludq	%xmm15, %xmm2
	pshufd	$232, %xmm2, %xmm2              # xmm2 = xmm2[0,2,2,3]
	movdqa	%xmm0, %xmm6
	pmuludq	%xmm4, %xmm6
	pshufd	$232, %xmm6, %xmm6              # xmm6 = xmm6[0,2,2,3]
	punpckldq	%xmm2, %xmm6            # xmm6 = xmm6[0],xmm2[0],xmm6[1],xmm2[1]
	pshufd	$85, %xmm1, %xmm7               # xmm7 = xmm1[1,1,1,1]
	movdqa	%xmm7, %xmm2
	pmuludq	%xmm8, %xmm2
	pshufd	$232, %xmm2, %xmm1              # xmm1 = xmm2[0,2,2,3]
	movdqa	%xmm7, %xmm2
	pmuludq	%xmm3, %xmm2
	pshufd	$232, %xmm2, %xmm2              # xmm2 = xmm2[0,2,2,3]
	punpckldq	%xmm1, %xmm2            # xmm2 = xmm2[0],xmm1[0],xmm2[1],xmm1[1]
	paddd	%xmm6, %xmm2
	movdqa	%xmm0, %xmm1
	pmuludq	%xmm14, %xmm1
	pshufd	$232, %xmm1, %xmm1              # xmm1 = xmm1[0,2,2,3]
	pmuludq	%xmm11, %xmm0
	pshufd	$232, %xmm0, %xmm0              # xmm0 = xmm0[0,2,2,3]
	punpckldq	%xmm1, %xmm0            # xmm0 = xmm0[0],xmm1[0],xmm0[1],xmm1[1]
	movdqa	%xmm7, %xmm1
	pmuludq	%xmm9, %xmm1
	pshufd	$232, %xmm1, %xmm1              # xmm1 = xmm1[0,2,2,3]
	pmuludq	%xmm10, %xmm7
	pshufd	$232, %xmm7, %xmm6              # xmm6 = xmm7[0,2,2,3]
	punpckldq	%xmm1, %xmm6            # xmm6 = xmm6[0],xmm1[0],xmm6[1],xmm1[1]
	movq	(%rsi,%rdx,4), %xmm1            # xmm1 = mem[0],zero
	paddd	%xmm0, %xmm6
	pshufd	$0, %xmm1, %xmm0                # xmm0 = xmm1[0,0,0,0]
	pmuludq	%xmm0, %xmm15
	pshufd	$232, %xmm15, %xmm7             # xmm7 = xmm15[0,2,2,3]
	pmuludq	%xmm0, %xmm4
	pshufd	$232, %xmm4, %xmm4              # xmm4 = xmm4[0,2,2,3]
	punpckldq	%xmm7, %xmm4            # xmm4 = xmm4[0],xmm7[0],xmm4[1],xmm7[1]
	pshufd	$85, %xmm1, %xmm7               # xmm7 = xmm1[1,1,1,1]
	pmuludq	%xmm7, %xmm8
	pshufd	$232, %xmm8, %xmm8              # xmm8 = xmm8[0,2,2,3]
	pmuludq	%xmm7, %xmm3
	pshufd	$232, %xmm3, %xmm1              # xmm1 = xmm3[0,2,2,3]
	punpckldq	%xmm8, %xmm1            # xmm1 = xmm1[0],xmm8[0],xmm1[1],xmm8[1]
	paddd	%xmm4, %xmm1
	pmuludq	%xmm0, %xmm14
	pmuludq	%xmm11, %xmm0
	pshufd	$232, %xmm14, %xmm3             # xmm3 = xmm14[0,2,2,3]
	pshufd	$232, %xmm0, %xmm0              # xmm0 = xmm0[0,2,2,3]
	punpckldq	%xmm3, %xmm0            # xmm0 = xmm0[0],xmm3[0],xmm0[1],xmm3[1]
	pmuludq	%xmm7, %xmm9
	pmuludq	%xmm10, %xmm7
	pshufd	$232, %xmm9, %xmm3              # xmm3 = xmm9[0,2,2,3]
	pshufd	$232, %xmm7, %xmm4              # xmm4 = xmm7[0,2,2,3]
	punpckldq	%xmm3, %xmm4            # xmm4 = xmm4[0],xmm3[0],xmm4[1],xmm3[1]
	paddd	%xmm0, %xmm4
	leal	(%rdi,%r9,2), %ecx
	movslq	%ecx, %rcx
	movdqu	%xmm12, 16(%rax,%rcx,4)
	movaps	-48(%rbp), %xmm0                # 16-byte Reload
	movups	%xmm0, (%rax,%rcx,4)
	movdqu	%xmm5, 24(%rax,%rcx,4)
	movdqu	%xmm13, 8(%rax,%rcx,4)
	movdqu	%xmm6, 32(%rax,%rcx,4)
	movdqu	%xmm2, 16(%rax,%rcx,4)
	movdqu	%xmm4, 40(%rax,%rcx,4)
	movdqu	%xmm1, 24(%rax,%rcx,4)
	movl	%edi, %ecx
	addl	$8, %ecx
	movl	%ecx, %edi
	cmpl	$2, %ecx
	jne	.LBB0_2
# %bb.5:                                # %rows.latch
                                        #   in Loop: Header=BB0_1 Depth=1
	addl	$4, %r9d
	addl	$8, %r8d
	cmpl	$2, %r9d
	jne	.LBB0_1
# %bb.6:                                # %continue
	movups	(%rax), %xmm0
	addq	$40, %rsp
	popq	%rbx
	popq	%r14
	popq	%r15
	popq	%rbp
	.cfi_def_cfa %rsp, 8
	retq
.Lfunc_end0:
	.size	foo, .Lfunc_end0-foo
	.cfi_endproc
                                        # -- End function
	.section	.rodata.cst16,"aM",@progbits,16
	.p2align	4                               # -- Begin function main
.LCPI1_0:
	.long	0                               # 0x0
	.long	1                               # 0x1
	.long	1                               # 0x1
	.long	0                               # 0x0
.LCPI1_1:
	.long	1                               # 0x1
	.long	2                               # 0x2
	.long	3                               # 0x3
	.long	4                               # 0x4
	.text
	.globl	main
	.p2align	4, 0x90
	.type	main,@function
main:                                   # @main
	.cfi_startproc
# %bb.0:
	pushq	%rbp
	.cfi_def_cfa_offset 16
	.cfi_offset %rbp, -16
	movq	%rsp, %rbp
	.cfi_def_cfa_register %rbp
	pushq	%rbx
	subq	$40, %rsp
	.cfi_offset %rbx, -24
	movaps	.LCPI1_0(%rip), %xmm0           # xmm0 = [0,1,1,0]
	movaps	.LCPI1_1(%rip), %xmm1           # xmm1 = [1,2,3,4]
	callq	foo
	movaps	%xmm0, -32(%rbp)                # 16-byte Spill
	xorl	%ebx, %ebx
	cmpl	$3, %ebx
	ja	.LBB1_3
	.p2align	4, 0x90
.LBB1_2:                                # =>This Inner Loop Header: Depth=1
	movaps	-32(%rbp), %xmm0                # 16-byte Reload
	movaps	%xmm0, -48(%rbp)
	movl	%ebx, %eax
	andl	$3, %eax
	movl	-48(%rbp,%rax,4), %esi
	movl	$.L.str, %edi
	xorl	%eax, %eax
	callq	printf
	addl	$1, %ebx
	cmpl	$3, %ebx
	jbe	.LBB1_2
.LBB1_3:
	xorl	%eax, %eax
	addq	$40, %rsp
	popq	%rbx
	popq	%rbp
	.cfi_def_cfa %rsp, 8
	retq
.Lfunc_end1:
	.size	main, .Lfunc_end1-main
	.cfi_endproc
                                        # -- End function
	.type	.L.str,@object                  # @.str
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str:
	.asciz	"output: %d\n"
	.size	.L.str, 12

	.ident	"clang version 12.0.0 (https://github.com/AkashIwnK/tensor-codegen.git 00441b8f4e5b7daa39ac6cbeb45ebfe54662b08d)"
	.section	".note.GNU-stack","",@progbits
