## CashCode ByteCode (CCBC) v1 – Draft

Goal: A compact, portable, streaming-friendly bytecode for server-side HTML rendering and lightweight app logic. No external runtime required.

### Endianness
- All integers are little-endian.

### File Layout
```
| Header (fixed 32 bytes) |
| Constant Table          |
| Function Table          |
| Route Table             |
| Code Segment            |
```

### Header (32 bytes)
- magic: 4 bytes = 'C' 'C' 'B' 'C'
- version: u16 (current: 1)
- flags: u16 (reserved: 0)
- off_consts: u32 (byte offset from start)
- off_funcs: u32
- off_routes: u32
- off_code: u32
- code_size: u32 (bytes of code segment)

### Constant Table
- count: u32
- entries[count]:
  - tag: u8
    - 0x01 = Text (UTF-8)
    - 0x02 = HtmlSafe (UTF-8, trusted)
    - 0x03 = Number (f64)
    - 0x04 = Bytes (raw)
  - payload: variant by tag
    - Text/HtmlSafe: len u32, bytes[len]
    - Number: f64 (8 bytes)
    - Bytes: len u32, bytes[len]

### Function Table
- count: u32
- entries[count]: { nameConstIdx: u32, codeOffset: u32 }

### Route Table
- count: u32
- entries[count]: { pathConstIdx: u32, funcIndex: u32 }

### Code Segment
- A stream of opcodes and immediates.
- Stack-based VM.

### Minimal Opcode Set (v1)
- 0x00 OP_HALT
- 0x01 OP_CONST u32 idx            ; push constant[idx]
- 0x02 OP_PRINT_ESC                ; escape and print top; pop
- 0x03 OP_PRINT_RAW                ; raw print top; pop
- 0x04 OP_DROP                     ; pop
- 0x10 OP_TAG_OPEN u32 nameIdx     ; print <name>
- 0x11 OP_TAG_ATTR u32 nameIdx     ; consume value (stack), escape, print ' name="val"'
- 0x12 OP_TAG_CLOSE u32 nameIdx    ; print </name>
- 0x13 OP_TAG_END                  ; print '>' (after open/attrs)
- 0x20 OP_JUMP i32 rel             ; ip += rel
- 0x21 OP_JF i32 rel               ; pop cond (truthy), if false ip += rel
- 0x30 OP_ITER_START               ; pop iterable -> iterator frame
- 0x31 OP_ITER_NEXT i32 relEnd     ; if next exists, push item else jump relEnd
- 0xF0 OP_DEBUG u32 n              ; implementation-defined

Notes:
- Truthiness for OP_JF: false, 0, empty string/bytes considered false.
- Escaping rules: OP_PRINT_ESC escapes &, <, >, ", ' for HTML text/attrs.

### Routing & Entry
- The host selects a function by route table entry and begins execution at its code offset within Code Segment.

### Streaming
- Printing ops write to the host output stream; hosts should use chunked transfer encoding to support streaming HTML.

### Future Extensions (not v1)
- $action/$form ops, channel/concurrency ops, cache ops, SQL ops.


