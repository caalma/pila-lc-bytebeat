(--- RUTA al LIVE ) (setq clm:block-target-file "../../tmp/live.p")

...
{ ------------- definiciones y asignaciones }
...
( ":" y ";"   Definición de palabras )
{a
  : ** ( n -- n ) d * ;
  : *** ( n -- n ) d d * * ;
  2 ** 2 *** ::
}
...
( "=:"   Asignación de valores )
{a
  2 =: a  3 =: b
  a b *  a b +  a b -  a b /  b a /  ::
}
...
{a
  8000 SR 1 SCOPE 0 STYPE 1000 SZOOM
  1 SAT 1 CH 0 DELAY
}
{b
  t 10 >> 13 % =: a
  t 2 >> 11 % =: b
  a b 1 + % =: c
}
{s  t c >> t 13 >> *  30 1 + %  0 OUT  }
...
( ":="   Alias )
{a
  1 DELAY 20 DTIME 70 DMIX
  mas := +
  : limitar-espectro ( n n -- n )  d 2 nr r % mas ;
  lies := limitar-espectro
  : bloque t 16 >> 2 % ;
}
{b bloque 65 * 5 + DMIX }
{s
  t 4 >> 100 lies
  t 9 >> 50 lies &
  t 3 >> 8 lies *
  bloque ?[ 31 ?! 93 ]? lies
  0 OUT
}
...
( Representaciones numéricas con base explícita )
{a
  1 SCOPE 100 SZOOM 2 CH
  0 VMIN 255 VMAX 1 VINT
  "---Mismo valor, distintas representaciones" _: e
  255 255'9 11111111'1 ff'f 47'Z ::
}
{s
( patrones de bits visibles en binario )
t 11111111'1 & 0 OUT
( mascara hex visible )
t ff'f & 1 OUT
}

...

{ ------------- pila - informadores - no destructivos }
...
( "::"  OP_SHOW_STACK )
{a 1 2 3 :: }
...
( ".:"  OP_PRINT )
{a 1 2 3 .: }
...
{a 1 2 "tres" .: _: :: }
...
( "_:"  OP_PRINT_STR )
{a "abc" _: "xyz" _: 0 _: 1 _: 2 _: 3 _: 4 _:  }
...
( "cs"   OP_CLEAR_STACK )
{a 1 2 3 :: cs 0 :: }
...
( "ls"   OP_STACK_LEN )
{a 0 1 2 ls :: }

...
{ ------------- pila - operadores }
...
( "e"   OP_ELIM )
{a 1 2 3 e :: }
...
( "d"   OP_DUP )
{a 1 2 3 d :: }
...
( "nd"  OP_NDUP )
{a
  1 2 3 0 nd :: cs
  1 2 3 1 nd ::
}
...
( "r"   OP_ROT )
{a 1 2 3 r :: }
...
( "n@"  OP_NROT )
{a 1 2 3 2 nr :: }

