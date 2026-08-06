.gb
.create "output.bin", 0

.org $0100
start:
.db $AA

.bank 1
.org $4000
.db $BB

.close
