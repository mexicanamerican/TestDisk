/*

    File: file_mp3.c

    Copyright (C) 1998-2008 Christophe GRENIER <grenier@cgsecurity.org>
  
    This software is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
  
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
  
    You should have received a copy of the GNU General Public License along
    with this program; if not, write the Free Software Foundation, Inc., 51
    Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#include <stdio.h>
#include "types.h"
#include "common.h"
#include "filegen.h"
#include "log.h"

extern const file_hint_t file_hint_mkv;

static void register_header_check_mp3(file_stat_t *file_stat);
static int header_check_id3(const unsigned char *buffer, const unsigned int buffer_size, const unsigned int safe_header_only, const file_recovery_t *file_recovery, file_recovery_t *file_recovery_new);
static int data_check_id3(const unsigned char *buffer, const unsigned int buffer_size, file_recovery_t *file_recovery);
static int header_check_mp3(const unsigned char *buffer, const unsigned int buffer_size, const unsigned int safe_header_only, const file_recovery_t *file_recovery, file_recovery_t *file_recovery_new);
static int data_check_mp3(const unsigned char *buffer, const unsigned int buffer_size, file_recovery_t *file_recovery);
static unsigned int pos_in_mem(const unsigned char *haystack, const unsigned int haystack_size, const unsigned char *needle, const unsigned int needle_size);
static unsigned int search_MMT(const unsigned char *buffer, const unsigned int i, const unsigned int buffer_size);

const file_hint_t file_hint_mp3= {
  .extension="mp3",
  .description="MP3 audio (MPEG ADTS, layer III, v1)",
  .min_header_distance=0,
  .max_filesize=PHOTOREC_MAX_FILE_SIZE,
  .recover=1,
  .enable_by_default=1,
  .register_header_check=&register_header_check_mp3
};

static const unsigned char id3_header[3]= {'I','D','3'};
static const unsigned char mp3_header[1]= {0xFF};

static void register_header_check_mp3(file_stat_t *file_stat)
{
  register_header_check(0, id3_header,sizeof(id3_header), &header_check_id3, file_stat);
  register_header_check(0, mp3_header,sizeof(mp3_header), &header_check_mp3, file_stat);
}

#define MPEG_V25	0
#define MPEG_V2		0x2
#define MPEG_V1		0x3
#define MPEG_L3	0x01
#define MPEG_L2	0x02
#define MPEG_L1	0x03

static const unsigned int sample_rate_table[4][4]={
  {11025, 12000,  8000, 0},	/* MPEG_V25 */
  {    0,     0,     0, 0},
  {22050, 24000, 16000, 0},	/* MPEG_V2 */
  {44100, 48000, 32000, 0}	/* MPEG_V1 */
};
static const unsigned int bit_rate_table[4][4][16]=
{
  /* MPEG_V25 */
  /* MPEG_V_INVALID */
  {
    /* MPEG_L_INVALID */
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* MPEG_L3 */
    { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},
    /* MPEG_L2 */
    { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},
    /* MPEG_L1 */
    { 0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0}
  },
  {
    /* MPEG_L_INVALID */
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* MPEG_L3 */
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* MPEG_L2 */
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* MPEG_L1 */
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
  },
  /* MPEG_V2 */
  {
    /* MPEG_L_INVALID */
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* MPEG_L3 */
    { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},
    /* MPEG_L2 */
    { 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},
    /* MPEG_L1 */
    { 0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0}
  },
  /* MPEG_V1 */
  {
    /* MPEG_L_INVALID */
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    /* MPEG_L3 */
    { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0},
    /* MPEG_L2 */
    { 0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0}, 
    /* MPEG_L1 */
    { 0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 294, 416, 448, 0}
  },
};

