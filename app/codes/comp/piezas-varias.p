(--- RUTA al LIVE ) (setq clm:block-target-file "../../tmp/live.p")

{a
  ( configuración general )
  default 8000 SR 2 CH 1 SAT 1 DCB 0 LP
  1 VINT 0 VMIN 255 VMAX 1 BCASC 0 T
  1 SCOPE 1600 SZOOM
  "#aa4444" 0 SCOLOR   1 0 SSTROKE
  "#44aa44" 1 SCOLOR   1 0 SSTROKE
  0 DELAY 20 DTIME 80 DFEED 90 DMIX


  ( definiciones y utilidades )

  : ** ( n -- n ) d * ;
  : LIM ( n -- ) 255 & ;
  : L ( n -- ) LIM 0 OUT ;
  : R ( n -- ) LIM 1 OUT ;
  : U ( n -- ) d L R ;
  : LR ( n -- ) d 0 OUT 1 OUT ;
  : XD ( -- ) 0 U ;
  : ign ( -- ) e 0 ;

  : errores ( n:muestra_de_espera --  ) "./errores.log" r anomlog 0 VINT ;
  : .pila ( n:magnitud --) t r % 0 == ?[ :: ]? ;

}


...
( avance infinito )
t 10000 hsin* t 191 % >> L
t 10000 gsin* t 101 % >> R

...
( desfasaje )
{a 2 CH 8000 SR 0 DCB 0 SAT 0 LP 0 T }
{s
  t 1 >> t 12 >> & t * 11 >> L
  t 3 >> t 11 >> & t * 12 >> R
}

...
( dialoogoo )
{a 2 LP }
t 1 >> t 5 %  * t 12 >> 9  % 10  *  >>  L
t 2 >> t 3 |  * t 11 >> 10 %  9  *  >>  R

...
( aspersor )
{a 2000 SZOOM 2 LP }
t 6 / d 24 / d 3 / & | 3 *  L
t 121 % t 155 % t 3 / * 127 & R

...
( murmullo )
{a
  2000 SZOOM 3 LP
 : trnsf t 4 >> 8 % 1 + nr d * * ;
}
t 2 >> 123 %   8 7 6 5 4 3 2 1 trnsf 191 ^ R
t 3 >> 123 %   1 2 3 4 5 6 7 8 trnsf 919 ^ L
<

...
( electro noise )
{a 0 LP 1 SCOPE 1000 SZOOM 0 DELAY }
t 2 /  t 4 %  * 3 >>   t 7 >> &
t 5 |  t 2 *  &   t 3 >> &
r L R

...
