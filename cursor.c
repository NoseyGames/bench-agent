#include "cursor.h"
#include <gio/gio.h>
static guint16 u16(const guchar *b) { return b[0]|((guint16)b[1]<<8); }
static guint32 u32(const guchar *b) { return u16(b)|((guint32)u16(b+2)<<16); }
GdkPixbuf *cursor_load(const char *path,int size,int *hot_x,int *hot_y,GError **e) {
    char *raw=NULL; gsize len=0;
    if(!g_file_get_contents(path,&raw,&len,e)) return NULL;
    const guchar *b=(guchar *)raw; GdkPixbuf *result=NULL;
    if(len<6 || u16(b)!=0 || u16(b+2)!=2 || !u16(b+4) || 6u+16u*u16(b+4)>len) goto bad;
    guint selected=0,best=0;
    for(guint i=0;i<u16(b+4);i++) { guint w=b[6+16*i]?b[6+16*i]:256; if(w>best) { best=w; selected=i; } }
    const guchar *entry=b+6+16*selected; guint off=u32(entry+12),bytes=u32(entry+8);
    if(off>len || bytes>len-off || bytes<40) goto bad;
    const guchar *dib=b+off; guint w=u32(dib+4),h=u32(dib+8)/2;
    guint header=u32(dib),stride=((w+31)/32)*4;
    if(w<1 || w>256 || h<1 || h>256 || header!=40 || u16(dib+12)!=1 || u16(dib+14)!=32 || u32(dib+16)!=0 ||
       header+w*h*4+stride*h>bytes || u16(entry+4)>=w || u16(entry+6)>=h) goto bad;
    GdkPixbuf *pix=gdk_pixbuf_new(GDK_COLORSPACE_RGB,TRUE,8,w,h);
    if(!pix) goto bad;
    guchar *dest=gdk_pixbuf_get_pixels(pix); int row=gdk_pixbuf_get_rowstride(pix);
    const guchar *src=dib+header, *mask=src+w*h*4; gboolean has_alpha=FALSE;
    for(guint i=0;i<w*h;i++) if(src[i*4+3]) { has_alpha=TRUE; break; }
    for(guint y=0;y<h;y++) for(guint x=0;x<w;x++) {
        guint sy=h-1-y; const guchar *p=src+(sy*w+x)*4; guchar *d=dest+y*row+x*4;
        d[0]=p[2]; d[1]=p[1]; d[2]=p[0];
        d[3]=has_alpha?p[3]:(mask[sy*stride+x/8]&(0x80>>(x%8)))?0:255;
    }
    result=gdk_pixbuf_scale_simple(pix,size,size,GDK_INTERP_BILINEAR); g_object_unref(pix);
    *hot_x=(int)(u16(entry+4)*size/w); *hot_y=(int)(u16(entry+6)*size/h);
    g_free(raw); return result;
bad:
    g_set_error_literal(e,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Unsupported or corrupt CUR: expected a 32-bit uncompressed Windows cursor");
    g_free(raw); return NULL;
}
