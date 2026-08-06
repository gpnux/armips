.gb

; Construct the expected 32 KiB image independently.
.create "expected.bin", 0
.org $0148
.db $00
.org $014D
.db $E7, $00, $E7
.org $7FFF
.db $00
.close

.create "output.bin", 0
.org $7FFF
.db $00
.header
.close
