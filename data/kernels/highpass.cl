/*
    This file is part of darktable,
    copyright (c) 2011 ulrich pegelow.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/

const sampler_t sampleri =  CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_NEAREST;
const sampler_t samplerf =  CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_LINEAR;


/* This is highpass for Lab space. We only do invert/blur/mix on L and desaturate a and b */

kernel void 
highpass_invert(read_only image2d_t in, write_only image2d_t out, const int width, const int height)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 pixel = read_imagef(in, sampleri, (int2)(x, y));
  pixel.x = clamp(100.0f - pixel.x, 0.0f, 100.0f);
  write_imagef (out, (int2)(x, y), pixel);
}


kernel void 
highpass_hblur(read_only image2d_t in, write_only image2d_t out, constant float *m, const int rad,
      const int width, const int height, const int blocksize, local float *buffer)
{
  const int lid = get_local_id(0);
  const int lsz = get_local_size(0);
  const int x = get_global_id(0);
  const int y = get_global_id(1);
  float4 pixel = (float4)0.0f;

  if(y >= height) return;

  /* read pixel and fill center part of buffer */
  if(x < width)
  {
    pixel = read_imagef(in, sampleri, (int2)(x, y));
    buffer[rad + lid] = pixel.x;
  }

  /* left wing of buffer */
  for(int n=0; n <= rad/lsz; n++)
  {
    const int l = n*lsz + lid + 1;
    if(l > rad) continue;
    const int xx = mad24((int)get_group_id(0), lsz, -l);
    if(xx < 0) break;
    buffer[rad - l] = read_imagef(in, sampleri, (int2)(xx, y)).x;
  }
    
  /* right wing of buffer */
  for(int n=0; n <= rad/lsz; n++)
  {
    const int r = n*lsz + (lsz - lid);
    if(r > rad) continue;
    const int xx = mad24((int)get_group_id(0), lsz, lsz - 1 + r);
    if(xx >= width) break;
    buffer[rad + lsz - 1 + r] = read_imagef(in, sampleri, (int2)(xx, y)).x;
  }

  barrier(CLK_LOCAL_MEM_FENCE);

  if(x >= width) return;

  buffer += lid + rad;
  m += rad;

  float sum = 0.0f;
  float weight = 0.0f;

  for (int i=-rad; i<rad; i++)
  {
    if (x + i < 0 || x + i >= width) continue;
    sum += buffer[i] * m[i];
    weight += m[i];
  }
  pixel.x = weight > 0.0f ? sum/weight : 0.0f;
  write_imagef (out, (int2)(x, y), pixel);
}



kernel void 
highpass_vblur(read_only image2d_t in, write_only image2d_t out, constant float *m, const int rad,
      const int width, const int height, const int blocksize, local float *buffer)
{
  const int lid = get_local_id(1);
  const int lsz = get_local_size(1);
  const int x = get_global_id(0);
  const int y = get_global_id(1);
  float4 pixel = (float4)0.0f;

  if(x >= width) return;

  /* read pixel and fill center part of buffer */
  if(y < height)
  {
    pixel = read_imagef(in, sampleri, (int2)(x, y));
    buffer[rad + lid] = pixel.x;
  }

  /* left wing of buffer */
  for(int n=0; n <= rad/lsz; n++)
  {
    const int l = n*lsz + lid + 1;
    if(l > rad) continue;
    const int yy = mad24((int)get_group_id(1), lsz, -l);
    if(yy < 0) break;
    buffer[rad - l] = read_imagef(in, sampleri, (int2)(x, yy)).x;
  }
    
  /* right wing of buffer */
  for(int n=0; n <= rad/lsz; n++)
  {
    const int r = n*lsz + (lsz - lid);
    if(r > rad) continue;
    const int yy = mad24((int)get_group_id(1), lsz, lsz - 1 + r);
    if(yy >= height) break;
    buffer[rad + lsz - 1 + r] = read_imagef(in, sampleri, (int2)(x, yy)).x;
  }

  barrier(CLK_LOCAL_MEM_FENCE);

  if(y >= height) return;

  buffer += lid + rad;
  m += rad;

  float sum = 0.0f;
  float weight = 0.0f;

  for (int i=-rad; i<rad; i++)
  {
    if (y + i < 0 || y + i >= height) continue;
    sum += buffer[i] * m[i];
    weight += m[i];
  }
  pixel.x = weight > 0.0f ? sum/weight : 0.0f;
  write_imagef (out, (int2)(x, y), pixel);
}



kernel void 
highpass_mix(read_only image2d_t in_a, read_only image2d_t in_b, write_only image2d_t out,
             const int width, const int height, const float contrast_scale)
{
  const int x = get_global_id(0);
  const int y = get_global_id(1);

  if(x >= width || y >= height) return;

  float4 o = 0.0f;
  float4 a = read_imagef(in_a, sampleri, (int2)(x, y));
  float4 b = read_imagef(in_b, sampleri, (int2)(x, y));
  float4 min = (float4)(0.0f, -128.0f, -128.0f, 0.0f);
  float4 max = (float4)(100.0f, 128.0f, 128.0f, 1.0f);

  o.x = 50.0f+((0.5f * a.x + 0.5f * b.x) - 50.0f)*contrast_scale;
  o.y = 0.0f;
  o.z = 0.0f;

  write_imagef (out, (int2)(x, y), clamp(o, min, max));
}

