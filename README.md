zst
---

As compressors go, `zstd` is awesome.  It's very fast while beating any
commonly used alternative except `xz` in compression ratio.  But alas,
its command-line tool leaves much to be wished for:
 * its package takes over 2MB, 1.2MB for the main executable
 * it fails to remove files it's done with
 * its compression levels use a different scale than the rest of the world
   (1..19 sometimes 22 instead of 1..9)
 * turning off its chattiness makes you lose warnings

The first bit is a blocker towards making that tool a guaranteed
(“*essential*”) part of a Linux distribution — it'd bloat the size of
minimal systems (eg. containers) too much.

The other bits sometimes interfere with making zstd a drop-in replacement:
in most places you can insert arbitrary arguments but it's not always the
case.

On the other hand, `libzstd` is **already** essential!  And while we're
here, there's three other essential compressor libraries: `zlib` `libbz2`
`liblzma` (xz).  Why not provide all four from a single small binary?

Status
------

This project is only an early (but working!) stab.

 * [ ] guess algorithm via header
 * [ ] threaded [de]compression
 * [ ] threading when multiple files
 * [ ] sparse files
 * [ ] io optimizations
 * [ ] decent test coverage
 * [ ] dlopen non-essential compressors?
 * [ ] behave according to argv[0]
