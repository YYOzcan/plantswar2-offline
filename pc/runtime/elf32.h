// Minimal 32-bit ELF definitions (no <elf.h> on Windows / macOS).
#pragma once
#include <cstdint>
struct Elf32_Ehdr { uint8_t e_ident[16]; uint16_t e_type, e_machine; uint32_t e_version, e_entry, e_phoff, e_shoff, e_flags;
                    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx; };
struct Elf32_Phdr { uint32_t p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align; };
struct Elf32_Dyn { int32_t d_tag; union { uint32_t d_val, d_ptr; } d_un; };
struct Elf32_Sym { uint32_t st_name, st_value, st_size; uint8_t st_info, st_other; uint16_t st_shndx; };
struct Elf32_Rel { uint32_t r_offset, r_info; };
enum { PT_LOAD = 1, PT_DYNAMIC = 2, SHN_UNDEF = 0 };
enum { DT_NULL = 0, DT_HASH = 4, DT_STRTAB = 5, DT_SYMTAB = 6, DT_REL = 17, DT_RELSZ = 18, DT_PLTRELSZ = 2, DT_JMPREL = 23,
       DT_INIT_ARRAY = 25, DT_INIT_ARRAYSZ = 27 };
enum { R_ARM_ABS32 = 2, R_ARM_GLOB_DAT = 21, R_ARM_JUMP_SLOT = 22, R_ARM_RELATIVE = 23 };
#define ELF32_R_SYM(i) ((i) >> 8)
#define ELF32_R_TYPE(i) ((i) & 0xff)
