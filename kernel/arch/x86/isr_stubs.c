#include "isr.h"
#include <stdint.h>

/* HASERR が 0 なら "pushq $0\n\t" を挿入、1 なら空文字 */
#define _PUSH0_IF0(flag)         _PUSH0_IF0_EXPAND(flag)
#define _PUSH0_IF0_EXPAND(flag)  _PUSH0_IF0_##flag
#define _PUSH0_IF0_0             "pushq $0\n\t"
#define _PUSH0_IF0_1             /* empty */

/* VEC: ベクタ番号、HASERR: そのベクタがエラーコードを自動 push するなら 1, しないなら 0 */
#define DEFINE_ISR_STUB(VEC, HASERR)                         \
    void isr_stub_##VEC(void) __attribute__((naked));        \
    void isr_stub_##VEC(void) {                              \
        __asm__ __volatile__ (                               \
            "cli\n\t"                                        \
            _PUSH0_IF0(HASERR)                               \
            "pushq $" #VEC "\n\t"                            \
            "jmp isr_common\n\t"                             \
        );                                                   \
    }

/* 共通 ISR 本体（レジスタ保存→C 呼び出し→復元→IRETQ） */
void isr_common(void) __attribute__((naked));
void isr_common(void)
{
    __asm__ __volatile__ (
        "pushq %rax \n\t"
        "pushq %rcx \n\t"
        "pushq %rdx \n\t"
        "pushq %rbx \n\t"
        "pushq %rsp \n\t"
        "pushq %rbp \n\t"
        "pushq %rsi \n\t"
        "pushq %rdi \n\t"
        "pushq %r15 \n\t"
        "pushq %r14 \n\t"
        "pushq %r13 \n\t"
        "pushq %r12 \n\t"
        "pushq %r11 \n\t"
        "pushq %r10 \n\t"
        "pushq %r9  \n\t"
        "pushq %r8  \n\t"

        /* ctx (= RSP) を第1引数 RDI に渡し、16B アラインして C を呼ぶ */
        "mov  %rsp, %rdi       \n\t"
        "push %rsp             \n\t"
        "push (%rsp)           \n\t"
        "and  $-16, %rsp       \n\t"

        "call intr_dispatch_entry \n\t"

        /* アライン調整解除 */
        "mov  8(%rsp), %rsp    \n\t"

        /* レジスタを戻す */
        "popq %r8  \n\t"
        "popq %r9  \n\t"
        "popq %r10 \n\t"
        "popq %r11 \n\t"
        "popq %r12 \n\t"
        "popq %r13 \n\t"
        "popq %r14 \n\t"
        "popq %r15 \n\t"
        "popq %rdi \n\t"
        "popq %rsi \n\t"
        "popq %rbp \n\t"
        "popq %rsp \n\t"
        "popq %rbx \n\t"
        "popq %rdx \n\t"
        "popq %rcx \n\t"
        "popq %rax \n\t"

        /* vector + error_code を捨てる（16 bytes） */
        "add  $16, %rsp        \n\t"

        "iretq                 \n\t"
    );
}

/* 便利マクロ */
#define DEF0(n) DEFINE_ISR_STUB(n, 0) /* errcode なし → push 0 する */
#define DEF1(n) DEFINE_ISR_STUB(n, 1) /* errcode あり → push 0 しない */

/* 0..31（例外） */
DEF0(0)   DEF0(1)   DEF0(2)   DEF0(3)   DEF0(4)   DEF0(5)   DEF0(6)   DEF0(7)
DEF1(8)   DEF0(9)   DEF1(10)  DEF1(11)  DEF1(12)  DEF1(13)  DEF1(14)  DEF0(15)
DEF0(16)  DEF1(17)  DEF0(18)  DEF0(19)  DEF1(20)  DEF1(21)  DEF0(22)  DEF0(23)
DEF0(24)  DEF0(25)  DEF0(26)  DEF0(27)  DEF0(28)  DEF0(29)  DEF0(30)  DEF0(31)

