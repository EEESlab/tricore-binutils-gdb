#objdump: -dr --prefix-addresses --show-raw-insn
#name: microMIPS branch to a weak symbol
#as: -32 -mips32r2 -mmicromips --defsym align=12
#source: branch-weak.s

.*: +file format .*mips.*

Disassembly of section \.text:
[0-9a-f]+ <[^>]*> 40e0 fffe 	bc	00000000 <foo>
[ 	]*[0-9a-f]+: R_MICROMIPS_PC16_S1	bar
	\.\.\.
[0-9a-f]+ <[^>]*> 459f      	jr	ra
[0-9a-f]+ <[^>]*> 0c00      	nop
	\.\.\.
