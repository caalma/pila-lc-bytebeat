(--- RUTA al LIVE ) (setq clm:block-target-file "../../tmp/live.p")

...
{a 1 SCOPE 5 STYPE  "#ff1100" 0 SCOLOR "#0035ff" 1 SCOLOR }

...
{a 8000 SR 1 LP 1 SAT 1 VINT 511 VMAX 0 VMIN }
20 square  23 squarepb &   t 4 >> &
d ~ 0 OUT 1 OUT

...
{a 8000 SR 20 LP 1 SAT 1 VINT 256 VMAX 0 VMIN }
t 4 >> 321 % 1234 gtan*
t 7 >> 4 % 127 + &
d 0 OUT ~ 1 OUT

...
{a 1 LP 1 DCB 1 VINT 256 VMAX 0 VMIN
   t 3 >> 20 % 1 + 1000 *  SR
}
300 sine 3 /
t 5 >> &
500 sine 5 /
t 8 >> &
+
d 0 OUT 1 OUT

...
{a 8000 SR 2 CH 0 VINT 1 LP }
t 2 >>  5003 & 441 | ~ sine 255 & 0 OUT
t 2 >> 10003 ^ 881 | ~ sine 255 & 1 OUT

...
{a 8000 SR 1 CH 0 VINT
  1 SCOPE 5 STYPE  "#ff0000" 0 SCOLOR "#0000ff" 1 SCOLOR
}
1000 square noise / 10 * 0 OUT

...
{a -1 VMIN 1 VMAX 0 VINT 1000 SR 2 CH }
t 1 + 8 >> 2 %   t 800  % sine  r OUT
t     8 >>   2 %   t 2 * sine t 3 >> 5 * sine |  r OUT

...
{a -1 VMIN 1 VMAX 0 VINT 1000 SR 2 CH }
t 3 >> 7 * t 5 >> & t 3 * t 13 >> & | sine 0 OUT
t 2 >> 7 * t 3 >> & t 5 * t 11 >> & | saw  1 OUT

...
{a
  4000 SR 2 CH 0 VINT
  : te t 2 << t  t 13 >> 9 % >> / ;
}
te 17 * sine 10 *  0 OUT
te 19 * saw  10 * 1 OUT


...
{a 2000 SR 2 CH 0 VINT }
t 4 << sr | sine 2 / 0 OUT
t 4 >> sr & sine 2 / 1 OUT

...
{a 1000 SR 2 CH 1 VINT }
t 139 sine
t t 12 >>  sine
+ r 2 +  % 1 +  / 128 &  d -1 *
0 OUT 1 OUT
...
{a 2000 SR 1 CH 0 VINT -1 VMIN 1 VMAX }
t 2 >>  t 77 square 2 + / * 0 OUT

...
{a 2000 SR 2 CH 0 VINT -1 VMIN 1 VMAX }
t 3 >> 22 & saw 50 *
t 2 >> 33 & saw 50 *
0 OUT 1 OUT

...
{a 2000 SR 2 CH 0 VINT -1 VMIN 1 VMAX }
t 5 >> sr 1 - % sine t 2 >> 8 % *
t 4 >> sr     % sine t 2 >> 8 % *
0 OUT 1 OUT

...
{a 1000 SR 0 VINT 2 CH -1 VMIN 1 VMAX  1 SCOPE 5 STYPE }
t 10000 t sr 2 * / 5 % -  & sine      0 OUT
t 10000 t sr 2 * / 5 % +  & squarepb  1 OUT

...
{a 5000 SR 0 VINT 2 CH -1 VMIN 1 VMAX  1 SCOPE 5 STYPE }
t noise 2 + %   t noise 2 + %   * 5 /
d 0 OUT  ~ *  1 OUT

...
{a 2000 SR 2 CH 0 VINT -1 VMIN 1 VMAX }
t 4 >>  noise 10 * % d t 1 >> 20 % -
0 OUT 1 OUT

...
{a 1000 SR 0 VINT 2 CH -1 VMIN 1 VMAX  1 SCOPE 5 STYPE }
{a
  ( cambia la dinámica según el bloque en:
  {a } solo se establece un valor por evaluación y permanece estable
  {b } una vez por buffer,  variando cíclicamente
  {s } una vez por muestra, haciendo un barrido más intenso
  )
  t 2 >> 120 % =: nA
  t 4 >> 121 % =: nB
  t 8 >> 122 % =: nC
  t 16 >> 123 % =: nX
}
nA sawpb 4    *  nB triangle  4 * -
nC squarepb 4 *  nD sine      4 * +
0 OUT 1 OUT

...
