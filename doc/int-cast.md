## Specification of Integer cast (narrowing, widening)

Byte, Int16 and Uint16 integer is internally represented by 32bit.


| b/a        | **Int32**  | **Int64**  | **Float**  |
|------------|------------|------------|------------|
| **Int32**  |   --       |   (1)      |   (3)      |
| **Int64**  |   (2)      |   --       |   (4)      |
| **Float**  |   (5)      |   (6)      |   --       |


1. **I32_TO_I64**: create Long_Object, and there two subcases depending on the most significant bit (31th bit).
   *  if the bit is 0, create Long_Object.
   *  if the bit is 1, create Long_Object and fill higher bits (32th ~ 63th) with 1.
2. **I64_TO_I32**: fill higher bits (32th ~ 63th) with 0 and create Int_Object.
3. **I32_TO_D**: signed int 32 to double.
4. **I64_TO_D**: signed int 64 to double.
5. **D_TO_I32**: double to signed int 32.
6. **D_TO_I64**: double to signed int 64.