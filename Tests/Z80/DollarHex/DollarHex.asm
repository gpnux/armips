.z80

.create "expected.bin", 0
.db 0x11, 0xE4, 0x74, 0xE6, 0x3E
.close

.create "output.bin", 0
ld de, $74E4
.dw $3EE6
.close
