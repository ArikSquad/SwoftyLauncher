# rnd_fifth_pie - site proofs (build d873a1c8, image base 0x140000000)

Disassembly from `F1_25.exe` via `tools/sites/fdis.py` (capstone over the `.pdata`
function bounds; `>` marks an address that is a branch target inside the function, including
jump-table targets). Regenerate from the repo root:
`EXE=<F1_25.exe> re/tools/py addons/rnd-tyres-thermal/tools/sites/fdis.py range <start> <end>`.

## Hook-site safety (all OK)

Every hook overwrites 5 bytes. The check walks whole instructions from the site until >= 5 bytes
and fails on any in-function branch/jump-table target strictly inside the covered range
(`tools/sites/fdis.py check`). A whole-`.xtls` rel32/rel8 scan (`tools/sites/xjump.py`) found no branch from
anywhere into those ranges (its one rel8 candidate, 0x141f4aa26, is a byte inside
`lea rdi,[rip+0x3725e7b]` at 0x141f4aa21, not an instruction).
```
OK  func 141f48030-141f49bb9 site 141f482a0: 5 bytes over 1 insn(s); internal branch targets: none
      141f482a0: 488b5c2440                   mov rbx, qword ptr [rsp + 0x40]
OK  func 141f48030-141f49bb9 site 141f485ab: 5 bytes over 1 insn(s); internal branch targets: none
      141f485ab: e840ab2102                   call 0x1441630f0
OK  func 141f48030-141f49bb9 site 141f485b5: 8 bytes over 1 insn(s); internal branch targets: none
      141f485b5: 488db424b0010000             lea rsi, [rsp + 0x1b0]
OK  func 141f48030-141f49bb9 site 141f48a64: 7 bytes over 1 insn(s); internal branch targets: none
      141f48a64: 4c8d0d35588103               lea r9, [rip + 0x3815835]
OK  func 141f4a9c0-141f4ac36 site 141f4aa28: 6 bytes over 2 insn(s); internal branch targets: none
      141f4aa28: 4889d9                       mov rcx, rbx
      141f4aa2b: 4889fa                       mov rdx, rdi
OK  func 1418914f0-141895104 site 1418945b6: 11 bytes over 2 insn(s); internal branch targets: none
      1418945b6: 488d1c04                     lea rbx, [rsp + rax]
      1418945ba: 4881c3a0000000               add rbx, 0xa0
OK  func 1418914f0-141895104 site 1418946d4: 5 bytes over 1 insn(s); internal branch targets: none
      1418946d4: 488d4c2440                   lea rcx, [rsp + 0x40]
OK  func 1418914f0-141895104 site 141894702: 12 bytes over 1 insn(s); internal branch targets: none
      141894702: 48c78424180e000040000000     mov qword ptr [rsp + 0xe18], 0x40
OK  func 1418914f0-141895104 site 141894e03: 5 bytes over 1 insn(s); internal branch targets: none
      141894e03: 4c8d7c2448                   lea r15, [rsp + 0x48]
```

## Scan gates (exactly one hit in `.xtls`, equal to the expected address)
```
1 ['0x141f48253'] 48 C7 44 24 50 00 00 00 00 48 8D 3D AA 5E 93 03 EB 3B 48 C7 44 24 50 00 00 00 00
1 ['0x141f4aa21'] 48 8D 3D 7B 5E 72 03 48 89 D9 48 89 FA E8 1D 2E 41 03 85 C0 0F 85 F2 01 00 00 B0 03
1 ['0x1418945a5'] 48 8B 84 24 A8 00 00 00 48 85 C0 0F 84 EA 09 00 00 48 8D 1C 04 48 81 C3 A0 00 00 00 48 8D AC 24 A0 00 00 00
```

## safetyhook: a written `ctx.rip` redirects (`native/third_party/safetyhook/safetyhook.cpp` at the repo root, x64 Windows stub)

`asm_data` starts `FF 35 79 01 00 00` (push the trampoline address = the `rip` slot), `54` push rsp
(-> `trampoline_rsp` = address of the rip slot), `54` push rsp (-> `rsp`, later `+0x10` = the
original rsp), then rbp rax rbx rcx rdx rsi rdi r8..r15, pushfq, 16 xmm. After the callback it
restores them and ends `5D` pop rbp, `48 8D 64 24 08` lea rsp,[rsp+8] (skip the rsp slot), `5C`
pop rsp (= trampoline_rsp = the rip slot), `C3` ret. So the function resumes at `Context64::rip`
with the original rsp. Leaving `rip` alone resumes in the trampoline (relocated original
instructions, then a jump back). Relative instructions inside a relocated window (call rel32,
jcc rel32, rip-relative disp32) are re-encoded by `create_trampoline` (safetyhook.cpp:560-600)
into a trampoline allocated within +-2 GB.

