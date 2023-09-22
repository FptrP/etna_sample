#ifndef ABUFFER_GLSL_INCLUDED
#define ABUFFER_GLSL_INCLUDED

#define ABUFFER_LIST_END 0xffffffff

struct FragmentEntry
{
  float depth;
  uint packedColor;
  uint next;
};

#endif