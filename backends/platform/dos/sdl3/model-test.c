// Independent-model tests of three SDL3 DOS arithmetic changes (ring cursors, DMA region pick, SB time constant).
// The functions are copies of the code in SDL_dosaudio_sb.c / SDL_dos.c (typed with the same widths); the models use 64-bit math.
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t Uint32; typedef uint8_t Uint8;
#define clamp(x,a,b) ((x)<(a)?(a):((x)>(b)?(b):(x)))
// --- ring: avail = (int)(Uint32)(write-read); copy position = cursor & mask
static int ring_avail(Uint32 w, Uint32 r){ return (int)(Uint32)(w - r); }
// --- DMA region pick (SDL_dos.c)
static int pick(uint32_t physical, int len, uint32_t *off_out){
  const uint32_t up = ((physical + 0xFFFF) & ~0xFFFFu) - physical;
  const uint32_t offsets[3] = { 0, (uint32_t)len, up };
  for (int i = 0; i < 3; i++){ uint32_t off = offsets[i];
    if (off + (uint32_t)len <= (uint32_t)len * 2 && ((physical + off) >> 16) == ((physical + off + (uint32_t)len - 1) >> 16)) { *off_out = off; return 1; } }
  return 0; }
// --- time constant
static void tcalc(int version, int minor, int channels, int freq, Uint8 *tc_out, int *real){
  int hs = (version > 2) || (version == 2 && minor >= 1); int max_hw = hs ? 44100 : 23000;
  int hw = channels * freq; hw = clamp(hw, 4000, max_hw);
  int tc = 256 - ((1000000 + (hw / 2)) / hw);
  if (!hs && tc > 211) tc = 211;
  tc = clamp(tc, 6, 255); *tc_out = (Uint8)tc; *real = (1000000 / (256 - tc)) / channels; }
int main(){
  uint64_t n=0, bad=0;
  // ring: random start cursors incl. near 2^32, random fill/drain steps; model = exact 64-bit counters
  srand(1);
  for (int t=0;t<2000;t++){
    uint64_t w64 = ((uint64_t)rand()<<16 ^ rand()) & 0xFFFFFFFFull; if (t%3==0) w64 = 0xFFFFFFFFull - (rand()%100000);
    uint64_t r64 = w64; Uint32 w=(Uint32)w64, r=(Uint32)r64;
    for (int k=0;k<2000;k++){
      int chunk = 1+rand()%8192; uint64_t used = w64 - r64;
      if (rand()&1){ if (used + chunk <= 40000){ w64+=chunk; w+=chunk; } }
      else { if (used >= (uint64_t)chunk){ r64+=chunk; r+=chunk; } }
      n++; if (ring_avail(w,r) != (int)(w64 - r64)) bad++;
      if ((int)(w & 16383) != (int)(w64 & 16383)) bad++;
    }
  }
  printf("ring cursors: %llu checks, %llu bad\n",(unsigned long long)n,(unsigned long long)bad);
  // DMA: every segment (0..0xFFFF) x every len 1..65536 step 7 + the powers/edges: result never crosses a 64K page and is found whenever one exists
  uint64_t dn=0, dbad=0, dnull=0;
  for (uint32_t seg=0; seg<0x10000; seg+=1){
    uint32_t phys=seg*16; if (phys>0xFFFFFF) break;
    for (int len=1; len<=65536; len += (len<2048?1:97)){
      uint32_t off; int ok = pick(phys,len,&off); dn++;
      // model: exists a start in [0, len] (in the 2*len block) with no crossing?  brute force on page of candidates
      int exists=0; for (uint32_t o=0;o<=(uint32_t)len && !exists;o+= (len>4096?251:1)){ if (((phys+o)>>16)==((phys+o+len-1)>>16)) exists=1; }
      if (ok){ if (((phys+off)>>16)!=((phys+off+len-1)>>16) || off+(uint32_t)len>(uint32_t)len*2) dbad++; }
      else { dnull++; if (exists) dbad++; }
    }
  }
  printf("DMA region: %llu cases, %llu returned no region, %llu bad\n",(unsigned long long)dn,(unsigned long long)dnull,(unsigned long long)dbad);
  // time constant: all rates 1..96000, channels 1,2, DSP 1.0 .. 4.x; model = brute force best tc in [6,255] by real-rate error
  uint64_t tn=0,tbad=0; double worst=0;
  for (int ver=1; ver<=3; ver++) for (int minor=0; minor<=1; minor++) for (int ch=1; ch<=2; ch++) for (int f=1; f<=96000; f++){
    Uint8 tc; int real; tcalc(ver,minor,ch,f,&tc,&real); tn++;
    int hs = (ver>2)||(ver==2&&minor>=1);
    if (tc<6) tbad++;                       // no wrap/underflow
    if (!hs && tc>211) tbad++;              // high-speed selected only with DSP >= 2.01
    int hw=clamp(ch*f,4000,hs?44100:23000);
    // nearest time constant by brute force
    int best=-1; double be=1e18; for (int c=6;c<=255;c++){ double rate=1000000.0/(256-c); double e=rate>hw?rate-hw:hw-rate; if (!hs&&c>211) break; if (e<be){be=e;best=c;} }
    if (abs((int)tc - best) > 1) tbad++;    // rounding of 1e6/hw to an integer divisor: at most one step from the exact nearest
    if (real != (1000000/(256-tc))/ch) tbad++;
    double err = (double)(real*ch - hw)/hw; if (err<0) err=-err; if (err>worst) worst=err;
  }
  printf("time constant: %llu cases, %llu bad, worst rate error %.2f%%\n",(unsigned long long)tn,(unsigned long long)tbad,worst*100);
  return bad||dbad||tbad;
}
