
rv32i.bin:     file format elf32-littleriscv


Disassembly of section .plt:

000102c0 <_PROCEDURE_LINKAGE_TABLE_>:
   102c0:	97 23 00 00 33 03 c3 41 03 ae 83 d3 13 03 43 fd     .#..3..A......C.
   102d0:	93 82 83 d3 13 53 23 00 83 a2 42 00 67 00 0e 00     .....S#...B.g...

000102e0 <__libc_start_main@plt>:
   102e0:	00002e17          	auipc	t3,0x2
   102e4:	d20e2e03          	lw	t3,-736(t3) # 12000 <__libc_start_main@GLIBC_2.34>
   102e8:	000e0367          	jalr	t1,t3
   102ec:	00000013          	nop

Disassembly of section .text:

000102f0 <_start>:
   102f0:	2005                	jal	10310 <load_gp>
   102f2:	87aa                	mv	a5,a0
   102f4:	00000517          	auipc	a0,0x0
   102f8:	01a50513          	addi	a0,a0,26 # 1030e <__wrap_main>
   102fc:	4582                	lw	a1,0(sp)
   102fe:	0050                	addi	a2,sp,4
   10300:	ff017113          	andi	sp,sp,-16
   10304:	4681                	li	a3,0
   10306:	4701                	li	a4,0
   10308:	880a                	mv	a6,sp
   1030a:	3fd9                	jal	102e0 <__libc_start_main@plt>
   1030c:	9002                	ebreak

0001030e <__wrap_main>:
   1030e:	a0bd                	j	1037c <main>

00010310 <load_gp>:
   10310:	00002197          	auipc	gp,0x2
   10314:	4f418193          	addi	gp,gp,1268 # 12804 <__global_pointer$>
   10318:	8082                	ret
   1031a:	0001                	nop

0001031c <_dl_relocate_static_pie>:
   1031c:	8082                	ret

0001031e <deregister_tm_clones>:
   1031e:	6549                	lui	a0,0x12
   10320:	67c9                	lui	a5,0x12
   10322:	00450513          	addi	a0,a0,4 # 12004 <__TMC_END__>
   10326:	00478793          	addi	a5,a5,4 # 12004 <__TMC_END__>
   1032a:	00a78663          	beq	a5,a0,10336 <deregister_tm_clones+0x18>
   1032e:	00000793          	li	a5,0
   10332:	c391                	beqz	a5,10336 <deregister_tm_clones+0x18>
   10334:	8782                	jr	a5
   10336:	8082                	ret

00010338 <register_tm_clones>:
   10338:	6549                	lui	a0,0x12
   1033a:	67c9                	lui	a5,0x12
   1033c:	00450513          	addi	a0,a0,4 # 12004 <__TMC_END__>
   10340:	00478593          	addi	a1,a5,4 # 12004 <__TMC_END__>
   10344:	8d89                	sub	a1,a1,a0
   10346:	4025d793          	srai	a5,a1,0x2
   1034a:	81fd                	srli	a1,a1,0x1f
   1034c:	95be                	add	a1,a1,a5
   1034e:	8585                	srai	a1,a1,0x1
   10350:	c589                	beqz	a1,1035a <register_tm_clones+0x22>
   10352:	00000793          	li	a5,0
   10356:	c391                	beqz	a5,1035a <register_tm_clones+0x22>
   10358:	8782                	jr	a5
   1035a:	8082                	ret

0001035c <__do_global_dtors_aux>:
   1035c:	67c9                	lui	a5,0x12
   1035e:	0087c703          	lbu	a4,8(a5) # 12008 <completed.0>
   10362:	eb19                	bnez	a4,10378 <__do_global_dtors_aux+0x1c>
   10364:	1141                	addi	sp,sp,-16
   10366:	c606                	sw	ra,12(sp)
   10368:	3f5d                	jal	1031e <deregister_tm_clones>
   1036a:	40b2                	lw	ra,12(sp)
   1036c:	4705                	li	a4,1
   1036e:	67c9                	lui	a5,0x12
   10370:	00e78423          	sb	a4,8(a5) # 12008 <completed.0>
   10374:	0141                	addi	sp,sp,16
   10376:	8082                	ret
   10378:	8082                	ret

