(--- RUTA al LIVE ) (setq clm:block-target-file "../../tmp/live.p")

...
440 sine 250 htan*
t 5 >> &
:: d 0 OUT 1 OUT

...
t 200 % 100 - 5 / 90 htan*  128 +
d 0 OUT 1 OUT

...
t 111 &
d 0 OUT 1 OUT

...
{a 80 LP }
t 5 >> 90 % 80 - * htan* 100 *
d 1 OUT 0 OUT

...
t 90 htan* :: 0 OUT

...
t 700 % t 8 >> 100 %  hsin* 0 OUT

...
t 345 %  t 9 >> t 3 >> &  8 & hcos* 0 OUT

...
t  t 30 % 80 +   & 160 min   0 OUT
t  t 130 %  & 160 min   1 OUT

...
t 255 & 64  max 0 OUT

...
t 7 * 10000 gsin*
t 10 / 1000 sqrt*
| t 2 / *
255 & 10 * 0 OUT

...
