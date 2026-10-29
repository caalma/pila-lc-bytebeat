(--- RUTA al LIVE ) (setq clm:block-target-file "../../tmp/live.p")

{a
  default 2 LP
  1 DELAY 23 DTIME 80 DFEED 50 DMIX
  0 DFHI 10000 DFLO
 }
{b
  t 4 >> 20 % 10 + DTIME
  t 12 >> 190 % 3000 + DFHI
  t 12 >> 390 % 100 + DFLO
}
t d 5 >> 1 nd 8 >> | * 56 & 0 OUT
t d 3 >> 1 nd 7 >> | * 65 & 1 OUT
