"""Inspect package headers and preserved title ID; this does not launch a 3DS."""
import pathlib,struct,sys
root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else '.')
cia=(root/'3ds_google_maps.cia').read_bytes()
header,kind,version,cert,ticket,tmd,meta,content=struct.unpack_from('<IHHIIIIQ',cia)
assert (header,kind,version)==(0x2020,0,0) and all((cert,ticket,tmd,content))
align=lambda n:(n+63)&~63
start=sum(map(align,(header,cert,ticket,tmd)))
assert cia[start+0x100:start+0x104]==b'NCCH'
assert start+content<=len(cia)<=align(start+content)+align(meta)
program_id=struct.unpack_from('<Q',cia,start+0x118)[0]
assert program_id==0x000400000FD34B00,hex(program_id)
hb=(root/'3ds_google_maps.3dsx').read_bytes()
magic,header,reloc,version,flags,code,rodata,data,bss=struct.unpack_from('<4sHH6I',hb)
assert magic==b'3DSX' and header>=32 and reloc>=8 and code>0
assert header+code+rodata+data-bss<=len(hb)
print(f'CIA / 3DSX headers valid; title ID {program_id:016X}; console execution unverified')
