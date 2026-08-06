.z80
.create "output.bin", 0
.headersize $8000

.org $8000
start:
    ld ix, $C000
    ld a, (ix+$02)
    ld (iy-$01), a
    add hl, de
    adc hl, bc
    ex de, hl
    out ($BF), a
    out (c), a
    in a, (c)
    bit 3, (ix+$04)
    djnz start
    jp nz, start
    call start
    rst $18

.close