static int header_check_id3(const unsigned char *buffer, const unsigned int buffer_size, const unsigned int safe_header_only, const file_recovery_t *file_recovery, file_recovery_t *file_recovery_new)
{
  if(buffer[0]=='I' && buffer[1]=='D' && buffer[2]=='3' && (buffer[3]==2 || buffer[3]==3 || buffer[3]==4) && buffer[4]==0)
  {
    unsigned int potential_frame_offset=0;
    /*
     * TODO Handle ID3 tag
     * http://www.id3.org/id3v2-00
     * http://www.id3.org/id3v2.3.0
     */
    if(buffer[3]==4 && (buffer[5]&0x10)==0x10) /* a footer is present http://www.id3.org/id3v2.4.0-structure chap. 3.1 */
      potential_frame_offset = 10;

    potential_frame_offset+=((buffer[6]&0x7f)<<21) + ((buffer[7]&0x7f)<<14)
      + ((buffer[8]&0x7f)<<7) + (buffer[9]&0x7f)+ 10;

    /*
       log_info("ID3v2.%u found \n potential_frame_offset at 0x%x\n",buffer[3], potential_frame_offset);
     */
    reset_file_recovery(file_recovery_new);
    file_recovery_new->calculated_file_size=potential_frame_offset;
    file_recovery_new->min_filesize=287;
    file_recovery_new->data_check=&data_check_id3;
    file_recovery_new->extension=file_hint_mp3.extension;
    file_recovery_new->file_check=&file_check_size;
    return 1;
  }
  return 0;
}

static int header_check_mp3(const unsigned char *buffer, const unsigned int buffer_size, const unsigned int safe_header_only, const file_recovery_t *file_recovery, file_recovery_t *file_recovery_new)
{
  unsigned int potential_frame_offset=0;
  unsigned int nbr=0;
  /*
     	A Frame sync  				11 (length in bits)
    	B MPEG audio version (MPEG-1, 2, etc.) 	2
    	C MPEG layer (Layer I, II, III, etc.) 	2
    	D Protection (if on, then checksum follows header) 	1
	AAAA AAAA AAAB BCCD
	1111 1111 1111 1010 = FA = MPEG-1 layer 3
	1111 1111 1111 0010 = F2 = MPEG-2 layer 3
	1111 1111 1110 0010 = E2 = MPEG-2.5 layer 3

	http://www.dv.co.yu/mpgscript/mpeghdr.htm
  */
  if(file_recovery!=NULL && file_recovery->file_stat!=NULL &&
      ((potential_frame_offset==0 && file_recovery->file_stat->file_hint==&file_hint_mp3)||
       file_recovery->file_stat->file_hint==&file_hint_mkv))
    return 0;
  if(!(buffer[potential_frame_offset+0]==0xFF &&
	((buffer[potential_frame_offset+1]&0xFE)==0xFA ||
	 (buffer[potential_frame_offset+1]&0xFE)==0xF2 ||
	 (buffer[potential_frame_offset+1]&0xFE)==0xE2)))
    return 0;
  while(potential_frame_offset+1 < buffer_size &&
      potential_frame_offset+1 < 2048)
  {
    if(buffer[potential_frame_offset+0]!=0xFF)
      return 0;
    {
      const unsigned int mpeg_version	=(buffer[potential_frame_offset+1]>>3)&0x03;
      const unsigned int mpeg_layer	=(buffer[potential_frame_offset+1]>>1)&0x03;
      const unsigned int bit_rate_key	=(buffer[potential_frame_offset+2]>>4)&0x0F;
      const unsigned int sampling_rate_key=(buffer[potential_frame_offset+2]>>2)&0x03;
      const unsigned int padding		=(buffer[potential_frame_offset+2]>>1)&0x01;
      const unsigned int bit_rate= bit_rate_table[mpeg_version][mpeg_layer][bit_rate_key]*1000;
      const unsigned int sample_rate=sample_rate_table[mpeg_version][sampling_rate_key];
      unsigned int frameLengthInBytes=0;
      if(sample_rate>0 && bit_rate>0)
      {
	if(mpeg_layer==MPEG_L3)
	{
	  if(mpeg_version==MPEG_V1)
	    frameLengthInBytes = 144 * bit_rate / sample_rate + padding;
	  else
	    frameLengthInBytes = 72 * bit_rate / sample_rate + padding;
	}
	else if(mpeg_layer==MPEG_L2)
	  frameLengthInBytes = 144 * bit_rate / sample_rate + padding;
	else
	  frameLengthInBytes = (12 * bit_rate / sample_rate + padding)*4;
      }
#ifdef DEBUG_MP3
      log_info("bit_rate=%u\n",bit_rate);
      log_info("sample_rate=%u\n",sample_rate);
      log_info("padding=%u\n", padding);
      log_info("frameLengthInBytes=%u\n",frameLengthInBytes);
#endif
      if(frameLengthInBytes==0)
	return 0;
      potential_frame_offset+=frameLengthInBytes;
      nbr++;
    }
  }
  if(nbr>1)
  {
#ifdef DEBUG_MP3
    log_info("header_check_mp3 mp3 found\n");
#endif
    reset_file_recovery(file_recovery_new);
    file_recovery_new->calculated_file_size=potential_frame_offset;
    file_recovery_new->min_filesize=287;
    file_recovery_new->data_check=&data_check_mp3;
    file_recovery_new->extension=file_hint_mp3.extension;
    file_recovery_new->file_check=&file_check_size;
    return 1;
  }
  return 0;
}