## Piece 1 - `Career_Populate_RnDPieSlices` 0x141f48030 (pie_tyres_slice)

r14d = department code for the whole loop body: written only at 0x141f48213, otherwise only
read (cmp at 0x141f48218/2b/76/559, `cmp [rcx+1],r14b`, `cmp [rax+0x72],r14b`, one spill
`mov [rsp+0xd0],r14b`), pushed/popped in the prologue/epilogue.

Name jump table @0x14576fc14 (4 entries: aero chassis maintenance engine). The addon relaxes
the guard immediate at 0x141f4821b (`03`->`04`); code 4 then hits `cmp r14d,3 ; ja 0x141f48253`
(DEFAULT, rdi = 0x14587e10d "error_department") and joins at **H1 0x141f482a0** with rdi = name.
`rdi` is then used for `lng_rnd_dept_%s` (0x141f482b8), the VM child hash + GetOrCreate
(0x141f482e8..0x141f483f2), `Vm_Holder_SetCStr(slot+0x100, rdi)` = `department_name`
(0x141f484e0) and the icon path (H4).
```
>141f48207: 48ffc5                       inc rbp
 141f4820a: 4839c5                       cmp rbp, rax
 141f4820d: 0f8480190000                 je 0x141f49b93
>141f48213: 440fb67500                   movzx r14d, byte ptr [rbp]
 141f48218: 4183fe03                     cmp r14d, 3
 141f4821c: 77e9                         ja 0x141f48207
 141f4821e: 4183fd04                     cmp r13d, 4
 141f48222: 0f876b190000                 ja 0x141f49b93
 141f48228: 4489f6                       mov esi, r14d
 141f4822b: 4183fe03                     cmp r14d, 3
 141f4822f: 7722                         ja 0x141f48253
 141f48231: 488d0ddc798203               lea rcx, [rip + 0x38279dc]
 141f48238: 486304b1                     movsxd rax, dword ptr [rcx + rsi*4]
 141f4823c: 4801c8                       add rax, rcx
 141f4823f: ffe0                         jmp rax
>141f48241: 48c744245000000000           mov qword ptr [rsp + 0x50], 0
 141f4824a: 488d3ddc4b7c03               lea rdi, [rip + 0x37c4bdc]
 141f48251: eb4d                         jmp 0x141f482a0
>141f48253: 48c744245000000000           mov qword ptr [rsp + 0x50], 0
 141f4825c: 488d3daa5e9303               lea rdi, [rip + 0x3935eaa]
 141f48263: eb3b                         jmp 0x141f482a0
>141f48265: 48c744245000000000           mov qword ptr [rsp + 0x50], 0
 141f4826e: 488d3dbd4b7c03               lea rdi, [rip + 0x37c4bbd]
 141f48275: eb29                         jmp 0x141f482a0
>141f48277: 48c744245000000000           mov qword ptr [rsp + 0x50], 0
 141f48280: 488d3d6ad48003               lea rdi, [rip + 0x380d46a]
 141f48287: eb17                         jmp 0x141f482a0
>141f48289: 48c744245000000000           mov qword ptr [rsp + 0x50], 0
 141f48292: 488d3d0a867203               lea rdi, [rip + 0x372860a]
 141f48299: 0f1f8000000000               nop dword ptr [rax]
>141f482a0: 488b5c2440                   mov rbx, qword ptr [rsp + 0x40]
 141f482a5: 4c8ba370060000               mov r12, qword ptr [rbx + 0x670]
 141f482ac: 488d4c2450                   lea rcx, [rsp + 0x50]
 141f482b1: 488d15fb5e9303               lea rdx, [rip + 0x3935efb]
 141f482b8: 4989f8                       mov r8, rdi
 141f482bb: e830ae2102                   call 0x1441630f0
```

