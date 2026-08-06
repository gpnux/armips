.sms

; Construct the expected 32 KiB image independently.
.create "expected.bin", 0
.org $7FF0
.ascii "TMR SEGA"
.org $7FFF
.db $0C
.close

.create "output.bin", 0
.org $7FF0
.ascii "TMR SEGA"
.org $7FFF
.db $00
.header
.close