0001037a <frame_dummy>:
   1037a:	bf7d                	j	10338 <register_tm_clones>

0001037c <main>:
#define PRINT(a) *(char*)0x11223344 = (a)

void print_dec(unsigned val);

int main()
{
   1037c:	b3010113          	addi	sp,sp,-1232
   10380:	4c112623          	sw	ra,1228(sp)
   10384:	4c812423          	sw	s0,1224(sp)
   10388:	4d010413          	addi	s0,sp,1232
//    print_s("Answer:\n");

    int A[10][10] = {{4,8,0,4,3,7,4,8,3,6},
   1038c:	00000717          	auipc	a4,0x0
   10390:	2ac70713          	addi	a4,a4,684 # 10638 <_IO_stdin_used+0x4>
   10394:	e5440793          	addi	a5,s0,-428
   10398:	86ba                	mv	a3,a4
   1039a:	19000713          	li	a4,400
   1039e:	863a                	mv	a2,a4
   103a0:	85b6                	mv	a1,a3
   103a2:	853e                	mv	a0,a5
   103a4:	2249                	jal	10526 <memcpy>
                     {3,5,2,5,6,8,9,5,8,8},
                     {5,3,4,6,7,8,6,4,3,5},
                     {7,3,2,5,7,8,4,6,6,4},
                     {1,3,5,7,4,7,2,6,4,6}};

    int B[10][10] = {{9,6,4,3,2,4,2,5,6,7},
   103a6:	00000717          	auipc	a4,0x0
   103aa:	42270713          	addi	a4,a4,1058 # 107c8 <_IO_stdin_used+0x194>
   103ae:	cc440793          	addi	a5,s0,-828
   103b2:	86ba                	mv	a3,a4
   103b4:	19000713          	li	a4,400
   103b8:	863a                	mv	a2,a4
   103ba:	85b6                	mv	a1,a3
   103bc:	853e                	mv	a0,a5
   103be:	22a5                	jal	10526 <memcpy>
                     {9,6,5,4,3,2,1,3,5,6},
                     {7,5,4,3,2,5,6,8,8,5},
                     {7,5,4,3,2,6,7,3,2,2}};

    int C[10][10];
    for (int i=0; i < 10; ++i) {
   103c0:	fe042623          	sw	zero,-20(s0)
   103c4:	a209                	j	104c6 <main+0x14a>
        for (int k=0; k < 10; ++k) {
   103c6:	fe042423          	sw	zero,-24(s0)
   103ca:	a8e9                	j	104a4 <main+0x128>
            C[i][k] = 0;
   103cc:	fec42703          	lw	a4,-20(s0)
   103d0:	87ba                	mv	a5,a4
   103d2:	078a                	slli	a5,a5,0x2
   103d4:	97ba                	add	a5,a5,a4
   103d6:	0786                	slli	a5,a5,0x1
   103d8:	fe842703          	lw	a4,-24(s0)
   103dc:	973e                	add	a4,a4,a5
   103de:	b3440793          	addi	a5,s0,-1228
   103e2:	070a                	slli	a4,a4,0x2
   103e4:	97ba                	add	a5,a5,a4
   103e6:	0007a023          	sw	zero,0(a5)
            for (int j=0; j < 10; ++j) {
   103ea:	fe042223          	sw	zero,-28(s0)
   103ee:	a049                	j	10470 <main+0xf4>
                C[i][k] += A[i][j] * B[j][k];
   103f0:	fec42703          	lw	a4,-20(s0)
   103f4:	87ba                	mv	a5,a4
   103f6:	078a                	slli	a5,a5,0x2
   103f8:	97ba                	add	a5,a5,a4
   103fa:	0786                	slli	a5,a5,0x1
   103fc:	fe842703          	lw	a4,-24(s0)
   10400:	973e                	add	a4,a4,a5
   10402:	b3440793          	addi	a5,s0,-1228
   10406:	070a                	slli	a4,a4,0x2
   10408:	97ba                	add	a5,a5,a4
   1040a:	4394                	lw	a3,0(a5)
   1040c:	fec42703          	lw	a4,-20(s0)
   10410:	87ba                	mv	a5,a4
   10412:	078a                	slli	a5,a5,0x2
   10414:	97ba                	add	a5,a5,a4
   10416:	0786                	slli	a5,a5,0x1
   10418:	fe442703          	lw	a4,-28(s0)
   1041c:	973e                	add	a4,a4,a5
   1041e:	e5440793          	addi	a5,s0,-428
   10422:	070a                	slli	a4,a4,0x2
   10424:	97ba                	add	a5,a5,a4
   10426:	4390                	lw	a2,0(a5)
   10428:	fe442703          	lw	a4,-28(s0)
   1042c:	87ba                	mv	a5,a4
   1042e:	078a                	slli	a5,a5,0x2
   10430:	97ba                	add	a5,a5,a4
   10432:	0786                	slli	a5,a5,0x1
   10434:	fe842703          	lw	a4,-24(s0)
   10438:	973e                	add	a4,a4,a5
   1043a:	cc440793          	addi	a5,s0,-828
   1043e:	070a                	slli	a4,a4,0x2
   10440:	97ba                	add	a5,a5,a4
   10442:	439c                	lw	a5,0(a5)
   10444:	02f607b3          	mul	a5,a2,a5
   10448:	96be                	add	a3,a3,a5
   1044a:	fec42703          	lw	a4,-20(s0)
   1044e:	87ba                	mv	a5,a4
   10450:	078a                	slli	a5,a5,0x2
   10452:	97ba                	add	a5,a5,a4
   10454:	0786                	slli	a5,a5,0x1
   10456:	fe842703          	lw	a4,-24(s0)
   1045a:	973e                	add	a4,a4,a5
   1045c:	b3440793          	addi	a5,s0,-1228
   10460:	070a                	slli	a4,a4,0x2
   10462:	97ba                	add	a5,a5,a4
   10464:	c394                	sw	a3,0(a5)
            for (int j=0; j < 10; ++j) {
   10466:	fe442783          	lw	a5,-28(s0)
   1046a:	0785                	addi	a5,a5,1
   1046c:	fef42223          	sw	a5,-28(s0)
   10470:	fe442703          	lw	a4,-28(s0)
   10474:	47a5                	li	a5,9
   10476:	f6e7dde3          	bge	a5,a4,103f0 <main+0x74>
            }
//            print_i(C[i][k]);
//            print_c(' ');
            print_dec(C[i][k]);
   1047a:	fec42703          	lw	a4,-20(s0)
   1047e:	87ba                	mv	a5,a4
   10480:	078a                	slli	a5,a5,0x2
   10482:	97ba                	add	a5,a5,a4
   10484:	0786                	slli	a5,a5,0x1
   10486:	fe842703          	lw	a4,-24(s0)
   1048a:	973e                	add	a4,a4,a5
   1048c:	b3440793          	addi	a5,s0,-1228
   10490:	070a                	slli	a4,a4,0x2
   10492:	97ba                	add	a5,a5,a4
   10494:	439c                	lw	a5,0(a5)
   10496:	853e                	mv	a0,a5
   10498:	20e5                	jal	10580 <print_dec>
        for (int k=0; k < 10; ++k) {
   1049a:	fe842783          	lw	a5,-24(s0)
   1049e:	0785                	addi	a5,a5,1
   104a0:	fef42423          	sw	a5,-24(s0)
   104a4:	fe842703          	lw	a4,-24(s0)
   104a8:	47a5                	li	a5,9
   104aa:	f2e7d1e3          	bge	a5,a4,103cc <main+0x50>
        }
//        print_s("\n");
        PRINT('\n');
   104ae:	112237b7          	lui	a5,0x11223
   104b2:	34478793          	addi	a5,a5,836 # 11223344 <__global_pointer$+0x11210b40>
   104b6:	4729                	li	a4,10
   104b8:	00e78023          	sb	a4,0(a5)
    for (int i=0; i < 10; ++i) {
   104bc:	fec42783          	lw	a5,-20(s0)
   104c0:	0785                	addi	a5,a5,1
   104c2:	fef42623          	sw	a5,-20(s0)
   104c6:	fec42703          	lw	a4,-20(s0)
   104ca:	47a5                	li	a5,9
   104cc:	eee7dde3          	bge	a5,a4,103c6 <main+0x4a>
    }

//    print_s("Raise exception to enable timer...\n");
//    print_s("Back to user mode\n");
    while (1)
   104d0:	a001                	j	104d0 <main+0x154>

000104d2 <memset>:
        ;
    return 0;
}

void *memset(void *dest, int ch, unsigned long count)
{
   104d2:	7179                	addi	sp,sp,-48
   104d4:	d606                	sw	ra,44(sp)
   104d6:	d422                	sw	s0,40(sp)
   104d8:	1800                	addi	s0,sp,48
   104da:	fca42e23          	sw	a0,-36(s0)
   104de:	fcb42c23          	sw	a1,-40(s0)
   104e2:	fcc42a23          	sw	a2,-44(s0)
    for (int i=0; i < count; ++i) {
   104e6:	fe042623          	sw	zero,-20(s0)
   104ea:	a00d                	j	1050c <memset+0x3a>
        ((unsigned char*)dest)[i] = ch;
   104ec:	fec42783          	lw	a5,-20(s0)
   104f0:	fdc42703          	lw	a4,-36(s0)
   104f4:	97ba                	add	a5,a5,a4
   104f6:	fd842703          	lw	a4,-40(s0)
   104fa:	0ff77713          	zext.b	a4,a4
   104fe:	00e78023          	sb	a4,0(a5)
    for (int i=0; i < count; ++i) {
   10502:	fec42783          	lw	a5,-20(s0)
   10506:	0785                	addi	a5,a5,1
   10508:	fef42623          	sw	a5,-20(s0)
   1050c:	fec42783          	lw	a5,-20(s0)
   10510:	fd442703          	lw	a4,-44(s0)
   10514:	fce7ece3          	bltu	a5,a4,104ec <memset+0x1a>
    }
    return dest;
   10518:	fdc42783          	lw	a5,-36(s0)
}
   1051c:	853e                	mv	a0,a5
   1051e:	50b2                	lw	ra,44(sp)
   10520:	5422                	lw	s0,40(sp)
   10522:	6145                	addi	sp,sp,48
   10524:	8082                	ret

00010526 <memcpy>:

void *memcpy(void *dest, void *src, unsigned long count)
{
   10526:	7179                	addi	sp,sp,-48
   10528:	d606                	sw	ra,44(sp)
   1052a:	d422                	sw	s0,40(sp)
   1052c:	1800                	addi	s0,sp,48
   1052e:	fca42e23          	sw	a0,-36(s0)
   10532:	fcb42c23          	sw	a1,-40(s0)
   10536:	fcc42a23          	sw	a2,-44(s0)
    for (int i=0; i < count; ++i) {
   1053a:	fe042623          	sw	zero,-20(s0)
   1053e:	a025                	j	10566 <memcpy+0x40>
        ((unsigned char*)dest)[i] = ((unsigned char*)src)[i];
   10540:	fec42783          	lw	a5,-20(s0)
   10544:	fd842703          	lw	a4,-40(s0)
   10548:	973e                	add	a4,a4,a5
   1054a:	fec42783          	lw	a5,-20(s0)
   1054e:	fdc42683          	lw	a3,-36(s0)
   10552:	97b6                	add	a5,a5,a3
   10554:	00074703          	lbu	a4,0(a4)
   10558:	00e78023          	sb	a4,0(a5)
    for (int i=0; i < count; ++i) {
   1055c:	fec42783          	lw	a5,-20(s0)
   10560:	0785                	addi	a5,a5,1
   10562:	fef42623          	sw	a5,-20(s0)
   10566:	fec42783          	lw	a5,-20(s0)
   1056a:	fd442703          	lw	a4,-44(s0)
   1056e:	fce7e9e3          	bltu	a5,a4,10540 <memcpy+0x1a>
    }
    return dest;
   10572:	fdc42783          	lw	a5,-36(s0)
}
   10576:	853e                	mv	a0,a5
   10578:	50b2                	lw	ra,44(sp)
   1057a:	5422                	lw	s0,40(sp)
   1057c:	6145                	addi	sp,sp,48
   1057e:	8082                	ret

00010580 <print_dec>:

void print_dec(unsigned val)
{
   10580:	7179                	addi	sp,sp,-48
   10582:	d606                	sw	ra,44(sp)
   10584:	d422                	sw	s0,40(sp)
   10586:	1800                	addi	s0,sp,48
   10588:	fca42e23          	sw	a0,-36(s0)
//    printf("(%d)", val);
    unsigned pow = 1;
   1058c:	4785                	li	a5,1
   1058e:	fef42623          	sw	a5,-20(s0)
    while (pow < val) {
   10592:	a809                	j	105a4 <print_dec+0x24>
        pow *= 10;
   10594:	fec42703          	lw	a4,-20(s0)
   10598:	87ba                	mv	a5,a4
   1059a:	078a                	slli	a5,a5,0x2
   1059c:	97ba                	add	a5,a5,a4
   1059e:	0786                	slli	a5,a5,0x1
   105a0:	fef42623          	sw	a5,-20(s0)
    while (pow < val) {
   105a4:	fec42703          	lw	a4,-20(s0)
   105a8:	fdc42783          	lw	a5,-36(s0)
   105ac:	fef764e3          	bltu	a4,a5,10594 <print_dec+0x14>
    }
    pow /= 10;
   105b0:	fec42703          	lw	a4,-20(s0)
   105b4:	ccccd7b7          	lui	a5,0xccccd
   105b8:	ccd78793          	addi	a5,a5,-819 # cccccccd <__global_pointer$+0xcccba4c9>
   105bc:	02f737b3          	mulhu	a5,a4,a5
   105c0:	838d                	srli	a5,a5,0x3
   105c2:	fef42623          	sw	a5,-20(s0)
    while (pow >= 1) {
   105c6:	a0b1                	j	10612 <print_dec+0x92>
        PRINT(val/pow + 0x30);
   105c8:	fdc42703          	lw	a4,-36(s0)
   105cc:	fec42783          	lw	a5,-20(s0)
   105d0:	02f757b3          	divu	a5,a4,a5
   105d4:	0ff7f713          	zext.b	a4,a5
   105d8:	112237b7          	lui	a5,0x11223
   105dc:	34478793          	addi	a5,a5,836 # 11223344 <__global_pointer$+0x11210b40>
   105e0:	03070713          	addi	a4,a4,48
   105e4:	0ff77713          	zext.b	a4,a4
   105e8:	00e78023          	sb	a4,0(a5)
        val -= val/pow*pow;
   105ec:	fdc42703          	lw	a4,-36(s0)
   105f0:	fec42783          	lw	a5,-20(s0)
   105f4:	02f777b3          	remu	a5,a4,a5
   105f8:	fcf42e23          	sw	a5,-36(s0)
        pow /= 10;
   105fc:	fec42703          	lw	a4,-20(s0)
   10600:	ccccd7b7          	lui	a5,0xccccd
   10604:	ccd78793          	addi	a5,a5,-819 # cccccccd <__global_pointer$+0xcccba4c9>
   10608:	02f737b3          	mulhu	a5,a4,a5
   1060c:	838d                	srli	a5,a5,0x3
   1060e:	fef42623          	sw	a5,-20(s0)
    while (pow >= 1) {
   10612:	fec42783          	lw	a5,-20(s0)
   10616:	fbcd                	bnez	a5,105c8 <print_dec+0x48>
    }
    PRINT(' ');
   10618:	112237b7          	lui	a5,0x11223
   1061c:	34478793          	addi	a5,a5,836 # 11223344 <__global_pointer$+0x11210b40>
   10620:	02000713          	li	a4,32
   10624:	00e78023          	sb	a4,0(a5)
}
   10628:	0001                	nop
   1062a:	50b2                	lw	ra,44(sp)
   1062c:	5422                	lw	s0,40(sp)
   1062e:	6145                	addi	sp,sp,48
   10630:	8082                	ret