Colour/description switch (table @0x14576fc24, 4 entries). DEFAULT case 0x141f48598:
`lea r8,[error_department]` then **H2 0x141f485ab** (the Format call, r8 = name for
`lng_rnd_dept_%s_overview_description`), `mov eax,0xffffffff`, **H3 0x141f485b5**
(eax = colour), join `mov [rsp+0x64],eax` (uStack_3d4).
```
>141f48550: 48c744243800000000           mov qword ptr [rsp + 0x38], 0
 141f48559: 4183fe03                     cmp r14d, 3
 141f4855d: 7739                         ja 0x141f48598
 141f4855f: 488d0dbe768203               lea rcx, [rip + 0x38276be]
 141f48566: 486304b1                     movsxd rax, dword ptr [rcx + rsi*4]
 141f4856a: 4801c8                       add rax, rcx
 141f4856d: 4c8d3dd4a86c03               lea r15, [rip + 0x36ca8d4]
 141f48574: ffe0                         jmp rax
>141f48576: 488d4c2438                   lea rcx, [rsp + 0x38]
 141f4857b: 488d15415c9303               lea rdx, [rip + 0x3935c41]
 141f48582: 4c8d05a4487c03               lea r8, [rip + 0x37c48a4]
 141f48589: e862ab2102                   call 0x1441630f0
 141f4858e: b800adeeff                   mov eax, 0xffeead00
 141f48593: e989000000                   jmp 0x141f48621
>141f48598: 488d4c2438                   lea rcx, [rsp + 0x38]
 141f4859d: 488d151f5c9303               lea rdx, [rip + 0x3935c1f]
 141f485a4: 4c8d05625b9303               lea r8, [rip + 0x3935b62]
 141f485ab: e840ab2102                   call 0x1441630f0
 141f485b0: b8ffffffff                   mov eax, 0xffffffff
 141f485b5: 488db424b0010000             lea rsi, [rsp + 0x1b0]
 141f485bd: 4c8d3d84a86c03               lea r15, [rip + 0x36ca884]
 141f485c4: eb63                         jmp 0x141f48629
>141f485c6: 488d4c2438                   lea rcx, [rsp + 0x38]
 141f485cb: 488d15f15b9303               lea rdx, [rip + 0x3935bf1]
 141f485d2: 4c8d0559487c03               lea r8, [rip + 0x37c4859]
 141f485d9: e812ab2102                   call 0x1441630f0
 141f485de: b88dc53eff                   mov eax, 0xff3ec58d
 141f485e3: eb3c                         jmp 0x141f48621
>141f485e5: 488d4c2438                   lea rcx, [rsp + 0x38]
 141f485ea: 488d15d25b9303               lea rdx, [rip + 0x3935bd2]
 141f485f1: 4c8d05f9d08003               lea r8, [rip + 0x380d0f9]
 141f485f8: e8f3aa2102                   call 0x1441630f0
 141f485fd: b8ffde16ff                   mov eax, 0xff16deff
 141f48602: eb1d                         jmp 0x141f48621
>141f48604: 488d4c2438                   lea rcx, [rsp + 0x38]
 141f48609: 488d15b35b9303               lea rdx, [rip + 0x3935bb3]
 141f48610: 4c8d058c827203               lea r8, [rip + 0x372828c]
 141f48617: e8d4aa2102                   call 0x1441630f0
 141f4861c: b8ed2a7aff                   mov eax, 0xff7a2aed
>141f48621: 488db424b0010000             lea rsi, [rsp + 0x1b0]
>141f48629: 89442464                     mov dword ptr [rsp + 0x64], eax
 141f4862d: 48c744244800000000           mov qword ptr [rsp + 0x48], 0
```

Icon path: `Format(&[rsp+0x160], "%s.%s.%s", [ctx+0x660], "rnd_wheel_icon", [rsp+0x20]=rdi)`.
**H4 0x141f48a64** runs after `mov [rsp+0x20],rdi`; the call is at 0x141f48a6b.
`overrides.uidb` `career.rnd_wheel_icon` frames: aero chassis maintenance(durability.tif) engine
regulation shutdown - no tyres, so code 4 borrows `maintenance`.
```
>141f48a20: 48c784246001000000000000     mov qword ptr [rsp + 0x160], 0
 141f48a2c: 488d9c2460010000             lea rbx, [rsp + 0x160]
 141f48a34: 4889d9                       mov rcx, rbx
 141f48a37: 488d15249d6c03               lea rdx, [rip + 0x36c9d24]
 141f48a3e: 4531c0                       xor r8d, r8d
 141f48a41: 4531c9                       xor r9d, r9d
 141f48a44: e897ea2402                   call 0x1441974e0
 141f48a49: 488b442468                   mov rax, qword ptr [rsp + 0x68]
 141f48a4e: 4c8b8060060000               mov r8, qword ptr [rax + 0x660]
 141f48a55: 48897c2420                   mov qword ptr [rsp + 0x20], rdi
 141f48a5a: 4889d9                       mov rcx, rbx
 141f48a5d: 488d1555d76e03               lea rdx, [rip + 0x36ed755]
 141f48a64: 4c8d0d35588103               lea r9, [rip + 0x3815835]
 141f48a6b: e880a62102                   call 0x1441630f0
 141f48a70: 488b842460010000             mov rax, qword ptr [rsp + 0x160]
```

