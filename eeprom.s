  .equ PORTB, 0x6000
  .equ PORTA, 0x6001
  .equ DDRB,  0x6002
  .equ DDRA,  0x6003

  .equ E,  0b10000000
  .equ RW, 0b01000000
  .equ RS, 0b00100000

  .org 0x8000

reset:
  ldx #0xff
  txs

  lda #0b11111111 ; Set all pins on port B to output
  sta DDRB
  lda #0b11100000 ; Set top 3 pins on port A to output
  sta DDRA

  lda #0b00111000 ; Set 8-bit mode; 2-line display; 5x8 font
  jsr lcd_instruction
  lda #0b00001110 ; Display on; cursor on; blink off
  jsr lcd_instruction
  lda #0b00000110 ; Increment and shift cursor; don't shift display
  jsr lcd_instruction
  lda #0b00000001 ; Clear display
  jsr lcd_instruction

  ldx #0

print:
  lda message,x
  beq loop
  inx
  cmp #0xA        ; check for newline
  beq carriage_return
  jsr lcd_wait
  sta PORTB
  lda #RS         ; Set RS; Clear RW/E bits
  sta PORTA
  lda #(RS | E)   ; Set E bit to send instruction
  sta PORTA
  lda #RS         ; Clear E bits
  sta PORTA
  jmp print

carriage_return:
  lda #0b10101000 ; put cursor at position 40
  jsr lcd_instruction
  jmp print


lcd_wait:
  pha
  lda #0b00000000  ; Port B is input
  sta DDRB
  
lcdbusy:
  lda #RW
  sta PORTA
  lda #(RW | E)
  sta PORTA
  lda PORTB
  and #0b10000000
  bne lcdbusy

  lda #RW
  sta PORTA
  lda #0b11111111  ; Port B is output
  sta DDRB
  pla
  rts

lcd_instruction:
  jsr lcd_wait
  sta PORTB
  lda #0         ; Clear RS/RW/E bits
  sta PORTA
  lda #E         ; Set E bit to send instruction
  sta PORTA
  lda #0         ; Clear RS/RW/E bits
  sta PORTA
  rts

loop:
  jmp loop

message: 
  .byte "testtesttesttestc\1234567890", 0

.org 0xfffc
.word reset
.word 0x0000
