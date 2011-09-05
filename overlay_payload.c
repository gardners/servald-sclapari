#include "mphlr.h"

int overlay_payload_verify(overlay_frame *p)
{
  /* Make sure that an incoming payload has a valid signature from the sender.
     This is used to prevent spoofing */

  return WHY("function not implemented");
}

int op_append_type(overlay_buffer *headers,overlay_frame *p)
{
  unsigned char c[3];
  switch(p->type&OF_TYPE_FLAG_BITS)
    {
    case OF_TYPE_FLAG_NORMAL:
      c[0]=p->type|p->modifiers;
      if (ob_append_bytes(headers,c,1)) return -1;
      break;
    case OF_TYPE_FLAG_E12:
      c[0]=(p->type&OF_MODIFIER_BITS)|OF_TYPE_EXTENDED12;
      c[1]=(p->type>>4)&0xff;
      if (ob_append_bytes(headers,c,2)) return -1;
      break;
    case OF_TYPE_FLAG_E20:
      c[0]=(p->type&OF_MODIFIER_BITS)|OF_TYPE_EXTENDED20;
      c[1]=(p->type>>4)&0xff;
      c[2]=(p->type>>12)&0xff;
      if (ob_append_bytes(headers,c,3)) return -1;
      break;
    default: 
      /* Don't know this type of frame */
      WHY("Asked for format frame with unknown TYPE_FLAG bits");
      return -1;
    }
  return 0;
}


int overlay_frame_package_fmt1(overlay_frame *p,overlay_buffer *b)
{
  /* Convert a payload (frame) structure into a series of bytes.
     Assumes that any encryption etc has already been done.
     Will pick a next hop if one has not been chosen.
  */

  unsigned char c[64];
  int nexthoplen=0;

  overlay_buffer *headers=ob_new(256);

  if (!headers) return WHY("could not allocate overlay buffer for headers");
  if (!p) return WHY("p is NULL");
  if (!b) return WHY("b is NULL");

  /* Build header */
  int fail=0;

  if (p->nexthop_address_status!=OA_RESOLVED) {
    if (overlay_get_nexthop((unsigned char *)p->destination,p->nexthop,&nexthoplen)) fail++;
    else p->nexthop_address_status=OA_RESOLVED;
  }

  if (p->source[0]<0x10||p->destination[0]<0x10||p->nexthop[0]<0x10) {
    // Make sure that addresses do not overload the special address spaces of 0x00*-0x0f*
    fail++;
    return WHY("one or more packet addresses begins with reserved value 0x00-0x0f");
  }

  /* XXX Write fields in correct order */

  /* Write out type field byte(s) */
  if (op_append_type(headers,p)) fail++;

  /* Write out TTL */
  c[0]=p->ttl; if (ob_append_bytes(headers,c,1)) fail++;

  /* Length.  This is the fun part, because we cannot calculate how many bytes we need until
     we have abbreviated the addresses, and the length encoding we use varies according to the
     length encoded.  The simple option of running the abbreviations twice won't work because 
     we rely on context for abbreviating the addresses.  So we write it initially and then patch it
     after.
  */
  int max_len=((SID_SIZE+3)*3+headers->length+p->payload->length);
  ob_append_rfs(b,max_len);

  int addrs_len=b->length;

  /* Write out addresses as abbreviated as possible */
  overlay_abbreviate_append_address(b,p->nexthop);
  overlay_abbreviate_set_most_recent_address(p->nexthop);
  overlay_abbreviate_append_address(b,p->destination);
  overlay_abbreviate_set_most_recent_address(p->destination);
  overlay_abbreviate_append_address(b,p->source);
  overlay_abbreviate_set_most_recent_address(p->source);

  addrs_len=b->length-addrs_len;
  int actual_len=addrs_len+p->payload->length;
  ob_patch_rfs(b,actual_len);

  if (fail) {
    ob_free(headers);
    return WHY("failure count was non-zero");
  }

  /* Write payload format plus total length of header bits */
  if (ob_makespace(b,2+headers->length+p->payload->length)) {
    /* Not enough space free in output buffer */
    ob_free(headers);
    return WHY("Could not make enough space free in output buffer");
  }
  
  /* Package up headers and payload */
  ob_checkpoint(b);
  if (ob_append_short(b,0x1000|(p->payload->length+headers->length))) 
    { fail++; WHY("could not append version and length bytes"); }
  if (ob_append_bytes(b,headers->bytes,headers->length)) 
    { fail++; WHY("could not append header"); }
  if (ob_append_bytes(b,p->payload->bytes,p->payload->length)) 
    { fail++; WHY("could not append payload"); }
  
  /* XXX SIGN &/or ENCRYPT */
  
  ob_free(headers);
  
  if (fail) { ob_rewind(b); return WHY("failure count was non-zero"); } else return 0;
}
  
overlay_buffer *overlay_payload_unpackage(overlay_frame *b) {
  /* Extract the payload at the current location in the buffer. */
    
  WHY("not implemented");
  return NULL;
}

int overlay_payload_enqueue(int q,overlay_frame *p)
{
  /* Add payload p to queue q.
  */

  return WHY("not implemented");
}

int op_free(overlay_frame *p)
{
  if (!p) return WHY("Asked to free NULL");
  if (p->prev&&p->prev->next==p) return WHY("p->prev->next still points here");
  if (p->next&&p->next->prev==p) return WHY("p->next->prev still points here");
  p->prev=NULL;
  p->next=NULL;
  if (p->payload) ob_free(p->payload);
  p->payload=NULL;
  free(p);
  return 0;
}

int overlay_frame_set_neighbour_as_source(overlay_frame *f,overlay_neighbour *n)
{
  if (!n) return WHY("Neighbour was null");
  bcopy(n->sid,f->source,SID_SIZE);
  f->source_address_status=OA_RESOLVED;

  return 0;
}

unsigned char *overlay_get_my_sid()
{

  /* Make sure we can find our SID */
  int zero=0;
  if (!findHlr(hlr,&zero,NULL,NULL)) { WHY("Could not find first entry in HLR"); return NULL; }
  return &hlr[zero+4];
}

int overlay_frame_set_me_as_source(overlay_frame *f)
{
  unsigned char *sid=overlay_get_my_sid();
  if (!sid) return WHY("overlay_get_my_sid() failed.");
  bcopy(sid,f->source,SID_SIZE);

  f->source_address_status=OA_RESOLVED;

  return 0;
}