## Piece 2 - 0x141f4a9c0 (pie_tyres_focus)

Reached from the class-B command handler 0x141f4ac40 when cmd == 0x3aae34e1
(djb2 "department_focused"): it tail-jumps with rdx = data+0x10. Also called from 0x141f4a959.
```
 141f4ac52: 4889ce                       mov rsi, rcx
 141f4ac55: 8b00                         mov eax, dword ptr [rax]
 141f4ac57: 3de134ae3a                   cmp eax, 0x3aae34e1
 141f4ac5c: 0f84e2000000                 je 0x141f4ad44
```
```
>141f4ad44: 4983c010                     add r8, 0x10
 141f4ad48: 4889f1                       mov rcx, rsi
 141f4ad4b: 4c89c2                       mov rdx, r8
 141f4ad4e: 4883c468                     add rsp, 0x68
 141f4ad52: 5f                           pop rdi
 141f4ad53: 5e                           pop rsi
 141f4ad54: e967fcffff                   jmp 0x141f4a9c0
```

rbx = data chars (0x141f4a9db), rsi = this (0x141f4a9c7). **F1 0x141f4aa28** (`mov rcx,rbx ;
mov rdx,rdi`, 6 bytes) sits before the "engine" strcmp; for data "tyres" the hook sets
rdi = "tyres" and `rip` = 0x141f4aa4f, skipping the compare and `mov [DAT_145f93c08],al`
(0x141f4aa49). The common block reads only rsi (this->+0x10 VM node), rdi (name, hashed) and
the frame; rcx/rdx/r8/r9 are reloaded, rbx is overwritten at 0x141f4aaf1.
```
 141f4a9c0: 56                           push rsi
 141f4a9c1: 57                           push rdi
 141f4a9c2: 53                           push rbx
 141f4a9c3: 4883ec60                     sub rsp, 0x60
 141f4a9c7: 4889ce                       mov rsi, rcx
 141f4a9ca: 488b02                       mov rax, qword ptr [rdx]
 141f4a9cd: 488d4809                     lea rcx, [rax + 9]
 141f4a9d1: 4885c0                       test rax, rax
 141f4a9d4: 488d1d6dabd504               lea rbx, [rip + 0x4d5ab6d]
 141f4a9db: 480f45d9                     cmovne rbx, rcx
 141f4a9df: 488d3d47247c03               lea rdi, [rip + 0x37c2447]
 141f4a9e6: 4889d9                       mov rcx, rbx
 141f4a9e9: 4889fa                       mov rdx, rdi
 141f4a9ec: e85f2e4103                   call 0x14535d850
 141f4a9f1: 85c0                         test eax, eax
 141f4a9f3: 744a                         je 0x141f4aa3f
 141f4a9f5: 488d3d36247c03               lea rdi, [rip + 0x37c2436]
 141f4a9fc: 4889d9                       mov rcx, rbx
 141f4a9ff: 4889fa                       mov rdx, rdi
 141f4aa02: e8492e4103                   call 0x14535d850
 141f4aa07: 85c0                         test eax, eax
 141f4aa09: 7438                         je 0x141f4aa43
 141f4aa0b: 488d3ddfac8003               lea rdi, [rip + 0x380acdf]
 141f4aa12: 4889d9                       mov rcx, rbx
 141f4aa15: 4889fa                       mov rdx, rdi
 141f4aa18: e8332e4103                   call 0x14535d850
 141f4aa1d: 85c0                         test eax, eax
 141f4aa1f: 7426                         je 0x141f4aa47
 141f4aa21: 488d3d7b5e7203               lea rdi, [rip + 0x3725e7b]
 141f4aa28: 4889d9                       mov rcx, rbx
 141f4aa2b: 4889fa                       mov rdx, rdi
 141f4aa2e: e81d2e4103                   call 0x14535d850
 141f4aa33: 85c0                         test eax, eax
 141f4aa35: 0f85f2010000                 jne 0x141f4ac2d
 141f4aa3b: b003                         mov al, 3
 141f4aa3d: eb0a                         jmp 0x141f4aa49
>141f4aa3f: 31c0                         xor eax, eax
 141f4aa41: eb06                         jmp 0x141f4aa49
>141f4aa43: b001                         mov al, 1
 141f4aa45: eb02                         jmp 0x141f4aa49
>141f4aa47: b002                         mov al, 2
>141f4aa49: 8805b9910404                 mov byte ptr [rip + 0x40491b9], al
 141f4aa4f: 488b4610                     mov rax, qword ptr [rsi + 0x10]
```