static int data_check_id3(const unsigned char *buffer, const unsigned int buffer_size, file_recovery_t *file_recovery)
{
  while(file_recovery->calculated_file_size + buffer_size/2  >= file_recovery->file_size &&
      file_recovery->calculated_file_size + 1 < file_recovery->file_size + buffer_size/2)
  {
    unsigned int i=file_recovery->calculated_file_size - file_recovery->file_size + buffer_size/2;
    if(buffer[i]==0)
    { /* Padding is present */
      file_recovery->calculated_file_size++;
    }
    else
    { /* no more padding or no padding */
      file_recovery->data_check=&data_check_mp3;
      file_recovery->file_check=&file_check_size;
      return data_check_mp3(buffer, buffer_size, file_recovery);
    }
  }
  return 1;
}

static int data_check_mp3(const unsigned char *buffer, const unsigned int buffer_size, file_recovery_t *file_recovery)
{
  while(file_recovery->calculated_file_size + buffer_size/2  >= file_recovery->file_size &&
      file_recovery->calculated_file_size + 16 < file_recovery->file_size + buffer_size/2)
  {
    unsigned int MMT_size = 0;
    const unsigned int i=file_recovery->calculated_file_size - file_recovery->file_size + buffer_size/2;
    /*
    log_trace("data_check_mp3 start i=0x%x buffer_size=0x%x calculated_file_size=%lu file_size=%lu\n",
	i, buffer_size,
	(long unsigned)file_recovery->calculated_file_size,
	(long unsigned)file_recovery->file_size);
    */
    if(buffer[i+0]==0xFF && ((buffer[i+1]&0xE0)==0xE0))
    {
      unsigned int mpeg_version	=(buffer[i+1]>>3)&0x03;
      unsigned int mpeg_layer	=(buffer[i+1]>>1)&0x03;
      unsigned int bit_rate_key	=(buffer[i+2]>>4)&0x0F;
      unsigned int sampling_rate_key=(buffer[i+2]>>2)&0x03;
      unsigned int padding		=(buffer[i+2]>>1)&0x01;
      unsigned int bit_rate= bit_rate_table[mpeg_version][mpeg_layer][bit_rate_key]*1000;
      unsigned int sample_rate=sample_rate_table[mpeg_version][sampling_rate_key];
      unsigned int frameLengthInBytes=0;
      /*
      log_info("frame_offset=%u\n",i);
      log_info("bit_rate=%u\n",bit_rate);
      log_info("sample_rate=%u\n",sample_rate);
      log_info("frameLengthInBytes=%u\n",frameLengthInBytes);
      */
      if(sample_rate>0 && bit_rate>0)
      {
	if(mpeg_layer==MPEG_L3)
	{
	  if(mpeg_version==MPEG_V1)
	    frameLengthInBytes = 144 * bit_rate / sample_rate + padding;
	  else
	    frameLengthInBytes = 72 * bit_rate / sample_rate + padding;
	}
	else if(mpeg_layer==MPEG_L2)
	  frameLengthInBytes = 144 * bit_rate / sample_rate + padding;
	else
	  frameLengthInBytes = (12 * bit_rate / sample_rate + padding)*4;
      }  
      if(frameLengthInBytes<3)
      {
	return 2;
      }
      file_recovery->calculated_file_size+=frameLengthInBytes;
    }
    else if(buffer[i]=='L' && buffer[i+1]=='Y' && buffer[i+2]=='R' && buffer[i+3]=='I' && buffer[i+4]=='C' && buffer[i+5]=='S' && buffer[i+6]=='B' && buffer[i+7]=='E' && buffer[i+8]=='G' && buffer[i+9]=='I' && buffer[i+10]=='N')
    {
      /*
	 Lyrics3 tag		http://www.id3.org/Lyrics3
	 Lyrics3 v2.00 tag	http://www.id3.org/Lyrics3v2
	 Lyrics Header : 
	 'LYRICSBEGIN' (both version)
	 Lyrics Footer : 
	 'LYRICSEND'  Lyrics3 tags 
	 'LYRICS200'  Lyrics3v2 Tags
	 The maximum length of the lyrics is 5100 bytes for Lyrics3 and 4096 bytes for Lyrics3 v2.
       */
      unsigned int pos_lyrics=0;
      /* FIXME */
      if(file_recovery->calculated_file_size + 5100 >= file_recovery->file_size + buffer_size/2)
	return 1;
      if((pos_lyrics=pos_in_mem(&buffer[i], 4096, (const unsigned char*)"LYRICS200", 9)) != 0)
      {
	file_recovery->calculated_file_size+=pos_lyrics;
      }
      else if((pos_lyrics=pos_in_mem(&buffer[i], 5100, (const unsigned char*)"LYRICSEND", 9)) != 0)
      {
	file_recovery->calculated_file_size+=pos_lyrics;
      }
      else 
      {
	/*
	   log_warning("End of Lyrics not found \n");
	 */
	return 2;
      }
    }
    else if(buffer[i]=='A' && buffer[i+1]=='P' && buffer[i+2]=='E' && buffer[i+3]=='T' && buffer[i+4]=='A' && buffer[i+5]=='G' && buffer[i+6]=='E' && buffer[i+7]=='X')
    { /* APE Tagv2 (APE Tagv1 has no header) http://wiki.hydrogenaudio.org/index.php?title=APE_Tags_Header */
      const unsigned int ape_tag_size = (buffer[i+12] + (buffer[i+13]<<8) + (buffer[i+14]<<16) + (buffer[i+15]<<24))+32;
      file_recovery->calculated_file_size+=ape_tag_size;
    }
    else if(buffer[i]=='T' && buffer[i+1]=='A' && buffer[i+2]=='G')	
    { /* http://www.id3.org/ID3v1 TAGv1 size = 128 bytes with header "TAG" */
      file_recovery->calculated_file_size+=128;
    }
    else if(file_recovery->calculated_file_size > file_recovery->file_size)
    {
      return 1;
    }
    else if((MMT_size=search_MMT(buffer,i,buffer_size))!=0)
    {
      /*
	 log_info("MusicMatch Tag found at offset 0x%x with size 0x%x \n", file_recovery->calculated_file_size, MMT_size);
       */
      file_recovery->calculated_file_size+=MMT_size;
    }
    else 
    {
      return 2;
    }
  }
  return 1;
}

