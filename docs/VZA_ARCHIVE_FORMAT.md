# VantaOS RLE archive format (.VZA)

The Explorer's `.VZA` format is a small, streaming, single-file compression format used by the early ring-3 File Explorer. It is **not ZIP-compatible** and does not store timestamps, permissions, directory trees, or a checksum.

## File layout

- The first eight bytes are the ASCII signature `VZARLE1\n`.
- The remaining bytes are a sequence of RLE packets. Packets never cross the logical end of the stream; the decoder must reject truncated packets.

## Packet encoding

Each packet starts with one control byte.

- `0x01..0x7F`: literal packet. The control byte is the number of literal bytes that follow (1–127).
- `0x81..0xFF`: run packet. The low seven bits plus three give the run length (4–130); the next byte is repeated that many times.
- `0x00` and `0x80` are invalid/reserved.

The encoder uses runs of at least four identical bytes. Shorter runs are emitted as literal data. Input and output are streamed in bounded buffers; compression can expand data that contains few repeated bytes.

## Safety and limitations

- Extraction validates the signature and packet boundaries before reporting success.
- Explorer refuses to overwrite an existing extraction target and removes partial output after a detected decode/write failure.
- Archives represent one file. Compressing several selected files creates one `.VZA` beside each source.
- The format has no cryptographic integrity or corruption-detection field. Do not use it as a backup format for important data.