## Piece 3 - performance publisher 0x1418914f0 (pie_tyres_performance)

Frame: `sub rsp, 0x1ac8` after `__chkstk`; all slots rsp-relative. Department list built once by
FUN_143518cc0(ctx=[rsp+0x200], list=[rsp+0xa0]) - bytes at +0xa0, count qword at +0xa8
(capacity 8; the callee writes at most 6 codes 0..5).
```
 141891866: c684240c02000000             mov byte ptr [rsp + 0x20c], 0
 14189186e: 4889842400020000             mov qword ptr [rsp + 0x200], rax
 141891876: 488d8c2400020000             lea rcx, [rsp + 0x200]
 14189187e: 488d9424a0000000             lea rdx, [rsp + 0xa0]
 141891886: e83574c801                   call 0x143518cc0
 14189188b: eb0c                         jmp 0x141891899
>14189188d: 48c78424a800000000000000     mov qword ptr [rsp + 0xa8], 0
>141891899: 488b442428                   mov rax, qword ptr [rsp + 0x28]
 14189189e: 4883783800                   cmp qword ptr [rax + 0x38], 0
```

The department_performance loop: count -> rax, **P0 0x1418945b6** (`lea rbx,[rsp+rax] ; add
rbx,0xa0` = end pointer), rbp = rsp+0xa0 walks the codes. Other per-team loops (the chart's
`%s_performance_index_fraction` / `%s_show_progress_pip`) read the stored count at +0xa8, which
P0 never changes.
```
 1418945a5: 488b8424a8000000             mov rax, qword ptr [rsp + 0xa8]
 1418945ad: 4885c0                       test rax, rax
 1418945b0: 0f84ea090000                 je 0x141894fa0
 1418945b6: 488d1c04                     lea rbx, [rsp + rax]
 1418945ba: 4881c3a0000000               add rbx, 0xa0
 1418945c1: 488dac24a0000000             lea rbp, [rsp + 0xa0]
 1418945c9: eb25                         jmp 0x1418945f0
 1418945cb: 0f1f440000                   nop dword ptr [rax + rax]
>1418945d0: 488b0d210f4105               mov rcx, qword ptr [rip + 0x5410f21]
 1418945d7: 488b942480000000             mov rdx, qword ptr [rsp + 0x80]
 1418945df: e88cf7a102                   call 0x1442b3d70
>1418945e4: 48ffc5                       inc rbp
 1418945e7: 4839dd                       cmp rbp, rbx
 1418945ea: 0f8460090000                 je 0x141894f50
>1418945f0: 0fb65500                     movzx edx, byte ptr [rbp]
```

