#ifndef INCBIN_HPP
#define INCBIN_HPP

#define INCBIN_SECTION ".rodata"
#define INCBIN_PREFIX ""
#define INCBIN_ALIGN ".align 8"

#define INCBIN(name, file) \
    __asm__( \
        ".section " INCBIN_SECTION "\n" \
        INCBIN_ALIGN "\n" \
        ".global " INCBIN_PREFIX #name "_data\n" \
        INCBIN_PREFIX #name "_data:\n" \
        ".incbin \"" file "\"\n" \
        ".global " INCBIN_PREFIX #name "_end\n" \
        INCBIN_PREFIX #name "_end:\n" \
        ".byte 0\n" \
        ".text\n" \
    ); \
    extern "C" const unsigned char name ## _data[]; \
    extern "C" const unsigned char name ## _end[]; \
    extern "C" const unsigned int name ## _size; \
    __asm__( \
        ".section " INCBIN_SECTION "\n" \
        INCBIN_ALIGN "\n" \
        ".global " INCBIN_PREFIX #name "_size\n" \
        INCBIN_PREFIX #name "_size:\n" \
        ".int " INCBIN_PREFIX #name "_end - " INCBIN_PREFIX #name "_data\n" \
        ".text\n" \
    );

#endif
