(--- RUTA al LIVE ) (setq clm:block-target-file "../../tmp/live.p")

...
{ onda float - onda generada }
{a
  default
  0 VINT 8000 SR 256 BUF
  -1 VMIN 1 VMAX 0 T 2 CH  1 SCOPE
}
{s
  t 9 + 440 * sr / 360 * 1 gsin*  0 OUT   ( ciclos -> grados -> gsin* )
  440 sine 1 OUT
  }

...
{ onda float - onda generada }
{a
  default
  0 VINT 8000 SR 256 BUF
  -1 VMIN 1 VMAX 0 T 2 CH 1 SCOPE
  1 DELAY 200 DTIME 60 DFEED 70 DMIX
}
{s
  t 9 + 440 * sr / 360 * 1 gsin*
  440 1 5 / + sine
  t 15 >> 2 % 0 == ?[ * ?! + ]?
  1 2 / min
  1 2 / -1 * max
  d 0 OUT 1 OUT
}

...
{ poliritmia }
{a
  1 ?[
    0 MUTE 2 CH  8000 SR 1 SCOPE 2 STYPE 900 SZOOM
    1 SAT 1 LP
    1 DELAY 30 DTIME
  ]?
}

{s
  t 10 >>
  1011 700 t 2 >> 2 %   ? &
  sine 2 * 0 OUT

  t 10 >>
  1011 101 t 9 >> 2 %   ? ^
  sine 2 * 1 OUT
}

...
{ ruidos bajos de conincidencia esporádica }
{a
  default "anom.log" 1 anomlog
  1 ?[
    1 MUTE 2 CH 8000 SR 1 SAT 0 LP
    1 DELAY 10 DTIME 60 DMIX 60 DFEED
    1 SCOPE 0 STYPE 1000 SZOOM 1 SRES
    "#AA0000" 0 SCOLOR "#00AA00" 1 SCOLOR
  ]?

}
{s
  t 5 >> t 11 >> & 13 * saw  0 OUT
  t 6 >> t 8 >> & 13 * saw  1 OUT
}

...
{ puentes desconectados }
{a
  default "anom.log" 1 anomlog
  1 ?[
    1 MUTE 2 CH 8000 SR 1 SAT 0 LP
    0 VINT -1 VMIN 1 VMAX
    1 SCOPE 2 STYPE 1000 SZOOM 1 SRES
    "#AA0000" 0 SCOLOR "#00AA00" 1 SCOLOR
  ]?

}
{s
  t sr ^ sine 0 OUT
  t sr & sine 1 OUT
}

...
{ notas de composicion }
{a
  ( seteos estables )
  1 ?[
    2 CH 8000 SR 1 SAT 0 LP
    1 VINT -1 VMIN 1 VMAX
    1 SCOPE 1000 SZOOM 1 SRES
    "#AA0000" 0 SCOLOR "#00AA00" 1 SCOLOR
  ]?

  ( seteos mutables )
  1 ?[
    1 MUTE  4 STYPE
  ]?

  ( seteos permanentes )
  0 T

  : base  t 4 >> ;
  : primera t 3 >> t 3 % * ;
  : segunda t 2 >> ;
  : tercera t 1 >>  ;

  1 =: _a  1 =: _b  1 =: _c  1 =: _d

  : rango ( v min max -- bool ) 2 nr d 2 nr < r 2 nr >= & ;
}

{s
  base
  t sr / 1 == _a &   ->Aa
  t sr / 1 3 rango   ->A
  t sr / 3 == _b &   ->Bb
  t sr / 5 ==        ->B
  t sr / 7 == _c &   ->Cc
  t sr / 7 10 rango  ->C
  t sr / 13 == _d &  ->Y
  1 ->Z

  #Aa 0 =: _a "primerrrr" _: 2 STYPE 1 ->A
  #Bb 0 =: _b "segundddd" _: 3 STYPE 1 ->B
  #Cc 0 =: _c "terceraaa" _: 0 STYPE 1 ->C
  #A primera 1 ->Z
  #B segunda 1 ->Z
  #C tercera 1 ->Z
  #Y 0 =: _d "fin." _: bye
  #Z d 255 & 0 OUT sine 3 * 1 OUT
}

...
{ controles temporales de único disparo }
{a 0 T
  1 =: _a  1 =: _b  1 =: _c
}
{b
  t sr / 1 == _a & ?[ 0 =: _a "uno" _: ]?
  t sr / 3 == _b & ?[ 0 =: _b "tres" _: ]?
  t sr / 5 == _c & ?[ 0 =: _c "cinco" _: ]?
}

...
{ generador aleatorio }
{a
  default 0 MUTE
  1 SCOPE 0 STYPE 200 SZOOM
  0 T 8000 SR 2 CH 1 SAT 128 BUF 1 VINT 0 VMIN 256 VMAX
  1 DELAY 9 DTIME 99 DFEED 40 DMIX

  0 =: ta 0 =: tb
}
{b
  t 8 tb - >> 5 %  3
  == ?[ noise 3 % noise 3 % -  =: ta ]?
  t 9 ta - >> 6 %  2
  == ?[ noise 3 % noise 3 % -  =: tb ]?
}
{s
  t 10 ta - 2 max >> 3 gsin*  2000 * 1001111'1 &
  t 10 tb - 2 max >> 3 gcos*  2000 * 1110001'1 &
  0 OUT 1 OUT
}