perf = (sum cur + FUN_143518660(ctx,code,0xffff)) / F -> [rsp+0x158] (0x141894674); potential =
sum max / F -> [rsp+0x154] (0x1418946b5); F = FUN_143518f50 stays in xmm0. Name #1 for
`lng_rnd_dept_%s` -> r8, join **P1 0x1418946d4**. Name #2 -> rax, join **P2 0x141894702**
(copied to [rsp+0xe28], hashed into the `department_performance.<name>` child).
```
>141894640: 488d8c2460010000             lea rcx, [rsp + 0x160]
 141894648: 6641b8ffff                   mov r8w, 0xffff
 14189464d: e80e40c801                   call 0x143518660
 141894652: c5f828f0                     vmovaps xmm6, xmm0
 141894656: 488d8c2488010000             lea rcx, [rsp + 0x188]
 14189465e: e8ed48c801                   call 0x143518f50
 141894663: c4c1782ec0                   vucomiss xmm0, xmm8
 141894668: 7502                         jne 0x14189466c
 14189466a: 7b08                         jnp 0x141894674
>14189466c: c5a258ce                     vaddss xmm1, xmm11, xmm6
 141894670: c5725ec0                     vdivss xmm8, xmm1, xmm0
>141894674: c57a11842458010000           vmovss dword ptr [rsp + 0x158], xmm8
 14189467d: 480fbe4500                   movsx rax, byte ptr [rbp]
 141894682: 488b8f381a0000               mov rcx, qword ptr [rdi + 0x1a38]
 141894689: 488b97401a0000               mov rdx, qword ptr [rdi + 0x1a40]
 141894690: c5f057c9                     vxorps xmm1, xmm1, xmm1
 141894694: c5e857d2                     vxorps xmm2, xmm2, xmm2
 141894698: 4839d1                       cmp rcx, rdx
 14189469b: 488dbc2480000000             lea rdi, [rsp + 0x80]
 1418946a3: 0f85d4000000                 jne 0x14189477d
>1418946a9: c5f82ec1                     vucomiss xmm0, xmm1
 1418946ad: 7502                         jne 0x1418946b1
 1418946af: 7b04                         jnp 0x1418946b5
>1418946b1: c5ea5ec8                     vdivss xmm1, xmm2, xmm0
>1418946b5: c5fa118c2454010000           vmovss dword ptr [rsp + 0x154], xmm1
 1418946be: 4c8d05489afe03               lea r8, [rip + 0x3fe9a48]
 1418946c5: 3c03                         cmp al, 3
 1418946c7: 770b                         ja 0x1418946d4
 1418946c9: 488d0df075e803               lea rcx, [rip + 0x3e875f0]
 1418946d0: 4c8b04c1                     mov r8, qword ptr [rcx + rax*8]
>1418946d4: 488d4c2440                   lea rcx, [rsp + 0x40]
 1418946d9: 488d15d39afe03               lea rdx, [rip + 0x3fe9ad3]
 1418946e0: e80bea8c02                   call 0x1441630f0
 1418946e5: 480fbe4d00                   movsx rcx, byte ptr [rbp]
 1418946ea: 488d051c9afe03               lea rax, [rip + 0x3fe9a1c]
 1418946f1: 4883f903                     cmp rcx, 3
 1418946f5: 770b                         ja 0x141894702
 1418946f7: 488d05c275e803               lea rax, [rip + 0x3e875c2]
 1418946fe: 488b04c8                     mov rax, qword ptr [rax + rcx*8]
```

dept_colour: bounded `< 6` read of the table 0x14571bce0 = {ffeead00, ff3ec58d, ff16deff,
ff7a2aed, ffffffff, ffffffff}; [4] is written to 0xff0078ff (layout-verified).
```
>141894cda: 480fbe4d00                   movsx rcx, byte ptr [rbp]
 141894cdf: b8ffffffff                   mov eax, 0xffffffff
 141894ce4: 4883f905                     cmp rcx, 5
 141894ce8: 770a                         ja 0x141894cf4
 141894cea: 488d05ef6fe803               lea rax, [rip + 0x3e86fef]
 141894cf1: 8b0488                       mov eax, dword ptr [rax + rcx*4]
>141894cf4: 8984249c000000               mov dword ptr [rsp + 0x9c], eax
```

Name #3 for `rnd_goto_%s_department` (dept_colour_anim) -> r8, join **P4 0x141894e03**.
```
>141894de6: 480fbe4500                   movsx rax, byte ptr [rbp]
 141894deb: 4c8d051b93fe03               lea r8, [rip + 0x3fe931b]
 141894df2: 4883f803                     cmp rax, 3
 141894df6: 770b                         ja 0x141894e03
 141894df8: 488d0dc16ee803               lea rcx, [rip + 0x3e86ec1]
 141894dff: 4c8b04c1                     mov r8, qword ptr [rcx + rax*8]
>141894e03: 4c8d7c2448                   lea r15, [rsp + 0x48]
 141894e08: 4c89f9                       mov rcx, r15
 141894e0b: 488d15f293fe03               lea rdx, [rip + 0x3fe93f2]
 141894e12: e8d9e28c02                   call 0x1441630f0
```

Code-indexed reads in the loop body (0x1418945f0-0x141894f50) are exactly these four: three
`<= 3` name-table reads and one `<= 5` colour read; no other table is indexed by the code.
