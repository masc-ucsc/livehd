
/mada/users/renau/projs/lhdtrack/var/work/20261006T185507/br_credit_sender/mc16_nf4_pcmc4_rpo1_w64/notech/sim_lhd_pyrope_llvm/SW/sim/br_credit_sender_harness.br_credit_sender_harness.color-kernel-body-color-0.llvm.o:     file format elf64-x86-64


Disassembly of section .text:

0000000000000000 <__lhd_color_kernel_br_credit_sender_harness_br_credit_sender_harness_body_color_0_llvm>:
   0:	53                   	push   %rbx
   1:	48 8b 0f             	mov    (%rdi),%rcx
   4:	4c 8b 4f 08          	mov    0x8(%rdi),%r9
   8:	41 89 c8             	mov    %ecx,%r8d
   b:	41 80 e0 0f          	and    $0xf,%r8b
   f:	44 89 c8             	mov    %r9d,%eax
  12:	25 ff 1f 00 00       	and    $0x1fff,%eax
  17:	4c 8b 57 10          	mov    0x10(%rdi),%r10
  1b:	49 c1 ea 0d          	shr    $0xd,%r10
  1f:	4c 33 57 18          	xor    0x18(%rdi),%r10
  23:	4d 89 d3             	mov    %r10,%r11
  26:	49 c1 e3 06          	shl    $0x6,%r11
  2a:	41 c1 e9 07          	shr    $0x7,%r9d
  2e:	41 83 e1 3f          	and    $0x3f,%r9d
  32:	49 c1 e2 0d          	shl    $0xd,%r10
  36:	49 09 c2             	or     %rax,%r10
  39:	48 bb c0 ff ff ff ff 	movabs $0x1ffffffffffffc0,%rbx
  40:	ff ff 01 
  43:	4c 21 db             	and    %r11,%rbx
  46:	4c 09 cb             	or     %r9,%rbx
  49:	4c 31 d3             	xor    %r10,%rbx
  4c:	4c 8b 4f 20          	mov    0x20(%rdi),%r9
  50:	49 89 da             	mov    %rbx,%r10
  53:	49 c1 e2 11          	shl    $0x11,%r10
  57:	49 89 db             	mov    %rbx,%r11
  5a:	49 81 e3 00 00 fe ff 	and    $0xfffffffffffe0000,%r11
  61:	4d 31 d3             	xor    %r10,%r11
  64:	81 e3 ff ff 01 00    	and    $0x1ffff,%ebx
  6a:	4c 09 db             	or     %r11,%rbx
  6d:	44 0b 4f 30          	or     0x30(%rdi),%r9d
  71:	41 f6 c1 01          	test   $0x1,%r9b
  75:	48 bf d3 08 a3 85 88 	movabs $0x243f6a8885a308d3,%rdi
  7c:	6a 3f 24 
  7f:	48 0f 44 fb          	cmove  %rbx,%rdi
  83:	48 c7 02 00 00 00 00 	movq   $0x0,(%rdx)
  8a:	44 0f b6 0e          	movzbl (%rsi),%r9d
  8e:	41 80 e1 0f          	and    $0xf,%r9b
  92:	45 31 d2             	xor    %r10d,%r10d
  95:	45 38 c1             	cmp    %r8b,%r9b
  98:	41 0f 95 c2          	setne  %r10b
  9c:	4c 89 12             	mov    %r10,(%rdx)
  9f:	83 e1 0f             	and    $0xf,%ecx
  a2:	48 89 0e             	mov    %rcx,(%rsi)
  a5:	0f b7 4e 08          	movzwl 0x8(%rsi),%ecx
  a9:	81 e1 ff 1f 00 00    	and    $0x1fff,%ecx
  af:	45 31 c0             	xor    %r8d,%r8d
  b2:	66 39 c1             	cmp    %ax,%cx
  b5:	41 0f 95 c0          	setne  %r8b
  b9:	45 01 c0             	add    %r8d,%r8d
  bc:	4c 0b 02             	or     (%rdx),%r8
  bf:	4c 89 02             	mov    %r8,(%rdx)
  c2:	48 89 46 08          	mov    %rax,0x8(%rsi)
  c6:	31 c0                	xor    %eax,%eax
  c8:	48 39 7e 10          	cmp    %rdi,0x10(%rsi)
  cc:	0f 95 c0             	setne  %al
  cf:	c1 e0 02             	shl    $0x2,%eax
  d2:	4c 09 c0             	or     %r8,%rax
  d5:	48 89 02             	mov    %rax,(%rdx)
  d8:	48 89 7e 10          	mov    %rdi,0x10(%rsi)
  dc:	5b                   	pop    %rbx
  dd:	c3                   	ret
