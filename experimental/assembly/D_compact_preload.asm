rst_begin:
0, 0, 0
rst_end:

dw_begin:
0, 0, 0
dw_end:

pw_begin:
0, 0, 1
pw_end:

barrier_begin:
barrier_end:

LOAD(UOP[0], MEM[@UOP_ADDR@, 3])

LOAD(INP[0], MEM[@INP_ADDR@, 1, 36, 36])
LOAD(WGT_DIAG[0], MEM[@DIAG_ADDR@, 1, 9, 9])
LOAD(WGT[1], MEM[@PW_ADDR@, 1, 1, 1])
PUSH (LD->EX)

POP (LD->EX)
FOR (4, 1) UOP (rst_begin, rst_end) FACTORS(1, 0, 0, 0, 0, 0)
GEMM.RST(ACC[0:1])

FOR (4, 9) UOP (dw_begin, dw_end) FACTORS(1, 0, 9, 1, 0, 1)
GEMM.DIAG(ACC[1:2], INP[1:2], WGT[1:2])

ACC_TO_INP(INP[0], ACC[0], 4)
FOR (4, 1) UOP (rst_begin, rst_end) FACTORS(1, 0, 0, 0, 0, 0)
GEMM.RST(ACC[0:1])

FOR (4, 1) UOP (pw_begin, pw_end) FACTORS(1, 0, 1, 0, 0, 0)
GEMM(ACC[2:3], INP[2:3], WGT[2:3])
PUSH (EX->ST)

POP (EX->ST)
STOR(MEM[@OUT_ADDR@, 1, 4, 4], ACC[0])
PUSH (ST->EX)

POP (ST->EX)
FOR (1, 1) UOP (barrier_begin, barrier_end)
GEMM.RST(ACC[3:3])
FINISH
