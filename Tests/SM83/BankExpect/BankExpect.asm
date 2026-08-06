.gb
.open "input.bin", "output.bin", 0

.bank 0
.org $0000
.db $58
.org $0000
.expect $41
.org $0001
.expect $42, $43
.db $5A

.close
