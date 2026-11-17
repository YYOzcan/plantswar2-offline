"""gen_imports.py <libGame.so> <out.c>: import table (name -> imp_ function) with weak fallbacks."""
import re, sys
from elftools.elf.elffile import ELFFile
elf = ELFFile(open(sys.argv[1], "rb"))
names = set()
for s in elf.get_section_by_name(".dynsym").iter_symbols():
    if s["st_shndx"] == "SHN_UNDEF" and s.name: names.add(s.name)
data = open(sys.argv[1], "rb").read()
names |= {m.decode() for m in re.findall(rb"(?<=\0)(gl[A-Z][A-Za-z0-9]+)(?=\0)", data)}          # dlsym'd GLES entry points
names |= {"pthread_cond_signal", "pthread_self", "longjmp", "siglongjmp", "_longjmp", "dlerror", "read", "munmap",
          "glColor4f", "glLoadMatrixf", "glRotatef", "glTranslatef", "glTexSubImage2D", "glTexParameterf", "glTexEnvi",
          "glPixelStorei", "glAlphaFunc", "glColorMask", "glCullFace", "glFlush", "glFinish", "glOrthox"}
names -= {"__sF", "__stack_chk_guard"}                                                   # data, handled by the loader
names = sorted(names)
with open(sys.argv[2], "w") as f:   # names only: the runtime binds them to its IMP() registry at start-up
    f.write("const char *const import_names[] = {\n")
    for n in names: f.write(f'  "{n}",\n')
    f.write("};\nconst unsigned import_count = %d;\n" % len(names))
print(len(names), "imports")
