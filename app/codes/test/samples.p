(--- RUTA al LIVE ) (setq clm:block-target-file "../../tmp/live.p")

...
{a
    "../aud/sample3.i16" 0 smpload
    1 smploop          ( ajusta el largo al zero-crossing )
    1 smpreverse       ( invierte el sample )
    2 0 smpgain        ( amplifica x 2 )
    -1 0 smpgain       ( espeja la onda )
    "../aud/sample1.i16" 1 smpload
    0 1 r  smpmix      ( mezcla 0 sobre 1 )
}
{s
  t ~ 2 * 0 smplen  %  0 sample
  t 6 * 1 smplen  %  1 sample 10 *
  t 255256 & 4 >> 401 % 56 < ?[ r ]?
  0 OUT
  1 OUT
}

...
{a 1 DELAY 10 DTIME 90 DFEED 90 DMIX  }
t 100 t 13 >> 2 % 1 + * &
t 261 % t 145 & >>
t 13 >> 8 % 3 < ?[ r 2 / ]?
0 OUT 1 OUT

...
t 8 >> 7 & 5 % 0 samplepitch 0 OUT
t 10 >> 9 & 3 % 0 samplepitch 1 OUT

...
t 6 >> 9 & 0 samplepitch 0 OUT
t 7 >> 8 & 1 samplepitch 5 * 1 OUT

...
( reproduce una vez sola )
{a 0 T  }
{s t 1 smplen < ?[ t 1 sample 10 * ?! 0 ]? 0 OUT }

...
t 5 >> 10000 %
  t 10 >> 6 % 10000 *  +
0 sample
t 0 sample * 7 >>
d
1 OUT
t 0 sample %
0 OUT

...
{a
    0 T
    1 VINT
    "corto.raw" recfile
    recstart
}
{s
    t 800 % 100 < ?[ 0 ?! 255 ]? 0 OUT
    t 200000 > ?[ recstop ]?
}

...
{a
  "../aud/corto.i16" 0 smpload
  0 smplen .:   ( imprime "Valor: 10932"  )
}
...
{a 600 SMPFADE 1 DCB 1 SAT 0 LP 0 DELAY }
t  0 smplen % 0 sample  5 >> 0 OUT
...

...
{a 0 DELAY 1 DCB 1 SAT 0 LP }
{a
    "../aud/sample1.i16" 0 smpload
    "../aud/sample2.f32" 1 smpload
    "../aud/sample3.i16" 2 smpload
}
...
{a 232 SMPFADE 1 DCB 1 SAT 0 LP 0 DELAY }
t 3 * 2 smplen % 2 sample 0 OUT

...
{a 1 smplen :: }
t 2 >>  d 3 % * 1 smplen 30 / % t 5 >> 50000 % + 1 sample 1 << LR

...
{a 956 SMPFADE 1 DCB 1 SAT 0 LP 0 DELAY }
{s
  ( loop completo del sample 0 a velocidad normal )
   t 10 * 0  smplen %  0 sample
   t 20 * 1  smplen %  100000  + 1 sample
   t 9 * 2  smplen %  200000  + 2 sample
   +  + t  123 & -
  0 OUT
  t 171 8080 rsin* t 97 % | t 100 %  - 1 >>  251 &
  1 OUT
}

...
{s
  ( loop completo del sample 0 a velocidad normal )
   t -1 * 2  smplen %  2 sample 0 OUT
}

...
{s
  ( loop de un rango especifico del sample 1: posiciones 30 a 45 )
  t 1500000 % 300 +  0 sample 0 OUT
}

...
{s
  ( sample 0 al doble de velocidad )
  t  1 smplen % 1 sample 0 OUT
}

...
{s
  ( sample 0 al reves )
  t -1 * 1 sample 1 OUT
}

...
{s
( mezcla: sample 0 en canal 0, sample 1 en canal 1 )
t 1 smplen % 1 smplen % 1 sample 0 OUT
t -8 * 1 smplen % 1 sample 1 OUT}


...


( registro y samples )
( graba y vuelve a cargar sin tocar nada )
{a
  0 VINT 0 T

  1 =: grabando
  grabando 1 == ?[
    "./registro-03.rawi16" recfile recstart
  ]?

}
{b
  grabando 1 == ?[
    t sr 4 * >  ?[
      0 =: grabando
      recstop
    ]?
  ]?

}
{s
  t 3 >> 0 % 0 OUT
}
...

{a
  0 VINT
  "./registro-03.rawf32" 0 smpload
  "Longitud del sample:" _: e 0 smploop .: e
}
{s t 0 smplen % 0 sample 0 OUT }


...
( lectura de archivos rotos o fuera de formato
  para sonidos glitcheados )
{a 0 T 1 VINT
  "./VOCABULARIO.md" 0 smpload
  "Total de muestras del archivo 0 :" _: e 0 smploop .: e
  "./bytebeat-rpn" 1 smpload
  "Total de muestras del archivo 1 :" _: e 1 smploop .: e
}