/* 32..255（外部/ソフト割り込みは errcode なし → すべて DEF0） */
DEF0(32)  DEF0(33)  DEF0(34)  DEF0(35)  DEF0(36)  DEF0(37)  DEF0(38)  DEF0(39)
DEF0(40)  DEF0(41)  DEF0(42)  DEF0(43)  DEF0(44)  DEF0(45)  DEF0(46)  DEF0(47)
DEF0(48)  DEF0(49)  DEF0(50)  DEF0(51)  DEF0(52)  DEF0(53)  DEF0(54)  DEF0(55)
DEF0(56)  DEF0(57)  DEF0(58)  DEF0(59)  DEF0(60)  DEF0(61)  DEF0(62)  DEF0(63)
DEF0(64)  DEF0(65)  DEF0(66)  DEF0(67)  DEF0(68)  DEF0(69)  DEF0(70)  DEF0(71)
DEF0(72)  DEF0(73)  DEF0(74)  DEF0(75)  DEF0(76)  DEF0(77)  DEF0(78)  DEF0(79)
DEF0(80)  DEF0(81)  DEF0(82)  DEF0(83)  DEF0(84)  DEF0(85)  DEF0(86)  DEF0(87)
DEF0(88)  DEF0(89)  DEF0(90)  DEF0(91)  DEF0(92)  DEF0(93)  DEF0(94)  DEF0(95)
DEF0(96)  DEF0(97)  DEF0(98)  DEF0(99)  DEF0(100) DEF0(101) DEF0(102) DEF0(103)
DEF0(104) DEF0(105) DEF0(106) DEF0(107) DEF0(108) DEF0(109) DEF0(110) DEF0(111)
DEF0(112) DEF0(113) DEF0(114) DEF0(115) DEF0(116) DEF0(117) DEF0(118) DEF0(119)
DEF0(120) DEF0(121) DEF0(122) DEF0(123) DEF0(124) DEF0(125) DEF0(126) DEF0(127)
DEF0(128) DEF0(129) DEF0(130) DEF0(131) DEF0(132) DEF0(133) DEF0(134) DEF0(135)
DEF0(136) DEF0(137) DEF0(138) DEF0(139) DEF0(140) DEF0(141) DEF0(142) DEF0(143)
DEF0(144) DEF0(145) DEF0(146) DEF0(147) DEF0(148) DEF0(149) DEF0(150) DEF0(151)
DEF0(152) DEF0(153) DEF0(154) DEF0(155) DEF0(156) DEF0(157) DEF0(158) DEF0(159)
DEF0(160) DEF0(161) DEF0(162) DEF0(163) DEF0(164) DEF0(165) DEF0(166) DEF0(167)
DEF0(168) DEF0(169) DEF0(170) DEF0(171) DEF0(172) DEF0(173) DEF0(174) DEF0(175)
DEF0(176) DEF0(177) DEF0(178) DEF0(179) DEF0(180) DEF0(181) DEF0(182) DEF0(183)
DEF0(184) DEF0(185) DEF0(186) DEF0(187) DEF0(188) DEF0(189) DEF0(190) DEF0(191)
DEF0(192) DEF0(193) DEF0(194) DEF0(195) DEF0(196) DEF0(197) DEF0(198) DEF0(199)
DEF0(200) DEF0(201) DEF0(202) DEF0(203) DEF0(204) DEF0(205) DEF0(206) DEF0(207)
DEF0(208) DEF0(209) DEF0(210) DEF0(211) DEF0(212) DEF0(213) DEF0(214) DEF0(215)
DEF0(216) DEF0(217) DEF0(218) DEF0(219) DEF0(220) DEF0(221) DEF0(222) DEF0(223)
DEF0(224) DEF0(225) DEF0(226) DEF0(227) DEF0(228) DEF0(229) DEF0(230) DEF0(231)
DEF0(232) DEF0(233) DEF0(234) DEF0(235) DEF0(236) DEF0(237) DEF0(238) DEF0(239)
DEF0(240) DEF0(241) DEF0(242) DEF0(243) DEF0(244) DEF0(245) DEF0(246) DEF0(247)
DEF0(248) DEF0(249) DEF0(250) DEF0(251) DEF0(252) DEF0(253) DEF0(254) DEF0(255)

