.sm83
.create "output.bin", 0
.headersize $4000

.org $4000
start:
    ld bc, $C000
    ld a, (hl+)
    ld (hl-), a
    ld ($C100), a
    ld a, ($C100)
    ldh ($80), a
    ldh a, ($80)
    add a, c
    adc a, $12
    swap a
    bit 7, h
    jr nz, start
    jp z, start
    call nc, start
    rst $20

.close
