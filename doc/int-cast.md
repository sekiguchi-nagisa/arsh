## Specification of Integer cast (narrowing, widening)

Byte, Int16 and Uint16 integer is internally represented by 32bit.


| b/a        | **Byte**   | **Int16**  | **Uint16** | **Int32**  | **Uint32** | **Int64**  | **Uint64** | **Float**  |
|------------|------------|------------|------------|------------|------------|------------|------------|------------|
| **Byte**   |   --       |   (1)      |   (1)      |   (1)      |   (1)      |   (5)      |   (5)      |   (9)      |
| **Int16**  |   (2)      |   --       |   (3)      |   (1)      |   (1)      |   (7)      |   (7)      |   (10)     |
| **Uint16** |   (2)      |   (4)      |   --       |   (1)      |   (1)      |   (5)      |   (5)      |   (9)      |
| **Int32**  |   (2)      |   (4)      |   (3)      |   --       |   (1)      |   (7)      |   (7)      |   (10)     |
| **Uint32** |   (2)      |   (4)      |   (3)      |   (1)      |   --       |   (5)      |   (5)      |   (9)      |
| **Int64**  |   (8)+(2)  |   (8)+(4)  |   (8)+(3)  |   (8)      |   (8)      |   --       |   (6)      |   (12)     |
| **Uint64** |   (8)+(2)  |   (8)+(4)  |   (8)+(3)  |   (8)      |   (8)      |   (6)      |   --       |   (11)     |
| **Float**  |   (15)+(2) |   (16)+(4) |   (15)+(3) |   (14)     |   (13)     |   (16)     |   (15)     |   --       |


1. **COPY_INT**: do nothing (copy Int_Object).
2. **TO_B**: fill higher bits (8th ~ 31th) with 0.
3. **TO_U16**: fill higher bits (16th ~ 31th) with 0.
4. **TO_I16**: fill higher bits (16th ~ 31th) with 0, and then if 15th bit is 1, fill higher bits with 1.
5. **NEW_LONG**: create Long_Object.
6. **COPY_LONG**: do nothing (copy Long_Object).
7. **I_NEW_LONG**: create Long_Object, and there two subcases depending on the most significant bit (31th bit).
   *  if the bit is 0, create Long_Object.
   *  if the bit is 1, create Long_Object and fill higher bits (32th ~ 63th) with 1.
8. **NEW_INT**: fill higher bits (32th ~ 63th) with 0 and create Int_Object.
9. **U32_TO_D**: unsigned int 32 to double.
10. **I32_TO_D**: signed int 32 to double.
11. **U64_TO_D**: unsigned int 64 to double.
12. **I64_TO_D**: signed int 64 to double.
13. **D_TO_U32**: double to unsigned int 32
14. **D_TO_I32**: double to signed int 32.
15. **D_TO_U64**: double to unsigned int 64.
16. **D_TO_I64**: double to signed int 64.