static unsigned int pos_in_mem(const unsigned char *haystack, const unsigned int haystack_size, const unsigned char *needle, const unsigned int needle_size)
{
  unsigned int i;
  for(i=0;i<haystack_size;i++)
    if(memcmp(&haystack[i],needle,needle_size)==0)
      return (i+needle_size);
  return 0;
}

static unsigned int search_MMT(const unsigned char *buffer, const unsigned int i, const unsigned int buffer_size)
{
  /*
     Check for MusicMatch Tag
	http://freenet-homepage.de/StefanRypalla/stuff/musicmatch.txt
	min size = 8192bytes
	header is optional
	structure : 
	header				256 bytes optional
	image extension	4 bytes
	image binary		>= 4 bytes
	unused				4 bytes
	version info		256 bytes
	audio meta-data	>= 7868 bytes
	In all versions of the MusicMatch format up to and including 3.00,
	this section (audio meta-data) is always 7868 bytes in length.
	All subsequent versions allowed three possible lengths for this section: 7936, 8004, and 8132 bytes.
	data offsets		20 bytes
	Footer				48 bytes (optional?!)
   */ 
  const unsigned char mm_header[10]= {'1','8','2','7','3','6','4','5',0x00, 0x00};
  const unsigned char mm_pad_version_info[14] = {0x00,0x00,0x00,0x00,'1','8','2','7','3','6','4','5',0x00,0x00};
  const char *mm_footer="Brava Software Inc.";
  const char *mm_footer_tag="TAG";
  unsigned int size=0;
  unsigned int image_size;
  if(i+sizeof(mm_header)>buffer_size)
    return 0;
  if(memcmp(&buffer[i],mm_header,sizeof(mm_header))==0)	// Optional Header
  {
    size=256;
    /* Don't check image extension */
    /* log_info("search_MMT: mm_header present\n"); */
  }
  else
  {
    /* Check image extension */
    if( memcmp(&buffer[i+size],"    ",4)!=0 &&
	memcmp(&buffer[i+size],"bmp ",4)!=0 &&
	memcmp(&buffer[i+size],"jpg ",4)!=0)
      return 0;
    /* log_info("search_MMT: image extension present\n"); */
  }
/*  dump_log(&buffer[i+size], buffer_size-(i+size)); */
  /* Image binary */
  image_size = buffer[i+size+4]+(buffer[i+size+5]<<8)+(buffer[i+size+6]<<16)+(buffer[i+size+7]<<24);
  size+=8+image_size;
  /* check null padding + version_info */
  if(i+size+sizeof(mm_pad_version_info)>buffer_size)
  { /* FIXME: Is it better to have a partial MusicMatch Tag or none ? */
    /* log_trace("search_MMT: partial MusicMatch Tag 1\n"); */
    return 0;
  }
  if(memcmp(&buffer[i+size], mm_pad_version_info, sizeof(mm_pad_version_info))!=0)
  {
    /* log_trace("search_MMT: mm_pad_version_info not present\n"); */
    return 0;
  }
  size+=4+256;	/* padding + version_info */
  size+=20;	/* data offset */
  /* check footer for various audio meta-data size: 7868, 7936, 8004, 8132 */
  if(i+size+8132+sizeof(*mm_footer)>buffer_size)
  { /* FIXME: Is it better to have a partial MusicMatch Tag or none ? */
    /* log_trace("search_MMT: partial MusicMatch 2\n"); */
    return 0;
  }
  if(memcmp(&buffer[i+size+7868],mm_footer,strlen(mm_footer))==0 ||
      memcmp(&buffer[i+size+7868],mm_footer_tag,strlen(mm_footer_tag))==0)
    size+=7868;
  else if(memcmp(&buffer[i+size+7936],mm_footer,strlen(mm_footer))==0 ||
      memcmp(&buffer[i+size+7936],mm_footer_tag,strlen(mm_footer_tag))==0)
    size+=7936;
  else if(memcmp(&buffer[i+size+8004],mm_footer,strlen(mm_footer))==0 ||
      memcmp(&buffer[i+size+8004],mm_footer_tag,strlen(mm_footer_tag))==0)
    size+=8004;
  else if(memcmp(&buffer[i+size+8132],mm_footer,strlen(mm_footer))==0 ||
      memcmp(&buffer[i+size+8132],mm_footer_tag,strlen(mm_footer_tag))==0)
    size+=8132;
  else
  {
    /* log_trace("search_MMT: no mm_footer present\n"); */
    return 0;
  }
  /* dump_log(&buffer[i+size], 16); */
  if(memcmp(&buffer[i+size],mm_footer,strlen(mm_footer))==0)
    size+=48;	/* footer */
  else
    size+=0x80;	/* TAG footer */
  /* log_trace("search_MMT: MMT found size=%u (0x%x)\n", size, size); */
  return(size);
}
