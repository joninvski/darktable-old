/*
    This file is part of darktable,
    copyright (c) 2010 Henrik Andersson.

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

#include <stdlib.h>
#include <stdio.h>
#include <memory.h>
#include <lcms.h> 
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfTiledOutputFile.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfStandardAttributes.h>
#include "common/exif.h"
#include "common/colorspaces.h"
#include "imageio_exr.h"

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

/** Special BLOB attribute implementation.*/
namespace Imf {
  typedef struct Blob {
    uint32_t size;
    uint8_t *data;
  } Blob;
  typedef Imf::TypedAttribute<Imf::Blob> BlobAttribute;
  template <> const char *BlobAttribute::staticTypeName(){ return "blob"; }
  template <> void BlobAttribute::writeValueTo (OStream &os, int version) const 
  {
    Xdr::write <StreamIO> (os, _value.size);
    Xdr::write <StreamIO> (os, (char *)_value.data,_value.size);
  }

  template <> void BlobAttribute::readValueFrom (IStream &is, int size, int version)
  {
    Xdr::read <StreamIO> (is, _value.size);
    Xdr::read <StreamIO> (is, (char *)_value.data, _value.size);
  }
}
 
int dt_imageio_exr_write_f(const char *filename, const float *in, const int width, const int height, void *exif, int exif_len)
{
  return dt_imageio_exr_write_with_icc_profile_f(filename,in,width,height,exif,exif_len,0);
}

int dt_imageio_exr_write_with_icc_profile_f(const char *filename, const float *in, const int width, const int height, void *exif, int exif_len,int imgid)
{
  Imf::BlobAttribute::registerAttributeType();
  Imf::Blob exif_blob={0};
  exif_blob.size=exif_len;
  exif_blob.data=(uint8_t *)exif;
  Imf::Header header(width,height,1,Imath::V2f (0, 0),1,Imf::INCREASING_Y,Imf::PIZ_COMPRESSION);
  header.insert("comment",Imf::StringAttribute("developed using "PACKAGE_NAME"-"PACKAGE_VERSION));
  header.insert("exif", Imf::BlobAttribute(exif_blob));
  header.channels().insert("R",Imf::Channel(Imf::FLOAT));
  header.channels().insert("B",Imf::Channel(Imf::FLOAT));
  header.channels().insert("G",Imf::Channel(Imf::FLOAT));
  header.setTileDescription(Imf::TileDescription(100, 100, Imf::ONE_LEVEL));
  Imf::TiledOutputFile file(filename, header);	
  Imf::FrameBuffer data;
  
  uint32_t channelsize=(width*height);
  float *red=(float *)malloc(channelsize*sizeof(float));
  float *green=(float *)malloc(channelsize*sizeof(float));
  float *blue=(float *)malloc(channelsize*sizeof(float));
  
  for(uint32_t j=0;j<channelsize;j++) { red[j]=in[j*3+0]; }
  data.insert("R",Imf::Slice(Imf::FLOAT,(char *)red,sizeof(float)*1,sizeof(float)*width));
  
  for(uint32_t j=0;j<channelsize;j++) { blue[j]=in[j*3+2]; }
  data.insert("B",Imf::Slice(Imf::FLOAT,(char *)blue,sizeof(float)*1,sizeof(float)*width));
  
  for(uint32_t j=0;j<channelsize;j++) { green[j]=in[j*3+1]; }
  data.insert("G",Imf::Slice(Imf::FLOAT,(char *)green,sizeof(float)*1,sizeof(float)*width));
  
  file.setFrameBuffer(data);
  file.writeTiles (0, file.numXTiles() - 1, 0, file.numYTiles() - 1);
  free(red);
  free(green);
  free(blue);
  return 1;
}