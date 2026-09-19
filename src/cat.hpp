
#pragma once
#include <cstdint>
#include <vector>

inline std::vector<uint32_t> catFrame(int phase) {
  const uint32_t outline=0xff493738, orange=0xffeeaa55, light=0xffffd992;
  const uint32_t stripe=0xffbb713f, pink=0xffe6a09a, cream=0xffffecd2;
  std::vector<uint32_t> p(32*32, 0);
  auto box=[&](int x,int y,int w,int h,uint32_t c) {
    for(int j=y;j<y+h;++j) for(int i=x;i<x+w;++i)
      if(i>=0&&i<32&&j>=0&&j<32) p[j*32+i]=c;
  };
  // Tail, body, neck, and pointed ears.
  box(1,11,3,10,outline); box(2,9,4,4,outline);
  box(3,18,5,6,outline); box(2,12,1,8,orange);
  box(3,10,2,2,orange); box(4,20,5,3,orange);
  box(7,17,17,10,outline); box(6,19,20,6,outline);
  box(8,18,16,8,orange); box(7,20,18,4,orange);
  box(12,17,11,2,orange); box(11,24,12,2,light);
  box(20,8,11,13,outline); box(19,11,13,9,outline);
  box(20,5,3,6,outline); box(23,7,2,4,outline);
  box(27,5,3,6,outline); box(26,7,5,4,outline);
  box(21,8,2,3,pink); box(28,7,1,4,pink);
  box(21,10,9,10,orange); box(20,12,11,7,orange);
  box(27,17,4,4,cream); box(30,16,2,2,pink);
  box(27,13,2,2,outline); box(27,13,1,1,0xffffffff);
  box(22,11,2,3,stripe); box(24,10,2,2,stripe);
  box(11,18,2,5,stripe); box(16,18,2,4,stripe);
  // Alternating paw positions create a four-frame walk cycle.
  const int rear[4]={8,10,12,10}, front[4]={22,20,18,20};
  int f=phase%4;
  box(rear[f],25,3,6,outline); box(rear[f]+1,25,1,5,orange);
  box(rear[f],30,5,2,outline); box(rear[f]+1,30,3,1,cream);
  box(front[f],24,3,7,outline); box(front[f]+1,25,1,5,orange);
  box(front[f],30,5,2,outline); box(front[f]+1,30,3,1,cream);
  return p;
}