/* IDT 登録用テーブル（関数ポインタ配列） */
#define PTR(n) [n] = isr_stub_##n
void (*__isr_stub_table[])(void) = {
    PTR(0),PTR(1),PTR(2),PTR(3),PTR(4),PTR(5),PTR(6),PTR(7),
    PTR(8),PTR(9),PTR(10),PTR(11),PTR(12),PTR(13),PTR(14),PTR(15),
    PTR(16),PTR(17),PTR(18),PTR(19),PTR(20),PTR(21),PTR(22),PTR(23),
    PTR(24),PTR(25),PTR(26),PTR(27),PTR(28),PTR(29),PTR(30),PTR(31),
    PTR(32),PTR(33),PTR(34),PTR(35),PTR(36),PTR(37),PTR(38),PTR(39),
    PTR(40),PTR(41),PTR(42),PTR(43),PTR(44),PTR(45),PTR(46),PTR(47),
    PTR(48),PTR(49),PTR(50),PTR(51),PTR(52),PTR(53),PTR(54),PTR(55),
    PTR(56),PTR(57),PTR(58),PTR(59),PTR(60),PTR(61),PTR(62),PTR(63),
    PTR(64),PTR(65),PTR(66),PTR(67),PTR(68),PTR(69),PTR(70),PTR(71),
    PTR(72),PTR(73),PTR(74),PTR(75),PTR(76),PTR(77),PTR(78),PTR(79),
    PTR(80),PTR(81),PTR(82),PTR(83),PTR(84),PTR(85),PTR(86),PTR(87),
    PTR(88),PTR(89),PTR(90),PTR(91),PTR(92),PTR(93),PTR(94),PTR(95),
    PTR(96),PTR(97),PTR(98),PTR(99),PTR(100),PTR(101),PTR(102),PTR(103),
    PTR(104),PTR(105),PTR(106),PTR(107),PTR(108),PTR(109),PTR(110),PTR(111),
    PTR(112),PTR(113),PTR(114),PTR(115),PTR(116),PTR(117),PTR(118),PTR(119),
    PTR(120),PTR(121),PTR(122),PTR(123),PTR(124),PTR(125),PTR(126),PTR(127),
    PTR(128),PTR(129),PTR(130),PTR(131),PTR(132),PTR(133),PTR(134),PTR(135),
    PTR(136),PTR(137),PTR(138),PTR(139),PTR(140),PTR(141),PTR(142),PTR(143),
    PTR(144),PTR(145),PTR(146),PTR(147),PTR(148),PTR(149),PTR(150),PTR(151),
    PTR(152),PTR(153),PTR(154),PTR(155),PTR(156),PTR(157),PTR(158),PTR(159),
    PTR(160),PTR(161),PTR(162),PTR(163),PTR(164),PTR(165),PTR(166),PTR(167),
    PTR(168),PTR(169),PTR(170),PTR(171),PTR(172),PTR(173),PTR(174),PTR(175),
    PTR(176),PTR(177),PTR(178),PTR(179),PTR(180),PTR(181),PTR(182),PTR(183),
    PTR(184),PTR(185),PTR(186),PTR(187),PTR(188),PTR(189),PTR(190),PTR(191),
    PTR(192),PTR(193),PTR(194),PTR(195),PTR(196),PTR(197),PTR(198),PTR(199),
    PTR(200),PTR(201),PTR(202),PTR(203),PTR(204),PTR(205),PTR(206),PTR(207),
    PTR(208),PTR(209),PTR(210),PTR(211),PTR(212),PTR(213),PTR(214),PTR(215),
    PTR(216),PTR(217),PTR(218),PTR(219),PTR(220),PTR(221),PTR(222),PTR(223),
    PTR(224),PTR(225),PTR(226),PTR(227),PTR(228),PTR(229),PTR(230),PTR(231),
    PTR(232),PTR(233),PTR(234),PTR(235),PTR(236),PTR(237),PTR(238),PTR(239),
    PTR(240),PTR(241),PTR(242),PTR(243),PTR(244),PTR(245),PTR(246),PTR(247),
    PTR(248),PTR(249),PTR(250),PTR(251),PTR(252),PTR(253),PTR(254),PTR(255),
};
