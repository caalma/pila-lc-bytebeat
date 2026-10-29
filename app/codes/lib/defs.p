(--- RUTA al LIVE ) (setq clm:block-target-file "../../tmp/live.p")

{a
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