...
{ ------------- estructuras de flujo }
...
( "?"   OP_IF_THEN_ELSE )
{a 2 3 -1 ?  2 3 1 ?  2 3 0 ? :: }
...
( "?["  OP_IF_START )
( "?!"  OP_ELSE_START )
( "]?"  OP_IF_END )
{b t 2 % ?[ "impar" ?! "par" ]? _:  }
...
( "@["  OP_LOOP_START )
( "@!"  OP_LEAVE )
( "]@"  OP_LOOP_END )
( "i"   OP_LOOP_I )
( "j"   OP_LOOP_J )
{a 9 0 1  @[ i :: ]@  }
...
{a
  8 1 2  @[
    4 -1 1  @[
      i j *  ::
    ]@
  ]@
}
...
{a
  10 1 1 @[
    i :: d 2 >=
    ?[ @! ]?
  ]@
}
...
( escape pre cuelgue por loop excesivo )
{a #A 1 ->A ( antes: cuelgue eterno; ahora: 1 anomalia y sigue ) }
{b 2000000000 0 1 @[ ]@ ( abandona a los 500k pasos ) }
{s 440 sine 4 / 0 OUT ( el audio sigue vivo ) }
...
( "#" y "->"       OP_ANCHOR  y OP_JUMP )
{a 1 SCOPE 1 SAT }
{b t 12 >> 19 & LP }
{s
  1 triangle t t 10101 & 9 logn* >> 2 %  ?[ ~ ]?
  t 11 %  2 <  ->A
  t 17 %  4 <  ->B
  t 31 %  8 <  ->C
  1 ->Z
  #A cs t 3 <<  1 ->Z
  #B cs t 5 >> t 5 << & 1 ->Z
  #C cs t t 3 >> * 1 ->Z
  #Z 1 >> 255 % 0 OUT
}


...
{ ------------- aritmética - operadores básicos }
...
( "+"   OP_ADD )
{a 2 3 + :: }
...
( "-"   OP_SUB )
{a 2 3 - :: }
...
( "*"   OP_MUL )
{a 2 3 * :: }
...
( "/"   OP_DIV )
{a
  0 VINT
  2 3 /
  3 5 /  2 7 / +
  3 5 /  2 7 / -
  3 5 /  2 7 / *
  1 VINT
  2 3 /
  ::
}
...
( "%"   OP_MOD )
{a
  0 VINT 3 2 %
  1 VINT 3 2 %
  ::
}
...
{a
  0 VINT
  2000000000 2 * .:   ( Valor: 4e+09 )
  7 2 / .:            ( Valor: 3.5 )
  1 VINT
  2000000000 2 * .:   ( Valor: -2.14748e+09 )
  7 2 / .:            ( Valor: 3 )
}
...

{ ------------- aritméticas - funciones trigonométricas }
...
( "fs"   OP_SIN )
{a 10000 =: m   90 m gsin*  45 m gsin*  30 m gsin*  0 m gsin* :: }
...
( "fc"   OP_COS )
{a 10000 =: m   90 m gcos*  45 m gcos*  30 m gcos*  0 m gcos* :: }
...
( "ft"   OP_TAN )
{a 10000 =: m   90 m gtan*  45 m gtan*  30 m gtan*  0 m gtan* :: }
...
( "fsr"  OP_SIN_RAD )
{a
  10000 =: m   314159 =: pi
  pi 2 / m rsin*  pi 3 / m rsin*  pi 4 / m rsin*  0 m rsin* ::
}
 ...
( "fcr"  OP_COS_RAD )
{a
  10000 =: m   314159 =: pi
  pi 2 / m rcos*  pi 3 / m rcos*  pi 4 / m rcos*  0 m rcos* ::
}
...
( "ftr"  OP_TAN_RAD )
{a
  10000 =: m   314159 =: pi
  pi 2 / m rtan*  pi 3 / m rtan*  pi 4 / m rtan*  0 m rtan* ::
}
...
( "fsh"  OP_SINH )
{a
  10000 =: m
  90 m hsin*  10 m hsin*  3 m hsin*  1 m hsin*  0 m hsin* ::
}
...
( "fch"  OP_COSH )
{a
  10000 =: m
  90 m hcos*  10 m hcos*  3 m hcos*  1 m hcos*  0 m hcos* ::
}
...
( "fth"  OP_TANH )
{a
  10000 =: m
  90 m htan*  10 m htan*  3 m htan*  1 m htan*  0 m htan* ::
}

...
{ ------------- artimética - funciones }
...
( "va"    OP_ABS )
{a 1 abs -1 abs :: }
...
( "fmin"  OP_MIN )
{a 1 2 min -1 1 min :: }
...
( "fmax"  OP_MAX )
{a 1 2 max -1 1 max :: }
...
( "fl"    OP_LOG )
{a 2 1000000 logn* 2 100 logn* 2 10 logn* :: }
...
( "fe"    OP_EXP )
{a 2 100000 exp* 2 100 exp* 2 10 exp* :: }
...
( "fr"    OP_SQRT )
{a 2 100000 sqrt* 2 100 sqrt* 2 10 sqrt* :: }

...
{ ------------- aritmética/binaria - operadores }
...
( "&"     OP_AND )
{a 1 1 &  0 1 & 0 0 & :: }
...
( "|"     OP_OR )
{a 1 1 |  0 1 | 0 0 | :: }
...
( "^"     OP_XOR )
{a 1 1 ^  0 1 ^ 0 0 ^ :: }
...
( "~"     OP_NEGATE )
{a -1 ~ 0 ~ 1 ~ 2 ~ ::  }
...
( "<<"    OP_LSHIFT )
{a 1 1 <<  1 2 << 0 1 <<  ::  }
...
( ">>"    OP_RSHIFT )
{a 4 1 >>  4 2 >> 4 3 >> ::  }
...
{a
  1 32 << .:        ( Valor: 0 )
  1 33 << .:        ( Valor: 0 )
  256 40 >> .:      ( Valor: 0 )
  -256 40 >> .:     ( Valor: -1 )
  1 31 << .:        ( Valor: -2.14748e+09: el wrap int32 de siempre )
  0 VINT 1 40 << .: ( Valor: 0: igual en modo float )
  1 VINT
}
...
{a
  default
  0 VINT
  1000 1 exp* 1 & .:              ( Valor: 0   — inf & 1 )
  -1 1 logn* 1 | .:                ( Valor: 1   — NaN | 1, NaN opera como 0 )
  1000 1 exp* ~ .:                ( Valor: -1  — ~0 )
  1000 1 exp* 1000 1 exp* ^ .:      ( Valor: 0   — inf ^ inf )
  5000000000 3 & .:             ( Valor: 0   — caso normal intacto en float )
}

...
{a
  default
  "modo float" _: e
  0 VINT
  1000 1 exp* 1 + .:
  -1 1 logn* 1 + .:
  1000 1 exp* 1 / .:
  -1 1 logn* abs .:
  1 2 3  -1 1 logn* ::
  1 1000 1 exp* << .:
  cs
  "modo entero" _: e
  1 VINT
  1000 1 exp* 1 + .:
  -1 1 logn* 1 + .:
  1000 1 exp* 1 / .:
  -1 1 logn* abs .:
  1 2 3  -1 1 logn* ::
  1 1000 1 exp* << .:
}

...
{a
  default
  ( A: el productor silencia en modo entero )
  1 VINT 1000 1 exp* .: e                 ( Valor: 127.5 )

  ( B: inf crudo en division entera = anomalia, sin SIGFPE )
  ( Con 1 STRICT falla )
  0 VINT 1000 1 exp* 1 VINT 7 r / .: e e ( Valor: inf, +1 anomalia; division no-op )

  ( C: indice NaN = INT_MIN = anomalia )
  1 2 3 0 VINT -1 1 logn* 1 VINT :: cs  ( +1 anomalia; Pila: 1 2 3 )

  ( D: cuentas de shift )
  1 1000 1 exp* << .: e                   ( Valor: 0: cuenta 127 -> clamp 63 )
  1 0 VINT 1000 1 exp* 1 VINT << .: e     ( Valor: 1: cuenta inf -> INT_MIN -> clamp 0 )
}

...
{ ------------- lógica - comparadores }
...
( "=="    OP_EQUAL )
{a 1 1 == .: 1 0 == .: }
...
( "!="    OP_NOT_EQUAL )
{a 1 1 != .: 1 0 != .: }
...
( ">"     OP_GREATER_THAN )
{a 0 1 > .: 1 0 > .: 1 1 > .: }
...
( "<"     OP_LESS_THAN )
{a 0 1 < .: 1 0 < .: 1 1 < .: }
...
( ">="    OP_GREATER_EQUAL_THAN )
{a 0 1 >= .: 1 0 >= .: 1 1 >= .: }
...
( "<="    OP_LESS_EQUAL_THAN )
{a 0 1 <= .: 1 0 <= .: 1 1 <= .: }
...

{ ------------- audio - getters }
...
( "t"     OP_T )
{b t .: }
...
( "sr"    OP_SR_GET )
{a sr .: }
...
( "ch"    OP_CH_GET )
{a ch .: }
...
( "vmin"  OP_VMIN_GET )
{a vmin .: }
...
( "vmax"  OP_VMAX_GET )
{a vmax .: }
...
( "buf"   OP_BUF_GET )
{a buf .: }

...
{ ------------- audio - setters }
...
( "T"     OP_TIME_SET )
{a 0 T }
{b
  t .:
  t 10000 >= ?[ 0 T ]?
}
...
( "SR"    OP_SR_SET )
{a
  1000 =: _sr
  _sr SR  1 CH
}
{b
  t _sr %
  0 == ?[ _sr 1000 + =: _sr _sr SR ]?
  _sr 48000 > ?[ 1000 =: _sr _sr SR ]?

}
{s t 255 & 0 OUT }
...
( "CH" OP_CH_SET )
{a 1 CH 1 SCOPE 1 STYPE 8000 SR 0 T }
{b
  ch 1 == ?[
    t 8000 2 * > ?[ 2 CH ]?
  ]?
}
{s
  t 3 / t 2 / | 255 & 0 OUT
  ch 2 == ?[
    t 5 / t 3 / | 255 & 1 OUT
  ]?
  }
...
( "VMIN"  OP_VMIN_SET )
( "VMAX"  OP_VMAX_SET )
{a
  8421 =: _sr  0 =: _vmin  256 =: _vmax
  _sr SR  _vmin VMIN _vmax VMAX
  1 SCOPE  5 STYPE  1 SRES  1 0 SSTROKE  "#ffff00" 0 SCOLOR
  2 CH 1 SAT 1 DCB 0 T
}
{b. _vmin VMIN _vmax VMAX }
{s
  t _sr 13 * < ?[
    t _sr 6 * == ?[ -512 =: _vmin  512 =: _vmax ]?
    t _sr 10 * == ?[ -128 =: _vmin  128 =: _vmax ]?
    t _sr 12 * == ?[ 0 =: _vmin  63 =: _vmax ]?
  ]?
  t 1 >> t 1 << ^ _vmax & _vmin -
  d 0 OUT
  t 3 >> | 1000 % _vmin -
  _vmax &
  1 OUT
}
...
( "BUF"   OP_BUF_SET )
{a
  100 =: _buf  0 =: _bufmodif  8000 =: _sr
  _buf BUF _sr SR
  1 SCOPE 1000 SZOOM 2 STYPE
  2 CH 0 VMIN 256 VMAX 0 T
}
{b.
  _bufmodif 0 == ?[
    t _sr 5 * > ?[
      1 =: _bufmodif
      1000 =: _buf  _buf BUF ]?
  ]?
}
{s
  t 4 >> d 99999 logn* * 127 &  0 OUT
  t 5 >> d 99999 sqrt* * 127 &  1 OUT
}
...
( "VINT"  OP_VINT_SET )
{a
  0 =: _float
  _float 0 == VINT ( operaciones de punto flotante )
  _float 1 == ?[ -1  1 ?! -128 128 ]? VMAX VMIN
  255 BUF 1 SAT 2 LP 1 DCB 0 T 8000 SR 0 DELAY
  1 SCOPE 1000 SZOOM 3 STYPE
  "#00ff00" 0 SCOLOR "#0000ff" 1 SCOLOR
}
{s
  _float 1 == ?[
    3 7 / 301 square * 0 OUT
    2 11 / 401 saw * 1 OUT
    ?!
    t 2 >> t 7 >> - 777 & 0 OUT
    t 3 >> t 5 >> * 11 / 555 & 1 OUT
  ]?
}

...
{ ------------- salida - setters }
( "OUT"   OP_OUT )
{a 1 CH 1 SCOPE 800 SZOOM 0 DCB 0 SAT 0 LP }
{s t 4 >> t 3 >> & t 2 >> * 255 & 0 OUT }
...
( "DCB"   OP_DCB_SET )
{a 1 CH 1 SCOPE 800 SZOOM 1 DCB 0 SAT 0 LP }
{s t 4 >> t 3 >> & t 2 >> * 255 & 0 OUT }
...
( "SAT"   OP_SAT_SET )
{a 1 CH 1 SCOPE 800 SZOOM 1 DCB 1 SAT 0 LP }
{s t 4 >> t 3 >> & t 2 >> * 255 & 0 OUT }
...
( "LP"    OP_LP_SET )
{a 1 CH 1 SCOPE 800 SZOOM 1 DCB 1 SAT 2 LP }
{s t 4 >> t 3 >> & t 2 >> * 255 & 0 OUT }
...
( "MUTE"  OP_MUTE_SET )
{a
  2 CH 1 SCOPE 800 SZOOM 1 STYPE 1 DCB 1 SAT 1 MUTE
  : pat ( n -- n )t 11 >> & sine 3 * ;
}
{s
  100 pat 0 OUT
  290 pat 1 OUT
}

...
{ ------------- visualizador de onda - setters }
( "SCOPE"    OP_SCOPE_SET )
( "SZOOM"    OP_ZOOM_SET )
( "SCOLOR"   OP_SCOLOR_SET )
( "SSTROKE"  OP_SSTROKE_SET )
( "STYPE"    OP_STYPE_SET )
( "SFFT"     OP_SFFT_SET )
( "SRES"     OP_SRES_SET )
{a
  default
  2 CH 0 T
  1 SCOPE 1000 SZOOM 0 STYPE 1 SRES 256 SFFT
  "#dd0000" 0 SCOLOR  "#0088ff" 1 SCOLOR
  3 0 SSTROKE  1 1 SSTROKE
}
{b t 14 >> 6 % STYPE :: }
{s
  100 sawpb  7 / 0 OUT
  100 squarepb  7 / 1 OUT
}
...
{a
  default
  2 CH 0 T
  1 SCOPE 4 STYPE 10 SZOOM 10 SFFT
  0 SDECOR
  0 VINT 1 SAT 2 LP
  "#00ff88" 0 SCOLOR  2 0 SSTROKE
  "#001100" SBGCOLOR
}
{s
  t 10 >> 100 % 80 + sine 3 / 0 OUT
  t 10 >> 100 % 60 + sine 3 / 1 OUT      ( Lissajous 3:2 que se abs encendiendo y apagando )
}
...
( "SWINX"     OP_SWINX_SET )
( "SWINY"     OP_SWINY_SET )
( "SWINW"     OP_SWINW_SET )
( "SWINH"     OP_SWINH_SET )
{a
  default
  1 SCOPE
  0 SWINX
  0 SWINY
  800 SWINW
  400 SWINH
  0 SFULLSCR
}
...
( "SFULLSCR"  OP_SFULLSCR_SET )
{a 1 SCOPE }
{b t 13 >> 2 % SFULLSCR }
...
( "SDECOR"    OP_SDECOR_SET )
{a 1 SCOPE }
{b t 13 >> 2 % SDECOR }
...
( "scrw"      OP_SCRW_GET )
( "scrh"      OP_SCRH_GET )
{a
  "Ancho y alto de pantalla:" _: e
  scrw scrh ::
}
...
{a
  1 SCOPE
  scrw 2 / d SWINW 2 / SWINX
  scrh 2 / d SWINH 2 / SWINY
}
...
( "SBGCOLOR"  OP_SBGCOLOR_SET )
( "SWINTOP"   OP_SWINTOP_SET )
( "winx"      OP_WINX_GET )
( "winy"      OP_WINY_GET )
( "winw"      OP_WINW_GET )
( "winh"      OP_WINH_GET )
{a
  1 SCOPE 0 SDECOR 1 SWINTOP
  "#112233" SBGCOLOR         ( fondo azul noche )
  winx winy :: e e
  scrw 2 / winw 2 / - SWINX   ( centrar a mano respetando el ancho actual )
  scrh 2 / winh 2 / - SWINY
  winx winy :: e e
}

...
{ ------------- delay - setters }
( "DELAY"    OP_DELAY_SET )
( "DTIME"    OP_DTIME_SET )
( "DFEED"    OP_DFEED_SET )
( "DMIX"     OP_DMIX_SET )
( "DFLO"     OP_DFLO_SET )
( "DFHI"     OP_DFHI_SET )
{a
  default
  1 CH 0 T
  1 SCOPE 1000 SZOOM 0 STYPE 1 SRES "#00aa00" 0 SCOLOR
  1 DELAY 10 DTIME 98 DFEED 60 DMIX 900 DFLO 50 DFHI
}
{b t 13 >> t 14 >> & 9 % 12 + DTIME }
{s t 4 >> 64 & 0 OUT }

...
{ ------------- samples - operadores }
( "smpload"     OP_SMP_LOAD )
( "smpremove"   OP_SMP_REMOVE )
( "smploop"     OP_SMP_LOOP )
( "smpreverse"  OP_SMP_REVERSE )
( "smpgain"     OP_SMP_GAIN )
( "smpmix"      OP_SMP_MIX )
{a
  default
  1 SCOPE 2 CH
  "../aud/sample3.i16" 0 smpload
  1 smploop                       ( ajusta el largo al zero-crossing )
  1 smpreverse                    ( invierte el sample )
  12 0 smpgain                     ( amplifica x 2 )
  -1 0 smpgain                    ( espeja la onda )
  "../aud/sample1.i16" 1 smpload
  0 1 r  smpmix                   ( mezcla 0 sobre 1 )
}
{s
  t ~ 2 * 0 smplen  %  0 sample
  t 6 * 1 smplen  %  1 sample 10 *
  t 255256 & 4 >> 401 % 56 < ?[ r ]?
  20 / 0 OUT  5 /  1 OUT
}
...
( "smplive"     OP_SMP_LIVE )
{a
  default
  22050 SR 1 CH 256 BUF
  1 SCOPE 1000 SZOOM
}
{b
  t sr buf / sr buf % * %  0
  == ?[ sr 2 * 0 0 smplive "capturando" _: e ]?
  ( 1 s de vivo al slot 0, cada segundo )
}
{s
  t sr 2 * % sr 2 / < ?[
    ( primera mitad del segundo: vivo )
    220 sine 4 / 0 OUT
  ?!
  ( segunda mitad: el segundo congelado )
    1 0 samplepitch 0 OUT
  ]?
}

...
{ ------------- samples - getters }
...
( "sample"     OP_SMP_GET )
{a
  default
  1 SCOPE 4 STYPE 2 CH
  "../aud/sample3.i16" 0 smpload
}
{s
  t 3 /  0 sample 0 OUT
  t 4 /  0 sample 1 OUT
}
...
( "smplen"     OP_SMP_LEN )
{a
  1 SCOPE 1 CH
  "../aud/sample3.i16" 0 smpload
  0 smplen ::
}
{s t 0 smplen 10 / % 5000 + 0 sample 0 OUT }

...
( "samplepitch"     OP_SMP_PITCH_GET )
{a
  1 SCOPE 1 CH
  "../aud/sample3.i16" 0 smpload
}
{s 2 0 samplepitch 3 / 0 OUT }

...
{ ------------- setters - samples }
...
( "SMPFADE"    OP_SMP_FADE_SET )
{a
  1 SCOPE 1 CH 5000 SMPFADE 2 LP
  "../aud/corto.i16" 0 smpload
  0 smplen ::
}
{b t 14 >> 2 % 5000 * SMPFADE }
{s t 0 sample 3 / 0 OUT }
...
( "smpreset"   OP_SMP_RESET )
{a
  "../aud/sample1.i16" 0 smpload
  "../aud/corto.i16" 1 smpload
  0 smplen  1 smplen
  smpreset
  0 smplen  1 smplen
  ::
}


...
{ ------------- generadores - osciladores }
...
( "sine"       OP_SINE )
{s 321 sine 0 OUT }
...
( "triangle"   OP_TRIANGLE )
{s 321 triangle 0 OUT }
...
( "saw"        OP_SAW )
{s 321 saw 0 OUT }
...
( "sawpb"      OP_SAW_PB )
{s 321 sawpb 0 OUT }
...
( "square"     OP_SQUARE )
{s 321 square 0 OUT }
...
( "squarepb"   OP_SQUARE_PB )
{s 321 squarepb 0 OUT }
...
( "noise"      OP_NOISE )
{s noise 0 OUT }
...
{ ------------- grabación - operadores }
...
( "recfile"     OP_RECFILE )
( "recstart"    OP_RECSTART )
( "recstop"     OP_RECSTOP )

{a
  0 =: cnt
  "registro.wav" recfile recstart
}
{b
  cnt .:
  cnt 1 + =: cnt
  cnt 100 > ?[ recstop bye ]?
}
{s
  t 2 >> 255 & 0 OUT
  t 3 >> 255 & 1 OUT
}

...
{ ------------- aplicación - controles }
...
( "bye"   OP_BYE )
{a "chau" _: bye }
...
( "reload"   OP_RELOAD )
{a
  0 mem 0 == ?[ default 0 =: mem ]?
  "Recarga" _: e mem .:
}
{b t 13 >> 3 & 3 == ?[
    mem 1 + =: mem
    0 T
    reload
]? }
...
( "default"   OP_DEFAULT )
( "log"       OP_ANOM_LOG )
( mesa limpia y captura de anomalias por 8 segundos )
{a
  default
  "anom.log" 1 anomlog
  1 SCOPE
}
{s
  1 0 / 0 OUT        ( division por cero: una anomalia por muestra, todas al anomlog )
}
...
( "STRICT"     OP_STRICT_SET )
{a 1 STRICT 1 2 + (1) 0 / }
...
{a 1 STRICT 1 (1) + }
...
{a 1 STRICT
  100 0 1 @[ 10 0 1 @[ 1 0 / ]@ ]@
  ( anomalia anidada: el reporte muestra los 2 frames )
}

...
( Comportamiento numérico - Desbordes )
{a
  9999999999999999999 .:  ( 19 digitos: antes UB, ahora wrap definido )
  2000000000 2 * .:       ( plegado en parse: antes UB, ahora wrap )
  ZZZZZZZZZZZ'Z .:        ( base 62, 11 digitos: idem )
}

...
( Recursión mediante prevención de inlineización - ',_' )
{a
  default
  : fact ( n -- n )
    d 1 > ?[
      d 1 - ,_ fact *
    ]?
  ;
  5 ,_ fact .:        ( Valor: 120 — recursion via cita )
}

...
( "showlimits"   OP_SHOW_LIMITS )
{a showlimits }

...
( Límites numéricos en ententos y punto flotante. )
{a
  1 VINT
  1 32 << .: e        ( Valor: 1   — 32 & 31 = 0 )
  1 33 << .: e        ( Valor: 2   — 33 & 31 = 1 )
  256 40 >> .: e      ( Valor: 1   — 40 & 31 = 8 )
  1 -1 << .: e        ( Valor: -2.14748e+09 — -1 & 31 = 31 )
  0 VINT
  1 32 << .: e        ( Valor: 0   — float satura )
  256 40 >> .: e      ( Valor: 0   — float satura )
  1 VINT
}
